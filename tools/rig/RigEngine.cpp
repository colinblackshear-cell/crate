#include "RigEngine.h"

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
        default:          return id >= fxTool && id < numTools ? (juce::AudioProcessor&) fx[(size_t) (id - fxTool)] : mpc;
    }
}

juce::String RigEngine::sampleSourceName (int source)
{
    switch ((SampleSource) source)
    {
        case SampleSource::mic:     return "Mic";
        case SampleSource::vocoder: return "Vocoder";
        case SampleSource::mix:     return "Rig mix";
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
    // The Rig is the host, so the tools follow its clock the way they follow Logic's.
    mpc.setPlayHead (&transport);
    sampler.setPlayHead (&transport);
    vocoder.setPlayHead (&transport);
    bass.setPlayHead (&transport);
    synthTool.setPlayHead (&transport);
    for (auto& f : fx)
        f.setPlayHead (&transport);
    transport.setBpm (mpc.getProject().currentSequence().bpm);
    // The MPC changes sequence (and moves through its songs) on the Rig's bar lines.
    mpc.getSequencer().switchWhileFollowing = true;
    mpc.switchAtBar = true;
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

RigEngine::~RigEngine()
{
    mpc.setPlayHead (nullptr);
    sampler.setPlayHead (nullptr);
    vocoder.setPlayHead (nullptr);
    bass.setPlayHead (nullptr);
    synthTool.setPlayHead (nullptr);
    for (auto& f : fx)
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

    for (auto* p : std::initializer_list<juce::AudioProcessor*> { &mpc, &sampler, &vocoder, &bass, &synthTool })
    {
        p->setRateAndBufferSizeDetails (sampleRate, maxBlock);
        p->prepareToPlay (sampleRate, maxBlock);
    }
    for (auto& f : fx)
    {
        f.setRateAndBufferSizeDetails (sampleRate, maxBlock);
        f.prepareToPlay (sampleRate, maxBlock);
    }
    transport.prepare (sampleRate);
    incoming.reset (sampleRate);

    mpcBuffer.setSize (2, maxBlock);
    samplerBuffer.setSize (2, maxBlock);
    vocoderBuffer.setSize (2, maxBlock);
    bassBuffer.setSize (2, maxBlock);
    synthBuffer.setSize (2, maxBlock);
    inputBuffer.setSize (2, maxBlock);
    rawInputBuffer.setSize (2, maxBlock);
    loopsBuffer.setSize (2, maxBlock);
    looper.prepare (sampleRate);
    mixBuffer.setSize (2, maxBlock);
    for (auto* m : { &midiIn, &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi })
        m->ensureSize (4096);

    for (int s = 0; s < numStrips; ++s)
    {
        gains[(size_t) s].reset (sampleRate, 0.02);
        gains[(size_t) s].setCurrentAndTargetValue (strips[(size_t) s].mute.load() ? 0.0f : faderGain (strips[(size_t) s].level.load()));
    }
    prepared = true;
}

void RigEngine::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
    if (! prepared)
        return;

    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (maxBlock, numSamples - offset);
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
                                           { RigRouting::loopsLevel, loopsStrip } };
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
    for (auto [control, strip] : { std::pair<int, int> { RigRouting::bassMute, bassStrip }, { RigRouting::synthMute, synthStrip }, { RigRouting::inputMute, inputStrip }, { RigRouting::loopsMute, loopsStrip } })
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
    if (! midiIn.isEmpty())
        lastMidiTime = juce::Time::getMillisecondCounter();

    mpcMidi.clear();
    samplerMidi.clear();
    vocoderMidi.clear();
    bassMidi.clear();
    synthMidi.clear();
    RigRouting::Result controls;
    std::array<juce::MidiBuffer*, RigRouting::kNumDests> destinations { nullptr, &mpcMidi, &samplerMidi, &vocoderMidi, &bassMidi, &synthMidi };
    routing.route (midiIn, destinations, controls);
    applyControls (controls);

    // Play and stop land here, at the start of the block, for every tool at once.
    transport.beginBlock();

    // A scene whose bar has come.
    {
        RigScene::State scene;
        double firedBeat = 0;
        if (scenes.takeDue (transport.getBlockPosition(), transport.isPlayingThisBlock(), scene, firedBeat))
        {
            applyScene (scene);
            controlsMoved = true;
        }
    }

    juce::AudioBuffer<float> mpcBlock (mpcBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> samplerBlock (samplerBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> vocoderBlock (vocoderBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> bassBlock (bassBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> synthBlock (synthBuffer.getArrayOfWritePointers(), 2, n);
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
    bassBlock.clear();
    bass.processBlock (bassBlock, bassMidi);
    synthBlock.clear();
    synthTool.processBlock (synthBlock, synthMidi);

    // The vocoder shapes its synth with your voice from the input.
    if (vocoderOn.load())
    {
        copyInput (vocoderBlock);
        vocoder.processBlock (vocoderBlock, vocoderMidi);
    }
    else
    {
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

    // The Input strip: a guitar or another mic, through its effects.
    juce::AudioBuffer<float> inputBlock (inputBuffer.getArrayOfWritePointers(), 2, n);
    if (inputOn.load() && numInputs > 0)
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
    }
    else
    {
        inputBlock.clear();
    }
    applyStrip (inputStrip, inputBlock);

    if (source == SampleSource::mix)
    {
        samplerBlock.clear();
        for (auto* b : { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock })
            for (int ch = 0; ch < 2; ++ch)
                samplerBlock.addFrom (ch, 0, *b, ch, 0, n);
    }

    sampler.processBlock (samplerBlock, samplerMidi);
    applyStrip (samplerStrip, samplerBlock);

    mix.clear();
    for (auto* buffer : { &mpcBlock, &vocoderBlock, &bassBlock, &synthBlock, &inputBlock, &samplerBlock })
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, *buffer, ch, 0, n);

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
    if (looper.process (clock, sources, loopsBlock))
    {
        transport.play();
        controlsMoved = true;
    }
    applyStrip (loopsStrip, loopsBlock);
    for (int ch = 0; ch < 2; ++ch)
        mix.addFrom (ch, 0, loopsBlock, ch, 0, n);

    // The metronome goes straight to the mix, under the master fader.
    transport.endBlock (n, &mix);

    fx[masterStrip].processBlock (mix, noMidi);

    auto& master = strips[masterStrip];
    auto& masterGain = gains[masterStrip];
    masterGain.setTargetValue (master.mute.load() ? 0.0f : faderGain (master.level.load()));
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
            mix.setSample (ch, i, juce::jlimit (-1.0f, 1.0f, mix.getSample (ch, i) * g));
    }
    const float peak = juce::jmax (mix.getMagnitude (0, 0, n), mix.getMagnitude (1, 0, n));
    if (peak > master.peak.load())
        master.peak = peak;
}

void RigEngine::applyStrip (int id, juce::AudioBuffer<float>& buffer)
{
    auto& strip = strips[(size_t) id];
    auto& gain = gains[(size_t) id];
    const int n = buffer.getNumSamples();
    fx[(size_t) id].processBlock (buffer, noMidi);
    gain.setTargetValue (strip.mute.load() ? 0.0f : faderGain (strip.level.load()));
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
    const float peak = juce::jmax (buffer.getMagnitude (0, 0, n), buffer.getMagnitude (1, 0, n));
    if (peak > strip.peak.load())
        strip.peak = peak;
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

void RigEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
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
    show.setProperty ("keysTo", routing.keysTo.load(), nullptr);
    show.setProperty ("vocoderOn", vocoderOn.load(), nullptr);
    show.setProperty ("splitNote", routing.splitNote.load(), nullptr);
    show.setProperty ("lowerKeysTo", routing.lowerKeysTo.load(), nullptr);
    show.setProperty ("sampleSource", sampleSource.load(), nullptr);
    show.setProperty ("inputOn", inputOn.load(), nullptr);
    show.setProperty ("inputJack", inputJack.load(), nullptr);

    for (int s = 0; s < numStrips; ++s)
    {
        juce::ValueTree strip ("Strip");
        strip.setProperty ("name", stripName (s), nullptr);
        strip.setProperty ("level", strips[(size_t) s].level.load(), nullptr);
        strip.setProperty ("mute", strips[(size_t) s].mute.load(), nullptr);
        show.appendChild (strip, nullptr);
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
        show.appendChild (loop, nullptr);
    }

    show.setProperty ("songMode", scenes.songMode, nullptr);
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
                                                                    { "Synth", &self.synthTool } };
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
    routing.keysTo = juce::jlimit (0, RigRouting::kNumDests - 1, (int) show.getProperty ("keysTo", (int) RigRouting::Dest::vocoder));
    vocoderOn = (bool) show.getProperty ("vocoderOn", true);
    routing.splitNote = juce::jlimit (0, 127, (int) show.getProperty ("splitNote", 0));
    routing.lowerKeysTo = juce::jlimit (0, RigRouting::kNumDests - 1, (int) show.getProperty ("lowerKeysTo", (int) RigRouting::Dest::bass));
    routing.clearParameterKnobs();
    sampleSource = juce::jlimit (0, 2, (int) show.getProperty ("sampleSource", 0));
    inputOn = (bool) show.getProperty ("inputOn", false);
    inputJack = juce::jlimit (0, 2, (int) show.getProperty ("inputJack", 0));

    for (int c = 0; c < RigRouting::numControls; ++c)
        routing.forget (c);
    scenes.clear();
    scenes.songMode = (bool) show.getProperty ("songMode", false);
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        looper.reset (i);
        loadedLoopBeats[(size_t) i] = 0;
    }
    // Shows from before the effects have none.
    for (auto& f : fx)
        f.reset();

    for (auto& s : strips)
    {
        s.level = 0.8f;
        s.mute = false;
    }
    int strip = 0;
    for (const auto child : show)
    {
        if (child.hasType ("Strip") && strip < numStrips)
        {
            strips[(size_t) strip].level = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("level", 0.8f));
            strips[(size_t) strip].mute = (bool) child.getProperty ("mute", false);
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
            else
                for (int st = 0; st < numStrips; ++st)
                    if (name == toolName (fxTool + st))
                        fx[(size_t) st].setStateInformation (state.getData(), (int) state.getSize());
        }
    }
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
    s.vocoderOn = vocoderOn.load() ? 1 : 0;
    s.inputOn = inputOn.load() ? 1 : 0;
    for (int st = 0; st < numStrips && st < RigScene::kMaxStrips; ++st)
        s.stripMutes[(size_t) st] = st == masterStrip ? -1 : (strips[(size_t) st].mute.load() ? 1 : 0);
    for (int l = 0; l < RigLooper::kLayers; ++l)
        s.loopMutes[(size_t) l] = looper.getLayer (l).mute.load() ? 1 : 0;
    return s;
}

void RigEngine::applyScene (const RigScene::State& s)
{
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
}

void RigEngine::tickScenes()
{
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
}
