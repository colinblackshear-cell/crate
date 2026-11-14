#include "RigEngine.h"
#include "RigSong.h"

#include "core/control/Launchpad.h"

#include <algorithm>

#include "core/AudioFiles.h"
#include "core/Edits.h"

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
        default:          return id >= fxTool && id < numTools ? "FX " + stripName (id - fxTool) : juce::String();
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
        default:          return id >= fxTool && id < numTools ? (juce::AudioProcessor&) fx[(size_t) (id - fxTool)] : mpc;
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

RigEngine::RigEngine()
{
    arrangeFrom.fill (1.0f);
    arrangeTo.fill (1.0f);
    arrangeRestart.fill (-1.0);
    // The Rig is the host, so the tools follow its clock the way they follow Logic's.
    mpc.setPlayHead (&transport);
    sampler.setPlayHead (&transport);
    vocoder.setPlayHead (&transport);
    bass.setPlayHead (&transport);
    synthTool.setPlayHead (&transport);
    drums.setPlayHead (&transport);
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
    blankShow = saveShow();
    blankShow.removeChild (blankShow.getChildWithName ("Quantize"), nullptr);   // kept by New song
}

void RigEngine::newShow()
{
    const auto keep = screens.createCopy();
    const auto quantizeSettings = quantizer.settings;
    loadShow (blankShow);
    screens = keep;
    quantizer.settings = quantizeSettings;
}

bool RigEngine::isBlank() const
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (looper.getState (i) != RigLooper::State::empty)
            return false;
    auto now = saveShow();
    now.removeChild (now.getChildWithName ("Screens"), nullptr);
    now.removeChild (now.getChildWithName ("Quantize"), nullptr);
    return now.isEquivalentTo (blankShow);
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
    loopRoom.stopTimer();
    mpc.setPlayHead (nullptr);
    sampler.setPlayHead (nullptr);
    vocoder.setPlayHead (nullptr);
    bass.setPlayHead (nullptr);
    synthTool.setPlayHead (nullptr);
    drums.setPlayHead (nullptr);
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

    for (auto* p : std::initializer_list<juce::AudioProcessor*> { &mpc, &sampler, &vocoder, &bass, &synthTool, &drums })
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
    inputBuffer.setSize (2, maxBlock);
    rawInputBuffer.setSize (2, maxBlock);
    loopsBuffer.setSize (2, maxBlock);
    looper.prepare (sampleRate, maxBlock);
    loopRoom.startTimerHz (10);
    performFx.prepare (sampleRate);
    mixBuffer.setSize (2, maxBlock);
    clickBuffer.setSize (2, maxBlock);
    capture.prepare (sampleRate);
    for (auto* m : { &midiIn, &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi, &drumsMidi, &vocoderMissed, &midiScratch })
        m->ensureSize (4096);
    vocoderFade.reset (sampleRate, 0.015);
    vocoderFade.setCurrentAndTargetValue (vocoderOn.load() ? 1.0f : 0.0f);
    inputFade.reset (sampleRate, 0.015);
    inputFade.setCurrentAndTargetValue (0.0f);
    micFade.reset (sampleRate, 0.015);
    micFade.setCurrentAndTargetValue (micOn.load() ? 1.0f : 0.0f);
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
}

juce::String RigEngine::stemName (int s)
{
    static const char* names[] = { "MPC", "Vocoder", "Bass", "Synth", "Input", "Sampler", "Loops", "Drum machine" };
    return juce::isPositiveAndBelow (s, (int) numStems) ? names[s] : "";
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

    if (r.presses[RigRouting::mpcMute] % 2 == 1)
    {
        strips[mpcStrip].mute = ! strips[mpcStrip].mute.load();
        moved = true;
    }
    if (r.presses[RigRouting::samplerMute] % 2 == 1)
    {
        strips[samplerStrip].mute = ! strips[samplerStrip].mute.load();
        moved = true;
    }
    if (r.presses[RigRouting::vocoderMute] % 2 == 1)
    {
        strips[vocoderStrip].mute = ! strips[vocoderStrip].mute.load();
        moved = true;
    }
    for (auto [control, strip] : { std::pair<int, int> { RigRouting::bassMute, bassStrip }, { RigRouting::synthMute, synthStrip }, { RigRouting::inputMute, inputStrip }, { RigRouting::loopsMute, loopsStrip }, { RigRouting::drumsMute, drumsStrip } })
        if (r.presses[(size_t) control] % 2 == 1)
        {
            strips[(size_t) strip].mute = ! strips[(size_t) strip].mute.load();
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
            moved = true;
        }
    }
    scenes.nextPresses += r.presses[RigRouting::nextScene];
    scenes.backPresses += r.presses[RigRouting::previousScene];
    if (r.presses[RigRouting::inputHear] % 2 == 1)
    {
        inputOn = ! inputOn.load();
        moved = true;
    }
    if (r.presses[RigRouting::vocoderOn] % 2 == 1)
    {
        vocoderOn = ! vocoderOn.load();
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
        lastMidiTime = juce::Time::getMillisecondCounter();

    mpcMidi.clear();
    samplerMidi.clear();
    vocoderMidi.clear();
    bassMidi.clear();
    synthMidi.clear();
    drumsMidi.clear();
    RigRouting::Result controls;
    std::array<juce::MidiBuffer*, RigRouting::kNumDests> destinations { nullptr, &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi, &drumsMidi };
    routing.route (midiIn, destinations, controls);
    applyControls (controls);
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

    mpcBlock.clear();
    mpc.processBlock (mpcBlock, mpcMidi);
    drumsBlock.clear();
    drums.processBlock (drumsBlock, drumsMidi);
    // A plugin in the Bass's or Synth's slot plays the notes the tool plays (its steps, arp and
    // keys) instead of the tool's own sound.
    auto playTool = [] (juce::AudioProcessor& tool, juce::AudioBuffer<float>& block, juce::MidiBuffer& midi, PluginSlot& slot,
                        juce::MidiBuffer& tap, auto setTap)
    {
        block.clear();
        const bool external = slot.isLoaded() && ! slot.bypassed.load();
        tap.clear();
        setTap (external ? &tap : nullptr);
        tool.processBlock (block, midi);
        if (external && ! slot.process (block, tap))
            block.clear();
    };
    playTool (bass, bassBlock, bassMidi, bassPlugin, bassTap, [this] (juce::MidiBuffer* t) { bass.setNoteTap (t); });
    playTool (synthTool, synthBlock, synthMidi, synthPlugin, synthTap, [this] (juce::MidiBuffer* t) { synthTool.setNoteTap (t); });

    // The vocoder shapes its synth with your voice from the input.
    const bool hearMic = micOn.load();
    // Switched off, it fades out and then stops (it uses the most processor time); the keys
    // you let go meanwhile are kept for it, so a note can't hang when it comes back on.
    vocoderFade.setTargetValue (vocoderOn.load() ? 1.0f : 0.0f);
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
    inputFade.setTargetValue (inputOn.load() && hearMic && numInputs > 0 ? 1.0f : 0.0f);
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

    sampler.processBlock (samplerBlock, samplerMidi);
    applyStrip (samplerStrip, samplerBlock);

    // Each tool's strip (in Stem order), for its own output in Logic.
    const std::array<juce::AudioBuffer<float>*, numStems> stems { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock, &samplerBlock, nullptr, &drumsBlock };

    mix.clear();
    for (int st = 0; st < numStems; ++st)
        if (st != loopsStem && stemOut[(size_t) st] == nullptr)
            for (int ch = 0; ch < 2; ++ch)
                mix.addFrom (ch, 0, *stems[(size_t) st], ch, 0, n);

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
    if (looper.process (clock, sources, loopsBlock))
    {
        transport.play();
        controlsMoved = true;
    }
    applyStrip (loopsStrip, loopsBlock);
    if (stemOut[loopsStem] == nullptr)
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, loopsBlock, ch, 0, n);
    for (int st = 0; st < numStems; ++st)
        if (auto* const* out = stemOut[(size_t) st])
        {
            const auto& from = st == loopsStem ? loopsBlock : *stems[(size_t) st];
            for (int ch = 0; ch < 2; ++ch)
                if (out[ch] != nullptr)
                    juce::FloatVectorOperations::copy (out[ch] + offset, from.getReadPointer (ch), n);
        }

    // The metronome: heard (under the master fader), but kept out of recordings.
    juce::AudioBuffer<float> click (clickBuffer.getArrayOfWritePointers(), 2, n);
    click.clear();
    transport.endBlock (n, &click);

    processReturns (mix, n);
    fx[masterStrip].processBlock (mix, noMidi);
    performFx.process (mix, transport.getBlockPosition(), transport.getBpm(), transport.isPlayingThisBlock());

    auto& master = strips[masterStrip];
    auto& masterGain = gains[masterStrip];
    masterGain.setTargetValue (master.mute.load() ? 0.0f : faderGain (master.level.load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
            mix.setSample (ch, i, juce::jlimit (-1.0f, 1.0f, mix.getSample (ch, i) * g));
    }

    recorder.process (recClock, { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock, &samplerBlock, &loopsBlock, &mix, &drumsBlock }, n);
    capture.process (recClock, { &rawInput, &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock, &samplerBlock, &loopsBlock, &mix, &drumsBlock }, n);

    const float clickGain = masterGain.getTargetValue();
    for (int ch = 0; ch < 2; ++ch)
        mix.addFrom (ch, 0, click, ch, 0, n, clickGain);

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
}

juce::String RigEngine::serviceCapture()
{
    if (capture.getState() != RigCapture::State::done)
        return {};
    double rate = 0, beats = 0;
    const int source = capture.getSource(), bars = capture.getBars();
    auto taken = capture.take (rate, beats);   // sets the rate, so first
    auto audio = crate::Edits::collapseFakeStereo (crate::AudioFiles::resample (taken, rate));
    if (audio.getNumSamples() < crate::samples (0.02) || audio.getMagnitude (0, audio.getNumSamples()) < 1.0e-4f)
        return "Nothing came from " + RigCapture::sourceName (source) + ", so nothing was recorded.";

    const auto name = RigCapture::sourceName (source).upToFirstOccurrenceOf (" /", false, false)
                      + (bars > 0 ? " " + juce::String (bars) + (bars == 1 ? " bar" : " bars") : juce::String());
    if (captureDest == RigCapture::toMpcPad)
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
    return recorder.start (folder, { "MPC", "Vocoder", "Bass", "Synth", "Input", "Sampler", "Loops", "Mix", "Drum machine" },
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
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
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
    // A Launchpad's pads come in on the drum pads' channel, whatever channel it's set to, so it
    // plays the MPC (or the drum machine) without setting anything up.
    if (source != nullptr && crate::launchpad::isLaunchpad (source->getName()) && crate::launchpad::isPadMessage (m))
    {
        auto pad = m;
        pad.setChannel (juce::jlimit (1, 16, routing.padChannel.load() > 0 ? routing.padChannel.load() : 10));
        incoming.addMessageToQueue (pad);
        return;
    }
    incoming.addMessageToQueue (m);
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

    if (screens.getNumChildren() > 0 || screens.getNumProperties() > 0)
        show.appendChild (screens.createCopy(), nullptr);
    show.appendChild (quantizer.toTree(), nullptr);

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

    show.setProperty ("songMode", scenes.songMode, nullptr);
    show.appendChild (timeline.toTree(), nullptr);
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
                                                                    { "Synth", &self.synthTool }, { "Drum machine", &self.drums } };
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
    padLoopLayer = -1;

    for (int c = 0; c < RigRouting::numControls; ++c)
        routing.forget (c);
    scenes.clear();
    scenes.songMode = (bool) show.getProperty ("songMode", false);
    timeline.fromTree (show.getChildWithName (RigTimeline::kTreeType));
    {
        // Older shows only had what the Sampler records.
        const int older = (int) show.getProperty ("sampleSource", 0);
        const int fallback = older == (int) SampleSource::vocoder ? RigCapture::vocoder : older == (int) SampleSource::mix ? RigCapture::mix : RigCapture::mic;
        captureSource = juce::jlimit (0, RigCapture::numSources - 1, (int) show.getProperty ("captureSource", fallback));
    }
    captureDest = juce::jlimit (0, RigCapture::numDests - 1, (int) show.getProperty ("captureDest", (int) RigCapture::toSampler));
    captureBars = juce::jlimit (0, 16, (int) show.getProperty ("captureBars", 0));
    quantizer.fromTree (show.getChildWithName ("Quantize"));
    screens = show.getChildWithName ("Screens").isValid() ? show.getChildWithName ("Screens").createCopy() : juce::ValueTree ("Screens");
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        looper.cancelSwap (i);
        looper.reset (i);
        loadedLoopBeats[(size_t) i] = 0;
        bankFxPending[(size_t) i] = false;
    }
    loopBanks.clear();
    currentLoopBank = -1;
    queuedLoopBank = -1;
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
    // Shows from before the effects have none.
    for (auto& f : fx)
        f.reset();
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
            else
                for (int st = 0; st < numStrips; ++st)
                    if (name == toolName (fxTool + st))
                        fx[(size_t) st].setStateInformation (state.getData(), (int) state.getSize());
        }
    }
    // Shows from before the Song screen: their song parts become its sections.
    rigsong::sectionsFromParts (*this);
    controlsMoved = true;
}

//==============================================================================
namespace
{
void setParameter (juce::AudioProcessorValueTreeState& state, const char* id, float plainValue)
{
    if (auto* p = state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
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
    for (int st = 0; st < numStrips && st < RigScene::kMaxStrips; ++st)
        if (s.stripMutes[(size_t) st] >= 0)
            strips[(size_t) st].mute = s.stripMutes[(size_t) st] == 1;
    for (int l = 0; l < RigLooper::kLayers; ++l)
        if (s.loopMutes[(size_t) l] >= 0)
            looper.getLayer (l).mute = s.loopMutes[(size_t) l] == 1;
    if (s.vocoderOn >= 0)
        vocoderOn = s.vocoderOn == 1;
    if (s.inputOn >= 0)
        inputOn = s.inputOn == 1;
    if (s.bassSequencer >= 0)
        setParameter (bass.params, "sequencer", (float) s.bassSequencer);
    if (s.arp >= 0)
        setParameter (synthTool.params, "arpOn", (float) s.arp);
    if (s.drumSequencer >= 0)
        setParameter (drums.params, "sequencer", (float) s.drumSequencer);
    // (On the audio thread: switched off, the sequencer stops itself as it stops following.)
    if (s.mpcBeat >= 0)
        mpc.getSequencer().followHost = s.mpcBeat == 1;
}

void RigEngine::arrange (int n)
{
    timeline.update();
    const bool running = timeline.on.load() && transport.isPlayingThisBlock() && ! timeline.liveEmpty();
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
        else if (timeline.stopAtEnd.load() && ! timeline.cycle.load()
                 && transport.getBlockPosition() >= (timeline.liveEnd() + 1.0) * bpb)
        {
            transport.stop();   // a bar after the end, so echoes ring out
            notesOffRequested = true;
            controlsMoved = true;
        }
    }

    const double beat0 = transport.getBlockPosition();
    const double beat1 = beat0 + n * transport.getBpm() / (60.0 * sampleRate);
    const auto& lanes = RigTimeline::lanes();
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        const auto& info = lanes[(size_t) lane];
        const float from = timeline.liveGain (lane, beat0 / bpb), to = timeline.liveGain (lane, beat1 / bpb);
        if (! info.isLoop)
        {
            arrangeFrom[(size_t) info.index] = from;
            arrangeTo[(size_t) info.index] = to;
            continue;
        }
        looper.setArrangeGain (info.index, from, to);
        // A block starts its loop from the top on the block's first beat.
        const double start = timeline.liveBlockStart (lane, beat1 / bpb);
        if (start >= 0 && std::abs (start * bpb - arrangeRestart[(size_t) info.index]) > 1.0e-6)
        {
            looper.restartAt (info.index, start * bpb);
            arrangeRestart[(size_t) info.index] = start * bpb;
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
    timeline.on = true;
    scenes.songMode = false;
    const auto& lanes = RigTimeline::lanes();
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        if (! timeline.laneUsed (lane))
            continue;
        const auto& info = lanes[(size_t) lane];
        if (info.isLoop)
            looper.getLayer (info.index).mute = false;
        else
            strips[(size_t) info.index].mute = false;
        if (! info.isLoop && info.index == vocoderStrip)
            vocoderOn = true;
        if (! info.isLoop && info.index == inputStrip)
            inputOn = true;
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
            setParameter (bass.params, "pattern", (float) clip);
            setParameter (bass.params, "sequencer", 1.0f);
            break;
        case drumsStrip:
            setParameter (drums.params, "pattern", (float) clip);
            setParameter (drums.params, "sequencer", 1.0f);
            break;
        case synthStrip:
            setParameter (synthTool.params, "arpOn", clip > 0 ? 1.0f : 0.0f);
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
        setParameter (bass.params, "pattern", (float) s.bassPattern);
    if (s.drumPattern >= 0)
        setParameter (drums.params, "pattern", (float) s.drumPattern);

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
            putLoopBankOnLayers (s.loopBank, at, &s);
        }
    }
}

//==============================================================================
juce::File RigEngine::loopsFolder (const juce::File& showFile)
{
    return showFile.getSiblingFile (showFile.getFileNameWithoutExtension() + " loops");
}

bool RigEngine::writeLoopAudio (const juce::File& showFile) const
{
    const auto folder = loopsFolder (showFile);
    folder.deleteRecursively();
    bool ok = true, any = false;
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto audio = looper.copyLayerAudio (i);
        if (audio.getNumSamples() == 0)
            continue;
        if (! any)
            folder.createDirectory();
        any = true;
        const auto file = folder.getChildFile ("Loop " + juce::String (i + 1) + ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
        {
            ok = false;
            continue;
        }
        const double rate = sampleRate > 0 ? sampleRate : 44100.0;
        auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            ok = writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()) && ok;
        else
            ok = false;
    }

    // The other loop banks, each in its own folder ("Bank 2/Loop 1.wav").
    for (int b = 0; b < (int) loopBanks.size(); ++b)
    {
        if (b == getCurrentLoopBank())
            continue;
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            const auto& l = loopBanks[(size_t) b].layers[(size_t) i];
            if (l.audio == nullptr || l.beats <= 0 || l.audio->getNumSamples() == 0)
                continue;
            const auto sub = folder.getChildFile ("Bank " + juce::String (b + 1));
            sub.createDirectory();
            std::unique_ptr<juce::OutputStream> stream (sub.getChildFile ("Loop " + juce::String (i + 1) + ".wav").createOutputStream());
            juce::WavAudioFormat wav;
            const double rate = sampleRate > 0 ? sampleRate : 44100.0;
            auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
            if (stream == nullptr)
                ok = false;
            else if (auto writer = wav.createWriterFor (stream, options))
                ok = writer->writeFromAudioSampleBuffer (*l.audio, 0, l.audio->getNumSamples()) && ok;
            else
                ok = false;
        }
    }
    return ok;
}

void RigEngine::readLoopAudio (const juce::File& showFile)
{
    const auto folder = loopsFolder (showFile);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const double beats = loadedLoopBeats[(size_t) i];
        const auto file = folder.getChildFile ("Loop " + juce::String (i + 1) + ".wav");
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
            looper.setLayerAudio (i, std::move (audio), beats);
        }
    }

    for (int b = 0; b < (int) loopBanks.size(); ++b)
    {
        if (b == getCurrentLoopBank())
            continue;
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            auto& l = loopBanks[(size_t) b].layers[(size_t) i];
            l.audio = nullptr;
            const auto file = folder.getChildFile ("Bank " + juce::String (b + 1)).getChildFile ("Loop " + juce::String (i + 1) + ".wav");
            if (l.beats > 0 && file.existsAsFile())
                if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
                {
                    auto audio = std::make_shared<juce::AudioBuffer<float>> (2, (int) reader->lengthInSamples);
                    reader->read (audio.get(), 0, audio->getNumSamples(), 0, true, true);
                    l.audio = std::move (audio);
                }
            if (l.audio == nullptr)
                l.beats = 0;
        }
    }
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
            auto audio = looper.copyLayerAudio (i);
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
        queuedLoopBank = -1;
}

void RigEngine::putLoopBankOnLayers (int index, double beat, const RigScene::State* scene)
{
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
        looper.queueLayerAudio (i, std::move (audio), l.audio != nullptr ? l.beats : 0.0, beat, l.level, mute);
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
    return "Kept your loops in \"" + kept + "\" and started \"" + getLoopBankName (index)
           + "\", empty and ready to record" + (transport.isPlaying() ? " (from the next bar)." : ".");
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
        return "\"" + getLoopBankName (index) + "\" is the bank playing.";
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const bool playing = transport.isPlaying();
    putLoopBankOnLayers (index, playing ? std::ceil (transport.getPositionInBeats() / bpb - 1.0e-9) * bpb : -1.0, nullptr);
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
    return "Made \"" + getLoopBankName (index + 1) + "\". Switch to it to change it; \"" + getLoopBankName (index) + "\" stays as it is.";
}

juce::String RigEngine::renameLoopBank (int index, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (index, getNumLoopBanks()) || name.trim().isEmpty())
        return {};
    storeLoopBank();
    loopBanks[(size_t) index].name = name.trim().substring (0, 40);
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
    return "Deleted \"" + name + "\". Now playing \"" + getLoopBankName (currentLoopBank) + "\".";
}
