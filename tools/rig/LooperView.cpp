#include "LooperView.h"

#include "core/AudioFiles.h"
#include "core/SoundFx.h"
#include "RigColours.h"
#include "Vintage.h"

namespace Theme = crate::Theme;

namespace
{
juce::String barsText (double beats, int beatsPerBar)
{
    const double bars = beats / juce::jmax (1, beatsPerBar);
    if (std::abs (bars - std::round (bars)) < 0.01)
        return juce::String ((int) std::round (bars)) + (std::round (bars) == 1.0 ? " bar" : " bars");
    return juce::String (bars, 1) + " bars";
}
}

//==============================================================================
// The loop drawn as a waveform, with the playhead.
class LoopWave : public juce::Component
{
public:
    LoopWave (RigEngine& e, int i) : engine (e), index (i) {}

    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }

    void tick()
    {
        auto& layer = engine.getLooper().getLayer (index);
        const int version = layer.version.load();
        const auto state = engine.getLooper().getState (index);
        if (version != lastVersion && juce::Time::getMillisecondCounter() - lastScan > 100)
        {
            lastVersion = version;
            lastScan = juce::Time::getMillisecondCounter();
            scan();
        }
        if (state != lastState)
        {
            lastState = state;
            scan();
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace vintage;
        auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0e0c0a));
        g.fillRoundedRectangle (area, 6.0f);

        const auto& looper = engine.getLooper();
        const auto& layer = looper.getLayer (index);
        const auto state = looper.getState (index);
        const int bpb = engine.getTransport().getBeatsPerBar();
        const bool muted = layer.mute.load();

        if (state == RigLooper::State::empty || state == RigLooper::State::armed)
        {
            // A dotted line through the middle, and what happens next.
            g.setColour (juce::Colour (0xff3a332d));
            for (float x = area.getX() + 6; x < area.getRight() - 6; x += 6.0f)
                g.fillRect (x, area.getCentreY() - 0.5f, 3.0f, 1.0f);
            const bool armed = state == RigLooper::State::armed;
            const int bars = layer.bars.load();
            const juce::String text = armed ? juce::String::fromUTF8 ("\xe2\x97\x8f Records ") + (engine.getTransport().isPlaying() ? "on the next bar" : "when you press Play")
                                                  + (bars > 0 ? juce::String::fromUTF8 (" \xe2\x80\x94 ") + juce::String (bars) + (bars == 1 ? " bar" : " bars") : juce::String())
                                            : juce::String::fromUTF8 ("Empty \xe2\x80\x94 press Rec next layer");
            const auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 20, 330.0f), 22.0f);
            g.setColour (juce::Colour (0xff0e0c0a));
            g.fillRect (box);
            g.setColour (armed ? colours::arm.brighter (0.3f) : pal().wellInk2);
            g.setFont (barlow (14.0f));
            g.drawText (text, box, juce::Justification::centred);
            return;
        }

        // The loop, as bars.
        const auto colour = muted ? colours::mute : colours::loops;
        const auto body = area.reduced (10.0f, 6.0f);
        const float mid = body.getCentreY();
        const float half = body.getHeight() * 0.5f;
        // While it records with a set length, it fills in from the left; with no length yet,
        // the whole width is what's recorded so far.
        const double filled = state == RigLooper::State::recording && layer.position.load() > 0
                                  ? juce::jlimit (0.0, 1.0, layer.position.load()) : 1.0;
        const float barW = 3.0f, gap = 1.0f;
        const int total = (int) (body.getWidth() / (barW + gap));
        const int drawn = (int) (total * filled);
        for (int b = 0; b < drawn && ! peaks.empty(); ++b)
        {
            const double t = (double) b / juce::jmax (1, total - 1);
            const auto p = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (t * (peaks.size() - 1)))];
            const float h = juce::jmax (1.5f, std::sqrt (p) * half);
            g.setColour ((state == RigLooper::State::recording ? colours::arm : colour).withAlpha (muted ? 0.55f : 0.85f));
            g.fillRect (body.getX() + b * (barW + gap), mid - h, barW, h * 2);
        }

        // Bar lines.
        const double beats = layer.lengthBeats.load();
        if (state != RigLooper::State::recording && beats > 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            for (double b = bpb; b < beats; b += bpb)
                g.fillRect ((float) (body.getX() + b / beats * body.getWidth()), area.getY() + 3, 1.0f, area.getHeight() - 6);
        }

        // The loop's ends, in yellow: double-click (or Edit) to trim them.
        if (state != RigLooper::State::recording)
        {
            g.setColour (colours::selected);
            g.drawRoundedRectangle (body.expanded (4.0f, 3.0f), 3.0f, 1.5f);
            for (float x : { body.getX() - 5.0f, body.getRight() + 1.0f })
                g.fillRoundedRectangle (x, area.getY() + 2, 4.0f, area.getHeight() - 4, 2.0f);
        }

        // Playhead.
        if (engine.getTransport().isPlaying() && state != RigLooper::State::recording)
        {
            const float x = (float) (body.getX() + layer.position.load() * body.getWidth());
            g.setColour (state == RigLooper::State::overdubbing ? colours::arm : juce::Colour (0xfffff3d6));
            g.fillRect (x - 1.0f, area.getY() + 2, 2.0f, area.getHeight() - 4);
        }

        if (state == RigLooper::State::recording || state == RigLooper::State::overdubbing)
        {
            g.setColour (colours::arm.brighter (0.4f));
            g.setFont (cond (13.0f, Weight::bold, 0.06f));
            g.drawText ((state == RigLooper::State::recording ? "REC  " : "OVERDUB  ") + barsText (beats, bpb).toUpperCase(),
                        area.reduced (12, 3), juce::Justification::topRight);
        }
    }

private:
    void scan() { peaks = engine.getLooper().readPeaks (index, 400); }

    RigEngine& engine;
    const int index;
    std::vector<float> peaks;
    int lastVersion = -1;
    juce::uint32 lastScan = 0;
    RigLooper::State lastState = RigLooper::State::empty;
};

//==============================================================================
namespace looperlayout
{
// The columns of a layer's row (and the headings over them).
constexpr int wNumber = 44, wName = 150, wState = 96, wFx = 150, wBars = 56, wLevel = 44, wMute = 44, wSolo = 44, wMore = 34, wGap = 8;
}

class LooperView::Row : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    Row (LooperView& o, RigEngine& e, int i) : owner (o), engine (e), index (i), wave (e, i)
    {
        using namespace vintage;
        auto& layer = engine.getLooper().getLayer (index);
        for (auto* c : std::initializer_list<juce::Component*> { &wave, &fx, &level, &mute, &solo, &more })
            addAndMakeVisible (c);
        for (auto* b : { &fx, &mute, &solo, &more })
        {
            setInWell (*b, true);
            b->getProperties().set ("fontSize", 15.0f);
        }
        fx.getProperties().set ("fontSize", 13.0f);
        setLit (mute, Lit::mute);
        setLit (solo, Lit::current);
        setLit (fx, Lit::current);

        setTooltip ("Click the number to record this layer: it starts at the next bar, and stops at the end of the bar where you "
                    "click again (or by itself after its bars). On a playing loop, it overdubs. Keys 1-8 do the same. "
                    "Click the name to pick what it records and how long.");

        mute.setClickingTogglesState (true);
        mute.onClick = [this, &layer] { layer.mute = mute.getToggleState(); };
        mute.setTooltip ("Mute this loop (it keeps its place in time).");
        solo.onClick = [this] { owner.soloLayer (index); };
        solo.setTooltip ("Hear only this loop. Press again to bring the others back.");

        level.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        level.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        level.setColour (juce::Slider::rotarySliderFillColourId, colours::loops);
        level.setRange (0.0, 1.0);
        level.setDoubleClickReturnValue (true, 0.8);
        level.setTooltip ("This loop's level.");
        level.onValueChange = [this, &layer] { layer.level = (float) level.getValue(); };

        fx.setTooltip ("This loop's own effects: filter, drive, crush, delay, reverb and beat repeat. Lit when any are on.");
        fx.onClick = [this] { owner.showFx (index); };
        wave.onDoubleClick = [this] { owner.editLoop (index); };
        wave.setInterceptsMouseClicks (true, false);
        more.setTooltip ("Edit the loop's start and end, undo an overdub, clear it, import a loop or a song, save it as a WAV, "
                         "or put Record and Mute on your keyboard.");
        more.onClick = [this] { showMenu(); };
        refresh();
    }

    void refresh()
    {
        const auto& layer = engine.getLooper().getLayer (index);
        mute.setToggleState (layer.mute.load(), juce::dontSendNotification);
        level.setValue (layer.level.load(), juce::dontSendNotification);
    }

    // As clicks do (the tests use these).
    void pressNumber() { engine.getLooper().pressRecord (index); owner.selected = index; }

    void tick()
    {
        const auto state = engine.getLooper().getState (index);
        const auto& layer = engine.getLooper().getLayer (index);
        mute.setToggleState (layer.mute.load(), juce::dontSendNotification);
        solo.setToggleState (owner.soloed == index, juce::dontSendNotification);
        const bool active = engine.getLoopFx (index).isActive();
        fx.setToggleState (active, juce::dontSendNotification);
        const auto fxText = active ? "FX " + juce::String::fromUTF8 ("\xc2\xb7 ") + fxName() : juce::String ("+ Add effect");
        if (fx.getButtonText() != fxText)
            fx.setButtonText (fxText);

        juce::String sig;
        sig << (int) state << "/" << layer.source.load() << "/" << layer.bars.load() << "/" << layer.lengthBeats.load() << "/" << (int) layer.mute.load()
            << "/" << (int) (owner.selected == index);
        if (sig != shown)
        {
            shown = sig;
            repaint();
        }
        wave.tick();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace vintage;
        const auto& layer = engine.getLooper().getLayer (index);
        const auto state = engine.getLooper().getState (index);
        const bool hot = state == RigLooper::State::armed || state == RigLooper::State::recording || state == RigLooper::State::overdubbing;
        const bool hasLoop = state == RigLooper::State::playing || state == RigLooper::State::overdubbing;
        const auto r = getLocalBounds().toFloat();
        drawWell (g, r, 8.0f);
        if (hot)
        {
            g.setColour (colours::arm);
            g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.0f);
        }
        else if (owner.selected == index && hasLoop)
        {
            g.setColour (colours::selected.withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 1.5f);
        }

        // The number: click it to record.
        const auto tile = numberArea.toFloat();
        const auto tileColour = hot ? colours::arm : hasLoop ? (layer.mute.load() ? colours::mute : colours::loops) : juce::Colour (0xff2e2925);
        g.setColour (tileColour);
        g.fillRoundedRectangle (tile, 6.0f);
        g.setColour (hot || hasLoop ? juce::Colour (0xff1e1a16) : pal().wellInk2);
        g.setFont (cond (juce::jmin (26.0f, tile.getHeight() * 0.7f), Weight::extra));
        g.drawText (juce::String (index + 1), tile, juce::Justification::centred);

        // The name and what it records from.
        auto nameBox = nameArea.reduced (2, 0);
        const bool twoLines = nameBox.getHeight() >= 30;
        g.setColour (pal().wellInk);
        g.setFont (cond (juce::jmin (19.0f, nameBox.getHeight() * 0.55f), Weight::extra, 0.05f));
        auto top = twoLines ? nameBox.removeFromTop (nameBox.getHeight() * 55 / 100) : nameBox;
        g.drawText ("LAYER " + juce::String (index + 1), top, twoLines ? juce::Justification::bottomLeft : juce::Justification::centredLeft);
        if (twoLines)
        {
            juce::String sub = RigLooper::sourceName (layer.source.load());
            if (state == RigLooper::State::armed)
                sub << juce::String::fromUTF8 (" \xc2\xb7 waiting");
            else if (state == RigLooper::State::empty)
                sub = "Empty " + juce::String::fromUTF8 ("\xc2\xb7 ") + sub;
            g.setColour (pal().wellInk2);
            g.setFont (barlow (12.0f));
            g.drawText (sub, nameBox, juce::Justification::topLeft);
        }

        // The state, as a lamp and a word.
        Lit lit = Lit::none;
        juce::String word = "Empty";
        switch (state)
        {
            case RigLooper::State::empty: break;
            case RigLooper::State::armed: lit = Lit::arm; word = "Armed"; break;
            case RigLooper::State::recording: lit = Lit::arm; word = "Rec"; break;
            case RigLooper::State::overdubbing: lit = Lit::arm; word = "Overdub"; break;
            case RigLooper::State::playing:
                lit = layer.mute.load() ? Lit::mute : Lit::play;
                word = layer.mute.load() ? "Muted" : "Playing";
                break;
        }
        drawLamp (g, { (float) stateArea.getX() + 8, (float) stateArea.getCentreY() }, lit, 14.0f);
        engraved (g, word, stateArea.withTrimmedLeft (22), juce::Justification::centredLeft, 12.0f, true);

        // Its length in bars.
        const auto box = barsArea.toFloat().withSizeKeepingCentre ((float) barsArea.getWidth(), juce::jmin (30.0f, (float) barsArea.getHeight() - 4));
        drawLcd (g, box, 5.0f);
        const double beats = layer.lengthBeats.load();
        const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
        juce::String bars = "-";
        if (hasLoop && beats > 0)
        {
            const double b = beats / bpb;
            bars = std::abs (b - std::round (b)) < 0.01 ? juce::String ((int) std::round (b)) : juce::String (b, 1);
        }
        else if (layer.bars.load() > 0)
            bars = juce::String (layer.bars.load());
        lcdText (g, bars, box, 22.0f);
    }

    void resized() override
    {
        using namespace looperlayout;
        auto area = getLocalBounds().reduced (6, 5);
        numberArea = area.removeFromLeft (wNumber);
        area.removeFromLeft (wGap + 2);
        nameArea = area.removeFromLeft (wName);
        area.removeFromLeft (wGap);
        stateArea = area.removeFromLeft (wState);
        area.removeFromLeft (wGap);
        const int h = juce::jmin (area.getHeight(), 34);
        more.setBounds (area.removeFromRight (wMore).withSizeKeepingCentre (wMore, h));
        area.removeFromRight (6);
        solo.setBounds (area.removeFromRight (wSolo).withSizeKeepingCentre (wSolo, h));
        area.removeFromRight (6);
        mute.setBounds (area.removeFromRight (wMute).withSizeKeepingCentre (wMute, h));
        area.removeFromRight (wGap);
        level.setBounds (area.removeFromRight (wLevel).withSizeKeepingCentre (wLevel, juce::jmin (wLevel, area.getHeight())));
        area.removeFromRight (wGap);
        barsArea = area.removeFromRight (wBars);
        area.removeFromRight (wGap);
        fx.setBounds (area.removeFromRight (wFx).withSizeKeepingCentre (wFx, h));
        area.removeFromRight (wGap);
        wave.setBounds (area);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (numberArea.contains (e.getPosition()))
            pressNumber();
        else if (nameArea.contains (e.getPosition()) || barsArea.contains (e.getPosition()))
            showSettingsMenu();
        else
        {
            owner.selected = index;
            repaint();
        }
    }

private:
    void say (const juce::String& s)
    {
        if (owner.onSay)
            owner.onSay (s);
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        const auto state = engine.getLooper().getState (index);
        menu.addItem ("Undo the last overdub", state == RigLooper::State::playing || state == RigLooper::State::overdubbing, false,
                      [this] { engine.getLooper().undo (index); });
        menu.addItem ("Clear this layer", state != RigLooper::State::empty, false, [this] { clearLayer(); });
        menu.addSeparator();
        menu.addItem ("Import a loop, stretched to the Rig's tempo...", [this] { importFile (false); });
        menu.addItem ("Import a song or loop, and set the Rig's tempo to it...", [this] { importFile (true); });
        menu.addItem ("Edit where it starts and ends...", engine.getLooper().getState (index) == RigLooper::State::playing, false,
                      [this] { owner.editLoop (index); });
        menu.addItem ("Save this loop as a WAV...", engine.getLooper().getState (index) == RigLooper::State::playing, false, [this] { exportFile(); });
        menu.addSeparator();
        const auto learnItem = [this, &menu] (int control, const juce::String& what)
        {
            const auto [channel, cc] = engine.getRouting().getAssignment (control);
            juce::ignoreUnused (channel);
            menu.addItem (cc >= 0 ? what + " is on CC " + juce::String (cc) + ": forget it" : "Put " + what + " on a keyboard button",
                          [this, control, cc, what]
                          {
                              auto& routing = engine.getRouting();
                              if (cc >= 0)
                              {
                                  routing.forget (control);
                                  say (what + " forgotten.");
                              }
                              else
                              {
                                  routing.learn (control);
                                  say ("Press the button on your keyboard for " + what + ".");
                              }
                          });
        };
        learnItem (RigRouting::loopRecord1 + index, "Loop " + juce::String (index + 1) + " record");
        learnItem (RigRouting::loopMute1 + index, "Loop " + juce::String (index + 1) + " mute");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more));
    }

    void clearLayer()
    {
        engine.getLooper().clear (index);
        juce::Timer::callAfterDelay (100, [safe = juce::Component::SafePointer<Row> (this)]
                                     { if (safe != nullptr) safe->engine.getLooper().makeRoom (safe->index); });
    }

    // What it records from, and for how long.
    void showSettingsMenu()
    {
        auto& layer = engine.getLooper().getLayer (index);
        juce::PopupMenu menu;
        menu.addSectionHeader ("Layer " + juce::String (index + 1) + " records");
        const bool canChange = engine.getLooper().getState (index) != RigLooper::State::recording;
        for (int s = 0; s < RigLooper::numSources; ++s)
            menu.addItem (RigLooper::sourceName (s), canChange, layer.source.load() == s, [this, s] { engine.getLooper().getLayer (index).source = s; });
        menu.addSectionHeader ("For");
        for (int b : RigLooper::barChoices())
            menu.addItem (b == 0 ? "Until you press again" : juce::String (b) + (b == 1 ? " bar" : " bars"), true, layer.bars.load() == b,
                          [this, b] { engine.getLooper().getLayer (index).bars = b; });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (nameArea)));
    }

    // The first effect that's on, for the effect slot.
    juce::String fxName() const
    {
        auto& p = engine.getLoopFx (index).params;
        const auto value = [&p] (const char* id) { auto* v = p.getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; };
        if (value ("repeat") > 0.5f) return "Repeat";
        if (value ("delayMix") > 0.01f) return "Echo";
        if (value ("reverbMix") > 0.01f) return "Reverb";
        if (std::abs (value ("filter")) >= 0.02f) return "Filter";
        if (value ("crush") > 0.01f) return "Lo-fi";
        if (value ("drive") > 0.01f) return "Drive";
        return "On";
    }

    void importFile (bool setTempo)
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Import onto loop " + juce::String (index + 1),
                                                            juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser, setTempo] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  juce::AudioFormatManager formats;
                                  formats.registerBasicFormats();
                                  juce::String error;
                                  auto audio = crate::AudioFiles::load (file, formats, error);
                                  if (audio == nullptr)
                                  {
                                      say ("Couldn't open " + file.getFileName() + ": " + error);
                                      return;
                                  }
                                  auto& transport = engine.getTransport();
                                  const int bpb = transport.getBeatsPerBar();
                                  double beats = crate::SoundFx::guessBeats (audio->getNumSamples(), transport.getBpm());
                                  if (beats <= 0)
                                      beats = bpb;
                                  if (setTempo)
                                  {
                                      // A song: guess its length in bars at a likely tempo (70-180 BPM).
                                      const double seconds = crate::seconds (audio->getNumSamples());
                                      double bpm = crate::SoundFx::tempoOf (audio->getNumSamples(), beats);
                                      while (bpm < 70.0) { beats *= 2; bpm = crate::SoundFx::tempoOf (audio->getNumSamples(), beats); }
                                      while (bpm > 180.0) { beats /= 2; bpm = crate::SoundFx::tempoOf (audio->getNumSamples(), beats); }
                                      transport.setBpm (std::round (bpm * 10.0) / 10.0);
                                      engine.getLooper().setLayerAudio (index, juce::AudioBuffer<float> (*audio), beats);
                                      say (file.getFileNameWithoutExtension() + " on loop " + juce::String (index + 1) + ": "
                                           + barsText (beats, bpb) + " at " + juce::String (bpm, 1) + " BPM (" + juce::String (seconds, 1)
                                           + " s). The Rig's tempo is now " + juce::String (bpm, 1) + ".");
                                      juce::ignoreUnused (seconds);
                                  }
                                  else
                                  {
                                      auto fitted = crate::SoundFx::fitToTempo (audio, beats, transport.getBpm());
                                      engine.getLooper().setLayerAudio (index, juce::AudioBuffer<float> (*fitted), beats);
                                      say (file.getFileNameWithoutExtension() + " on loop " + juce::String (index + 1) + ": "
                                           + barsText (beats, bpb) + ", stretched to " + juce::String (transport.getBpm(), 1) + " BPM.");
                                  }
                                  owner.repaint();
                              });
    }

    void exportFile()
    {
        auto audio = std::make_shared<juce::AudioBuffer<float>> (engine.getLooper().copyLayerAudio (index));
        if (audio->getNumSamples() == 0)
            return;
        auto chooser = std::make_shared<juce::FileChooser> ("Save loop " + juce::String (index + 1),
                                                            juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                                .getChildFile ("Loop " + juce::String (index + 1) + ".wav"),
                                                            "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, chooser, audio] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  file = file.withFileExtension ("wav");
                                  juce::WavAudioFormat wav;
                                  file.deleteFile();
                                  std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
                                  const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
                                  auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
                                  if (stream != nullptr)
                                      if (auto writer = wav.createWriterFor (stream, options))
                                          if (writer->writeFromAudioSampleBuffer (*audio, 0, audio->getNumSamples()))
                                          {
                                              say ("Saved " + file.getFileName() + ".");
                                              return;
                                          }
                                  say ("Couldn't save " + file.getFileName() + ".");
                              });
    }

    LooperView& owner;
    RigEngine& engine;
    const int index;
    juce::TextButton mute { "M" }, solo { "S" }, fx { "+ Add effect" }, more { "..." };
    juce::Slider level;
    LoopWave wave;
    juce::Rectangle<int> numberArea, nameArea, stateArea, barsArea;
    juce::String shown;
};

//==============================================================================
namespace
{
const std::array<int, 5> lengthChoices { 0, 1, 2, 4, 8 };
const std::array<std::pair<int, const char*>, 3> sourceChoices { { { RigLooper::input, "Mic" }, { RigLooper::vocoder, "Vocoder" },
                                                                   { RigLooper::mix, "Master" } } };
}

LooperView::LooperView (RigEngine& e) : engine (e)
{
    using namespace vintage;
    setOpaque (true);
    for (int i = 0; i < RigLooper::kLayers; ++i)
        addAndMakeVisible (rows.add (new Row (*this, engine, i)));
    help.setText ("Loops stay in time with the Rig. Rec starts at the next bar; press again to finish the bar. "
                  "Rec on a playing loop overdubs. Keys 1-8 record too.",
                  juce::dontSendNotification);
    addChildComponent (help);   // the headings and tooltips say this now

    setLit (recNext, Lit::arm);
    recNext.getProperties().set ("fontSize", 19.0f);
    recNext.setTooltip ("Records the next empty layer from the next bar. Press again to finish it at the end of that bar "
                        "(or it stops by itself after the length).");
    recNext.onClick = [this] { pressRecNext(); };
    overdub.setTooltip ("Records over the layer you picked last (click a row to pick it). Press again to stop.");
    overdub.onClick = [this] { pressOverdub(); };
    setLit (overdub, Lit::arm);
    undoButton.setTooltip ("Takes back the last overdub on the layer you picked last.");
    undoButton.onClick = [this] { pressUndo(); };
    for (auto* b : { &recNext, &overdub, &undoButton })
        addAndMakeVisible (*b);
    overdub.getProperties().set ("fontSize", 17.0f);
    undoButton.getProperties().set ("fontSize", 17.0f);

    for (int b : lengthChoices)
    {
        auto* button = lengthButtons.add (new juce::TextButton (b == 0 ? "Free" : juce::String (b)));
        button->getProperties().set ("fontSize", 17.0f);
        button->setTooltip (b == 0 ? "The next layer records until you press again." : "The next layer records " + juce::String (b) + (b == 1 ? " bar." : " bars."));
        button->onClick = [this, b] { setNextBars (b); };
        addAndMakeVisible (button);
    }
    for (const auto& [source, name] : sourceChoices)
    {
        auto* button = sourceButtons.add (new juce::TextButton (name));
        button->getProperties().set ("fontSize", 16.0f);
        button->setTooltip ("The next layer records " + RigLooper::sourceName (source).toLowerCase() + ".");
        button->onClick = [this, s = source] { setNextSource (s); };
        addAndMakeVisible (button);
    }
    setLit (songButton, Lit::current);
    songButton.getProperties().set ("fontSize", 17.0f);
    songButton.setTooltip ("Build the song: which loops (and tools) play in each part, and for how many bars.");
    songButton.onClick = [this] { showSongGrid (songGrid == nullptr); };
    addAndMakeVisible (songButton);
    updateTop();
    startTimerHz (30);
}

int LooperView::nextEmptyLayer() const
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (engine.getLooper().getState (i) == RigLooper::State::empty)
            return i;
    return -1;
}

int LooperView::busyLayer() const
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto state = engine.getLooper().getState (i);
        if (state == RigLooper::State::armed || state == RigLooper::State::recording)
            return i;
    }
    return -1;
}

void LooperView::pressLayer (int layer)
{
    if (juce::isPositiveAndBelow (layer, rows.size()))
        rows[layer]->pressNumber();
}

void LooperView::pressRecNext()
{
    if (const int busy = busyLayer(); busy >= 0)
    {
        engine.getLooper().pressRecord (busy);
        return;
    }
    const int next = nextEmptyLayer();
    if (next < 0)
    {
        if (onSay)
            onSay ("Every layer has a loop. Clear one (its ... menu) to record another.");
        return;
    }
    engine.getLooper().pressRecord (next);
    selected = next;
    updateTop();
}

void LooperView::pressOverdub()
{
    int layer = selected;
    if (layer < 0 || engine.getLooper().getState (layer) == RigLooper::State::empty)
        for (int i = RigLooper::kLayers; --i >= 0;)
            if (engine.getLooper().getState (i) == RigLooper::State::playing)
            {
                layer = i;
                break;
            }
    if (layer < 0 || engine.getLooper().getState (layer) == RigLooper::State::empty)
    {
        if (onSay)
            onSay ("Record a loop first, then Overdub records over it.");
        return;
    }
    selected = layer;
    const auto state = engine.getLooper().getState (layer);
    if (state == RigLooper::State::playing || state == RigLooper::State::overdubbing)
        engine.getLooper().pressRecord (layer);
    updateTop();
}

void LooperView::pressUndo()
{
    if (selected >= 0)
        engine.getLooper().undo (selected);
}

void LooperView::setNextBars (int bars)
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (engine.getLooper().getState (i) == RigLooper::State::empty)
            engine.getLooper().getLayer (i).bars = bars;
    updateTop();
}

void LooperView::setNextSource (int source)
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (engine.getLooper().getState (i) == RigLooper::State::empty)
            engine.getLooper().getLayer (i).source = source;
    updateTop();
}

void LooperView::soloLayer (int layer)
{
    auto& looper = engine.getLooper();
    if (soloed >= 0)
    {
        for (int i = 0; i < RigLooper::kLayers; ++i)
            looper.getLayer (i).mute = mutesBeforeSolo[(size_t) i];
        const bool same = soloed == layer;
        soloed = -1;
        if (same)
            return;
    }
    if (! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return;
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        mutesBeforeSolo[(size_t) i] = looper.getLayer (i).mute.load();
        looper.getLayer (i).mute = i != layer;
    }
    soloed = layer;
}

juce::String LooperView::statusText() const
{
    const auto& looper = engine.getLooper();
    const auto& transport = engine.getTransport();
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    if (const int busy = busyLayer(); busy >= 0)
    {
        const juce::String name = "Layer " + juce::String (busy + 1);
        if (looper.getState (busy) == RigLooper::State::armed)
        {
            if (! transport.isPlaying())
                return name + " waits for Play";
            const double beat = transport.getPositionInBeats();
            const int left = juce::jmax (1, (int) std::ceil (bpb - std::fmod (beat, (double) bpb) - 1.0e-6));
            return name + " in " + juce::String (left) + (left == 1 ? " beat" : " beats");
        }
        return name + " recording";
    }
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (looper.getState (i) == RigLooper::State::overdubbing)
            return "Overdub on layer " + juce::String (i + 1);
    const int next = nextEmptyLayer();
    return next < 0 ? juce::String ("All layers in use") : "Next: layer " + juce::String (next + 1);
}

void LooperView::updateTop()
{
    using namespace vintage;
    const int next = nextEmptyLayer();
    const auto& looper = engine.getLooper();
    const int bars = next >= 0 ? looper.getLayer (next).bars.load() : -1;
    const int source = next >= 0 ? looper.getLayer (next).source.load() : -1;
    for (int i = 0; i < (int) lengthChoices.size(); ++i)
        lengthButtons[i]->setToggleState (bars == lengthChoices[(size_t) i], juce::dontSendNotification);
    for (int i = 0; i < (int) sourceChoices.size(); ++i)
        sourceButtons[i]->setToggleState (source == sourceChoices[(size_t) i].first, juce::dontSendNotification);

    const int busy = busyLayer();
    const auto recText = busy >= 0 ? (looper.getState (busy) == RigLooper::State::armed ? juce::String ("Armed: layer ") + juce::String (busy + 1)
                                                                                        : juce::String ("Stop layer ") + juce::String (busy + 1))
                                   : juce::String::fromUTF8 ("\xe2\x97\x8f Rec next layer");
    if (recNext.getButtonText() != recText)
        recNext.setButtonText (recText);
    // Lit red while a layer waits or records; otherwise a cream button with the red dot.
    recNext.setToggleState (busy >= 0 && (looper.getState (busy) == RigLooper::State::recording
                                          || (juce::Time::getMillisecondCounter() / 400) % 2 == 0),
                            juce::dontSendNotification);
    bool dubbing = false;
    for (int i = 0; i < RigLooper::kLayers; ++i)
        dubbing = dubbing || looper.getState (i) == RigLooper::State::overdubbing;
    overdub.setToggleState (dubbing, juce::dontSendNotification);
    overdub.setButtonText (dubbing ? "Stop dub" : "Overdub");
    undoButton.setEnabled (selected >= 0 && looper.getState (selected) != RigLooper::State::empty);

    const auto status = statusText() + "/" + juce::String (engine.getTransport().getBpm()) + "/" + juce::String ((int) engine.getTransport().isPlaying());
    if (status != shownStatus)
    {
        shownStatus = status;
        repaint (controlsArea);
    }
}

LooperView::~LooperView() { stopTimer(); }

void LooperView::refresh()
{
    for (auto* r : rows)
        r->refresh();
    if (songGrid != nullptr)
        songGrid->refresh();
}

void LooperView::timerCallback()
{
    updateTop();
    for (auto* r : rows)
        r->tick();
    if (fxPanel != nullptr)
        fxPanel->tick();
    if (songGrid != nullptr)
        songGrid->tick();
    if (editor != nullptr)
    {
        editor->tick();
        if (! editor->isValid())
            closeEditor();   // the loop was cleared (from a key, a scene, or the keyboard)
    }
}

void LooperView::showSongGrid (bool show)
{
    if (songGrid != nullptr)
        removeChildComponent (songGrid.get());
    songGrid.reset();
    if (show)
    {
        closeEditor();
        songGrid = std::make_unique<SongGrid> (engine);
        songGrid->onSay = [this] (const juce::String& text) { if (onSay) onSay (text); };
        songGrid->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LooperView> (this)]
                                                                     { if (safe != nullptr) safe->showSongGrid (false); }); };
        addAndMakeVisible (*songGrid);
    }
    songButton.setToggleState (show, juce::dontSendNotification);
    resized();
}

void LooperView::showFx (int layer)
{
    const bool same = fxPanel != nullptr && fxLayer == layer;
    if (fxPanel != nullptr)
        removeChildComponent (fxPanel.get());
    fxPanel.reset();
    fxLayer = -1;
    if (! same && juce::isPositiveAndBelow (layer, RigLooper::kLayers))
    {
        fxLayer = layer;
        fxPanel = std::make_unique<StripFxPanel> (engine.getLoopFx (layer), "Loop " + juce::String (layer + 1) + " effects", vintage::colours::loops);
        fxPanel->closeButton.onClick = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LooperView> (this)]
                                                                               { if (safe != nullptr && safe->fxPanel != nullptr) safe->showFx (safe->fxLayer); }); };
        addAndMakeVisible (*fxPanel);
    }
    resized();
}

void LooperView::editLoop (int layer)
{
    if (songGrid != nullptr)
        showSongGrid (false);
    if (editor != nullptr && editor->getLayer() == layer)
        return;
    closeEditor();
    if (engine.getLooper().getState (layer) != RigLooper::State::playing)
        return;
    auto e = std::make_unique<LoopEditor> (engine, layer);
    if (! e->isValid())
        return;
    e->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LooperView> (this)]
                                                           { if (safe != nullptr) safe->closeEditor(); }); };
    editor = std::move (e);
    addAndMakeVisible (*editor);
    resized();
}

void LooperView::closeEditor()
{
    if (editor == nullptr)
        return;
    removeChildComponent (editor.get());
    editor.reset();
    resized();
}

void LooperView::paint (juce::Graphics& g)
{
    using namespace vintage;
    drawFace (g, getLocalBounds());
    engraved (g, "Length", lengthLabel, juce::Justification::centredLeft, 12.0f);
    engraved (g, "Record from", sourceLabel, juce::Justification::centredLeft, 12.0f);

    // In sync, and what happens next.
    const auto& transport = engine.getTransport();
    const double bpm = transport.getBpm();
    drawLamp (g, { (float) syncArea.getX() + 8, (float) syncArea.getCentreY() }, transport.isPlaying() ? Lit::play : Lit::none, 14.0f);
    engraved (g, juce::String::fromUTF8 ("Synced \xc2\xb7 ") + (std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 1))
                     + juce::String::fromUTF8 (" BPM \xc2\xb7 bars"),
              syncArea.withTrimmedLeft (22), juce::Justification::centredLeft, 12.0f);
    const auto lcdBox = statusArea.toFloat();
    drawLcd (g, lcdBox);
    lcdText (g, statusText().toUpperCase(), lcdBox, juce::jmin (26.0f, lcdBox.getHeight() * 0.8f));

    // The headings over the rows.
    using namespace looperlayout;
    auto h = headings.reduced (6, 0);
    const auto heading = [&g] (const juce::String& text, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft)
    { engraved (g, text, r, j, 11.0f); };
    heading ("Layer", h.removeFromLeft (wNumber + wGap + 2 + wName));
    h.removeFromLeft (wGap);
    heading ("State", h.removeFromLeft (wState));
    h.removeFromLeft (wGap);
    auto right = h.removeFromRight (wMore + 6 + wSolo + 6 + wMute);
    heading ("Mute / solo", right.withTrimmedRight (wMore + 6), juce::Justification::centred);
    h.removeFromRight (wGap);
    heading ("Level", h.removeFromRight (wLevel), juce::Justification::centred);
    h.removeFromRight (wGap);
    heading ("Bars", h.removeFromRight (wBars), juce::Justification::centred);
    h.removeFromRight (wGap);
    heading ("Effect slot", h.removeFromRight (wFx));
    h.removeFromRight (wGap);
    heading (juce::String::fromUTF8 ("Waveform \xc2\xb7 double-click to trim start / end"), h);
}

void LooperView::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (22).withTrimmedRight (22).withTrimmedTop (6).withTrimmedBottom (12);
    auto top = area.removeFromTop (54);
    controlsArea = top;
    {
        auto t = top;
        recNext.setBounds (t.removeFromLeft (200));
        t.removeFromLeft (12);
        overdub.setBounds (t.removeFromLeft (110).reduced (0, 6));
        t.removeFromLeft (8);
        undoButton.setBounds (t.removeFromLeft (84).reduced (0, 6));
        t.removeFromLeft (18);
        auto lengths = t.removeFromLeft (5 * 46 + 4 * 6);
        lengthLabel = lengths.removeFromTop (16);
        for (auto* b : lengthButtons)
        {
            b->setBounds (lengths.removeFromLeft (b == lengthButtons[0] ? 52 : 40).reduced (0, 1));
            lengths.removeFromLeft (6);
        }
        t.removeFromLeft (14);
        auto sources = t.removeFromLeft (3 * 86 + 2 * 6);
        sourceLabel = sources.removeFromTop (16);
        for (auto* b : sourceButtons)
        {
            b->setBounds (sources.removeFromLeft (86).reduced (0, 1));
            sources.removeFromLeft (6);
        }
        t.removeFromLeft (14);
        songButton.setBounds (t.removeFromRight (96).reduced (0, 6));
        t.removeFromRight (10);
        statusArea = t.removeFromRight (juce::jmin (260, juce::jmax (180, t.getWidth() / 2))).reduced (0, 6);
        t.removeFromRight (10);
        syncArea = t;
    }
    area.removeFromTop (8);
    headings = area.removeFromTop (18);
    area.removeFromTop (4);
    if (fxPanel != nullptr)
    {
        fxPanel->setBounds (area.removeFromBottom (130));
        area.removeFromBottom (6);
    }
    const int h = juce::jmin (60, (area.getHeight() - 6 * (rows.size() - 1)) / rows.size());
    for (auto* r : rows)
    {
        r->setBounds (area.removeFromTop (h));
        area.removeFromTop (6);
    }
    if (songGrid != nullptr)
    {
        auto bounds = getLocalBounds().reduced (22, 8).withTrimmedTop (86);
        songGrid->setBounds (bounds);
        songGrid->toFront (false);
    }
    // The editor sits over the layers, leaving its own row (and the ones above) in view.
    if (editor != nullptr)
    {
        auto bounds = getLocalBounds().reduced (22, 8).withTrimmedTop (86);
        if (fxPanel != nullptr)
            bounds = bounds.withTrimmedBottom (136);
        const auto* row = rows[editor->getLayer()];
        const int below = row->getBottom() + 6;
        const int height = juce::jmin (bounds.getHeight(), 320);
        if (bounds.getBottom() - below >= height)
            bounds = bounds.withTop (below).withHeight (height);
        else
            bounds = bounds.withTop (bounds.getBottom() - height);
        editor->setBounds (bounds);
        editor->toFront (false);
    }
}
