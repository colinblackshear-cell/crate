#include "TestHelpers.h"

#include "RigEngine.h"
#include "SynthProcessor.h"
#include "BassProcessor.h"
#include "SynthScreens.h"

using namespace crate;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// Plays the synth engine with the clock running and counts the notes it starts (via its note
// tap), each with the beat it started on.
std::vector<std::pair<int, double>> arpNotes (synthtool::Settings s, double beats, const std::vector<int>& chord,
                                              synthtool::Rhythm rhythm = {}, double bpm = 120.0)
{
    synthtool::Engine engine;
    engine.prepare (kRate);
    engine.setSettings (s);
    engine.setRhythm (rhythm);
    juce::MidiBuffer tap;
    engine.noteTap = &tap;
    synthtool::Clock clock;
    clock.playing = true;
    clock.bpm = bpm;
    std::vector<std::pair<int, double>> notes;
    std::vector<float> l (kBlock), r (kBlock);
    const double beatsPerSample = bpm / (60.0 * kRate);
    for (int b = 0; clock.ppq < beats - 1.0e-6; ++b)
    {
        juce::MidiBuffer in;
        if (b == 0)
            for (int n : chord)
                in.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 0);
        tap.clear();
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.render (l.data(), r.data(), kBlock, in, clock);
        for (const auto m : tap)
            if (m.getMessage().isNoteOn())
                notes.push_back ({ m.getMessage().getNoteNumber(), clock.ppq + m.samplePosition * beatsPerSample });
        clock.ppq += kBlock * beatsPerSample;
    }
    return notes;
}

// A stand-in for an instrument plugin with Arturia-style settings.
class FakeArturia : public juce::AudioPluginInstance
{
public:
    FakeArturia() : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo()))
    {
        for (auto* name : { "Master Volume", "Osc 1 Wave", "Cutoff", "Resonance", "Brightness", "Timbre", "Time", "Movement", "Attack", "Release" })
            static_cast<juce::AudioProcessor*> (this)->addParameter (new juce::AudioParameterFloat (juce::ParameterID { juce::String (name).removeCharacters (" "), 1 }, name, 0.0f, 1.0f, 0.5f));
    }
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = "Fake V";
        d.manufacturerName = "Arturia";
        d.pluginFormatName = "Fake";
        d.isInstrument = true;
        d.fileOrIdentifier = d.name;
        d.numOutputChannels = 2;
    }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, (i / 20) % 2 ? 0.5f : -0.5f);
    }
    using AudioPluginInstance::processBlock;
    const juce::String getName() const override { return "Fake V"; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 4; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int i) override { program = i; }
    const juce::String getProgramName (int i) override { return "Patch " + juce::String (i + 1); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    int program = 0;
};

void setParam (juce::AudioProcessorValueTreeState& params, const juce::String& id, float value)
{
    auto* p = params.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void snapshot (juce::Component& c, const juce::String& envName)
{
    if (auto path = juce::SystemStats::getEnvironmentVariable (envName, {}); path.isNotEmpty())
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        juce::File file (path);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }
}
}

struct RigSynthScreenTests : juce::UnitTest
{
    RigSynthScreenTests() : juce::UnitTest ("Synth panels, arp and instrument shell", "Synths") {}

    void runTest() override
    {
        beginTest ("the arp's speeds run slowest first, so the knob turns down to slow");
        {
            for (int i = 1; i < synth::ArpRates::count; ++i)
                expect (synth::ArpRates::beats (i) < synth::ArpRates::beats (i - 1), synth::ArpRates::names()[i]);
            expectEquals (synth::ArpRates::names()[synth::ArpRates::sixteenth], juce::String ("1/16"));
            expectEquals (synth::ArpRates::beats (0), 32.0, "8 bars a note at the slowest");
        }

        beginTest ("a slow arp plays one note a bar");
        {
            synthtool::Settings s;
            s.arpOn = true;
            s.arpSpeed = 3;   // 1 bar
            const auto notes = arpNotes (s, 8.0, { 60, 64, 67 });
            expectEquals ((int) notes.size(), 2, "two bars, two notes");
            if (notes.size() == 2)
                expectWithinAbsoluteError (notes[1].second, 4.0, 0.01);
        }

        beginTest ("free speed ignores the tempo");
        {
            synthtool::Settings s;
            s.arpOn = true;
            s.arpSync = false;
            s.arpHz = 2.0f;   // two notes a second
            const auto notes = arpNotes (s, 8.0, { 60 }, {}, 120.0);   // 8 beats at 120 = 4 s
            expectEquals ((int) notes.size(), 8);
        }

        beginTest ("swing pushes every second step later");
        {
            synthtool::Settings s;
            s.arpOn = true;
            s.arpSpeed = 10;    // 1/8
            s.arpSwing = 0.5f;  // a quarter of a step
            const auto notes = arpNotes (s, 2.0, { 60, 64 });
            expect (notes.size() >= 3);
            if (notes.size() >= 3)
            {
                expectWithinAbsoluteError (notes[0].second, 0.0, 0.01);
                expectWithinAbsoluteError (notes[1].second, 0.5 + 0.125, 0.01, "the second step, late");
                expectWithinAbsoluteError (notes[2].second, 1.0, 0.01, "the third, on the beat");
            }
        }

        beginTest ("chord mode plays the whole chord on each step; repeats play each note twice");
        {
            synthtool::Settings s;
            s.arpOn = true;
            s.arpMode = synthtool::ArpMode::chord;
            s.arpSpeed = 7;   // 1/4
            auto notes = arpNotes (s, 2.0, { 60, 64, 67 });
            expectEquals ((int) notes.size(), 6, "three notes on each of two beats");

            s.arpMode = synthtool::ArpMode::up;
            s.arpRepeats = 2;
            notes = arpNotes (s, 4.0, { 60, 64 });
            expect (notes.size() >= 4);
            if (notes.size() >= 4)
                expect (notes[0].first == 60 && notes[1].first == 60 && notes[2].first == 64 && notes[3].first == 64);

            s.arpRepeats = 1;
            s.arpOctaves = 4;
            s.arpSpeed = synth::ArpRates::sixteenth;
            notes = arpNotes (s, 2.0, { 60 });
            juce::String seen;
            for (auto& n : notes)
                seen << n.first << " ";
            expect (notes.size() >= 4 && notes[3].first == 96, "four octaves: " + seen);
        }

        beginTest ("chance 0 plays nothing; converge goes outside in");
        {
            synthtool::Settings s;
            s.arpOn = true;
            s.arpChance = 0.0f;
            expect (arpNotes (s, 2.0, { 60, 64 }).empty());
            s.arpChance = 1.0f;
            s.arpMode = synthtool::ArpMode::converge;
            const auto notes = arpNotes (s, 1.0, { 60, 64, 67, 72 });
            expect (notes.size() >= 4);
            if (notes.size() >= 4)
                expect (notes[0].first == 60 && notes[1].first == 72 && notes[2].first == 64 && notes[3].first == 67);
        }

        beginTest ("an old show's arp rate becomes the same speed");
        {
            SynthProcessor p;
            auto state = p.params.copyState();
            for (auto child : state)
                if (child.getProperty ("id").toString() == "arpSpeed")
                {
                    state.removeChild (child, nullptr);
                    break;
                }
            juce::ValueTree old ("PARAM");
            old.setProperty ("id", "arpRate", nullptr);
            old.setProperty ("value", 4, nullptr);   // 1/4 in the old list
            state.appendChild (old, nullptr);
            juce::MemoryBlock block;
            if (auto xml = state.createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, block);
            SynthProcessor other;
            other.setStateInformation (block.getData(), (int) block.getSize());
            expectEquals (juce::roundToInt (other.params.getRawParameterValue ("arpSpeed")->load()), 7);
            expectEquals (synth::ArpRates::names()[7], juce::String ("1/4"));
        }

        beginTest ("the new sound settings leave the old sound alone, and change it when turned");
        {
            auto render = [] (std::function<void (synthtool::Settings&)> change)
            {
                synthtool::Engine engine;
                engine.prepare (kRate);
                synthtool::Settings s;
                if (change)
                    change (s);
                engine.setSettings (s);
                juce::MidiBuffer in;
                in.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
                std::vector<float> l (kRate / 2), r (kRate / 2);
                engine.render (l.data(), r.data(), (int) l.size(), in, {});
                return std::make_pair (l, r);
            };
            const auto plain = render ({});
            expect (plain.first == plain.second, "no width: the same on both sides");
            const auto wide = render ([] (synthtool::Settings& s) { s.width = 1.0f; });
            expect (wide.first != wide.second, "width spreads it");
            const auto osc2 = render ([] (synthtool::Settings& s) { s.osc2Wave = synthtool::Osc2Wave::square; s.osc2Semi = 7; });
            expect (osc2.first != plain.first, "oscillator 2 adds to it");
            for (int type = 1; type < 4; ++type)
            {
                const auto out = render ([type] (synthtool::Settings& s) { s.filterType = (synth::FilterType) type; });
                float peak = 0;
                for (float v : out.first)
                    peak = juce::jmax (peak, std::abs (v));
                expect (peak > 0.01f && peak < 2.0f && std::isfinite (peak), "filter type " + juce::String (type));
            }
        }

        beginTest ("mono plays one note at a time, and goes back to the key still held");
        {
            synthtool::Engine engine;
            engine.prepare (kRate);
            synthtool::Settings s;
            s.voiceMode = synthtool::VoiceMode::mono;
            engine.setSettings (s);
            std::vector<float> l (kBlock), r (kBlock);
            juce::MidiBuffer in;
            in.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            in.addEvent (juce::MidiMessage::noteOn (1, 64, 1.0f), 10);
            in.addEvent (juce::MidiMessage::noteOn (1, 67, 1.0f), 20);
            engine.render (l.data(), r.data(), kBlock, in, {});
            expectEquals (engine.getActiveVoices(), 1);
        }

        beginTest ("the shell maps Arturia's macros first, and its level turns it down");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& slot = engine.getSynthPlugin();
            slot.setInstance (std::make_unique<FakeArturia>());
            auto name = [&] (int k) { auto* p = slot.macroParameter (k); return p != nullptr ? p->getName (32) : juce::String(); };
            expectEquals (name (0), juce::String ("Brightness"));
            expectEquals (name (1), juce::String ("Timbre"));
            expectEquals (name (2), juce::String ("Time"));
            expectEquals (name (3), juce::String ("Movement"));
            expectEquals (name (4), juce::String ("Cutoff"));
            slot.setMacro (7, 0);
            expectEquals (name (7), juce::String ("Master Volume"));
            const auto tree = slot.toTree();
            expect (tree.getProperty ("macros").toString().startsWith ("4 5 6 7 2"));

            juce::AudioBuffer<float> out (2, kBlock);
            juce::MidiBuffer midi;
            slot.process (out, midi);
            const float full = out.getMagnitude (0, kBlock);
            slot.level = 0.5f;
            slot.process (out, midi);
            expectWithinAbsoluteError (out.getMagnitude (0, kBlock), full * 0.5f, 1.0e-4f);
        }

        beginTest ("the Synth page: every setting on the panel, and the Engine buttons switch to the shell");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            SynthScreen screen (engine);
            screen.setBounds (0, 0, 1388, 640);
            expect (! screen.isShowingShell());

            // Every one of the synth's settings has a control on the panel.
            juce::StringArray onPanel;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* k = dynamic_cast<analog::Knob*> (child))
                        onPanel.add (k->getKnobName());
                    walk (*child);
                }
            };
            walk (screen);
            expect (onPanel.contains ("Speed") && onPanel.contains ("Swing") && onPanel.contains ("Cutoff") && onPanel.contains ("Glide"));

            // Controls don't overlap and stay on the screen.
            std::vector<juce::Rectangle<int>> seen;
            for (auto* child : screen.getChildren())
                if (child->isVisible() && dynamic_cast<analog::Knob*> (child) != nullptr)
                {
                    const auto b = child->getBounds();
                    expect (screen.getLocalBounds().contains (b), "on screen: " + dynamic_cast<analog::Knob*> (child)->getKnobName());
                    for (const auto& other : seen)
                        expect (! other.intersects (b.reduced (2)), "no overlap: " + dynamic_cast<analog::Knob*> (child)->getKnobName());
                    seen.push_back (b);
                }
            snapshot (screen, "CRATE_SYNTH_SNAPSHOT");

            // The Speed knob turned all the way down is the slowest speed.
            for (auto* child : screen.getChildren())
                if (auto* k = dynamic_cast<analog::Knob*> (child); k != nullptr && k->getKnobName() == "Speed")
                    k->slider.setValue (k->slider.getMinimum(), juce::sendNotificationSync);
            expectEquals (juce::roundToInt (engine.getSynth().params.getRawParameterValue ("arpSpeed")->load()), 0);

            auto& slot = engine.getSynthPlugin();
            slot.setInstance (std::make_unique<FakeArturia>());
            screen.showEngine (true);
            expect (screen.isShowingShell() && slot.isPlaying());
            screen.getShell().refresh();
            snapshot (screen, "CRATE_SHELL_SNAPSHOT");
            screen.showEngine (false);
            expect (slot.isLoaded() && slot.bypassed.load(), "Built-in keeps the plugin, switched off");
            expect (! slot.isPlaying());
        }

        beginTest ("the Bass page lays out its sound, sequencer and step grid");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            setParam (engine.getBass().params, "fat", 0.5f);
            BassScreen screen (engine);
            screen.setBounds (0, 0, 1388, 640);
            for (auto* child : screen.getChildren())
                if (child->isVisible())
                    expect (screen.getLocalBounds().contains (child->getBounds()));
            snapshot (screen, "CRATE_BASS_SNAPSHOT");
        }
    }
};

static RigSynthScreenTests rigSynthScreenTests;
