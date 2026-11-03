#include "RigEngine.h"

juce::String RigEngine::stripName (int id)
{
    switch (id)
    {
        case mpcStrip:     return "MPC";
        case samplerStrip: return "Sampler";
        case masterStrip:  return "Master";
        default:           return {};
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
    transport.setBpm (mpc.getProject().currentSequence().bpm);
}

RigEngine::~RigEngine()
{
    mpc.setPlayHead (nullptr);
    sampler.setPlayHead (nullptr);
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

    for (auto* p : std::initializer_list<juce::AudioProcessor*> { &mpc, &sampler })
    {
        p->setRateAndBufferSizeDetails (sampleRate, maxBlock);
        p->prepareToPlay (sampleRate, maxBlock);
    }
    transport.prepare (sampleRate);
    incoming.reset (sampleRate);

    mpcBuffer.setSize (2, maxBlock);
    samplerBuffer.setSize (2, maxBlock);
    mixBuffer.setSize (2, maxBlock);
    for (auto* m : { &midiIn, &mpcMidi, &samplerMidi })
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
                                           { RigRouting::masterLevel, masterStrip } };
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
    RigRouting::Result controls;
    routing.route (midiIn, mpcMidi, samplerMidi, controls);
    applyControls (controls);

    // Play and stop land here, at the start of the block, for every tool at once.
    transport.beginBlock();

    juce::AudioBuffer<float> mpcBlock (mpcBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> samplerBlock (samplerBuffer.getArrayOfWritePointers(), 2, n);
    juce::AudioBuffer<float> mix (mixBuffer.getArrayOfWritePointers(), 2, n);

    mpcBlock.clear();
    mpc.processBlock (mpcBlock, mpcMidi);

    // The sampler hears the audio input, so it can record from the mic.
    samplerBlock.clear();
    for (int ch = 0; ch < 2 && numInputs > 0; ++ch)
    {
        const auto* in = inputs[juce::jmin (ch, numInputs - 1)];
        if (in != nullptr)
            samplerBlock.copyFrom (ch, 0, in + offset, n);
    }
    sampler.processBlock (samplerBlock, samplerMidi);

    // The mixer.
    mix.clear();
    const std::pair<int, juce::AudioBuffer<float>*> tools[] = { { mpcStrip, &mpcBlock }, { samplerStrip, &samplerBlock } };
    for (auto [id, buffer] : tools)
    {
        auto& strip = strips[(size_t) id];
        auto& gain = gains[(size_t) id];
        gain.setTargetValue (strip.mute.load() ? 0.0f : faderGain (strip.level.load()));
        if (gain.isSmoothing())
        {
            for (int i = 0; i < n; ++i)
            {
                const float g = gain.getNextValue();
                for (int ch = 0; ch < 2; ++ch)
                    buffer->setSample (ch, i, buffer->getSample (ch, i) * g);
            }
        }
        else
        {
            buffer->applyGain (gain.getTargetValue());
        }
        const float peak = juce::jmax (buffer->getMagnitude (0, 0, n), buffer->getMagnitude (1, 0, n));
        if (peak > strip.peak.load())
            strip.peak = peak;
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, *buffer, ch, 0, n);
    }

    // The metronome goes straight to the mix, under the master fader.
    transport.endBlock (n, &mix);

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

    // The tools save themselves the way they do inside a Logic project.
    auto& self = const_cast<RigEngine&> (*this);
    const std::pair<const char*, juce::AudioProcessor*> tools[] = { { "MPC", &self.mpc }, { "Sampler", &self.sampler } };
    for (auto [name, tool] : tools)
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
    routing.keysTo = juce::jlimit (0, 2, (int) show.getProperty ("keysTo", (int) RigRouting::Dest::sampler));

    for (int c = 0; c < RigRouting::numControls; ++c)
        routing.forget (c);

    int strip = 0;
    for (const auto child : show)
    {
        if (child.hasType ("Strip") && strip < numStrips)
        {
            strips[(size_t) strip].level = juce::jlimit (0.0f, 1.0f, (float) child.getProperty ("level", 0.8f));
            strips[(size_t) strip].mute = (bool) child.getProperty ("mute", false);
            ++strip;
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
        }
    }
    controlsMoved = true;
}
