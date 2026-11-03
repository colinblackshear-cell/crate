#include "EditorProcessor.h"

#include <algorithm>

#include "EditorView.h"
#include "core/AudioFiles.h"

juce::AudioProcessor::BusesProperties EditorProcessor::makeBuses()
{
    auto buses = BusesProperties();
    if (! isInstrument())
        buses = buses.withInput ("Input", juce::AudioChannelSet::stereo(), true);
    return buses.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

EditorProcessor::EditorProcessor()
    : AudioProcessor (makeBuses()),
      library ((formats.registerBasicFormats(), formats))
{
    document.addChangeListener (this);
}

EditorProcessor::~EditorProcessor()
{
    document.removeChangeListener (this);
}

void EditorProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncToDocument();
}

void EditorProcessor::syncToDocument()
{
    // the keys always play the latest version of the sound
    if (document.getAudio().get() == keysAudio)
        return;

    keysAudio = document.getAudio().get();
    const int length = document.getAudio() != nullptr ? document.getAudio()->getNumSamples() : 0;
    if (length != keysLength && keysLength > 0)
        setChops ({});   // the slices no longer line up with the sound
    keysLength = length;
    keys.setSound (document.getAudio());
}

void EditorProcessor::setChops (std::vector<int> markers)
{
    std::sort (markers.begin(), markers.end());
    markers.erase (std::unique (markers.begin(), markers.end()), markers.end());
    markers.erase (std::remove_if (markers.begin(), markers.end(), [] (int m) { return m <= 0; }), markers.end());
    if ((int) markers.size() > crate::KeyboardSampler::kMaxSlices - 1)
        markers.resize ((size_t) crate::KeyboardSampler::kMaxSlices - 1);
    chops = std::move (markers);
    keys.setSlices (chops);
}

bool EditorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (isInstrument())
        return in.isDisabled();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void EditorProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate > 0 ? sampleRate : 44100.0;
    keys.prepare (hostRate);
}

//==============================================================================
void EditorProcessor::play (crate::SharedAudio audio, juce::Range<int> range, bool shouldLoop)
{
    if (audio == nullptr || audio->getNumSamples() == 0)
        return stop();

    range = range.getIntersectionWith ({ 0, audio->getNumSamples() });
    if (range.getLength() < 2)
        range = { range.getStart(), audio->getNumSamples() };
    if (range.getLength() < 2)
        range = { 0, audio->getNumSamples() };

    crate::SharedAudio old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (playAudio);   // released here, not on the audio thread
        playAudio = std::move (audio);
        playRange = range;
        playPos = range.getStart();
        loop = shouldLoop;
        fadeGain = 1.0f;
        stopping = false;
    }
    playPosition = range.getStart();
    playing = true;
}

void EditorProcessor::stop()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (playing.load())
        stopping = true;   // the audio thread fades out quickly, then stops
}

void EditorProcessor::swapAudio (crate::SharedAudio audio)
{
    if (audio == nullptr || audio->getNumSamples() < 2)
        return;

    crate::SharedAudio old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (! playing.load() || stopping)
            return;
        const double fraction = playRange.getLength() > 0 ? (playPos - playRange.getStart()) / playRange.getLength() : 0.0;
        old = std::move (playAudio);
        playAudio = std::move (audio);
        playRange = { 0, playAudio->getNumSamples() };
        playPos = juce::jlimit (0.0, (double) playRange.getEnd() - 2, fraction * playRange.getLength());
    }
}

double EditorProcessor::getPlayFraction() const
{
    return playing.load() ? playFraction.load() : -1.0;
}

void EditorProcessor::setLooping (bool shouldLoop)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    loop = shouldLoop;
}

//==============================================================================
void EditorProcessor::startRecording()
{
    for (auto& r : recorded)
        r.clear();
    fifo.reset();
    recordRate = hostRate;
    recording = true;
}

void EditorProcessor::collectRecording()
{
    const int ready = fifo.getNumReady();
    if (ready == 0)
        return;

    const auto scope = fifo.read (ready);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& r = recorded[(size_t) ch];
        const auto* d = ring.getReadPointer (ch);
        r.insert (r.end(), d + scope.startIndex1, d + scope.startIndex1 + scope.blockSize1);
        r.insert (r.end(), d + scope.startIndex2, d + scope.startIndex2 + scope.blockSize2);
    }

    if (getRecordedSeconds() >= crate::AudioFiles::kMaxSeconds)
        recording = false;
}

double EditorProcessor::getRecordedSeconds() const
{
    return (double) recorded[0].size() / recordRate;
}

juce::AudioBuffer<float> EditorProcessor::stopRecording()
{
    recording = false;
    collectRecording();

    const int channels = juce::jlimit (1, 2, recordChannels.load());
    const int n = (int) recorded[0].size();
    juce::AudioBuffer<float> raw (channels, n);
    for (int ch = 0; ch < channels; ++ch)
        if (n > 0)
            raw.copyFrom (ch, 0, recorded[(size_t) ch].data(), n);

    for (auto& r : recorded)
    {
        r.clear();
        r.shrink_to_fit();
    }

    return crate::Edits::collapseFakeStereo (crate::AudioFiles::resample (raw, recordRate));
}

//==============================================================================
void EditorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    // Input: meter it, and record it if we're recording.
    if (numIn > 0)
    {
        float peak = 0;
        for (int ch = 0; ch < juce::jmin (2, numIn); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        if (peak > inputLevel.load())
            inputLevel = peak;

        if (recording.load())
        {
            const int channels = juce::jmin (2, numIn);
            recordChannels = channels;
            const auto scope = fifo.write (juce::jmin (numSamples, fifo.getFreeSpace()));
            for (int ch = 0; ch < 2; ++ch)
            {
                const int from = juce::jmin (ch, channels - 1);
                if (scope.blockSize1 > 0)
                    ring.copyFrom (ch, scope.startIndex1, buffer, from, 0, scope.blockSize1);
                if (scope.blockSize2 > 0)
                    ring.copyFrom (ch, scope.startIndex2, buffer, from, scope.blockSize1, scope.blockSize2);
            }
        }
    }

    // Output: the sound being played, nothing else (the input isn't passed through, so a mic
    // and speakers can't feed back).
    buffer.clear();
    renderPlayback (buffer);

    // Notes from the host or a keyboard, plus the on-screen keys, play the sound across the keys.
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    keys.render (buffer, midi);
}

void EditorProcessor::renderPlayback (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || ! playing.load() || playAudio == nullptr)
        return;

    const auto& src = *playAudio;
    const int outChannels = juce::jmin (2, buffer.getNumChannels());
    const double step = crate::kSampleRate / hostRate;
    const int last = playRange.getEnd();
    constexpr float fadeStep = 1.0f / 256.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        if (playPos >= last - 1)
        {
            if (loop && ! stopping)
                playPos = playRange.getStart() + (playPos - (last - 1));
            else
            {
                playing = false;
                break;
            }
        }

        if (stopping)
        {
            fadeGain -= fadeStep;
            if (fadeGain <= 0.0f)
            {
                playing = false;
                break;
            }
        }

        const int index = (int) playPos;
        const float frac = (float) (playPos - index);
        for (int ch = 0; ch < outChannels; ++ch)
        {
            const auto* d = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
            const float v = d[index] + (d[index + 1] - d[index]) * frac;
            buffer.setSample (ch, i, v * fadeGain);
        }
        playPos += step;
    }

    playPosition = playing.load() ? (int) playPos : -1;
    playFraction = playRange.getLength() > 0 ? (playPos - playRange.getStart()) / playRange.getLength() : 0.0;
}

//==============================================================================
void EditorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state ("SampleEditor");
    state.setProperty ("name", document.getName(), nullptr);
    state.setProperty ("source", document.getSourceFile().getFullPathName(), nullptr);
    state.setProperty ("rootNote", keys.getRootNote(), nullptr);
    state.setProperty ("keyMode", (int) keys.getMode(), nullptr);
    {
        juce::StringArray list;
        for (int m : chops)
            list.add (juce::String (m));
        state.setProperty ("chops", list.joinIntoString (" "), nullptr);
    }
    state.setProperty ("attack", keys.getAttack(), nullptr);
    state.setProperty ("release", keys.getRelease(), nullptr);

    if (const auto& audio = document.getAudio();
        audio != nullptr && audio->getNumSamples() > 0 && crate::seconds (audio->getNumSamples()) <= kMaxSavedSeconds)
        state.setProperty ("wav", juce::var (crate::AudioFiles::toWavData (*audio)), nullptr);

    juce::MemoryOutputStream out (dest, false);
    state.writeToStream (out);
}

void EditorProcessor::setStateInformation (const void* data, int size)
{
    const auto state = juce::ValueTree::readFromData (data, (size_t) size);
    if (! state.hasType ("SampleEditor"))
        return;

    keys.setRootNote (state.getProperty ("rootNote", 60));
    {
        std::vector<int> restored;
        for (const auto& token : juce::StringArray::fromTokens (state["chops"].toString(), " ", {}))
            if (token.isNotEmpty())
                restored.push_back (token.getIntValue());
        setChops (std::move (restored));
        keysLength = 0;   // the sound that arrives next is the one these chops belong to
    }
    keys.setMode ((crate::KeyboardSampler::Mode) juce::jlimit (0, 3, (int) state.getProperty ("keyMode", 0)));
    keys.setAttack ((float) state.getProperty ("attack", 0.002));
    keys.setRelease ((float) state.getProperty ("release", 0.15));

    const auto* wav = state["wav"].getBinaryData();
    if (wav == nullptr || wav->getSize() == 0)
        return;

    if (auto audio = crate::AudioFiles::fromWavData (wav->getData(), wav->getSize()))
    {
        // Hosts may call this off the message thread; the document belongs to the UI side.
        const auto name = state["name"].toString();
        const juce::File source (state["source"].toString());
        auto apply = [this, audio, name, source] { document.load (audio, name, "Open", source, true); };
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            apply();
        else
            juce::MessageManager::callAsync (apply);
    }
}

juce::AudioProcessorEditor* EditorProcessor::createEditor()
{
    return new EditorView (*this);
}

// Called by the plugin wrappers to make the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EditorProcessor();
}
