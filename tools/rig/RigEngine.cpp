#include "RigEngine.h"
#include "core/TrackAnalysis.h"
#include "RigSong.h"
#include "FactoryKits.h"


#include <algorithm>
#include <map>
#include <mutex>

#include "core/AudioFiles.h"
#include "core/Edits.h"
#include "core/SoundFx.h"

static_assert (RigScene::kLayers == RigLooper::kLayers, "a scene has a mute for each loop layer");

namespace
{
    // Every RigEngine in this program (message thread): the boxes a one-tool plugin can reach.
    juce::Array<RigEngine*>& allEngines()
    {
        static juce::Array<RigEngine*> engines;
        return engines;
    }
}

juce::String RigEngine::stripLabel (int id)
{
    switch (id)
    {
        case drumsStrip: return "Drums";
        case inputStrip: return "Line in";
        default:         return stripName (id);
    }
}

juce::String RigEngine::stripName (int id)
{
    switch (id)
    {
        case mpcStrip:     return "MPC";
        case samplerStrip: return "Sampler";
        case masterStrip:  return "Master";
        case vocoderStrip: return "Vocoder";
        case bassStrip:    return "Bass";
        case synthStrip:   return "Synth";
        case inputStrip:   return "Input";
        case loopsStrip:   return "Loops";
        case drumsStrip:   return "Drum machine";
        case droneStrip:   return "Drone";
        default:           return {};
    }
}

juce::String RigEngine::toolName (int id)
{
    switch (id)
    {
        case mpcTool:     return "MPC";
        case samplerTool: return "Sampler";
        case vocoderTool: return "Vocoder";
        case bassTool:    return "Bass";
        case synthTool_:  return "Synth";
        case drumsTool:   return "Drum machine";
        case droneTool:   return "Drone";
        default:          return isFxTool (id) ? "FX " + stripName (id - fxTool) : juce::String();
    }
}

juce::AudioProcessor& RigEngine::getTool (int id)
{
    switch (id)
    {
        case samplerTool: return sampler;
        case vocoderTool: return vocoder;
        case bassTool:    return bass;
        case synthTool_:  return synthTool;
        case drumsTool:   return drums;
        case droneTool:   return drone;
        default:          return isFxTool (id) ? (juce::AudioProcessor&) fx[(size_t) (id - fxTool)] : mpc;
    }
}

juce::String RigEngine::sampleSourceName (int source)
{
    switch ((SampleSource) source)
    {
        case SampleSource::mic:     return "Mic";
        case SampleSource::vocoder: return "Vocoder";
        case SampleSource::mix:     return "Whole mix";
        default:                    return {};
    }
}

juce::String RigEngine::whyDroneDeaf() const
{
    if (drone.getFeed() != DroneProcessor::feedInput)
        return {};
    if (! micOn.load())
        return "The mic is off";
    return {};
}

juce::String RigEngine::inputJackName (int jack)
{
    switch ((InputJack) jack)
    {
        case InputJack::both:   return "1+2";
        case InputJack::first:  return "In 1";
        case InputJack::second: return "In 2";
        default:                return {};
    }
}

juce::String RigEngine::returnName (int id)
{
    return id == reverbReturn ? "Reverb" : id == echoReturn ? "Echo" : juce::String();
}

const std::vector<std::pair<juce::String, double>>& RigEngine::echoTimes()
{
    static const std::vector<std::pair<juce::String, double>> times { { "1/16", 0.25 }, { "1/8", 0.5 }, { "1/8 dotted", 0.75 },
                                                                      { "1/4 triplet", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/2", 2.0 } };
    return times;
}

bool RigEngine::anySolo() const
{
    for (int s = 0; s < numStrips; ++s)
        if (s != masterStrip && strips[(size_t) s].solo.load())
            return true;
    return false;
}

bool RigEngine::isSilenced (int s) const
{
    if (! juce::isPositiveAndBelow (s, (int) numStrips))
        return false;
    const auto& strip = strips[(size_t) s];
    if (strip.mute.load())
        return true;
    return s != masterStrip && ! strip.solo.load() && anySolo();
}

float RigEngine::faderGain (float level)
{
    // 0.8 -> 0 dB, 1.0 -> +6 dB, and down to silence at 0 along a dB curve.
    level = juce::jlimit (0.0f, 1.0f, level);
    if (level <= 0.0f)
        return 0.0f;
    const float db = (level - 0.8f) * 30.0f;    // +6 at the top, -24 at 0
    const float gain = juce::Decibels::decibelsToGain (db);
    // Fade the bottom of the fader smoothly into silence.
    return level < 0.1f ? gain * (level / 0.1f) : gain;
}

static juce::String timingKey()
{
    return juce::JUCEApplicationBase::isStandaloneApp() ? "app" : "plugin";
}

RigEngine::RigEngine()
{
    automation.numTools = numTools;
    automation.getParameter = [this] (int tool, int parameter) -> juce::AudioProcessorParameter*
    {
        if (! juce::isPositiveAndBelow (tool, (int) numTools))
            return nullptr;
        const auto& list = getTool (tool).getParameters();
        return juce::isPositiveAndBelow (parameter, list.size()) ? list[parameter] : nullptr;
    };
    automation.numParameters = [this] (int tool) { return juce::isPositiveAndBelow (tool, (int) numTools) ? getTool (tool).getParameters().size() : 0; };
    automation.toolName = [] (int tool) { return toolName (tool); };
    macros.getParameter = automation.getParameter;
    macros.numParameters = automation.numParameters;
    macros.toolName = automation.toolName;
    automation.dirty = &dirty;
    macros.dirty = &dirty;
    arrangeFrom.fill (1.0f);
    arrangeTo.fill (1.0f);
    arrangeRestart.fill (-1.0);
    // The Rig is the host, so the tools follow its clock the way they follow Logic's.
    bassTrim.attach (bass.params, RigSounds::bass());
    synthTrim.attach (synthTool.params, RigSounds::synth());
    vocoderTrim.attach (vocoder.params.state, "program", [this] { return RigSounds::vocoderTrimDb (vocoder.getProgramName (vocoder.getCurrentProgram())); });
    mpc.setPlayHead (&transport);
    sampler.setPlayHead (&transport);
    vocoder.setPlayHead (&transport);
    bass.setPlayHead (&transport);
    synthTool.setPlayHead (&transport);
    drums.setPlayHead (&transport);
    drone.setPlayHead (&transport);   // (for a pluck on every beat; it sounds with the clock stopped too)
    drone.getStateInformation (droneDefaults);
    for (auto& f : fx)
        f.setPlayHead (&transport);
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        loopFx[(size_t) i].setPlayHead (&transport);
        looper.setEffects (i, &loopFx[(size_t) i]);
    }
    transport.setBpm (mpc.getProject().currentSequence().bpm);
    // The MPC changes sequence (and moves through its songs) on the Rig's bar lines.
    mpc.getSequencer().switchWhileFollowing = true;
    mpc.switchAtBar = true;
    // The MPC's Play, Stop and Rec are the Rig's: one clock, and Stop always stops it.
    mpc.hostCommand = [this] (MpcProcessor::HostCommand c) { mpcCommand (c); };
    // The drum machine waits for its Play (on the Groove screen), so a new Rig starts quiet.
    if (auto* p = drums.params.getParameter ("sequencer"))
        p->setValueNotifyingHost (0.0f);
    // This Mac's recording timing.
    if (std::unique_ptr<juce::XmlElement> xml { juce::XmlDocument::parse (timingFile()) }; xml != nullptr && xml->hasTagName ("Timing"))
        timingOffsetMs = juce::jlimit (-50.0f, 100.0f, (float) xml->getDoubleAttribute (timingKey(), 0.0));
    // The phone's Street kit on the pads, so the pads play from the first launch (and after New song).
    if (startWithFactoryKit.load())
        loadFactoryKit (0, 0);
    blankShow = saveShow();
    blankShow.removeChild (blankShow.getChildWithName ("Quantize"), nullptr);   // kept by New song
    blankShow.removeChild (blankShow.getChildWithName (RigChords::kTreeType), nullptr);   // so are your chords
    allEngines().add (this);
}

void RigEngine::newShow()
{
    const auto keep = screens.createCopy();
    const auto quantizeSettings = quantizer.settings;
    const auto keepChords = chords.toTree();
    loadShow (blankShow);
    // The factory kit's own sounds again (not their saved copies, which a WAV round trip changes by
    // a hair), so New song is blank.
    if (startWithFactoryKit.load())
        loadFactoryKit (0, 0);
    screens = keep;
    quantizer.settings = quantizeSettings;
    chords.fromTree (keepChords);
    dirty.showMadeNew();
}

bool RigEngine::isBlank() const
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (looper.getState (i) != RigLooper::State::empty)
            return false;
    auto now = saveShow();
    now.removeChild (now.getChildWithName ("Screens"), nullptr);
    now.removeChild (now.getChildWithName ("Quantize"), nullptr);
    now.removeChild (now.getChildWithName (RigChords::kTreeType), nullptr);
    return now.isEquivalentTo (blankShow);
}

void RigEngine::pressRecMoves()
{
    const bool wasOff = automation.getRecState() == RigAutomation::RecState::off;
    automation.pressRecord (songInCharge());
    if (wasOff && ! transport.isPlaying())
    {
        transport.play();
        controlsMoved = true;
    }
}

void RigEngine::playMpcSequence (int index)
{
    mpc.selectSequence (index);
}

bool RigEngine::playMpcSong (int song)
{
    auto& project = mpc.getProject();
    if (! juce::isPositiveAndBelow (song, (int) project.getSongs().size()))
        return false;
    if (song != project.getCurrentSongIndex())
        project.setCurrentSong (song);
    if (! mpc.playSong (0))
        return false;
    if (! transport.isPlaying())
        transport.play();   // stopped is always at the top
    return true;
}

void RigEngine::setMpcBeatOn (bool on)
{
    mpc.getSequencer().followHost = on;
    if (! on)
        mpc.stopTransport();
    controlsMoved = true;
}

void RigEngine::mpcCommand (MpcProcessor::HostCommand c)
{
    using C = MpcProcessor::HostCommand;
    switch (c)
    {
        case C::play:
        case C::playFromStart:
            setMpcBeatOn (true);
            if (! transport.isPlaying())
                transport.play();   // stopped is always at the top
            else if (c == C::playFromStart && ! mpc.isSongPlaying())
                transport.setPosition (0.0);
            break;
        case C::stop:
            transport.stop();
            allNotesOff();
            mpc.stopTransport();
            break;
        case C::record:
            // Recording goes onto the MPC's sequence on the Rig's clock (from the top when stopped).
            setMpcBeatOn (true);
            mpc.toggleRecord();
            if (! transport.isPlaying())
                transport.play();
            break;
        case C::beatOn:  setMpcBeatOn (true); break;
        case C::beatOff: setMpcBeatOn (false); break;
    }
}

int RigEngine::getPadLoopLayer() const
{
    if (! juce::isPositiveAndBelow (padLoopLayer, RigLooper::kLayers))
        return -1;
    // (Just pressed, the layer is still empty until the audio thread takes the press.)
    const auto state = looper.getState (padLoopLayer);
    return state == RigLooper::State::armed || state == RigLooper::State::recording || state == RigLooper::State::empty ? padLoopLayer : -1;
}

juce::String RigEngine::loopMpcPads (int bars)
{
    if (const int busy = getPadLoopLayer(); busy >= 0)
    {
        const bool waiting = looper.getState (busy) == RigLooper::State::armed;
        looper.pressRecord (busy);
        padLoopLayer = -1;
        return waiting ? "Loop " + juce::String (busy + 1) + " won't record after all."
                       : "Loop " + juce::String (busy + 1) + " ends at the end of this bar, then plays round.";
    }
    int layer = -1;
    for (int i = 0; i < RigLooper::kLayers && layer < 0; ++i)
        if (looper.getState (i) == RigLooper::State::empty && ! looper.isSwapPending (i))
            layer = i;
    if (layer < 0)
        return "Every looper layer has a loop. Clear one on the Looper (F3) to record the pads.";
    auto& l = looper.getLayer (layer);
    l.source = (int) RigLooper::mpc;
    l.bars = juce::jmax (0, bars);
    looper.pressRecord (layer);
    padLoopLayer = layer;
    juce::String text ("Loop " + juce::String (layer + 1) + " records the MPC from the next bar");
    text << (bars > 0 ? ", for " + juce::String (bars) + (bars == 1 ? " bar." : " bars.") : juce::String (": press Loop pads again to end it."));
    if (isMpcBeatOn())
        text << " (The MPC's beat is on, so it goes in too; switch Beat off for just your pads.)";
    return text;
}

RigEngine::~RigEngine()
{
    allEngines().removeAllInstancesOf (this);
    loopRoom.stopTimer();
    mpc.setPlayHead (nullptr);
    sampler.setPlayHead (nullptr);
    vocoder.setPlayHead (nullptr);
    bass.setPlayHead (nullptr);
    synthTool.setPlayHead (nullptr);
    drums.setPlayHead (nullptr);
    drone.setPlayHead (nullptr);
    for (auto& f : fx)
        f.setPlayHead (nullptr);
    for (auto& f : loopFx)
        f.setPlayHead (nullptr);
}

void RigEngine::addMidi (const juce::MidiMessage& m)
{
    auto copy = m;
    copy.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    incoming.addMessageToQueue (copy);
}

void RigEngine::addMidiNow (const juce::MidiMessage& m)
{
    const juce::SpinLock::ScopedLockType sl (nowLock);
    nowMidi.addEvent (m, 0);
}

void RigEngine::prepare (double rate, int maximumBlockSize)
{
    sampleRate = rate > 0 ? rate : 44100.0;
    maxBlock = juce::jmax (16, maximumBlockSize);
    chords.prepare (sampleRate);
    masterLimiter.prepare (sampleRate);
    outputLimiter.prepare (sampleRate);
    for (auto& l : stemLimiters)
        l.prepare (sampleRate);

    for (auto* p : std::initializer_list<juce::AudioProcessor*> { &mpc, &sampler, &vocoder, &bass, &synthTool, &drums, &drone })
    {
        p->setRateAndBufferSizeDetails (sampleRate, maxBlock);
        p->prepareToPlay (sampleRate, maxBlock);
    }
    for (auto& f : fx)
    {
        f.setRateAndBufferSizeDetails (sampleRate, maxBlock);
        f.prepareToPlay (sampleRate, maxBlock);
    }
    for (auto* slot : { &bassPlugin, &synthPlugin })
    {
        slot->setPlayHead (&transport);
        slot->prepare (sampleRate, maxBlock);
    }
    bassTap.ensureSize (2048);
    synthTap.ensureSize (2048);
    transport.prepare (sampleRate);
    incoming.reset (sampleRate);

    mpcBuffer.setSize (2, maxBlock);
    samplerBuffer.setSize (2, maxBlock);
    vocoderBuffer.setSize (2, maxBlock);
    bassBuffer.setSize (2, maxBlock);
    synthBuffer.setSize (2, maxBlock);
    drumsBuffer.setSize (2, maxBlock);
    droneBuffer.setSize (2, maxBlock);
    droneFeedBuffer.setSize (2, maxBlock);
    inputBuffer.setSize (2, maxBlock);
    rawInputBuffer.setSize (2, maxBlock);
    loopsBuffer.setSize (2, maxBlock);
    looper.prepare (sampleRate, maxBlock);
    loopRoom.startTimerHz (10);
    performFx.prepare (sampleRate);
    mixBuffer.setSize (2, maxBlock);
    clickBuffer.setSize (2, maxBlock);
    friendBuffer.setSize (2, maxBlock);
    friendTrack.prepare (sampleRate);
    capture.prepare (sampleRate);
    for (auto* m : { &midiIn, &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi, &drumsMidi, &droneMidi, &vocoderMissed, &midiScratch, &takeMidi })
        m->ensureSize (4096);
    vocoderFade.reset (sampleRate, 0.015);
    vocoderFade.setCurrentAndTargetValue (vocoderOn.load() ? 1.0f : 0.0f);
    inputFade.reset (sampleRate, 0.015);
    inputFade.setCurrentAndTargetValue (0.0f);
    micFade.reset (sampleRate, 0.015);
    micFade.setCurrentAndTargetValue (micOn.load() ? 1.0f : 0.0f);
    droneMicFade.reset (sampleRate, 0.015);
    droneMicFade.setCurrentAndTargetValue (0.0f);
    vocoderMissed.clear();

    for (int s = 0; s < numStrips; ++s)
    {
        gains[(size_t) s].reset (sampleRate, 0.02);
        gains[(size_t) s].setCurrentAndTargetValue (isSilenced (s) ? 0.0f : faderGain (strips[(size_t) s].level.load()));
        pans[(size_t) s].reset (sampleRate, 0.02);
        pans[(size_t) s].setCurrentAndTargetValue (strips[(size_t) s].pan.load());
        sendGainsA[(size_t) s].reset (sampleRate, 0.02);
        sendGainsA[(size_t) s].setCurrentAndTargetValue (strips[(size_t) s].sendA.load());
        sendGainsB[(size_t) s].reset (sampleRate, 0.02);
        sendGainsB[(size_t) s].setCurrentAndTargetValue (strips[(size_t) s].sendB.load());
    }
    // The mixer's shared reverb and echo (up to 4 seconds of echo, for slow tempos).
    sendBufferA.setSize (2, maxBlock);
    sendBufferB.setSize (2, maxBlock);
    echoLine.setSize (2, (int) (sampleRate * 4.0) + 1);
    echoLine.clear();
    echoWrite = 0;
    echoTone[0] = echoTone[1] = 0.0f;
    sendReverb.setSampleRate (sampleRate);
    sendReverb.reset();
    for (int r = 0; r < numReturns; ++r)
    {
        returnGains[(size_t) r].reset (sampleRate, 0.02);
        returnGains[(size_t) r].setCurrentAndTargetValue (returns[(size_t) r].mute.load() ? 0.0f : faderGain (returns[(size_t) r].level.load()));
    }
    prepared = true;
    applyTiming();
}

juce::String RigEngine::stemName (int s)
{
    static const char* names[] = { "MPC", "Vocoder", "Bass", "Synth", "Input", "Sampler", "Loops", "Drum machine" };
    return juce::isPositiveAndBelow (s, (int) numStems) ? names[s] : "";
}

int RigEngine::stripOfStem (int stem)
{
    static constexpr int strips[numStems] { mpcStrip, vocoderStrip, bassStrip, synthStrip, inputStrip, samplerStrip, loopsStrip, drumsStrip };
    return juce::isPositiveAndBelow (stem, (int) numStems) ? strips[stem] : -1;
}

int RigEngine::stemOfStrip (int strip)
{
    for (int st = 0; st < numStems; ++st)
        if (stripOfStem (st) == strip)
            return st;
    return -1;
}

void RigEngine::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    if (! prepared)
        return;

    {
        float peak = 0.0f;
        for (int ch = 0; ch < juce::jmin (numInputs, 2); ++ch)
            if (inputs[ch] != nullptr)
                peak = juce::jmax (peak, juce::FloatVectorOperations::findMaximum (inputs[ch], numSamples),
                                   -juce::FloatVectorOperations::findMinimum (inputs[ch], numSamples));
        if (peak > micPeak.load (std::memory_order_relaxed))
            micPeak.store (peak, std::memory_order_relaxed);
    }

    for (int offset = 0; offset < numSamples;)
    {
        // (Up to the timeline's next jump or the end of its cycle, so it lands exactly.)
        const int n = samplesToTimelineEvent (juce::jmin (maxBlock, numSamples - offset));
        processChunk (inputs, numInputs, offset, n);
        // The click and the preview join after the master: the last safety limiter catches them
        // (a no-op below -3 dBFS).
        outputLimiter.process (mixBuffer.getWritePointer (0), mixBuffer.getWritePointer (1), n);

        // Mono outputs get both sides; stereo gets left and right.
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
            {
                if (numOutputs == 1)
                {
                    juce::FloatVectorOperations::copy (outputs[ch] + offset, mixBuffer.getReadPointer (0), n);
                    juce::FloatVectorOperations::add (outputs[ch] + offset, mixBuffer.getReadPointer (1), n);
                    juce::FloatVectorOperations::multiply (outputs[ch] + offset, 0.5f, n);
                }
                else if (ch < 2)
                {
                    juce::FloatVectorOperations::copy (outputs[ch] + offset, mixBuffer.getReadPointer (ch), n);
                }
            }
        offset += n;
    }
}

void RigEngine::applyControls (const RigRouting::Result& r)
{
    bool moved = false;
    const std::pair<int, int> faders[] = { { RigRouting::mpcLevel, mpcStrip },
                                           { RigRouting::samplerLevel, samplerStrip },
                                           { RigRouting::masterLevel, masterStrip },
                                           { RigRouting::vocoderLevel, vocoderStrip },
                                           { RigRouting::bassLevel, bassStrip },
                                           { RigRouting::synthLevel, synthStrip },
                                           { RigRouting::inputLevel, inputStrip },
                                           { RigRouting::loopsLevel, loopsStrip },
                                           { RigRouting::drumsLevel, drumsStrip } };
    for (auto [control, strip] : faders)
        if (r.values[(size_t) control] >= 0.0f)
        {
            strips[(size_t) strip].level = r.values[(size_t) control];
            moved = true;
        }

    // (Learned buttons are your hand: the dirty model takes these as yours.)
    if (r.presses[RigRouting::mpcMute] % 2 == 1)
    {
        strips[mpcStrip].mute = ! strips[mpcStrip].mute.load();
        dirty.authoredField (RigDirty::stripMute, mpcStrip);
        moved = true;
    }
    if (r.presses[RigRouting::samplerMute] % 2 == 1)
    {
        strips[samplerStrip].mute = ! strips[samplerStrip].mute.load();
        dirty.authoredField (RigDirty::stripMute, samplerStrip);
        moved = true;
    }
    if (r.presses[RigRouting::vocoderMute] % 2 == 1)
    {
        strips[vocoderStrip].mute = ! strips[vocoderStrip].mute.load();
        dirty.authoredField (RigDirty::stripMute, vocoderStrip);
        moved = true;
    }
    for (auto [control, strip] : { std::pair<int, int> { RigRouting::bassMute, bassStrip }, { RigRouting::synthMute, synthStrip }, { RigRouting::inputMute, inputStrip }, { RigRouting::loopsMute, loopsStrip }, { RigRouting::drumsMute, drumsStrip } })
        if (r.presses[(size_t) control] % 2 == 1)
        {
            strips[(size_t) strip].mute = ! strips[(size_t) strip].mute.load();
            dirty.authoredField (RigDirty::stripMute, strip);
            moved = true;
        }

    // Learned knobs move their settings. The tools read their settings every block, so the
    // change is heard in this one.
    for (int i = 0; i < r.numParamMoves; ++i)
    {
        const auto& move = r.paramMoves[(size_t) i];
        if (! juce::isPositiveAndBelow (move.tool, (int) numTools))
            continue;
        const auto& list = getTool (move.tool).getParameters();
        if (juce::isPositiveAndBelow (move.parameter, list.size()))
            list[move.parameter]->setValueNotifyingHost (move.value);
    }

    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        for (int p = r.presses[(size_t) (RigRouting::loopRecord1 + i)]; p > 0; --p)
            looper.pressRecord (i);
        if (r.presses[(size_t) (RigRouting::loopMute1 + i)] % 2 == 1)
        {
            auto& layer = looper.getLayer (i);
            layer.mute = ! layer.mute.load();
            dirty.authoredField (RigDirty::loopMute, i);
            moved = true;
        }
    }
    // The chord builder's live key (learned buttons: a key, its octave, home).
    for (int k = 0; k < 12; ++k)
        if (r.presses[(size_t) (RigRouting::liveKeyC + k)] > 0)
            chords.setLiveKey (k);
    if (const int down = r.presses[RigRouting::liveOctaveDown], up = r.presses[RigRouting::liveOctaveUp]; down != up)
        chords.nudgeLiveOctave (up - down);
    if (r.presses[RigRouting::liveKeyHome] > 0)
        chords.liveKeyHome();
    // NOTE REPEAT from the keyboard: held while the learned button is down; the other steps the rate.
    {
        auto& sequencer = mpc.getSequencer();
        if (const int hold = r.holds[RigRouting::noteRepeatHold]; hold >= 0)
            sequencer.midiRepeat = hold == 1;
        for (int p = r.presses[RigRouting::noteRepeatRate]; p > 0; --p)
            sequencer.stepRepeatRate();
    }
    if (r.presses[RigRouting::chordsOnOff] % 2 == 1)
        chords.toggleChords();
    scenes.nextPresses += r.presses[RigRouting::nextScene];
    scenes.backPresses += r.presses[RigRouting::previousScene];
    if (r.presses[RigRouting::inputHear] % 2 == 1)
    {
        inputOn = ! inputOn.load();
        dirty.authoredField (RigDirty::inputOn, 0);
        moved = true;
    }
    if (r.presses[RigRouting::vocoderOn] % 2 == 1)
    {
        vocoderOn = ! vocoderOn.load();
        dirty.authoredField (RigDirty::vocoderOn, 0);
        moved = true;
    }
    if (r.presses[RigRouting::metronome] % 2 == 1)
    {
        transport.metronome = ! transport.metronome.load();
        moved = true;
    }
    if (r.presses[RigRouting::stop] > 0)
    {
        transport.stop();
        notesOffRequested = true;
        moved = true;
    }
    else if (r.presses[RigRouting::play] > 0)
    {
        transport.togglePlay();
        moved = true;
    }
    if (moved)
        controlsMoved = true;
}

void RigEngine::processChunk (const float* const* inputs, int numInputs, int offset, int n)
{
    // What came in from the keyboards, and what the window or tests queued.
    midiIn.clear();
    incoming.removeNextBlockOfMessages (midiIn, n);
    {
        const juce::SpinLock::ScopedTryLockType sl (nowLock);
        if (sl.isLocked() && ! nowMidi.isEmpty())
        {
            midiIn.addEvents (nowMidi, 0, -1, 0);
            nowMidi.clear();
        }
    }
    if (hostMidi != nullptr)
        for (const auto m : *hostMidi)
            if (m.samplePosition >= offset && m.samplePosition < offset + n)
                midiIn.addEvent (m.getMessage(), m.samplePosition - offset);
    if (! midiIn.isEmpty())
    {
        lastMidiTime = juce::Time::getMillisecondCounter();
        dirty.noteMidi();
    }

    mpcMidi.clear();
    samplerMidi.clear();
    vocoderMidi.clear();
    bassMidi.clear();
    synthMidi.clear();
    drumsMidi.clear();
    RigRouting::Result controls;
    std::array<juce::MidiBuffer*, RigRouting::kNumDests> destinations { nullptr, &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi, &drumsMidi };
    // The chord builder's pads and keys come out first (they play chords, not their own notes).
    chords.takeTriggers (midiIn, routing.padChannel.load(), routing.splitNote.load());
    routing.route (midiIn, destinations, controls);
    applyControls (controls);
    if (notesOffRequested.load())
        chords.stopAll();
    // A one-tool plugin: whatever the routing made of it, every note plays its tool.
    auto allToOnlyTool = [&]
    {
        if (const int only = onlyTool.load(); only >= 0)
        {
            auto* to = only == mpcStrip ? &mpcMidi : only == drumsStrip ? &drumsMidi : only == vocoderStrip ? &vocoderMidi
                     : only == bassStrip ? &bassMidi : &synthMidi;
            for (auto* m : { &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi, &drumsMidi })
                if (m != to && ! m->isEmpty())
                {
                    to->addEvents (*m, 0, -1, 0);
                    m->clear();
                }
        }
    };
    allToOnlyTool();
    if (notesOffRequested.exchange (false))
        for (auto* m : { &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi, &drumsMidi })
        {
            m->addEvent (juce::MidiMessage::allNotesOff (1), 0);
            m->addEvent (juce::MidiMessage::allSoundOff (1), 0);
        }

    // Play and stop land here, at the start of the block, for every tool at once.
    transport.beginBlock();

    // A scene whose bar has come.
    {
        RigScene::State scene;
        double firedBeat = 0;
        if (scenes.takeDue (transport.getBlockPosition(), transport.isPlayingThisBlock(), scene, firedBeat))
        {
            applyScene (scene, firedBeat);
            controlsMoved = true;
        }
    }
    arrange (n);

    // The show's macro knobs move their settings (before automation, so Rec moves records them).
    macros.process();

    // Automation: the knob moves recorded, and what you move now (after the learned knobs
    // above, so their moves are caught), before the tools play.
    {
        RigAutomation::Clock autoClock;
        autoClock.beat = transport.getBlockPosition();
        autoClock.bpm = transport.getBpm();
        autoClock.beatsPerBar = transport.getBeatsPerBar();
        autoClock.playing = transport.isPlayingThisBlock();
        autoClock.sampleRate = sampleRate;
        autoClock.samples = n;
        automation.process (autoClock);
    }

    // The chord builder's chords, into the instruments they're sent to (before the recorder,
    // so Record set has them).
    {
        RigChords::Clock chordClock;
        chordClock.beat = transport.getBlockPosition();
        chordClock.bpm = transport.getBpm();
        chordClock.sampleRate = sampleRate;
        chordClock.beatsPerBar = transport.getBeatsPerBar();
        chordClock.playing = transport.isPlayingThisBlock();
        chords.process (chordClock, n, routing.keysTo.load(), destinations);
        // In a one-tool plugin the chords play its tool too, wherever the chord builder sends them.
        allToOnlyTool();
    }

    // The drone's Follow keys: what the keys play, whichever tool they play.
    droneMidi.clear();
    if (drone.params.getRawParameterValue ("droneFollow")->load() > 0.5f)
        if (auto* keys = destinations[(size_t) juce::jlimit (0, RigRouting::kNumDests - 1, routing.keysTo.load())])
            droneMidi.addEvents (*keys, 0, -1, 0);

    // Recording the set: what's played, by tool, before the tools see it.
    RigRecorder::Clock recClock;
    recClock.beat = transport.getBlockPosition();
    recClock.bpm = transport.getBpm();
    recClock.sampleRate = sampleRate;
    recClock.beatsPerBar = transport.getBeatsPerBar();
    recClock.playing = transport.isPlayingThisBlock();
    {
        int track = 0;
        for (auto* buffer : { &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi, &drumsMidi })
        {
            for (const auto m : *buffer)
                recorder.addMidi (track, m.getMessage(), m.samplePosition, recClock);
            ++track;
        }
    }

    // A take from a tool played by notes keeps what's played into it (before the song's own
    // notes go in, below).
    takeMidi.clear();
    {
        const int source = takes.getLayer() >= 0 ? takes.getSource() : -1;
        const juce::MidiBuffer* from = source == RigLooper::bass ? &bassMidi : source == RigLooper::synth ? &synthMidi
                                     : source == RigLooper::sampler ? &samplerMidi : source == RigLooper::mpc ? &mpcMidi
                                     : source == RigLooper::drums ? &drumsMidi : nullptr;
        if (from != nullptr && RigTakes::isNotesLayer (takes.getLayer()))
            takeMidi.addEvents (*from, 0, -1, 0);
    }
    // The song's notes (the notes takes), into their tools.
    {
        std::array<juce::MidiBuffer*, numStrips> dests {};
        dests[mpcStrip] = &mpcMidi;
        dests[samplerStrip] = &samplerMidi;
        dests[bassStrip] = &bassMidi;
        dests[synthStrip] = &synthMidi;
        dests[drumsStrip] = &drumsMidi;
        const bool songPlays = timeline.on.load() && transport.isPlayingThisBlock();
        const int recLayer = takes.getLayer();
        notePlayer.process (transport.getBlockPosition(), n, transport.getBpm(), sampleRate, transport.getBeatsPerBar(), songPlays,
                            dests.data(), (int) dests.size(),
                            RigTakes::isNotesLayer (recLayer) ? RigTakes::stripOfNotesLayer (recLayer) : -1, takes.getStartBar());
    }

    // The mixer: a solo anywhere silences the strips that aren't soloed; the sends fill up as
    // each strip plays.
    soloActive = anySolo();
    for (auto* b : { &sendBufferA, &sendBufferB })
        b->clear (0, n);

    juce::AudioBuffer<float> mpcBlock (mpcBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> samplerBlock (samplerBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> vocoderBlock (vocoderBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> bassBlock (bassBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> synthBlock (synthBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> drumsBlock (drumsBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> mix (mixBuffer.getArrayOfWritePointers(), 2, n);

    // The audio input, on both sides (a mono mic plays in the middle).
    auto copyInput = [&] (juce::AudioBuffer<float>& dest)
    {
        dest.clear();
        for (int ch = 0; ch < 2 && numInputs > 0; ++ch)
            if (const auto* in = inputs[juce::jmin (ch, numInputs - 1)])
                dest.copyFrom (ch, 0, in + offset, n);
    };

    // (In a one-tool plugin only its tool runs; the others stay quiet and cost nothing.)
    mpcBlock.clear();
    if (runsStrip (mpcStrip))
        mpc.processBlock (mpcBlock, mpcMidi);
    drumsBlock.clear();
    if (runsStrip (drumsStrip))
        drums.processBlock (drumsBlock, drumsMidi);
    // A plugin in the Bass's or Synth's slot plays the notes the tool plays (its steps, arp and
    // keys) instead of the tool's own sound.
    // The tool's own sound gets its level trim (so every sound is as loud as the others); a
    // plugin's sound is left as the plugin makes it.
    // With Visuals open, the notes the tool plays (its steps, arp and keys) are heard by the picture too.
    const bool visualsHear = visualTap.listening();
    auto playTool = [this, visualsHear] (juce::AudioProcessor& tool, juce::AudioBuffer<float>& block, juce::MidiBuffer& midi, PluginSlot& slot,
                                         juce::MidiBuffer& tap, auto setTap, const RigSounds::Trim& trim, float& trimWas)
    {
        block.clear();
        const bool external = slot.isLoaded() && ! slot.bypassed.load();
        tap.clear();
        setTap (external || visualsHear ? &tap : nullptr);
        tool.processBlock (block, midi);
        if (visualsHear)
            for (const auto m : tap)
                if (m.getMessage().isNoteOn())
                    visualTap.noteOn (m.getMessage().getNoteNumber(), m.getMessage().getFloatVelocity());
        if (external && ! slot.process (block, tap))
            block.clear();
        const float to = external ? 1.0f : trim.gain();
        if (to != trimWas || to != 1.0f)
            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                block.applyGainRamp (ch, 0, block.getNumSamples(), trimWas, to);
        trimWas = to;
    };
    bassBlock.clear();
    if (runsStrip (bassStrip))
        playTool (bass, bassBlock, bassMidi, bassPlugin, bassTap, [this] (juce::MidiBuffer* t) { bass.setNoteTap (t); }, bassTrim, bassTrimWas);
    synthBlock.clear();
    if (runsStrip (synthStrip))
        playTool (synthTool, synthBlock, synthMidi, synthPlugin, synthTap, [this] (juce::MidiBuffer* t) { synthTool.setNoteTap (t); }, synthTrim, synthTrimWas);

    // The vocoder shapes its synth with your voice from the input.
    const bool hearMic = micOn.load();
    // Switched off, it fades out and then stops (it uses the most processor time); the keys
    // you let go meanwhile are kept for it, so a note can't hang when it comes back on.
    vocoderFade.setTargetValue (vocoderOn.load() && runsStrip (vocoderStrip) ? 1.0f : 0.0f);
    if (vocoderFade.getCurrentValue() > 0.0f || vocoderFade.isSmoothing())
    {
        if (! vocoderMissed.isEmpty())
        {
            midiScratch.clear();
            midiScratch.addEvents (vocoderMissed, 0, -1, 0);
            midiScratch.addEvents (vocoderMidi, 0, -1, 0);
            vocoderMidi.swapWith (midiScratch);
            vocoderMissed.clear();
        }
        micFade.setTargetValue (hearMic ? 1.0f : 0.0f);
        if (micFade.getCurrentValue() > 0.0f || micFade.isSmoothing())
        {
            copyInput (vocoderBlock);
            if (micFade.isSmoothing())
            {
                const float from = micFade.getCurrentValue();
                micFade.skip (n);
                for (int ch = 0; ch < 2; ++ch)
                    vocoderBlock.applyGainRamp (ch, 0, n, from, micFade.getCurrentValue());
            }
        }
        else
        {
            vocoderBlock.clear();
        }
        vocoder.processBlock (vocoderBlock, vocoderMidi);
        {
            // The sound's level trim, so every vocoder sound is as loud as the others.
            const float to = vocoderTrim.gain();
            if (to != vocoderTrimWas || to != 1.0f)
                for (int ch = 0; ch < 2; ++ch)
                    vocoderBlock.applyGainRamp (ch, 0, n, vocoderTrimWas, to);
            vocoderTrimWas = to;
        }
        if (vocoderFade.isSmoothing())
        {
            const float from = vocoderFade.getCurrentValue();
            vocoderFade.skip (n);
            for (int ch = 0; ch < 2; ++ch)
                vocoderBlock.applyGainRamp (ch, 0, n, from, vocoderFade.getCurrentValue());
        }
    }
    else
    {
        for (const auto m : vocoderMidi)
        {
            const auto message = m.getMessage();
            if (message.isNoteOff() || message.isAllNotesOff() || message.isAllSoundOff())
                vocoderMissed.addEvent (message, 0);
        }
        vocoderBlock.clear();
    }

    // What the sampler hears (and records): the mic, the vocoder as it sounds before its
    // fader, or every other tool as you hear it. Never the sampler itself.
    const auto source = (SampleSource) sampleSource.load();
    if (source == SampleSource::vocoder)
        for (int ch = 0; ch < 2; ++ch)
            samplerBlock.copyFrom (ch, 0, vocoderBlock, ch, 0, n);
    else if (source == SampleSource::mic)
        copyInput (samplerBlock);

    applyStrip (mpcStrip, mpcBlock);
    applyStrip (vocoderStrip, vocoderBlock);
    applyStrip (bassStrip, bassBlock);
    applyStrip (synthStrip, synthBlock);
    applyStrip (drumsStrip, drumsBlock);

    // The Input strip: a guitar or another mic, through its effects.
    juce::AudioBuffer<float> inputBlock (inputBuffer.getArrayOfWritePointers(), 2, n);
    inputFade.setTargetValue (inputOn.load() && hearMic && numInputs > 0 && runsStrip (inputStrip) ? 1.0f : 0.0f);
    if (numInputs > 0 && (inputFade.getCurrentValue() > 0.0f || inputFade.isSmoothing()))
    {
        const auto jack = (InputJack) inputJack.load();
        if (jack == InputJack::both)
            copyInput (inputBlock);
        else
        {
            const int ch = juce::jmin (jack == InputJack::first ? 0 : 1, numInputs - 1);
            inputBlock.clear();
            if (const auto* in = inputs[ch])
                for (int side = 0; side < 2; ++side)
                    inputBlock.copyFrom (side, 0, in + offset, n);
        }
        if (inputFade.isSmoothing())
        {
            const float from = inputFade.getCurrentValue();
            inputFade.skip (n);
            for (int ch = 0; ch < 2; ++ch)
                inputBlock.applyGainRamp (ch, 0, n, from, inputFade.getCurrentValue());
        }
    }
    else
    {
        inputBlock.clear();
    }
    applyStrip (inputStrip, inputBlock);

    if (source == SampleSource::mix)
    {
        samplerBlock.clear();
        for (auto* b : { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &drumsBlock, &inputBlock })
            for (int ch = 0; ch < 2; ++ch)
                samplerBlock.addFrom (ch, 0, *b, ch, 0, n);
    }

    if (runsStrip (samplerStrip))
        sampler.processBlock (samplerBlock, samplerMidi);
    else
        samplerBlock.clear();
    applyStrip (samplerStrip, samplerBlock);

    // The drone, last of the tools: its strings can ring to any of them (as you hear them, after
    // their strips), or to all of them together.
    juce::AudioBuffer<float> droneBlock (droneBuffer.getArrayOfWritePointers(), 2, n);
    droneBlock.clear();
    if (drone.getFeed() != DroneProcessor::feedInput)
        droneMicFade.setCurrentAndTargetValue (0.0f);   // (so it fades in when you pick the mic)
    if (const bool plucked = dronePlucked.exchange (false); runsStrip (droneStrip) && (plucked || drone.wantsToPlay()))
    {
        const juce::AudioBuffer<float>* feed = nullptr;
        switch (drone.getFeed())
        {
            case DroneProcessor::feedInput:
            {
                // Your voice straight from the mic or interface (not the Input strip, which is off
                // until you turn it on and has its own fader): heard while the mic is on.
                juce::AudioBuffer<float> voice (droneFeedBuffer.getArrayOfWritePointers(), 2, n);
                droneMicFade.setTargetValue (hearMic && numInputs > 0 ? 1.0f : 0.0f);
                if (droneMicFade.getCurrentValue() > 0.0f || droneMicFade.isSmoothing())
                {
                    copyInput (voice);
                    if (droneMicFade.isSmoothing())
                    {
                        const float from = droneMicFade.getCurrentValue();
                        droneMicFade.skip (n);
                        for (int ch = 0; ch < 2; ++ch)
                            voice.applyGainRamp (ch, 0, n, from, droneMicFade.getCurrentValue());
                    }
                }
                else
                {
                    voice.clear();
                }
                feed = &droneFeedBuffer;
                break;
            }
            case DroneProcessor::feedVocoder: feed = &vocoderBlock; break;
            case DroneProcessor::feedBass:    feed = &bassBlock; break;
            case DroneProcessor::feedSynth:   feed = &synthBlock; break;
            case DroneProcessor::feedPads:    feed = &mpcBlock; break;
            case DroneProcessor::feedSampler: feed = &samplerBlock; break;
            case DroneProcessor::feedDrums:   feed = &drumsBlock; break;
            case DroneProcessor::feedAll:
            {
                juce::AudioBuffer<float> all (droneFeedBuffer.getArrayOfWritePointers(), 2, n);
                all.clear();
                for (auto* b : { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &drumsBlock, &inputBlock, &samplerBlock })
                    for (int ch = 0; ch < 2; ++ch)
                        all.addFrom (ch, 0, *b, ch, 0, n);
                feed = &droneFeedBuffer;
                break;
            }
            default: break;
        }
        drone.process (droneBlock, feed, droneMidi);
    }
    applyStrip (droneStrip, droneBlock);

    // Each tool's strip (in Stem order), for its own output in Logic.
    const std::array<juce::AudioBuffer<float>*, numStems> stems { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock, &samplerBlock, nullptr, &drumsBlock };

    // A tool plays out of its own Logic output only when its strip says so (Own out on the
    // Mixer); otherwise it stays in the main mix.
    std::array<float* const*, numStems> outs {};
    for (int st = 0; st < numStems; ++st)
        if (stemOut[(size_t) st] != nullptr && strips[(size_t) stripOfStem (st)].ownOutput.load())
            outs[(size_t) st] = stemOut[(size_t) st];

    mix.clear();
    for (int st = 0; st < numStems; ++st)
        if (st != loopsStem && outs[(size_t) st] == nullptr)
            for (int ch = 0; ch < 2; ++ch)
                mix.addFrom (ch, 0, *stems[(size_t) st], ch, 0, n);
    // (The drone has no output of its own in Logic: it's always in the main mix.)
    for (int ch = 0; ch < 2; ++ch)
        mix.addFrom (ch, 0, droneBlock, ch, 0, n);

    // The looper records from the input, a tool, or the mix so far (never itself), and plays
    // its layers through the Loops strip.
    juce::AudioBuffer<float> rawInput (rawInputBuffer.getArrayOfWritePointers(), 2, n);
    copyInput (rawInput);
    juce::AudioBuffer<float> loopsBlock (loopsBuffer.getArrayOfWritePointers(), 2, n);
    RigLooper::Clock clock;
    clock.beat = transport.getBlockPosition();
    clock.bpm = transport.getBpm();
    clock.beatsPerBar = transport.getBeatsPerBar();
    clock.playing = transport.isPlayingThisBlock();
    std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
    sources[RigLooper::input] = &rawInput;
    sources[RigLooper::vocoder] = &vocoderBlock;
    sources[RigLooper::bass] = &bassBlock;
    sources[RigLooper::synth] = &synthBlock;
    sources[RigLooper::mpc] = &mpcBlock;
    sources[RigLooper::sampler] = &samplerBlock;
    sources[RigLooper::mix] = &mix;
    sources[RigLooper::line] = &inputBlock;
    sources[RigLooper::drums] = &drumsBlock;
    sources[RigLooper::drone] = &droneBlock;
    if (looper.process (clock, sources, loopsBlock))
    {
        transport.play();
        controlsMoved = true;
    }
    {
        // A loop counting in: the click counts the bars until it records.
        double until = -1.0;
        for (int l = 0; l < RigLooper::kLayers; ++l)
            if (looper.getState (l) == RigLooper::State::armed && looper.getLayer (l).countIn.load() > 0)
                until = juce::jmax (until, looper.getLayer (l).startsAt.load());
        transport.countInUntil = juce::jmax (until, songCountInUntil.load());
    }
    {
        // A take for the song (Rec into), from the same sources.
        const int source = takes.getSource();
        takes.process (juce::isPositiveAndBelow (source, (int) RigLooper::numSources) ? sources[(size_t) source] : nullptr,
                       n, clock.beat, clock.bpm, clock.playing, sampleRate, &takeMidi);
    }
    applyStrip (loopsStrip, loopsBlock);
    if (outs[loopsStem] == nullptr)
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, loopsBlock, ch, 0, n);
    for (int st = 0; st < numStems; ++st)
        if (auto* const* out = outs[(size_t) st])
        {
            const auto& from = st == loopsStem ? loopsBlock : *stems[(size_t) st];
            for (int ch = 0; ch < 2; ++ch)
                if (out[ch] != nullptr)
                    juce::FloatVectorOperations::copy (out[ch] + offset, from.getReadPointer (ch), n);
            if (out[0] != nullptr)
                stemLimiters[(size_t) st].process (out[0] + offset, out[1] != nullptr ? out[1] + offset : nullptr, n);
        }

    // A friend's track, under everything: in the mix (so it's recorded), or, for a "my parts only"
    // bounce, heard like the click and kept out of recordings.
    juce::AudioBuffer<float> friendBlock (friendBuffer.getArrayOfWritePointers(), 2, n);
    friendBlock.clear();
    friendTrack.render (friendBlock, n, transport.getBlockPosition(), transport.getBpm(), transport.isPlayingThisBlock());
    const bool friendRecorded = friendTrack.recorded.load();
    if (friendRecorded)
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, friendBlock, ch, 0, n);

    // The metronome: heard (under the master fader), but kept out of recordings.
    juce::AudioBuffer<float> click (clickBuffer.getArrayOfWritePointers(), 2, n);
    click.clear();
    transport.endBlock (n, &click);

    processReturns (mix, n);
    fx[masterStrip].processBlock (mix, noMidi);
    performFx.beatsPerBar = transport.getBeatsPerBar();
    performFx.process (mix, transport.getBlockPosition(), transport.getBpm(), transport.isPlayingThisBlock());

    auto& master = strips[masterStrip];
    auto& masterGain = gains[masterStrip];
    masterGain.setTargetValue (master.mute.load() ? 0.0f : faderGain (master.level.load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
            mix.setSample (ch, i, mix.getSample (ch, i) * g);
    }
    // The safety limiter (and NaN guard), before the mix is recorded.
    masterLimiter.process (mix.getWritePointer (0), mix.getWritePointer (1), n);

    recorder.process (recClock, { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock, &samplerBlock, &loopsBlock, &mix, &drumsBlock, &droneBlock }, n);
    capture.process (recClock, { &rawInput, &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock, &samplerBlock, &loopsBlock, &mix, &drumsBlock }, n);

    const float clickGain = masterGain.getTargetValue();
    for (int ch = 0; ch < 2; ++ch)
        mix.addFrom (ch, 0, click, ch, 0, n, clickGain);
    if (! friendRecorded)
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, friendBlock, ch, 0, n, clickGain);

    // A sound being previewed from the Sounds tab, like the click: heard, not recorded.
    {
        const juce::SpinLock::ScopedTryLockType sl (previewLock);
        if (sl.isLocked() && previewVersion != previewSeen)
        {
            previewSeen = previewVersion;
            previewSound = previewNext;
            previewPos = 0;
        }
    }
    if (previewSound != nullptr)
    {
        const double step = crate::kSampleRate / sampleRate;
        const int length = previewSound->getNumSamples();
        const int chans = previewSound->getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            const int at = (int) previewPos;
            if (at + 1 >= length)
            {
                previewSound = nullptr;
                break;
            }
            const float frac = (float) (previewPos - at);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = previewSound->getReadPointer (juce::jmin (ch, chans - 1));
                mix.addSample (ch, i, (d[at] + (d[at + 1] - d[at]) * frac) * 0.8f * clickGain);
            }
            previewPos += step;
        }
    }
    previewPlaying = previewSound != nullptr;

    const float peak = juce::jmax (mix.getMagnitude (0, 0, n), mix.getMagnitude (1, 0, n));
    if (peak > master.peak.load())
        master.peak = peak;

    // Kaleidobox Visuals hears what you hear (a copy of this block; nothing while it's off).
    if (visualTap.listening())
        visualTap.push (mix.getReadPointer (0), mix.getReadPointer (1), n);
}

juce::File RigEngine::logicFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig").getChildFile ("To Logic");
}

juce::String RigEngine::serviceCapture()
{
    if (capture.getState() != RigCapture::State::done)
        return {};
    double rate = 0, beats = 0;
    const int source = capture.getSource(), bars = capture.getBars();
    const int dest = captureDestOnce >= 0 ? captureDestOnce : captureDest;
    captureDestOnce = -1;
    auto taken = capture.take (rate, beats);   // sets the rate, so first
    auto audio = crate::Edits::collapseFakeStereo (crate::AudioFiles::resample (taken, rate));
    if (audio.getNumSamples() < crate::samples (0.02) || audio.getMagnitude (0, audio.getNumSamples()) < 1.0e-4f)
        return "Nothing came from " + RigCapture::sourceName (source) + ", so nothing was recorded.";

    const auto name = RigCapture::sourceName (source).upToFirstOccurrenceOf (" /", false, false)
                      + (bars > 0 ? " " + juce::String (bars) + (bars == 1 ? " bar" : " bars") : juce::String());
    if (dest == RigCapture::toLogic)
    {
        // A WAV, exactly the bars recorded from the bar it started on, to drag onto a Logic track
        // at that bar.
        auto folder = logicFolder();
        folder.createDirectory();
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
        const auto file = folder.getChildFile (name.removeCharacters ("/\\:*?\"<>|").trim() + " " + stamp + ".wav");
        bool written = false;
        if (std::unique_ptr<juce::OutputStream> stream (file.createOutputStream()); stream != nullptr)
        {
            juce::WavAudioFormat wav;
            auto options = juce::AudioFormatWriterOptions().withSampleRate (crate::kSampleRate)
                               .withNumChannels (audio.getNumChannels()).withBitsPerSample (24);
            if (auto writer = wav.createWriterFor (stream, options))
                written = writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }
        if (! written)
            return "Couldn't save the recording for Logic in " + folder.getFullPathName() + ".";
        lastLogicFile = file;
        return "Recorded " + RigCapture::sourceName (source) + " for Logic: drag it from Drag to Logic (Effects screen) onto a track, at the bar you started it.";
    }
    if (dest == RigCapture::toMpcPad)
    {
        const auto padName = putOnMpcPad (std::move (audio), name, bars > 0 ? beats : 0);
        if (padName.isEmpty())
            return "Every MPC pad in this kit has a sound: clear one and record again.";
        return "Recorded " + RigCapture::sourceName (source) + " onto MPC pad " + padName + ".";
    }

    putInSampler (std::move (audio), name);
    return "Recorded " + RigCapture::sourceName (source) + " into the Sampler: play it from the keys or edit it in its tab.";
}

juce::String RigEngine::putOnMpcPad (juce::AudioBuffer<float> audio, const juce::String& name, double beats)
{
    // The first empty pad in the bank your keyboard's pads play, else anywhere in the kit.
    auto& project = mpc.getProject();
    auto& kit = project.currentBank();
    const int first = juce::jlimit (0, 3, routing.padBank.load()) * 16;
    int pad = -1;
    for (int i = 0; i < mpc::kPadCount && pad < 0; ++i)
        if (kit.pads[(size_t) ((first + i) % mpc::kPadCount)].isEmpty())
            pad = (first + i) % mpc::kPadCount;
    if (pad < 0)
        return {};
    auto& p = kit.pads[(size_t) pad];
    p.audio = crate::share (std::move (audio));
    p.name = name.substring (0, 16);
    p.beats = beats;
    project.changed();
    return juce::String::charToString ((juce::juce_wchar) ('A' + pad / 16)) + juce::String (pad % 16 + 1);
}

void RigEngine::putInSampler (juce::AudioBuffer<float> audio, const juce::String& name)
{
    sampler.getDocument().load (crate::share (std::move (audio)), name, "Sample");
}

int RigEngine::putOnLoopLayer (crate::SharedAudio audio, const juce::String& name, double beats)
{
    if (audio == nullptr || audio->getNumSamples() == 0)
        return -1;
    int layer = -1;
    for (int i = 0; i < RigLooper::kLayers && layer < 0; ++i)
        if (looper.getState (i) == RigLooper::State::empty)
            layer = i;
    if (layer < 0)
        return -1;
    if (beats <= 0)
        beats = crate::SoundFx::guessBeats (audio->getNumSamples(), transport.getBpm());
    if (beats <= 0)
        beats = transport.getBeatsPerBar();
    auto fitted = crate::SoundFx::fitToTempo (audio, beats, transport.getBpm());
    looper.makeRoom (layer);
    looper.setLayerAudio (layer, juce::AudioBuffer<float> (*fitted), beats);
    setLoopName (layer, name);
    return layer;
}

void RigEngine::preview (std::shared_ptr<const juce::AudioBuffer<float>> sound)
{
    {
        const juce::SpinLock::ScopedLockType sl (previewLock);
        previewNext = sound.get();
        ++previewVersion;
    }
    if (sound != nullptr)
        previewHeld.push_back (std::move (sound));
    // The audio thread moves on within a block; the last few are kept well past that.
    while (previewHeld.size() > 4)
        previewHeld.erase (previewHeld.begin());
}

juce::File RigEngine::recordingsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig").getChildFile ("Recordings");
}

bool RigEngine::startRecording (const juce::File& folder)
{
    return recorder.start (folder, { "MPC", "Vocoder", "Bass", "Synth", "Input", "Sampler", "Loops", "Mix", "Drum machine", "Drone" },
                           { "MPC pads", "Sampler", "Vocoder", "Bass", "Synth", "Drum machine" }, sampleRate > 0 ? sampleRate : 44100.0);
}

void RigEngine::applyStrip (int id, juce::AudioBuffer<float>& buffer)
{
    auto& strip = strips[(size_t) id];
    auto& gain = gains[(size_t) id];
    const int n = buffer.getNumSamples();
    // The timeline brings the tool in and out (before its effects, so they ring out).
    if (const float from = arrangeFrom[(size_t) id], to = arrangeTo[(size_t) id]; from < 1.0f || to < 1.0f)
        for (int ch = 0; ch < 2; ++ch)
            buffer.applyGainRamp (ch, 0, n, from, to);
    fx[(size_t) id].processBlock (buffer, noMidi);
    const bool silenced = strip.mute.load() || (soloActive && ! strip.solo.load());
    gain.setTargetValue (silenced ? 0.0f : faderGain (strip.level.load()));
    if (gain.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
        }
    }
    else
    {
        buffer.applyGain (gain.getTargetValue());
    }

    // Pan, after the fader: the middle leaves both sides as they were; turning it one way
    // turns the other side down.
    auto& pan = pans[(size_t) id];
    pan.setTargetValue (juce::jlimit (-1.0f, 1.0f, strip.pan.load()));
    if (pan.isSmoothing() || std::abs (pan.getTargetValue()) > 1.0e-4f)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float p = pan.getNextValue();
            l[i] *= p > 0.0f ? 1.0f - p : 1.0f;
            r[i] *= p < 0.0f ? 1.0f + p : 1.0f;
        }
    }

    // The sends, after the fader and pan (so a muted strip sends nothing).
    for (auto [send, amount, to] : { std::tuple<juce::LinearSmoothedValue<float>*, float, juce::AudioBuffer<float>*> { &sendGainsA[(size_t) id], strip.sendA.load(), &sendBufferA },
                                     std::tuple<juce::LinearSmoothedValue<float>*, float, juce::AudioBuffer<float>*> { &sendGainsB[(size_t) id], strip.sendB.load(), &sendBufferB } })
    {
        send->setTargetValue (juce::jlimit (0.0f, 1.0f, amount));
        if (! send->isSmoothing() && send->getTargetValue() <= 0.0f)
            continue;
        if (to->getNumSamples() < n)
            continue;
        const float from = send->getCurrentValue();
        send->skip (n);
        for (int ch = 0; ch < 2; ++ch)
            to->addFromWithRamp (ch, 0, buffer.getReadPointer (ch), n, from, send->getCurrentValue());
    }

    const float peak = juce::jmax (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n));
    if (peak > strip.peak.load())
        strip.peak = peak;
    // Each tool's colour in Visuals' "Tools" colours: DRUMS, PADS (the MPC and the Sampler), BASS, SYNTH, VOCODER, DRONE.
    if (visualTap.listening())
    {
        const int slot = id == drumsStrip ? 0 : (id == mpcStrip || id == samplerStrip) ? 1 : id == bassStrip ? 2
                       : id == synthStrip ? 3 : id == vocoderStrip ? 4 : id == droneStrip ? 5 : -1;
        visualTap.toolLevel (slot, peak);
    }
}

void RigEngine::processReturns (juce::AudioBuffer<float>& mix, int n)
{
    if (sendBufferA.getNumSamples() < n || sendBufferB.getNumSamples() < n || echoLine.getNumSamples() < 2)
        return;

    // A: the reverb.
    {
        auto& ret = returns[reverbReturn];
        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit (0.0f, 1.0f, ret.amount.load());
        p.damping = 1.0f - juce::jlimit (0.0f, 1.0f, ret.tone.load());
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        p.width = 1.0f;
        sendReverb.setParameters (p);
        sendReverb.processStereo (sendBufferA.getWritePointer (0), sendBufferA.getWritePointer (1), n);
    }

    // B: the echo, in time: each repeat crosses to the other side (ping-pong), a little darker.
    {
        auto& ret = returns[echoReturn];
        const auto& times = echoTimes();
        const double beats = times[(size_t) juce::jlimit (0, (int) times.size() - 1, ret.time.load())].second;
        const double bpm = juce::jmax (20.0, transport.getBpm());
        const int length = echoLine.getNumSamples();
        const int delay = juce::jlimit (1, length - 1, (int) std::round (beats * 60.0 / bpm * sampleRate));
        const float feedback = juce::jlimit (0.0f, 0.9f, ret.amount.load());
        auto* inL = sendBufferB.getWritePointer (0);
        auto* inR = sendBufferB.getWritePointer (1);
        auto* lineL = echoLine.getWritePointer (0);
        auto* lineR = echoLine.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            int read = echoWrite - delay;
            if (read < 0)
                read += length;
            const float dl = lineL[read], dr = lineR[read];
            echoTone[0] += 0.45f * (dr - echoTone[0]);
            echoTone[1] += 0.45f * (dl - echoTone[1]);
            lineL[echoWrite] = inL[i] + echoTone[0] * feedback;
            lineR[echoWrite] = inR[i] + echoTone[1] * feedback;
            inL[i] = dl;
            inR[i] = dr;
            if (++echoWrite >= length)
                echoWrite = 0;
        }
    }

    for (int r = 0; r < numReturns; ++r)
    {
        auto& ret = returns[(size_t) r];
        auto& buffer = r == reverbReturn ? sendBufferA : sendBufferB;
        auto& gain = returnGains[(size_t) r];
        gain.setTargetValue (ret.mute.load() ? 0.0f : faderGain (ret.level.load()));
        const float from = gain.getCurrentValue();
        gain.skip (n);
        for (int ch = 0; ch < 2; ++ch)
        {
            buffer.applyGainRamp (ch, 0, n, from, gain.getCurrentValue());
            mix.addFrom (ch, 0, buffer, ch, 0, n);
        }
        const float peak = juce::jmax (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n));
        if (peak > ret.peak.load())
            ret.peak = peak;
    }
}

void RigEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                  float* const* outputs, int numOutputs, int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&)
{
    process (inputs, numInputs, outputs, numOutputs, numSamples);
}

void RigEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // The round trip: a buffer out and the output's latency before you hear it, a buffer in and
    // the input's latency before what you sang gets here.
    const int buffer = device->getCurrentBufferSizeSamples();
    deviceRoundTrip = juce::jlimit (0, 48000, device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + 2 * buffer);
    prepare (device->getCurrentSampleRate(), buffer);
    applyTiming();
}

namespace
{
// A factory kit's sounds, made once per run and shared (they're read-only), so a new Rig doesn't make them again.
std::vector<std::pair<juce::String, crate::SharedAudio>> factorySounds (int index)
{
    static std::mutex madeLock;
    static std::map<int, std::vector<std::pair<juce::String, crate::SharedAudio>>> made;
    const std::lock_guard<std::mutex> hold (madeLock);
    auto& list = made[index];
    if (list.empty())
        for (auto& [name, audio] : crate::factory::makeSounds (index))
            list.emplace_back (name, std::make_shared<const juce::AudioBuffer<float>> (std::move (audio)));
    return list;
}

// A factory sound on a pad, as loadFactoryKit puts it there.
void putFactorySound (Pad& pad, const std::pair<juce::String, crate::SharedAudio>& sound)
{
    pad = {};
    pad.audio = sound.second;
    pad.name = sound.first.substring (0, 16);   // the MPC keeps 16 characters
    pad.mono = true;
    if (pad.name.containsIgnoreCase ("hat"))
        pad.muteGroup = 1;   // the open hat stops when the closed one plays
}
}

juce::String RigEngine::loadFactoryKit (int index, int kit)
{
    const auto& kits = crate::factory::kits();
    if (! juce::isPositiveAndBelow (index, (int) kits.size()))
        return {};
    const auto sounds = factorySounds (index);

    auto& project = mpc.getProject();
    const juce::String kitName (kits[(size_t) index].name);
    if (kit < 0 || kit >= (int) project.getBanks().size())
        kit = project.addBank (kitName);
    auto& bank = project.getBanks()[(size_t) kit];
    bank.name = kitName;
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        auto& pad = bank.pads[(size_t) i];
        pad = {};
        if (i < (int) sounds.size())
            putFactorySound (pad, sounds[(size_t) i]);
    }
    project.setCurrent (kit);
    mpc.padBankOnScreen = 0;
    routing.padBank = 0;
    project.changed();
    return kitName + " is on the pads, A01 to A" + juce::String (juce::jmin (16, (int) sounds.size())).paddedLeft ('0', 2)
           + ": " + kits[(size_t) index].what + ".";
}

int RigEngine::factoryKitOf (int kit)
{
    const auto& banks = mpc.getProject().getBanks();
    if (! juce::isPositiveAndBelow (kit, (int) banks.size()))
        return -1;
    const auto& kits = crate::factory::kits();
    for (int i = 0; i < (int) kits.size(); ++i)
        if (banks[(size_t) kit].name.trim().equalsIgnoreCase (kits[(size_t) i].name))
            return i;
    return -1;
}

juce::String RigEngine::factoryPadName (int pad)
{
    const int index = factoryKitOf (mpc.getProject().getCurrentIndex());
    if (index < 0 || ! juce::isPositiveAndBelow (pad, kPadsPerBank))
        return {};
    const auto names = crate::factory::soundNames (index);
    return pad < (int) names.size() ? names[(size_t) pad].substring (0, 16) : juce::String();
}

bool RigEngine::padDiffersFromFactory (int pad)
{
    const auto name = factoryPadName (pad);
    if (name.isEmpty())
        return false;
    const auto& slot = mpc.getProject().currentBank().pads[(size_t) pad];
    const auto sounds = factorySounds (factoryKitOf (mpc.getProject().getCurrentIndex()));
    return slot.isEmpty() || slot.audio != sounds[(size_t) pad].second;
}

juce::String RigEngine::restoreFactoryPad (int pad)
{
    const int index = factoryKitOf (mpc.getProject().getCurrentIndex());
    if (index < 0 || ! juce::isPositiveAndBelow (pad, kPadsPerBank))
        return "Pad " + padLabel (pad) + " isn't part of a factory kit. Pick one with Kits.";
    const auto sounds = factorySounds (index);
    if (pad >= (int) sounds.size())
        return {};
    mpc.releasePad (pad);
    putFactorySound (mpc.getProject().currentBank().pads[(size_t) pad], sounds[(size_t) pad]);
    mpc.getProject().changed();
    return "Pad " + padLabel (pad) + " plays the factory " + sounds[(size_t) pad].first + " again.";
}

SoundSet RigEngine::padsAsSet (int bank, const juce::String& name)
{
    auto& kit = mpc.getProject().currentBank();
    SoundSet set;
    set.name = name;
    set.wholeKit = bank < 0;
    set.bpm = kit.bpm;
    set.madeOn = "Mac";
    set.saved = juce::Time::getCurrentTime();
    const int first = bank < 0 ? 0 : juce::jlimit (0, 3, bank) * kPadsPerBank;
    const int count = bank < 0 ? mpc::kPadCount : kPadsPerBank;
    for (int i = 0; i < count; ++i)
    {
        const auto& p = kit.pads[(size_t) (first + i)];
        if (p.isEmpty())
            continue;
        SoundSet::Pad s;
        s.index = i;
        s.name = p.name;
        s.audio = p.audio;
        s.volume = p.volume;
        s.pan = p.pan;
        s.tune = p.tune;
        s.mode = p.mode == PadMode::noteOff ? 2 : p.mode == PadMode::mono ? 1 : 0;
        s.mono = p.mono;
        s.muteGroup = p.muteGroup;
        s.cutoff = p.cutoff;
        s.resonance = p.resonance;
        s.attack = p.attack;
        s.decay = p.decay;
        s.beats = p.beats;
        set.pads.push_back (s);
    }
    return set;
}

juce::String RigEngine::loadSet (const SoundSet& set, int bank)
{
    auto& project = mpc.getProject();
    int first = 0;
    if (set.wholeKit)
        project.setCurrent (project.addBank (set.name.substring (0, 16)));
    else
    {
        first = juce::jlimit (0, 3, bank) * kPadsPerBank;
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            mpc.releasePad (first + i);
            project.currentBank().pads[(size_t) (first + i)] = {};
        }
    }
    auto& kit = project.currentBank();
    if (set.wholeKit && set.bpm > 0)
        kit.bpm = set.bpm;
    for (const auto& s : set.pads)
    {
        const int pad = first + s.index;
        if (! juce::isPositiveAndBelow (pad, mpc::kPadCount) || s.audio == nullptr)
            continue;
        auto& p = kit.pads[(size_t) pad];
        p = {};
        p.audio = s.audio;
        p.name = s.name.substring (0, 16);
        p.volume = s.volume;
        p.pan = s.pan;
        p.tune = s.tune;
        p.mode = s.mode == 2 ? PadMode::noteOff : s.mode == 1 ? PadMode::mono : PadMode::oneShot;
        p.mono = s.mono;
        p.muteGroup = s.muteGroup;
        p.cutoff = s.cutoff;
        p.resonance = s.resonance;
        p.attack = s.attack;
        p.decay = s.decay;
        p.beats = s.beats;
    }
    project.changed();
    const juce::String what = juce::String (set.padCount()) + (set.padCount() == 1 ? " pad" : " pads");
    return set.wholeKit ? "The set " + set.name + " is on a new kit (" + what + "); your kits stay in Kits."
                        : "The set " + set.name + " is on bank " + juce::String::charToString ((juce::juce_wchar) ('A' + first / kPadsPerBank)) + " (" + what + ").";
}

juce::String RigEngine::resetFactoryKit()
{
    auto& project = mpc.getProject();
    const int index = factoryKitOf (project.getCurrentIndex());
    if (index < 0)
        return "This kit isn't a factory kit. Pick one with Kits (it goes on a new kit).";
    for (int p = 0; p < kPadsPerBank; ++p)
        mpc.releasePad (p);
    loadFactoryKit (index, project.getCurrentIndex());
    return project.currentBank().name + " has all 16 of its factory sounds again, A01 to A16.";
}

//==============================================================================
juce::File RigEngine::timingFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Crate Rig").getChildFile ("Timing.xml");
}

void RigEngine::setTimingOffsetMs (float ms, bool save)
{
    timingOffsetMs = juce::jlimit (-50.0f, 100.0f, ms);
    applyTiming();
    if (! save)
        return;
    const auto file = timingFile();
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
    if (xml == nullptr || ! xml->hasTagName ("Timing"))
        xml = std::make_unique<juce::XmlElement> ("Timing");
    xml->setAttribute (timingKey(), (double) timingOffsetMs);
    file.getParentDirectory().createDirectory();
    xml->writeTo (file);
}

int RigEngine::getLiveDelaySamples() const
{
    const double rate = sampleRate > 0 ? sampleRate : 44100.0;
    return juce::jmax (0, deviceRoundTrip.load() + (int) std::round (timingOffsetMs * 0.001 * rate));
}

void RigEngine::applyTiming()
{
    const int delay = getLiveDelaySamples();
    looper.setLiveDelay (delay);
    takes.setDelay (RigLooper::isLiveSource (takes.getSource()) ? delay : 0);
}

void RigEngine::audioDeviceStopped()
{
    prepared = false;
    mpc.releaseResources();
    sampler.releaseResources();
    vocoder.releaseResources();
    bass.releaseResources();
    synthTool.releaseResources();
}

void RigEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    // Each controller is read as what it is (Settings > MIDI controllers): a Launchpad's or an
    // MPD's pads come in as the drum pads, on the pads' channel, whatever channel it's set to; a
    // plain keyboard's notes are keys even on channel 10; learned knobs turn knobs 1-8.
    auto msg = m;
    const int pads = routing.padChannel.load();
    if (source != nullptr && ! controllers.translate (source->getName(), msg, pads > 0 ? pads : 10))
        return;
    incoming.addMessageToQueue (msg);
}

//==============================================================================
juce::ValueTree RigEngine::saveShow() const
{
    juce::ValueTree show (kShowType);
    show.setProperty ("version", 1, nullptr);
    show.setProperty ("bpm", transport.getBpm(), nullptr);
    show.setProperty ("metronome", transport.metronome.load(), nullptr);
    show.setProperty ("padChannel", routing.padChannel.load(), nullptr);
    show.setProperty ("padBank", routing.padBank.load(), nullptr);
    show.setProperty ("padsTo", routing.padsTo.load(), nullptr);
    show.setProperty ("keysTo", routing.keysTo.load(), nullptr);
    show.setProperty ("vocoderOn", vocoderOn.load(), nullptr);
    show.setProperty ("splitNote", routing.splitNote.load(), nullptr);
    show.setProperty ("lowerKeysTo", routing.lowerKeysTo.load(), nullptr);
    show.setProperty ("sampleSource", sampleSource.load(), nullptr);
    show.setProperty ("captureSource", captureSource, nullptr);
    show.setProperty ("captureDest", captureDest, nullptr);
    show.setProperty ("captureBars", captureBars, nullptr);
    show.setProperty ("inputOn", inputOn.load(), nullptr);
    show.setProperty ("inputJack", inputJack.load(), nullptr);
    show.setProperty ("mpcBeat", isMpcBeatOn(), nullptr);
    show.setProperty ("repeatRate", mpc.getSequencer().getRepeatRateIndex(), nullptr);   // NOTE REPEAT's rate

    if (screens.getNumChildren() > 0 || screens.getNumProperties() > 0)
        show.appendChild (screens.createCopy(), nullptr);
    show.appendChild (quantizer.toTree(), nullptr);
    show.appendChild (chords.toTree(), nullptr);
    show.appendChild (automation.toTree(), nullptr);
    show.appendChild (macros.toTree(), nullptr);

    for (int s = 0; s < numStrips; ++s)
    {
        juce::ValueTree strip ("Strip");
        strip.setProperty ("name", stripName (s), nullptr);
        strip.setProperty ("level", strips[(size_t) s].level.load(), nullptr);
        strip.setProperty ("mute", strips[(size_t) s].mute.load(), nullptr);
        strip.setProperty ("pan", strips[(size_t) s].pan.load(), nullptr);
        strip.setProperty ("solo", strips[(size_t) s].solo.load(), nullptr);
        strip.setProperty ("sendA", strips[(size_t) s].sendA.load(), nullptr);
        strip.setProperty ("sendB", strips[(size_t) s].sendB.load(), nullptr);
        strip.setProperty ("ownOutput", strips[(size_t) s].ownOutput.load(), nullptr);
        show.appendChild (strip, nullptr);
    }
    for (int r = 0; r < numReturns; ++r)
    {
        const auto& ret = returns[(size_t) r];
        juce::ValueTree t ("Return");
        t.setProperty ("name", returnName (r), nullptr);
        t.setProperty ("level", ret.level.load(), nullptr);
        t.setProperty ("mute", ret.mute.load(), nullptr);
        t.setProperty ("amount", ret.amount.load(), nullptr);
        t.setProperty ("tone", ret.tone.load(), nullptr);
        t.setProperty ("time", ret.time.load(), nullptr);
        show.appendChild (t, nullptr);
    }

    for (int c = 0; c < RigRouting::numControls; ++c)
    {
        const auto [channel, cc] = routing.getAssignment (c);
        if (cc < 0)
            continue;
        juce::ValueTree control ("Control");
        control.setProperty ("id", c, nullptr);
        control.setProperty ("channel", channel, nullptr);
        control.setProperty ("cc", cc, nullptr);
        show.appendChild (control, nullptr);
    }

    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto& layer = looper.getLayer (i);
        juce::ValueTree loop ("Loop");
        loop.setProperty ("index", i, nullptr);
        loop.setProperty ("source", layer.source.load(), nullptr);
        loop.setProperty ("bars", layer.bars.load(), nullptr);
        loop.setProperty ("level", layer.level.load(), nullptr);
        loop.setProperty ("mute", layer.mute.load(), nullptr);
        if (const auto name = getLoopName (i); name.isNotEmpty())
            loop.setProperty ("name", name, nullptr);
        const auto state = looper.getState (i);
        if (state == RigLooper::State::playing || state == RigLooper::State::overdubbing)
            loop.setProperty ("beats", layer.lengthBeats.load(), nullptr);
        if (loopFx[(size_t) i].isActive())
        {
            juce::MemoryBlock fxState;
            const_cast<StripFx&> (loopFx[(size_t) i]).getStateInformation (fxState);
            loop.setProperty ("fx", fxState.toBase64Encoding(), nullptr);
        }
        show.appendChild (loop, nullptr);
    }

    // The loop banks. The current one's layers are the Loops above; the others are kept here.
    if (loopBanks.size() > 1 || (loopBanks.size() == 1 && loopBanks[0].name.isNotEmpty()))
    {
        juce::ValueTree banks ("LoopBanks");
        banks.setProperty ("current", getCurrentLoopBank(), nullptr);
        for (int b = 0; b < (int) loopBanks.size(); ++b)
        {
            const auto& bank = loopBanks[(size_t) b];
            juce::ValueTree t ("LoopBank");
            t.setProperty ("name", getLoopBankName (b), nullptr);
            if (b != getCurrentLoopBank())
                for (int i = 0; i < RigLooper::kLayers; ++i)
                {
                    const auto& l = bank.layers[(size_t) i];
                    juce::ValueTree loop ("Loop");
                    loop.setProperty ("index", i, nullptr);
                    loop.setProperty ("source", l.source, nullptr);
                    loop.setProperty ("bars", l.bars, nullptr);
                    loop.setProperty ("level", l.level, nullptr);
                    loop.setProperty ("mute", l.mute, nullptr);
                    if (l.audio != nullptr && l.beats > 0)
                        loop.setProperty ("beats", l.beats, nullptr);
                    if (! l.fx.isEmpty())
                        loop.setProperty ("fx", l.fx.toBase64Encoding(), nullptr);
                    t.appendChild (loop, nullptr);
                }
            banks.appendChild (t, nullptr);
        }
        show.appendChild (banks, nullptr);
    }

    // The takes recorded into the song (their sound is saved beside the show, like the loops).
    if (! takes.getSets().empty())
    {
        juce::ValueTree all ("Takes");
        for (const auto& set : takes.getSets())
        {
            if (set.takes.empty())
                continue;
            juce::ValueTree t ("TakeSet");
            t.setProperty ("layer", set.layer, nullptr);
            t.setProperty ("source", set.source, nullptr);
            t.setProperty ("startBar", set.startBar, nullptr);
            t.setProperty ("bars", set.bars, nullptr);
            t.setProperty ("playing", set.playing, nullptr);
            t.setProperty ("nextNumber", set.nextNumber, nullptr);
            for (const auto& take : set.takes)
            {
                juce::ValueTree one ("Take");
                one.setProperty ("number", take.number, nullptr);
                one.setProperty ("beats", take.beats, nullptr);
                one.setProperty ("partial", take.partial, nullptr);
                if (! take.notes.empty())
                    one.setProperty ("notes", rignotes::encode (take.notes), nullptr);
                t.appendChild (one, nullptr);
            }
            all.appendChild (t, nullptr);
        }
        show.appendChild (all, nullptr);
    }

    if (! noteClips.empty())
        show.appendChild (rignotes::toTree (noteClips), nullptr);
    show.setProperty ("songMode", scenes.songMode, nullptr);
    show.appendChild (timeline.toTree(), nullptr);
    if (friendTrack.hasAudio())
        show.appendChild (friendTrack.toTree(), nullptr);
    if (! liveSong.getTakes().empty())
        show.appendChild (liveSong.toTree(), nullptr);
    for (const auto& scene : scenes.getScenes())
        show.appendChild (scene.toTree(), nullptr);

    // Knobs on settings, by the setting's ID so they survive the tools changing.
    for (const auto& k : routing.getParameterKnobs())
    {
        auto& tool = const_cast<RigEngine&> (*this).getTool (k.tool);
        auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (tool.getParameters()[k.parameter]);
        if (p == nullptr)
            continue;
        juce::ValueTree knob ("Knob");
        knob.setProperty ("tool", toolName (k.tool), nullptr);
        knob.setProperty ("parameter", p->paramID, nullptr);
        knob.setProperty ("channel", k.channel, nullptr);
        knob.setProperty ("cc", k.cc, nullptr);
        show.appendChild (knob, nullptr);
    }

    // The tools save themselves the way they do inside a Logic project.
    auto& self = const_cast<RigEngine&> (*this);
    const std::pair<const char*, juce::AudioProcessor*> tools[] = { { "MPC", &self.mpc }, { "Sampler", &self.sampler },
                                                                    { "Vocoder", &self.vocoder }, { "Bass", &self.bass },
                                                                    { "Synth", &self.synthTool }, { "Drum machine", &self.drums },
                                                                    { "Drone", &self.drone } };
    std::vector<std::pair<juce::String, juce::AudioProcessor*>> all (std::begin (tools), std::end (tools));
    for (int st = 0; st < numStrips; ++st)
        all.emplace_back (toolName (fxTool + st), &self.fx[(size_t) st]);
    for (auto [name, tool] : all)
    {
        juce::MemoryBlock state;
        tool->getStateInformation (state);
        juce::ValueTree t ("Tool");
        t.setProperty ("name", name, nullptr);
        t.setProperty ("state", state.toBase64Encoding(), nullptr);
        show.appendChild (t, nullptr);
    }
    for (auto [name, slot] : { std::pair<const char*, const PluginSlot*> { "Bass", &bassPlugin }, { "Synth", &synthPlugin } })
        if (slot->isLoaded())
        {
            juce::ValueTree t ("Instrument");
            t.setProperty ("tool", name, nullptr);
            t.appendChild (slot->toTree(), nullptr);
            show.appendChild (t, nullptr);
        }
    return show;
}

void RigEngine::loadShow (const juce::ValueTree& show)
{
    if (! show.hasType (kShowType))
        return;

    transport.stop();
    transport.setBpm (show.getProperty ("bpm", transport.getBpm()));
    transport.metronome = (bool) show.getProperty ("metronome", false);
    routing.padChannel = juce::jlimit (0, 16, (int) show.getProperty ("padChannel", 10));
    routing.padBank = juce::jlimit (0, 3, (int) show.getProperty ("padBank", 0));
    mpc.padBankOnScreen = routing.padBank.load();   // the MPC shows the bank the drum pads play
    routing.padsTo = (int) show.getProperty ("padsTo", (int) RigRouting::Dest::mpc) == (int) RigRouting::Dest::drums ? (int) RigRouting::Dest::drums : (int) RigRouting::Dest::mpc;
    routing.keysTo = juce::jlimit (0, RigRouting::kNumDests - 1, (int) show.getProperty ("keysTo", (int) RigRouting::Dest::vocoder));
    vocoderOn = (bool) show.getProperty ("vocoderOn", true);
    routing.splitNote = juce::jlimit (0, 127, (int) show.getProperty ("splitNote", 0));
    routing.lowerKeysTo = juce::jlimit (0, RigRouting::kNumDests - 1, (int) show.getProperty ("lowerKeysTo", (int) RigRouting::Dest::bass));
    routing.clearParameterKnobs();
    sampleSource = juce::jlimit (0, 2, (int) show.getProperty ("sampleSource", 0));
    inputOn = (bool) show.getProperty ("inputOn", false);
    // The show's own instruments (or none: the tools' own sounds).
    bassPlugin.fromTree (show.getChildWithProperty ("tool", "Bass").getChildWithName ("Plugin"));
    synthPlugin.fromTree (show.getChildWithProperty ("tool", "Synth").getChildWithName ("Plugin"));
    inputJack = juce::jlimit (0, 2, (int) show.getProperty ("inputJack", 0));
    // (Shows from before the MPC had its own on/off played it whenever the Rig played.)
    mpc.getSequencer().followHost = (bool) show.getProperty ("mpcBeat", true);
    mpc.getSequencer().setRepeatRateIndex ((int) show.getProperty ("repeatRate", noterepeat::kDefaultRate));
    padLoopLayer = -1;

    for (int c = 0; c < RigRouting::numControls; ++c)
        routing.forget (c);
    scenes.clear();
    scenes.songMode = (bool) show.getProperty ("songMode", false);
    timeline.fromTree (show.getChildWithName (RigTimeline::kTreeType));
    // (its sound comes in readLoopAudio)
    friendTrack.fromTree (show.getChildWithName (RigFriendTrack::kTreeType));
    friendBars = 0.0;
    liveSong.fromTree (show.getChildWithName (RigLiveSong::kTreeType));
    takes.clear();
    loadedTakeSets.clear();
    loadedAudioRevision = show.getProperty (rigshow::kRevisionAttribute).toString();
    noteClips = rignotes::fromTree (show.getChildWithName (rignotes::kTreeType));
    notesChanged();
    notePlayer.releaseAll();
    {
        // Older shows only had what the Sampler records.
        const int older = (int) show.getProperty ("sampleSource", 0);
        const int fallback = older == (int) SampleSource::vocoder ? RigCapture::vocoder : older == (int) SampleSource::mix ? RigCapture::mix : RigCapture::mic;
        captureSource = juce::jlimit (0, RigCapture::numSources - 1, (int) show.getProperty ("captureSource", fallback));
    }
    captureDest = juce::jlimit (0, RigCapture::numDests - 1, (int) show.getProperty ("captureDest", (int) RigCapture::toSampler));
    captureBars = juce::jlimit (0, 16, (int) show.getProperty ("captureBars", 0));
    quantizer.fromTree (show.getChildWithName ("Quantize"));
    chords.fromTree (show.getChildWithName (RigChords::kTreeType));
    automation.fromTree (show.getChildWithName (RigAutomation::kTreeType));
    macros.fromTree (show.getChildWithName (RigMacros::kTreeType));
    screens = show.getChildWithName ("Screens").isValid() ? show.getChildWithName ("Screens").createCopy() : juce::ValueTree ("Screens");
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        looper.cancelSwap (i);
        looper.reset (i);
        loadedLoopBeats[(size_t) i] = 0;
        loopNames[(size_t) i] = loadedLoopNames[(size_t) i] = {};
        bankFxPending[(size_t) i] = false;
    }
    loopBanks.clear();
    currentLoopBank = -1;
    queuedLoopBank = -1;
    clearedLoops.reset();
    if (const auto banks = show.getChildWithName ("LoopBanks"); banks.isValid())
    {
        for (const auto t : banks)
        {
            if (! t.hasType ("LoopBank") || (int) loopBanks.size() >= kMaxLoopBanks)
                continue;
            LoopBank bank;
            bank.name = t.getProperty ("name").toString().substring (0, 40);
            for (const auto loop : t)
            {
                const int i = loop.getProperty ("index", -1);
                if (! loop.hasType ("Loop") || ! juce::isPositiveAndBelow (i, RigLooper::kLayers))
                    continue;
                auto& l = bank.layers[(size_t) i];
                l.source = juce::jlimit (0, RigLooper::numSources - 1, (int) loop.getProperty ("source", (int) RigLooper::mix));
                l.bars = (int) loop.getProperty ("bars", 0);
                l.level = juce::jlimit (0.0f, 1.0f, (float) loop.getProperty ("level", 0.8f));
                l.mute = (bool) loop.getProperty ("mute", false);
                l.beats = juce::jmax (0.0, (double) loop.getProperty ("beats", 0.0));   // its sound comes in readLoopAudio
                if (loop.hasProperty ("fx"))
                    l.fx.fromBase64Encoding (loop.getProperty ("fx").toString());
            }
            loopBanks.push_back (std::move (bank));
        }
        if (! loopBanks.empty())
            currentLoopBank = juce::jlimit (0, (int) loopBanks.size() - 1, (int) banks.getProperty ("current", 0));
    }
    // Shows from before the effects have none, and shows from before the drone have it as a new
    // Rig has it (off).
    for (auto& f : fx)
        f.reset();
    drone.setStateInformation (droneDefaults.getData(), (int) droneDefaults.getSize());
    for (auto& f : loopFx)
        f.reset();

    for (auto& s : strips)
    {
        s.level = 0.8f;
        s.mute = false;
        s.pan = 0.0f;
        s.solo = false;
        s.sendA = 0.0f;
        s.sendB = 0.0f;
    }
    for (int r = 0; r < numReturns; ++r)
    {
        const auto t = show.getChildWithProperty ("name", returnName (r));
        auto& ret = returns[(size_t) r];
        const bool has = t.hasType ("Return");
        ret.level = juce::jlimit (0.0f, 1.0f, has ? (float) t.getProperty ("level", 0.8f) : 0.8f);
        ret.mute = has && (bool) t.getProperty ("mute", false);
        ret.amount = juce::jlimit (0.0f, 1.0f, has ? (float) t.getProperty ("amount", 0.5f) : 0.5f);
        ret.tone = juce::jlimit (0.0f, 1.0f, has ? (float) t.getProperty ("tone", 0.5f) : 0.5f);
        ret.time = juce::jlimit (0, (int) echoTimes().size() - 1, has ? (int) t.getProperty ("time", 1) : 1);
    }
    int strip = 0;
    for (const auto child : show)
    {
        if (child.hasType ("Strip") && strip < numStrips)
        {
            strips[(size_t) strip].level = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("level", 0.8f));
            strips[(size_t) strip].mute = (bool) child.getProperty ("mute", false);
            strips[(size_t) strip].pan = juce::jlimit (-1.0f, 1.0f, (float) child.getProperty ("pan", 0.0f));
            strips[(size_t) strip].solo = strip != masterStrip && (bool) child.getProperty ("solo", false);
            strips[(size_t) strip].sendA = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("sendA", 0.0f));
            strips[(size_t) strip].sendB = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("sendB", 0.0f));
            strips[(size_t) strip].ownOutput = (bool) child.getProperty ("ownOutput", false);
            ++strip;
        }
        else if (child.hasType ("Knob"))
        {
            for (int t = 0; t < numTools; ++t)
                if (toolName (t) == child.getProperty ("tool").toString())
                {
                    const auto& list = getTool (t).getParameters();
                    for (int i = 0; i < list.size(); ++i)
                        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (list[i]))
                            if (p->paramID == child.getProperty ("parameter").toString())
                                routing.addParameterKnob (child.getProperty ("channel"), child.getProperty ("cc"), t, i);
                }
        }
        else if (child.hasType ("Scene"))
        {
            if ((int) scenes.getScenes().size() < RigScenes::kMaxScenes)
                scenes.getScenes().push_back (RigScene::fromTree (child));
        }
        else if (child.hasType ("Takes"))
        {
            for (const auto& t : child)
            {
                if (! t.hasType ("TakeSet"))
                    continue;
                RigTakes::Set set;
                set.layer = t.getProperty ("layer", -1);
                set.source = t.getProperty ("source", 0);
                set.startBar = t.getProperty ("startBar", 0.0);
                set.bars = t.getProperty ("bars", 0.0);
                set.playing = t.getProperty ("playing", 0);
                set.nextNumber = t.getProperty ("nextNumber", 1);
                for (const auto& one : t)
                {
                    if (! one.hasType ("Take"))
                        continue;
                    RigTakes::Take take;
                    take.number = one.getProperty ("number", 1);
                    take.beats = one.getProperty ("beats", 0.0);
                    take.partial = one.getProperty ("partial", false);
                    take.notes = rignotes::decode (one.getProperty ("notes").toString());
                    set.takes.push_back (std::move (take));   // (its sound comes in readLoopAudio)
                }
                const bool layerOk = juce::isPositiveAndBelow (set.layer, RigLooper::kLayers)
                                     || (RigTakes::isNotesLayer (set.layer) && RigTakes::stripOfNotesLayer (set.layer) < numStrips);
                if (layerOk && ! set.takes.empty() && set.bars > 0)
                    loadedTakeSets.push_back (std::move (set));
            }
        }
        else if (child.hasType ("Loop"))
        {
            const int i = child.getProperty ("index", -1);
            if (juce::isPositiveAndBelow (i, RigLooper::kLayers))
            {
                auto& layer = looper.getLayer (i);
                layer.source = juce::jlimit (0, RigLooper::numSources - 1, (int) child.getProperty ("source", (int) RigLooper::mix));
                layer.bars = (int) child.getProperty ("bars", 0);
                layer.level = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("level", 0.8f));
                layer.mute = (bool) child.getProperty ("mute", false);
                loadedLoopBeats[(size_t) i] = (double) child.getProperty ("beats", 0.0);
                loadedLoopNames[(size_t) i] = child.getProperty ("name").toString();
                juce::MemoryBlock fxState;
                if (child.hasProperty ("fx") && fxState.fromBase64Encoding (child.getProperty ("fx").toString()) && ! fxState.isEmpty())
                    loopFx[(size_t) i].setStateInformation (fxState.getData(), (int) fxState.getSize());
            }
        }
        else if (child.hasType ("Control"))
        {
            routing.setAssignment (child.getProperty ("id", -1), child.getProperty ("channel", 0), child.getProperty ("cc", -1));
        }
        else if (child.hasType ("Tool"))
        {
            juce::MemoryBlock state;
            if (! state.fromBase64Encoding (child.getProperty ("state").toString()) || state.isEmpty())
                continue;
            const auto name = child.getProperty ("name").toString();
            if (name == "MPC")
                mpc.setStateInformation (state.getData(), (int) state.getSize());
            else if (name == "Sampler")
                sampler.setStateInformation (state.getData(), (int) state.getSize());
            else if (name == "Vocoder")
                vocoder.setStateInformation (state.getData(), (int) state.getSize());
            else if (name == "Bass")
                bass.setStateInformation (state.getData(), (int) state.getSize());
            else if (name == "Synth")
                synthTool.setStateInformation (state.getData(), (int) state.getSize());
            else if (name == "Drum machine")
                drums.setStateInformation (state.getData(), (int) state.getSize());
            else if (name == "Drone")
                drone.setStateInformation (state.getData(), (int) state.getSize());
            else
                for (int st = 0; st < numStrips; ++st)
                    if (name == toolName (fxTool + st))
                        fx[(size_t) st].setStateInformation (state.getData(), (int) state.getSize());
        }
    }
    // Shows from before the Song screen: their song parts become its sections.
    rigsong::sectionsFromParts (*this);
    // A one-tool plugin's keys and pads always play its tool, whatever the show says.
    if (onlyTool.load() >= 0)
        setOnlyTool (onlyTool.load());
    // A show opens silent: the clock is stopped (above), and the Drone, the one thing that
    // sounds without the clock, waits for its On button (its other settings stay as saved).
    if (auto* droneOn = drone.params.getParameter ("droneOn"))
        droneOn->setValueNotifyingHost (0.0f);
    controlsMoved = true;
    // Nothing to compare with until it's opened (readLoopAudio, or markOpened).
    dirty.showLoaded();
}

//==============================================================================
void RigEngine::setOnlyTool (int strip)
{
    if (strip != mpcStrip && strip != vocoderStrip && strip != bassStrip && strip != synthStrip && strip != drumsStrip)
        strip = -1;
    onlyTool = strip;
    if (strip < 0)
        return;
    using Dest = RigRouting::Dest;
    const auto dest = strip == mpcStrip ? Dest::mpc : strip == drumsStrip ? Dest::drums : strip == vocoderStrip ? Dest::vocoder
                    : strip == bassStrip ? Dest::bass : Dest::synth;
    routing.keysTo = (int) dest;
    routing.splitNote = 0;
    // The drum pads play the MPC's or the drum machine's pads; on the others they're just keys.
    const bool pads = strip == mpcStrip || strip == drumsStrip;
    routing.padChannel = pads ? 10 : 0;
    routing.padsTo = (int) (strip == drumsStrip ? Dest::drums : Dest::mpc);
    vocoderOn = strip == vocoderStrip;
    inputOn = false;
}

bool RigEngine::runsStrip (int strip) const
{
    const int only = onlyTool.load();
    return only < 0 || strip == only || strip == masterStrip || (only == mpcStrip && strip == samplerStrip);
}

juce::String RigEngine::onlyToolName (int strip)
{
    switch (strip)
    {
        case mpcStrip:     return "MPC";
        case vocoderStrip: return "Vocoder";
        case bassStrip:    return "Bass";
        case synthStrip:   return "Synth";
        case drumsStrip:   return "Drums";
        default:           return {};
    }
}

namespace
{
    // The show's Tool names and strips that make up one tool's sound.
    juce::StringArray toolSoundTools (int strip)
    {
        switch (strip)
        {
            case RigEngine::mpcStrip:     return { "MPC", "Sampler" };
            case RigEngine::vocoderStrip: return { "Vocoder" };
            case RigEngine::bassStrip:    return { "Bass" };
            case RigEngine::synthStrip:   return { "Synth" };
            case RigEngine::drumsStrip:   return { "Drum machine" };
            default:                      return {};
        }
    }

    juce::Array<int> toolSoundStrips (int strip)
    {
        if (strip == RigEngine::mpcStrip)
            return { RigEngine::mpcStrip, RigEngine::samplerStrip };
        return { strip };
    }
}

juce::ValueTree RigEngine::toolSoundFromShow (const juce::ValueTree& show, int strip)
{
    const auto names = toolSoundTools (strip);
    if (! show.hasType (kShowType) || names.isEmpty())
        return {};
    juce::ValueTree sound (kToolSoundType);
    sound.setProperty ("tool", onlyToolName (strip), nullptr);
    sound.setProperty ("strip", strip, nullptr);
    bool found = false;
    for (const auto& child : show)
    {
        if (child.hasType ("Tool"))
        {
            const auto name = child.getProperty ("name").toString();
            bool mine = names.contains (name);
            for (int st : toolSoundStrips (strip))
                mine = mine || name == toolName (fxTool + st);
            if (mine)
            {
                sound.appendChild (child.createCopy(), nullptr);
                found = found || names.contains (name);
            }
        }
        else if (child.hasType ("Strip"))
        {
            for (int st : toolSoundStrips (strip))
                if (child.getProperty ("name").toString() == stripName (st))
                    sound.appendChild (child.createCopy(), nullptr);
        }
        else if (child.hasType ("Instrument") && names.contains (child.getProperty ("tool").toString()))
        {
            sound.appendChild (child.createCopy(), nullptr);
        }
    }
    return found ? sound : juce::ValueTree();
}

juce::ValueTree RigEngine::saveToolSound (int strip) const
{
    return toolSoundFromShow (saveShow(), strip);
}

bool RigEngine::loadToolSound (const juce::ValueTree& sound)
{
    if (! sound.hasType (kToolSoundType))
        return false;
    const int strip = sound.getProperty ("strip", -1);
    const auto names = toolSoundTools (strip);
    if (names.isEmpty())
        return false;
    for (const auto& child : sound)
    {
        if (child.hasType ("Tool"))
        {
            juce::MemoryBlock state;
            if (! state.fromBase64Encoding (child.getProperty ("state").toString()) || state.isEmpty())
                continue;
            const auto name = child.getProperty ("name").toString();
            juce::AudioProcessor* tool = nullptr;
            if (name == "MPC") tool = &mpc;
            else if (name == "Sampler") tool = &sampler;
            else if (name == "Vocoder") tool = &vocoder;
            else if (name == "Bass") tool = &bass;
            else if (name == "Synth") tool = &synthTool;
            else if (name == "Drum machine") tool = &drums;
            else
                for (int st : toolSoundStrips (strip))
                    if (name == toolName (fxTool + st))
                        tool = &fx[(size_t) st];
            if (tool != nullptr)
                tool->setStateInformation (state.getData(), (int) state.getSize());
        }
        else if (child.hasType ("Strip"))
        {
            for (int st : toolSoundStrips (strip))
                if (child.getProperty ("name").toString() == stripName (st))
                {
                    auto& s = strips[(size_t) st];
                    s.level = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("level", s.level.load()));
                    s.pan = juce::jlimit (-1.0f, 1.0f, (float) child.getProperty ("pan", s.pan.load()));
                    s.sendA = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("sendA", s.sendA.load()));
                    s.sendB = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("sendB", s.sendB.load()));
                    s.mute = false;   // you got it to hear it
                }
        }
    }
    // The plugin in its slot comes along (or goes, when the sound has none).
    if (strip == bassStrip || strip == synthStrip)
    {
        auto& slot = strip == bassStrip ? bassPlugin : synthPlugin;
        slot.fromTree (sound.getChildWithProperty ("tool", strip == bassStrip ? "Bass" : "Synth").getChildWithName ("Plugin"));
    }
    if (strip == vocoderStrip)
        vocoderOn = true;
    controlsMoved = true;
    return true;
}

void RigEngine::setLoopName (int layer, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return;
    loopNames[(size_t) layer] = name;
    loopNameVersions[(size_t) layer] = looper.getLayer (layer).version.load();
}

juce::String RigEngine::getLoopName (int layer) const
{
    if (! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return {};
    const auto& l = looper.getLayer (layer);
    return l.version.load() == loopNameVersions[(size_t) layer] && l.lengthBeats.load() > 0 ? loopNames[(size_t) layer] : juce::String();
}

juce::File RigEngine::toolSoundsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("Crate").getChildFile ("Rig").getChildFile ("Tool sounds");
}

juce::Array<RigEngine*> RigEngine::openBoxes()
{
    juce::Array<RigEngine*> boxes;
    for (auto* e : allEngines())
        if (e->getOnlyTool() < 0)
            boxes.add (e);
    return boxes;
}

//==============================================================================
namespace
{
// (Only the song and its parts use this: playback's writes, which the dirty model keeps apart
// from the values you set.)
void setParameter (RigDirty& dirty, juce::AudioProcessorValueTreeState& state, const char* id, float plainValue)
{
    if (auto* p = state.getParameter (id))
    {
        dirty.runtimeParam (p, p->convertTo0to1 (plainValue));
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        dirty.runtimeParamWritten (p);
    }
}

float getParameter (juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* v = state.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}
}

RigScene::State RigEngine::captureScene()
{
    RigScene::State s;
    s.mpcSequence = mpc.getProject().getCurrentSequenceIndex();
    s.bassPattern = juce::roundToInt (getParameter (bass.params, "pattern"));
    s.bassSequencer = getParameter (bass.params, "sequencer") > 0.5f ? 1 : 0;
    s.arp = getParameter (synthTool.params, "arpOn") > 0.5f ? 1 : 0;
    s.drumPattern = juce::roundToInt (getParameter (drums.params, "pattern"));
    s.drumSequencer = getParameter (drums.params, "sequencer") > 0.5f ? 1 : 0;
    s.mpcBeat = isMpcBeatOn() ? 1 : 0;
    s.vocoderOn = vocoderOn.load() ? 1 : 0;
    s.inputOn = inputOn.load() ? 1 : 0;
    s.loopBank = queuedLoopBank >= 0 ? queuedLoopBank : getCurrentLoopBank();
    for (int st = 0; st < numStrips && st < RigScene::kMaxStrips; ++st)
        s.stripMutes[(size_t) st] = st == masterStrip ? -1 : (strips[(size_t) st].mute.load() ? 1 : 0);
    for (int l = 0; l < RigLooper::kLayers; ++l)
        s.loopMutes[(size_t) l] = looper.getLayer (l).mute.load() ? 1 : 0;
    return s;
}

void RigEngine::applyScene (const RigScene::State& s, double beat)
{
    // A loop that comes in with a part starts from its top, on the part's first beat.
    for (int l = 0; l < RigLooper::kLayers; ++l)
        if (s.loopMutes[(size_t) l] == 0 && looper.getLayer (l).mute.load())
            looper.restartAt (l, beat);
    // Playback's writes (a check reading these meanwhile tries again).
    dirty.beginAudioFields();
    for (int st = 0; st < numStrips && st < RigScene::kMaxStrips; ++st)
        if (s.stripMutes[(size_t) st] >= 0)
        {
            dirty.runtimeField (RigDirty::stripMute, st, strips[(size_t) st].mute.load() ? 1.0f : 0.0f, s.stripMutes[(size_t) st] == 1 ? 1.0f : 0.0f);
            strips[(size_t) st].mute = s.stripMutes[(size_t) st] == 1;
        }
    for (int l = 0; l < RigLooper::kLayers; ++l)
        if (s.loopMutes[(size_t) l] >= 0)
        {
            // (A loop bank coming in with the part has its mute from the part already.)
            if (! looper.isSwapPending (l))
                dirty.runtimeField (RigDirty::loopMute, l, looper.getLayer (l).mute.load() ? 1.0f : 0.0f, s.loopMutes[(size_t) l] == 1 ? 1.0f : 0.0f);
            looper.getLayer (l).mute = s.loopMutes[(size_t) l] == 1;
        }
    if (s.vocoderOn >= 0)
    {
        dirty.runtimeField (RigDirty::vocoderOn, 0, vocoderOn.load() ? 1.0f : 0.0f, s.vocoderOn == 1 ? 1.0f : 0.0f);
        vocoderOn = s.vocoderOn == 1;
    }
    if (s.inputOn >= 0)
    {
        dirty.runtimeField (RigDirty::inputOn, 0, inputOn.load() ? 1.0f : 0.0f, s.inputOn == 1 ? 1.0f : 0.0f);
        inputOn = s.inputOn == 1;
    }
    if (s.bassSequencer >= 0)
        setParameter (dirty, bass.params, "sequencer", (float) s.bassSequencer);
    if (s.arp >= 0)
        setParameter (dirty, synthTool.params, "arpOn", (float) s.arp);
    if (s.drumSequencer >= 0)
        setParameter (dirty, drums.params, "sequencer", (float) s.drumSequencer);
    // (On the audio thread: switched off, the sequencer stops itself as it stops following.)
    if (s.mpcBeat >= 0)
    {
        dirty.runtimeField (RigDirty::mpcBeat, 0, isMpcBeatOn() ? 1.0f : 0.0f, s.mpcBeat == 1 ? 1.0f : 0.0f);
        mpc.getSequencer().followHost = s.mpcBeat == 1;
    }
    dirty.endAudioFields();
}

double RigEngine::songLengthBarsOfTrack() const
{
    if (! friendTrack.hasAudio())
        return 0.0;
    const auto info = friendTrack.getInfo();
    // (beats are beats at any tempo: it plays like a tape, locked to them)
    return friendTrack.lengthSeconds() * info.bpm / 60.0 / juce::jmax (1, transport.getBeatsPerBar());
}

double RigEngine::songLengthBars() const
{
    return juce::jmax (timeline.lengthBars(), songLengthBarsOfTrack());
}

juce::String RigEngine::bringInFriendTrack (std::shared_ptr<const juce::AudioBuffer<float>> audio, const RigFriendTrack::Info& info,
                                            bool setTempo, bool setKey)
{
    if (audio == nullptr || audio->getNumSamples() == 0)
    {
        friendTrack.clear();
        friendBars = 0.0;
        dirty.noteChange();
        return "The friend's track is gone from the song.";
    }
    transport.stop();
    if (setTempo)
    {
        transport.setBpm (info.bpm);
        transport.setBeatsPerBar (info.beatsPerBar);
    }
    friendTrack.set (std::move (audio), info);
    friendBars = songLengthBarsOfTrack();
    juce::String said = (info.name.isNotEmpty() ? info.name : juce::String ("The track")) + " is in: "
                        + juce::String ((int) std::round (friendBars.load())) + " bars from bar 1 at "
                        + juce::String (info.bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM.";
    if (setKey && juce::isPositiveAndBelow (info.key, 12))
    {
        chords.setKey (info.key, info.minor ? 1 : 0, chords.fillType);
        chords.liveKeyHome();
        said << " Chords are in " << crate::TrackAnalysis::keyName (info.key, info.minor) << ".";
    }
    dirty.noteChange();
    controlsMoved = true;
    return said;
}

void RigEngine::practiceLoop (double fromBar, double toBar)
{
    fromBar = juce::jmax (0.0, std::floor (fromBar + 1.0e-6));
    toBar = juce::jmax (fromBar + 1.0, std::round (toBar));
    timeline.cycleStart = fromBar;
    timeline.cycleEnd = toBar;
    timeline.cycle = true;
    if (! transport.isPlaying())
    {
        // the song from the loop's first bar
        timeline.on = true;
        transport.setPosition (fromBar * juce::jmax (1, transport.getBeatsPerBar()));
        transport.play();
    }
    else
    {
        const double bar = transport.getPositionInBeats() / juce::jmax (1, transport.getBeatsPerBar());
        timeline.on = true;
        if (bar < fromBar || bar >= toBar)
            timeline.jumpTo (fromBar);
    }
    controlsMoved = true;
}

void RigEngine::arrange (int n)
{
    timeline.update();
    // (A friend's track is the song too: its bars cycle and it plays to its end.)
    const bool running = timeline.on.load() && transport.isPlayingThisBlock() && (! timeline.liveEmpty() || friendTrack.hasAudio());
    if (! running)
    {
        arrangeFrom.fill (1.0f);
        arrangeTo.fill (1.0f);
        arrangeRestart.fill (-1.0);
        for (int l = 0; l < RigLooper::kLayers; ++l)
            looper.setArrangeGain (l, 1.0f, 1.0f);
        return;
    }
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    if (! transport.isHosted())   // (in Logic, Logic's clock goes where it likes)
    {
        if (const double jump = timeline.takeJump (transport.getBlockPosition(), bpb); jump >= 0)
        {
            transport.jumpTo (jump);
            controlsMoved = true;
        }
        else if (timeline.stopAtEnd.load() && ! timeline.cycle.load() && timeline.gated.load() == 0
                 && transport.getBlockPosition() >= (juce::jmax (timeline.liveEnd(), friendBars.load()) + 1.0) * bpb)
        {
            transport.stop();   // a bar after the end, so echoes ring out
            notesOffRequested = true;
            controlsMoved = true;
        }
    }

    const double beat0 = transport.getBlockPosition();
    const double beat1 = beat0 + n * transport.getBpm() / (60.0 * sampleRate);
    const auto& lanes = RigTimeline::lanes();
    // Recording a take: you hear what you're playing all through the song, and not the older
    // take on its layer.
    const int takeLayer = takes.getLayer(), takeStrip = takeLayer >= 0 ? stripForSource (takes.getSource()) : -1;
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        const auto& info = lanes[(size_t) lane];
        float from = timeline.liveGain (lane, beat0 / bpb), to = timeline.liveGain (lane, beat1 / bpb);
        if (info.isLoop ? info.index == takeLayer : info.index == takeStrip)
            from = to = info.isLoop ? 0.0f : 1.0f;
        if (! info.isLoop)
        {
            arrangeFrom[(size_t) info.index] = from;
            arrangeTo[(size_t) info.index] = to;
            continue;
        }
        looper.setArrangeGain (info.index, from, to);
        // A block starts its loop from the top on the block's first beat (less its slip: a
        // slipped block comes in part way through the loop).
        if (timeline.liveBlockStart (lane, beat1 / bpb) >= 0)
        {
            double origin = timeline.liveBlockOrigin (lane, beat1 / bpb) * bpb;
            if (const double length = looper.getLayer (info.index).lengthBeats.load(); length > 0 && origin < 0)
                origin += std::ceil (-origin / length) * length;
            origin = juce::jmax (0.0, origin);
            if (std::abs (origin - arrangeRestart[(size_t) info.index]) > 1.0e-6)
            {
                looper.restartAt (info.index, origin);
                arrangeRestart[(size_t) info.index] = origin;
            }
        }
    }
}

int RigEngine::samplesToTimelineEvent (int n) const
{
    if (! timeline.on.load() || ! transport.isPlayingThisBlock() || transport.isHosted() || sampleRate <= 0)
        return n;
    const double beats = timeline.beatsToNextEvent (transport.getBlockPosition(), transport.getBeatsPerBar());
    const double samples = std::ceil (beats * 60.0 / transport.getBpm() * sampleRate - 1.0e-6);
    return samples >= 1.0 && samples < n ? (int) samples : n;
}

void RigEngine::playTimeline (double bar)
{
    // What playing the song switches on is playback's, not yours (the dirty model keeps yours).
    dirty.runtimeField (RigDirty::timelineOn, 0, timeline.on.load() ? 1.0f : 0.0f, 1.0f);
    dirty.runtimeField (RigDirty::songMode, 0, scenes.songMode ? 1.0f : 0.0f, 0.0f);
    timeline.on = true;
    scenes.songMode = false;
    const auto& lanes = RigTimeline::lanes();
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        if (! timeline.laneUsed (lane))
            continue;
        const auto& info = lanes[(size_t) lane];
        if (info.isLoop)
        {
            dirty.runtimeField (RigDirty::loopMute, info.index, looper.getLayer (info.index).mute.load() ? 1.0f : 0.0f, 0.0f);
            looper.getLayer (info.index).mute = false;
        }
        else
        {
            dirty.runtimeField (RigDirty::stripMute, info.index, strips[(size_t) info.index].mute.load() ? 1.0f : 0.0f, 0.0f);
            strips[(size_t) info.index].mute = false;
        }
        if (! info.isLoop && info.index == vocoderStrip)
        {
            dirty.runtimeField (RigDirty::vocoderOn, 0, vocoderOn.load() ? 1.0f : 0.0f, 1.0f);
            vocoderOn = true;
        }
        if (! info.isLoop && info.index == inputStrip)
        {
            dirty.runtimeField (RigDirty::inputOn, 0, inputOn.load() ? 1.0f : 0.0f, 1.0f);
            inputOn = true;
        }
    }
    if (transport.isPlaying())
    {
        timeline.jumpTo (bar);
    }
    else
    {
        transport.setPosition (bar * juce::jmax (1, transport.getBeatsPerBar()));
        transport.play();
    }
    controlsMoved = true;
}

void RigEngine::launchSection (int i)
{
    const auto list = rigsong::sections (timeline);
    if (! juce::isPositiveAndBelow (i, (int) list.size()))
        return;
    playTimeline (list[(size_t) i].start);
}

int RigEngine::stripForSource (int source)
{
    switch (source)
    {
        case RigLooper::vocoder: return vocoderStrip;
        case RigLooper::bass:    return bassStrip;
        case RigLooper::synth:   return synthStrip;
        case RigLooper::mpc:     return mpcStrip;
        case RigLooper::sampler: return samplerStrip;
        case RigLooper::line:    return inputStrip;
        case RigLooper::drums:   return drumsStrip;
        case RigLooper::drone:   return droneStrip;
        default:                 return -1;
    }
}

int RigEngine::freeLayerForSong() const
{
    for (int l = 0; l < RigLooper::kLayers; ++l)
        if (looper.getState (l) == RigLooper::State::empty && ! timeline.laneUsed (RigTimeline::laneForLoop (l))
            && takes.setFor (l) == nullptr && takes.getLayer() != l)
            return l;
    return -1;
}

void RigEngine::cycleSection (int section, bool on)
{
    const auto list = rigsong::sections (timeline);
    if (on && juce::isPositiveAndBelow (section, (int) list.size()))
    {
        timeline.cycleStart = list[(size_t) section].start;
        timeline.cycleEnd = list[(size_t) section].end();
        timeline.cycle = true;
    }
    else
        timeline.cycle = false;
    takes.setCycle (on);
    controlsMoved = true;
}

bool RigEngine::recordInto (int section, int layer, int source, bool cycle, juce::String* why)
{
    const auto list = rigsong::sections (timeline);
    if (! juce::isPositiveAndBelow (section, (int) list.size()))
    {
        if (why != nullptr)
            *why = "There's no such section in the song.";
        return false;
    }
    const auto& sec = list[(size_t) section];
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const double rate = sampleRate > 0 ? sampleRate : 44100.0;
    takes.setDelay (RigLooper::isLiveSource (source) ? getLiveDelaySamples() : 0);
    if (! takes.start (layer, source, sec.start, sec.bars, cycle, rate, transport.getBpm(), bpb, why))
        return false;
    // A loop holding something else (not in the song, or it would be busy) makes way.
    if (const auto* set = takes.setFor (layer); ! RigTakes::isNotesLayer (layer) && set != nullptr && set->takes.empty()
                                                && looper.getState (layer) != RigLooper::State::empty)
    {
        looper.reset (layer);
        setLoopName (layer, {});
    }
    cycleSection (section, cycle);
    const bool songOn = timeline.on.load() && transport.isPlaying();
    const bool inSection = songOn && rigsong::sectionAt (timeline, transport.getPositionInBeats() / bpb) == section;
    if (! (cycle && inSection))
        playTimeline (sec.start);
    controlsMoved = true;
    return true;
}

bool RigEngine::useTake (int layer, int index)
{
    auto* set = takes.setFor (layer);
    if (set == nullptr || ! juce::isPositiveAndBelow (index, (int) set->takes.size()))
        return false;
    const auto& take = set->takes[(size_t) index];
    if (RigTakes::isNotesLayer (layer))
    {
        // Its notes, as the tool's clip in the section (taking the place of what was there).
        const int strip = RigTakes::stripOfNotesLayer (layer);
        set->playing = index;
        set->fresh = false;
        int c = noteClipAt (strip, set->startBar);
        if (c < 0)
        {
            noteClips.push_back ({});
            c = (int) noteClips.size() - 1;
        }
        auto& clip = noteClips[(size_t) c];
        clip.strip = strip;
        clip.startBar = set->startBar;
        clip.bars = set->bars;
        clip.notes = take.notes;
        notesChanged();
        dirty.noteChange();
        // The tool's lane plays in that section (so the song brings it in there).
        if (const int lane = RigTimeline::laneForStrip (strip); lane >= 0)
        {
            const auto list = rigsong::sections (timeline);
            for (int i = 0; i < (int) list.size(); ++i)
                if (std::abs (list[(size_t) i].start - set->startBar) < 1.0e-6)
                {
                    if (rigsong::boxBlock (timeline, i, lane) < 0)
                        rigsong::setBox (timeline, i, lane, -1);
                    break;
                }
        }
        controlsMoved = true;
        return true;
    }
    dirty.takeOnLayer (take.id, layer, looper.setLayerAudio (layer, take.audio, take.beats));
    looper.getLayer (layer).mute = false;
    set->playing = index;
    set->fresh = false;
    const auto source = set->source == RigLooper::drums ? juce::String ("Drums") : RigLooper::sourceName (set->source);
    setLoopName (layer, source + " take " + juce::String (take.number));
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    looper.restartAt (layer, set->startBar * bpb);
    // In the song, over its section (unless something's put it there already).
    const int lane = RigTimeline::laneForLoop (layer);
    const auto list = rigsong::sections (timeline);
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::abs (list[(size_t) i].start - set->startBar) < 1.0e-6)
        {
            if (rigsong::boxBlock (timeline, i, lane) < 0)
                rigsong::setBox (timeline, i, lane, -1);
            break;
        }
    controlsMoved = true;
    return true;
}

int RigEngine::serviceTakes()
{
    const int made = takes.collect();
    std::vector<int> layers;
    for (const auto& set : takes.getSets())
        layers.push_back (set.layer);
    for (const int l : layers)
        if (auto* set = takes.setFor (l))
        {
            if (set->fresh)
                useTake (l, set->playing);
            // Cleared since (or recorded over some other way): its takes go with it.
            else if (! RigTakes::isNotesLayer (l) && looper.getState (l) == RigLooper::State::empty && takes.getLayer() != l)
                takes.forget (l);
        }
    if (made > 0)
        dirty.noteChange();
    return made;
}

bool RigEngine::isNotesSource (int source)
{
    return source == RigLooper::bass || source == RigLooper::synth || source == RigLooper::sampler
           || source == RigLooper::mpc || source == RigLooper::drums;
}

int RigEngine::noteClipAt (int strip, double startBar) const
{
    for (int i = 0; i < (int) noteClips.size(); ++i)
        if (noteClips[(size_t) i].strip == strip && std::abs (noteClips[(size_t) i].startBar - startBar) < 1.0e-6)
            return i;
    return -1;
}

void RigEngine::notesChanged()
{
    // (Kept in the order they're in: Edit notes picks them by place while you drag.)
    notePlayer.publish (noteClips);
    dirty.noteChange();
}

int RigEngine::takeToLoop (int notesLayer)
{
    const auto* set = takes.setFor (notesLayer);
    if (set == nullptr || ! juce::isPositiveAndBelow (set->playing, (int) set->takes.size()))
        return -1;
    const auto& take = set->takes[(size_t) set->playing];
    const int layer = freeLayerForSong();
    if (layer < 0 || take.audio.getNumSamples() == 0)
        return -1;
    dirty.takeOnLayer (take.id, layer, looper.setLayerAudio (layer, take.audio, take.beats));
    looper.getLayer (layer).source = set->source;
    looper.getLayer (layer).mute = false;
    const auto source = set->source == RigLooper::drums ? juce::String ("Drums") : RigLooper::sourceName (set->source);
    setLoopName (layer, source + " take " + juce::String (take.number));
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    looper.restartAt (layer, set->startBar * bpb);
    const auto list = rigsong::sections (timeline);
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::abs (list[(size_t) i].start - set->startBar) < 1.0e-6)
        {
            rigsong::setBox (timeline, i, RigTimeline::laneForLoop (layer), -1);
            break;
        }
    // The notes stop playing there (the sound has their place now).
    if (const int c = noteClipAt (RigTakes::stripOfNotesLayer (notesLayer), set->startBar); c >= 0)
    {
        noteClips.erase (noteClips.begin() + c);
        notesChanged();
    }
    controlsMoved = true;
    return layer;
}

void RigEngine::playOrStop()
{
    if (transport.isPlaying())
    {
        transport.togglePlay();
        return;
    }
    if (timeline.on.load() && ! timeline.isEmpty())
        playTimeline (songStartBar);
    else
        transport.togglePlay();
}

void RigEngine::applyClip (int lane, int clip)
{
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    if (info.isLoop || clip < 0)
        return;
    switch (info.index)
    {
        case mpcStrip:
        {
            auto& project = mpc.getProject();
            if (clip < (int) project.getSequences().size() && (clip != project.getCurrentSequenceIndex() || mpc.isSongPlaying()))
            {
                const bool atBar = std::exchange (mpc.switchAtBar, true);
                mpc.selectSequence (clip);
                mpc.switchAtBar = atBar;
            }
            break;
        }
        case bassStrip:
            setParameter (dirty, bass.params, "pattern", (float) clip);
            setParameter (dirty, bass.params, "sequencer", 1.0f);
            break;
        case drumsStrip:
            setParameter (dirty, drums.params, "pattern", (float) clip);
            setParameter (dirty, drums.params, "sequencer", 1.0f);
            break;
        case synthStrip:
            setParameter (dirty, synthTool.params, "arpOn", clip > 0 ? 1.0f : 0.0f);
            break;
        default:
            break;
    }
}

void RigEngine::tickSong()
{
    const bool running = timeline.on.load() && transport.isPlaying() && ! timeline.isEmpty();
    if (! running)
    {
        // Stopped: the next start sets every box's pattern again.
        songClipAt.fill (-2.0);
        songSection = -2;
        return;
    }
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    // A moment early, so the MPC and the patterns are ready on the box's first beat (a jump
    // that's waiting goes there instead).
    double beat = transport.getPositionInBeats() + 0.25;
    if (const double jump = timeline.getPendingJump(); jump >= 0 && std::ceil (beat / bpb - 1.0e-9) * bpb - beat < 0.5)
        beat = jump * bpb;
    const double bar = beat / bpb;
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        const int i = timeline.blockAt (lane, bar);
        const double at = i >= 0 ? timeline.getBlocks()[(size_t) i].start : -1.0;
        if (std::abs (at - songClipAt[(size_t) lane]) < 1.0e-9)
            continue;
        songClipAt[(size_t) lane] = at;
        if (i >= 0)
            applyClip (lane, timeline.getBlocks()[(size_t) i].clip);
    }
    // A section's loop bank comes in on its first beat.
    const int section = rigsong::sectionAt (timeline, bar);
    if (section == songSection)
        return;
    songSection = section;
    if (section < 0 || section >= (int) timeline.getMarkers().size())
        return;
    const int bank = timeline.getMarkers()[(size_t) section].loopBank;
    if (bank >= 0 && bank < getNumLoopBanks() && bank != (queuedLoopBank >= 0 ? queuedLoopBank : getCurrentLoopBank()))
    {
        juce::String why;
        if (! loopBankBusy (why))
        {
            storeLoopBank();
            dirty.runtimeField (RigDirty::bankCurrent, 0, (float) getCurrentLoopBank(), (float) bank);
            putLoopBankOnLayers (bank, rigsong::sections (timeline)[(size_t) section].start * bpb, nullptr);
        }
    }
}

void RigEngine::tickScenes()
{
    // The keyboard's learned Next and Back step through the song's sections, when it has some.
    if (const auto list = rigsong::sections (timeline); ! list.empty())
    {
        const int n = scenes.nextPresses.exchange (0), b = scenes.backPresses.exchange (0);
        if (n != 0 || b != 0)
        {
            const int bpb = juce::jmax (1, transport.getBeatsPerBar());
            const int now = juce::jmax (0, rigsong::sectionAt (timeline, transport.getPositionInBeats() / bpb));
            launchSection (juce::jlimit (0, (int) list.size() - 1, now + n - b));
        }
    }
    liveSong.tick();
    tickSong();
    const int early = scenes.tick (transport.getPositionInBeats(), transport.isPlaying(), transport.getBeatsPerBar());
    if (! juce::isPositiveAndBelow (early, (int) scenes.getScenes().size()))
        return;
    // The MPC's sequence and the bass pattern follow the clock's position, so changing them
    // just before the bar has them play the new part from its first beat.
    const auto& s = scenes.getScenes()[(size_t) early].state;
    auto& project = mpc.getProject();
    if (s.mpcSequence >= 0 && s.mpcSequence < (int) project.getSequences().size()
        && (s.mpcSequence != project.getCurrentSequenceIndex() || mpc.isSongPlaying()))
    {
        // At the MPC's next bar line, which is the Rig's (whatever the MPC's own switch setting).
        const bool atBar = std::exchange (mpc.switchAtBar, true);
        mpc.selectSequence (s.mpcSequence);
        mpc.switchAtBar = atBar;
    }
    if (s.bassPattern >= 0)
        setParameter (dirty, bass.params, "pattern", (float) s.bassPattern);
    if (s.drumPattern >= 0)
        setParameter (dirty, drums.params, "pattern", (float) s.drumPattern);

    // The part's loop bank comes in on its first beat (unless a layer is still recording).
    if (s.loopBank >= 0 && s.loopBank < getNumLoopBanks()
        && s.loopBank != (queuedLoopBank >= 0 ? queuedLoopBank : getCurrentLoopBank()))
    {
        juce::String why;
        if (! loopBankBusy (why))
        {
            storeLoopBank();
            const double at = ! transport.isPlaying() ? -1.0
                              : scenes.getQueued() == early ? scenes.getQueuedBeat() : scenes.getCurrentStartBeat();
            dirty.runtimeField (RigDirty::bankCurrent, 0, (float) getCurrentLoopBank(), (float) s.loopBank);
            putLoopBankOnLayers (s.loopBank, at, &s);
        }
    }
}

//==============================================================================
juce::File RigEngine::loopsFolder (const juce::File& showFile)
{
    return showFile.getSiblingFile (showFile.getFileNameWithoutExtension() + " loops");
}

static juce::String takesFolderName (int layer)
{
    return RigTakes::isNotesLayer (layer) ? "Notes " + juce::String (RigTakes::stripOfNotesLayer (layer))
                                          : "Loop " + juce::String (layer + 1);
}

// Every WAV of the show, at the paths shows have always used: "Loop 3.wav" (the current bank),
// "Takes/Loop 5/Take 2.wav" and "Takes/Notes 4/Take 1.wav", "Bank 2/Loop 1.wav" (other banks).
std::vector<rigshow::Asset> RigEngine::loopAssets() const
{
    // (Each says what it is in the engine too, for the dirty model; saving ignores that.)
    std::vector<rigshow::Asset> assets;
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        int version = -1;
        auto audio = std::make_shared<juce::AudioBuffer<float>> (looper.copyLayerAudio (i, &version));
        if (audio->getNumSamples() > 0)
        {
            rigshow::Asset a { "Loop " + juce::String (i + 1) + ".wav", std::move (audio) };
            a.kind = rigshow::Asset::Kind::liveLayer;
            a.layer = i;
            a.bank = getCurrentLoopBank();
            a.version = version;
            assets.push_back (std::move (a));
        }
    }
    for (const auto& set : takes.getSets())
        for (const auto& take : set.takes)
            if (take.audio.getNumSamples() > 0)
            {
                rigshow::Asset a { "Takes/" + takesFolderName (set.layer) + "/Take " + juce::String (take.number) + ".wav",
                                   std::shared_ptr<const juce::AudioBuffer<float>> (&take.audio, [] (const juce::AudioBuffer<float>*) {}) };
                a.kind = rigshow::Asset::Kind::take;
                a.layer = set.layer;
                a.takeId = take.id;
                assets.push_back (std::move (a));
            }
    if (friendTrack.hasAudio())
        assets.push_back ({ RigFriendTrack::kAssetPath, friendTrack.getAudio() });
    for (int b = 0; b < (int) loopBanks.size(); ++b)
    {
        if (b == getCurrentLoopBank())
            continue;
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            const auto& l = loopBanks[(size_t) b].layers[(size_t) i];
            if (l.audio == nullptr || l.beats <= 0 || l.audio->getNumSamples() == 0)
                continue;
            rigshow::Asset a { "Bank " + juce::String (b + 1) + "/Loop " + juce::String (i + 1) + ".wav", l.audio };
            a.kind = rigshow::Asset::Kind::bank;
            a.layer = i;
            a.bank = b;
            assets.push_back (std::move (a));
        }
    }
    return assets;
}

rigshow::Result RigEngine::saveShowFile (const juce::File& showFile, rigshow::SaveKind kind)
{
    // The dirty model's hashing makes no loop copies while this one copies them (every way out).
    const RigDirty::SaveGuard saving (dirty);
    const juce::ScopedLock sl (rigshow::saveLock());
    // The dirty model's baseline moves only when this save is committed (and is a document's).
    auto ticket = dirty.beginSave();
    rigshow::Result result;
    rigshow::Staged staged;
    auto assets = loopAssets();
    const bool stagedOk = rigshow::stage (showFile, assets, sampleRate, committedAudio, staged, result);
    // The loops' copies go now, as before (the dirty model only needs to know what they were).
    for (auto& a : assets)
        if (a.kind == rigshow::Asset::Kind::liveLayer)
            a.audio = nullptr;
    if (! stagedOk)
    {
        dirty.finishSave (ticket, false, kind);
        return result;
    }

    dirty.beforeTree (ticket);
    auto show = saveShow();
    dirty.captureSave (ticket, show, staged, assets);
    show.setProperty (rigshow::kRevisionAttribute, staged.revision, nullptr);
    auto xml = show.createXml();
    rigshow::reach ("beforeCommit");
    if (xml == nullptr || ! rigshow::commitXml (*xml, showFile))
    {
        rigshow::discard (staged);
        result.ok = false;
        result.error = "Couldn't write " + showFile.getFileName() + " (is the disk full or the folder read-only?). The last saved show is unchanged.";
        dirty.finishSave (ticket, false, kind);
        return result;
    }
    rigshow::reach ("afterCommit");
    committedAudio = rigshow::committedFrom (staged);
    rigshow::finish (staged, result);
    result.ok = true;
    dirty.finishSave (ticket, true, kind);
    return result;
}

juce::ValueTree RigEngine::saveShowForHost (const juce::File& loopsShowFile, rigshow::Result& result)
{
    // The host keeps the state; its "commit" is out of our hands. So the loops are staged and
    // tidied first, and the state only names a revision that is complete on disk. Nothing here
    // deletes a revision, so whichever state the host ends up keeping (this one, an older one,
    // an undo step) still finds its own sound.
    const RigDirty::SaveGuard saving (dirty);
    const juce::ScopedLock sl (rigshow::saveLock());
    result = {};
    rigshow::Staged staged;
    if (! rigshow::stage (loopsShowFile, loopAssets(), sampleRate, committedAudio, staged, result))
        return {};
    auto show = saveShow();
    show.setProperty (rigshow::kRevisionAttribute, staged.revision, nullptr);
    committedAudio = rigshow::committedFrom (staged);
    rigshow::finish (staged, result);
    result.ok = true;
    return show;
}

void RigEngine::noteShowFilesMessage (const juce::String& text, bool error)
{
    const juce::ScopedLock sl (messageLock);
    showFilesMessage = text;
    showFilesMessageIsError = error;
}

juce::String RigEngine::takeShowFilesMessage (bool& error)
{
    const juce::ScopedLock sl (messageLock);
    error = showFilesMessageIsError;
    showFilesMessageIsError = false;
    return std::exchange (showFilesMessage, {});
}

void RigEngine::readLoopAudio (const juce::File& showFile)
{
    // The folder of the revision the show names ("<show> loops" for an older build's show). If
    // it can't be found, no loop sound comes in (never another revision's), and why is kept.
    loopAudioProblem = {};
    const auto folder = rigshow::resolve (showFile, loadedAudioRevision, &loopAudioProblem);
    const bool haveFolder = folder != juce::File();
    auto fileAt = [&] (const juce::String& path) { return haveFolder ? folder.getChildFile (path) : juce::File(); };

    // What's on disk for this show, so the next save can link the files that haven't changed.
    rigshow::Committed loaded;
    loaded.showFile = showFile;
    loaded.revision = loadedAudioRevision;
    auto remember = [&loaded] (const juce::String& path, const juce::File& file, const juce::AudioBuffer<float>& audio)
    {
        rigshow::Entry e;
        e.path = path;
        e.frames = audio.getNumSamples();
        e.channels = audio.getNumChannels();
        e.bytes = file.getSize();
        e.modified = file.getLastModificationTime().toMilliseconds();
        e.sourceKey = e.decodedKey = rigshow::hashAudio (audio);
        loaded.entries.push_back (e);
    };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::array<int, RigLooper::kLayers> versions;   // what each layer's sound is, for the dirty model
    versions.fill (-1);
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const double beats = loadedLoopBeats[(size_t) i];
        const juce::String path ("Loop " + juce::String (i + 1) + ".wav");
        const auto file = fileAt (path);
        if (beats <= 0 || ! file.existsAsFile())
        {
            looper.makeRoom (i);
            continue;
        }
        // Loops play by the beat, so the file's sample rate doesn't matter.
        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
        {
            juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
            reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            remember (path, file, audio);
            versions[(size_t) i] = looper.setLayerAudio (i, std::move (audio), beats);
            setLoopName (i, loadedLoopNames[(size_t) i]);
        }
    }

    // The takes, for the layers that still have a loop.
    for (auto& set : loadedTakeSets)
    {
        const bool notes = RigTakes::isNotesLayer (set.layer);
        if (! notes && looper.getState (set.layer) == RigLooper::State::empty && loadedLoopBeats[(size_t) set.layer] <= 0)
            continue;
        const juce::String sub ("Takes/" + takesFolderName (set.layer) + "/");
        std::vector<RigTakes::Take> kept;
        int playingNumber = juce::isPositiveAndBelow (set.playing, (int) set.takes.size()) ? set.takes[(size_t) set.playing].number : -1;
        for (auto& take : set.takes)
        {
            const auto path = sub + "Take " + juce::String (take.number) + ".wav";
            const auto file = fileAt (path);
            if (std::unique_ptr<juce::AudioFormatReader> reader { file.existsAsFile() ? formats.createReaderFor (file) : nullptr })
            {
                take.audio.setSize (2, (int) reader->lengthInSamples);
                reader->read (&take.audio, 0, take.audio.getNumSamples(), 0, true, true);
                take.id = RigTakes::newId();   // its sound is new
                remember (path, file, take.audio);
                kept.push_back (std::move (take));
            }
            else if (notes && ! take.notes.empty())
                kept.push_back (std::move (take));   // (its notes are what count)
        }
        set.takes = std::move (kept);
        set.playing = 0;
        for (int k = 0; k < (int) set.takes.size(); ++k)
            if (set.takes[(size_t) k].number == playingNumber)
                set.playing = k;
        takes.restore (std::move (set));
    }
    loadedTakeSets.clear();

    for (int b = 0; b < (int) loopBanks.size(); ++b)
    {
        if (b == getCurrentLoopBank())
            continue;
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            auto& l = loopBanks[(size_t) b].layers[(size_t) i];
            l.audio = nullptr;
            const juce::String path ("Bank " + juce::String (b + 1) + "/Loop " + juce::String (i + 1) + ".wav");
            const auto file = fileAt (path);
            if (l.beats > 0 && file.existsAsFile())
                if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
                {
                    auto audio = std::make_shared<juce::AudioBuffer<float>> (2, (int) reader->lengthInSamples);
                    reader->read (audio.get(), 0, audio->getNumSamples(), 0, true, true);
                    remember (path, file, *audio);
                    l.audio = std::move (audio);
                }
            if (l.audio == nullptr)
                l.beats = 0;
        }
    }
    if (friendTrack.expectsAudio())
    {
        // A friend's track: its sound plays at the tools' 44.1 kHz whatever the file says (it was
        // saved at the Rig's rate, by the frame).
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        const auto file = fileAt (RigFriendTrack::kAssetPath);
        if (std::unique_ptr<juce::AudioFormatReader> reader { file.existsAsFile() ? formats.createReaderFor (file) : nullptr })
        {
            audio = std::make_shared<juce::AudioBuffer<float>> (2, (int) reader->lengthInSamples);
            reader->read (audio.get(), 0, audio->getNumSamples(), 0, true, true);
            remember (RigFriendTrack::kAssetPath, file, *audio);
        }
        friendTrack.setAudioFromShow (std::move (audio));
        friendBars = friendTrack.hasAudio() ? songLengthBarsOfTrack() : 0.0;
    }
    committedAudio = std::move (loaded);
    // The show is open: as it is now is what's saved.
    dirty.opened (committedAudio, versions);
}

//==============================================================================
juce::String RigEngine::sendSlicesToMpc (std::vector<crate::SharedAudio> slices, const juce::String& name)
{
    slices.erase (std::remove_if (slices.begin(), slices.end(),
                                  [] (const crate::SharedAudio& a) { return a == nullptr || a->getNumSamples() == 0; }),
                  slices.end());
    if (slices.empty())
        return "There are no slices to send.";

    auto& project = mpc.getProject();
    const auto base = name.trim().isNotEmpty() ? name.trim().substring (0, 12) : juce::String ("Chop");
    const int index = project.addBank ("Chop " + base);
    auto& bank = project.getBanks()[(size_t) index];

    const int count = juce::jmin ((int) slices.size(), (int) mpc::kPadCount);
    for (int i = 0; i < count; ++i)
    {
        auto& pad = bank.pads[(size_t) i];
        pad.audio = slices[(size_t) i];
        pad.name = base + " " + juce::String (i + 1);   // the MPC keeps 16 characters
        pad.mode = PadMode::mono;                        // a new hit cuts the last slice off
        pad.mono = pad.audio->getNumChannels() == 1;
    }

    project.setCurrent (index);
    mpc.padBankOnScreen = 0;   // the drum pads play A01 to A16
    routing.padBank = 0;
    project.changed();

    juce::String text = "Sent " + juce::String (count) + " slices to a new MPC kit, \"" + bank.name + "\": pads A01 to "
                        + padLabel (count - 1) + ". The drum pads play them now.";
    if (count < (int) slices.size())
        text << " (The other " << ((int) slices.size() - count) << " didn't fit; a kit has 64 pads.)";
    return text;
}

//==============================================================================
// Loop banks.
int RigEngine::LoopBank::loopCount() const
{
    int n = 0;
    for (const auto& l : layers)
        n += l.audio != nullptr && l.beats > 0 ? 1 : 0;
    return n;
}

juce::String RigEngine::getLoopBankName (int bank) const
{
    if (juce::isPositiveAndBelow (bank, (int) loopBanks.size()) && loopBanks[(size_t) bank].name.isNotEmpty())
        return loopBanks[(size_t) bank].name;
    return "Bank " + juce::String (bank + 1);
}

bool RigEngine::loopBankBusy (juce::String& why) const
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto state = looper.getState (i);
        if (state == RigLooper::State::armed || state == RigLooper::State::recording || state == RigLooper::State::overdubbing)
        {
            why = "Layer " + juce::String (i + 1) + " is "
                  + (state == RigLooper::State::overdubbing ? "overdubbing" : state == RigLooper::State::armed ? "about to record" : "recording")
                  + ". Finish it first, then switch banks.";
            return true;
        }
    }
    return false;
}

void RigEngine::storeLoopBank()
{
    if (loopBanks.empty())
    {
        loopBanks.emplace_back();
        currentLoopBank = 0;
    }
    currentLoopBank = juce::jlimit (0, (int) loopBanks.size() - 1, currentLoopBank);
    auto& bank = loopBanks[(size_t) currentLoopBank];
    std::array<int, RigLooper::kLayers> copied;   // the version each copy is of (-1: not copied)
    copied.fill (-1);
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto& live = looper.getLayer (i);
        auto& l = bank.layers[(size_t) i];
        // A layer whose new loop is still waiting for its bar: what's coming is already kept.
        if (looper.isSwapPending (i))
            continue;
        // Only copies a loop that changed since it was last kept (versions move when it does).
        const int version = live.version.load();
        if (version != l.version)
        {
            int of = -1;
            auto audio = looper.copyLayerAudio (i, &of);
            copied[(size_t) i] = of == version ? of : -1;
            const double beats = audio.getNumSamples() > 0 ? live.lengthBeats.load() : 0.0;
            l.audio = beats > 0 ? std::make_shared<const juce::AudioBuffer<float>> (std::move (audio)) : nullptr;
            l.beats = l.audio != nullptr ? beats : 0.0;
            l.version = version;
        }
        l.source = live.source.load();
        l.bars = live.bars.load();
        l.level = live.level.load();
        l.mute = live.mute.load();
        l.fx.reset();
        if (loopFx[(size_t) i].isActive())
            loopFx[(size_t) i].getStateInformation (l.fx);
    }
    dirty.bankStored (currentLoopBank, copied);
}

const std::vector<RigEngine::LoopBank>& RigEngine::getLoopBanks()
{
    storeLoopBank();
    return loopBanks;
}

void RigEngine::applyBankFx (int i)
{
    auto& rack = loopFx[(size_t) i];
    if (pendingBankFx[(size_t) i].isEmpty())
        rack.reset();
    else
        rack.setStateInformation (pendingBankFx[(size_t) i].getData(), (int) pendingBankFx[(size_t) i].getSize());
    bankFxPending[(size_t) i] = false;
}

void RigEngine::serviceLoopBanks()
{
    bool waiting = false;
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        if (! bankFxPending[(size_t) i])
            continue;
        if (looper.isSwapPending (i))
            waiting = true;
        else
        {
            applyBankFx (i);
            if (looper.getState (i) == RigLooper::State::empty)
                looper.makeRoom (i);
        }
    }
    if (! waiting)
    {
        if (queuedLoopBank >= 0)
            dirty.noteChange();   // the switch is done
        queuedLoopBank = -1;
    }
}

void RigEngine::putLoopBankOnLayers (int index, double beat, const RigScene::State* scene)
{
    dirty.bankPut (index, scene);
    const auto& bank = loopBanks[(size_t) index];
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto& l = bank.layers[(size_t) i];
        auto& live = looper.getLayer (i);
        live.source = l.source;
        live.bars = l.bars;
        bool mute = l.mute;
        if (scene != nullptr && scene->loopMutes[(size_t) i] >= 0)
            mute = scene->loopMutes[(size_t) i] == 1;
        // The bank keeps its own copy; the layer gets one it can overdub on.
        juce::AudioBuffer<float> audio;
        if (l.audio != nullptr)
            audio.makeCopyOf (*l.audio);
        dirty.bankQueued (index, i, looper.queueLayerAudio (i, std::move (audio), l.audio != nullptr ? l.beats : 0.0, beat, l.level, mute));
        pendingBankFx[(size_t) i] = l.fx;
        bankFxPending[(size_t) i] = true;
        loopBanks[(size_t) index].layers[(size_t) i].version = beat < 0 ? live.version.load() : -1;
        if (beat < 0)
        {
            applyBankFx (i);
            if (looper.getState (i) == RigLooper::State::empty)
                looper.makeRoom (i);
        }
    }
    currentLoopBank = index;
    queuedLoopBank = beat < 0 ? -1 : index;
    dirty.noteChange();
}

juce::String RigEngine::saveLoopBank (const juce::String& name)
{
    juce::String why;
    if (loopBankBusy (why))
        return why;
    storeLoopBank();
    auto& bank = loopBanks[(size_t) currentLoopBank];
    if (name.trim().isNotEmpty())
        bank.name = name.trim().substring (0, 40);
    dirty.noteChange();
    const int n = bank.loopCount();
    return "Kept " + juce::String (n) + (n == 1 ? " loop" : " loops") + " in \"" + getLoopBankName (currentLoopBank)
           + "\". Save the show (Cmd+S) to keep it on disk.";
}

juce::String RigEngine::newLoopBank (const juce::String& name)
{
    juce::String why;
    if (loopBankBusy (why))
        return why;
    if ((int) loopBanks.size() >= kMaxLoopBanks)
        return "That's the most banks a show can have (" + juce::String (kMaxLoopBanks) + "). Delete one first.";
    storeLoopBank();
    const auto kept = getLoopBankName (currentLoopBank);
    LoopBank fresh;
    fresh.name = name.trim().isNotEmpty() ? name.trim().substring (0, 40) : "Bank " + juce::String ((int) loopBanks.size() + 1);
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        // Ready to record the same way: the same sources and lengths.
        fresh.layers[(size_t) i].source = looper.getLayer (i).source.load();
        fresh.layers[(size_t) i].bars = looper.getLayer (i).bars.load();
    }
    loopBanks.insert (loopBanks.begin() + currentLoopBank + 1, std::move (fresh));
    const int index = currentLoopBank + 1;
    putLoopBankOnLayers (index, transport.isPlaying() ? std::ceil (transport.getPositionInBeats() / juce::jmax (1, transport.getBeatsPerBar()) - 1.0e-9) * juce::jmax (1, transport.getBeatsPerBar()) : -1.0, nullptr);
    // Song parts that pick banks after it keep the same banks.
    for (auto& scene : scenes.getScenes())
        if (scene.state.loopBank >= index)
            ++scene.state.loopBank;
    dirty.banksChanged();
    dirty.authoredField (RigDirty::bankCurrent, 0);
    return "Kept your loops in \"" + kept + "\" and started \"" + getLoopBankName (index)
           + "\", empty and ready to record" + (transport.isPlaying() ? " (from the next bar)." : ".");
}

juce::String RigEngine::clearAllLoops()
{
    bool anything = false;
    for (int i = 0; i < RigLooper::kLayers; ++i)
        anything = anything || looper.getState (i) != RigLooper::State::empty || looper.isSwapPending (i) || bankFxPending[(size_t) i]
                   || loopFx[(size_t) i].isActive() || looper.getLayer (i).mute.load();
    if (! anything)
        return "The looper is already empty: no loops and no loop effects. Press Rec next layer to start.";

    // Keep it all for Undo: each loop that's playing (not one half recorded), its effects and name.
    auto kept = std::make_unique<LoopBank>();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto& live = looper.getLayer (i);
        auto& l = kept->layers[(size_t) i];
        const auto state = looper.getState (i);
        if (state == RigLooper::State::playing || state == RigLooper::State::overdubbing)
        {
            auto audio = looper.copyLayerAudio (i);
            if (audio.getNumSamples() > 0 && live.lengthBeats.load() > 0)
            {
                l.beats = live.lengthBeats.load();
                l.audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (audio));
            }
        }
        l.source = live.source.load();
        l.bars = live.bars.load();
        l.level = live.level.load();
        l.mute = live.mute.load();
        if (loopFx[(size_t) i].isActive())
            loopFx[(size_t) i].getStateInformation (l.fx);
        clearedLoopNames[(size_t) i] = getLoopName (i);
    }
    const int loops = kept->loopCount();

    // Empty now, from scratch: no loop waiting to come in, nothing armed, no effects.
    queuedLoopBank = -1;
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        looper.cancelSwap (i);
        bankFxPending[(size_t) i] = false;
        pendingBankFx[(size_t) i].reset();
        looper.reset (i);
        looper.queueLayerAudio (i, {}, 0.0, -1.0, LoopBank::Layer().level, false);
        loopFx[(size_t) i].reset();
        loopNames[(size_t) i] = {};
        looper.makeRoom (i);
    }
    clearedLoops = std::move (kept);
    dirty.banksChanged();
    for (int i = 0; i < RigLooper::kLayers; ++i)
        dirty.authoredField (RigDirty::loopMute, i);
    return "Cleared all " + juce::String (loops) + (loops == 1 ? " loop" : " loops")
           + " and their effects: empty layers, ready to record. Undo brings them back.";
}

bool RigEngine::canUndoClearAllLoops() const
{
    if (clearedLoops == nullptr)
        return false;
    // Only while the layers are still empty: it never records over something new.
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (looper.getState (i) != RigLooper::State::empty || looper.isSwapPending (i))
            return false;
    return true;
}

juce::String RigEngine::undoClearAllLoops()
{
    if (clearedLoops == nullptr)
        return "Nothing to undo: Clear all hasn't been used.";
    if (! canUndoClearAllLoops())
    {
        clearedLoops.reset();
        return "Can't bring the cleared loops back: you've recorded since. (Clear all again first if you want them gone.)";
    }
    const auto kept = std::move (clearedLoops);
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto& l = kept->layers[(size_t) i];
        auto& live = looper.getLayer (i);
        live.source = l.source;
        live.bars = l.bars;
        juce::AudioBuffer<float> audio;
        if (l.audio != nullptr)
            audio.makeCopyOf (*l.audio);
        looper.queueLayerAudio (i, std::move (audio), l.audio != nullptr ? l.beats : 0.0, -1.0, l.level, l.mute);
        if (l.fx.isEmpty())
            loopFx[(size_t) i].reset();
        else
            loopFx[(size_t) i].setStateInformation (l.fx.getData(), (int) l.fx.getSize());
        if (l.audio != nullptr && clearedLoopNames[(size_t) i].isNotEmpty())
            setLoopName (i, clearedLoopNames[(size_t) i]);
        if (looper.getState (i) == RigLooper::State::empty)
            looper.makeRoom (i);
    }
    dirty.banksChanged();
    for (int i = 0; i < RigLooper::kLayers; ++i)
        dirty.authoredField (RigDirty::loopMute, i);
    const int loops = kept->loopCount();
    return "Brought back " + juce::String (loops) + (loops == 1 ? " loop" : " loops") + " and their effects.";
}

juce::String RigEngine::switchLoopBank (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumLoopBanks()))
        return {};
    juce::String why;
    if (loopBankBusy (why))
        return why;
    storeLoopBank();
    if (index == currentLoopBank && queuedLoopBank < 0)
    {
        dirty.authoredField (RigDirty::bankCurrent, 0);   // you picked it (whoever switched to it)
        return "\"" + getLoopBankName (index) + "\" is the bank playing.";
    }
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const bool playing = transport.isPlaying();
    putLoopBankOnLayers (index, playing ? std::ceil (transport.getPositionInBeats() / bpb - 1.0e-9) * bpb : -1.0, nullptr);
    dirty.authoredField (RigDirty::bankCurrent, 0);   // you picked it
    const int n = loopBanks[(size_t) index].loopCount();
    return "\"" + getLoopBankName (index) + "\" (" + juce::String (n) + (n == 1 ? " loop" : " loops") + ")"
           + (playing ? " comes in at the next bar." : " is on the layers.");
}

juce::String RigEngine::duplicateLoopBank (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumLoopBanks()))
        return {};
    if ((int) loopBanks.size() >= kMaxLoopBanks)
        return "That's the most banks a show can have (" + juce::String (kMaxLoopBanks) + "). Delete one first.";
    storeLoopBank();
    auto copy = loopBanks[(size_t) index];   // the sounds are shared until one is recorded over
    copy.name = getLoopBankName (index).substring (0, 34) + " copy";
    loopBanks.insert (loopBanks.begin() + index + 1, std::move (copy));
    if (currentLoopBank > index)
        ++currentLoopBank;
    if (queuedLoopBank > index)
        ++queuedLoopBank;
    for (auto& scene : scenes.getScenes())
        if (scene.state.loopBank > index)
            ++scene.state.loopBank;
    dirty.banksChanged();
    return "Made \"" + getLoopBankName (index + 1) + "\". Switch to it to change it; \"" + getLoopBankName (index) + "\" stays as it is.";
}

juce::String RigEngine::renameLoopBank (int index, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (index, getNumLoopBanks()) || name.trim().isEmpty())
        return {};
    storeLoopBank();
    loopBanks[(size_t) index].name = name.trim().substring (0, 40);
    dirty.noteChange();
    return "Renamed the bank \"" + getLoopBankName (index) + "\".";
}

juce::String RigEngine::deleteLoopBank (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) loopBanks.size()))
        return {};
    if (loopBanks.size() < 2)
        return "That's your only bank. Clear its layers instead.";
    const auto name = getLoopBankName (index);
    if (index == currentLoopBank)
    {
        juce::String why;
        if (loopBankBusy (why))
            return why;
        const int to = index > 0 ? index - 1 : 1;
        storeLoopBank();
        const int bpb = juce::jmax (1, transport.getBeatsPerBar());
        putLoopBankOnLayers (to, transport.isPlaying() ? std::ceil (transport.getPositionInBeats() / bpb - 1.0e-9) * bpb : -1.0, nullptr);
    }
    else
        storeLoopBank();
    loopBanks.erase (loopBanks.begin() + index);
    if (currentLoopBank > index)
        --currentLoopBank;
    if (queuedLoopBank > index)
        --queuedLoopBank;
    for (auto& scene : scenes.getScenes())
    {
        if (scene.state.loopBank == index)
            scene.state.loopBank = -1;
        else if (scene.state.loopBank > index)
            --scene.state.loopBank;
    }
    dirty.banksChanged();
    dirty.authoredField (RigDirty::bankCurrent, 0);
    return "Deleted \"" + name + "\". Now playing \"" + getLoopBankName (currentLoopBank) + "\".";
}
