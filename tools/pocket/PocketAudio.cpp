#include "PocketAudio.h"

PocketAudio::PocketAudio() { writerThread.startThread(); }

PocketAudio::~PocketAudio()
{
    stopRecording();
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    writerThread.stopThread (1000);
}

void PocketAudio::start (std::function<void (juce::String)> done)
{
    juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, [this, done] (bool granted)
    {
        auto error = devices.initialise (granted ? 1 : 0, 2, nullptr, true);
        if (error.isEmpty())
        {
            devices.addAudioCallback (this);
            running = true;
        }
        if (error.isEmpty() && ! granted)
            error = "Microphone access is off. Turn it on in Settings to record.";
        if (done)
            done (error);
    });
}

void PocketAudio::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    sampleRate = d->getCurrentSampleRate();
    latency = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
    if (renderer != nullptr)
    {
        renderer->prepare (sampleRate);
        renderer->setLatency (latency.load());
    }
}

void PocketAudio::updateLatency (int samples)
{
    if (samples <= 0 || ! running)
        return;
    latency = samples;
    if (renderer != nullptr)
        renderer->setLatency (samples);   // the looper keeps it in an atomic
}

bool PocketAudio::startRecording (const juce::File& file)
{
    if (! running || recording.load())
        return false;
    auto* dev = devices.getCurrentAudioDevice();
    if (dev == nullptr || dev->getActiveInputChannels().isZero())
        return false;

    file.deleteFile();
    file.getParentDirectory().createDirectory();
    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sampleRate, 1, 24, {}, 0));
    if (w == nullptr)
        return false;
    stream.release();   // the writer owns it now

    recordedSamples = 0;
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 32768);
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        writer = std::move (threaded);
    }
    recording = true;
    return true;
}

void PocketAudio::stopRecording()
{
    recording = false;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        old = std::move (writer);
    }
    old.reset();   // flushes and closes the file
}

double PocketAudio::recordedSeconds() const
{
    return (double) recordedSamples.load() / sampleRate;
}

void PocketAudio::play (crate::SharedAudio audio, int start, int end)
{
    if (audio == nullptr || end <= start)
        return;
    const juce::SpinLock::ScopedLockType lock (playLock);
    playAudio = std::move (audio);
    playEnd = (double) juce::jmin (end, playAudio->getNumSamples());
    playPos = (double) start;
    playing = true;
}

void PocketAudio::stopPlaying()
{
    playing = false;
}

void PocketAudio::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int n, const juce::AudioIODeviceCallbackContext&)
{
    if (numIn > 0 && in[0] != nullptr)
    {
        float p = 0;
        for (int i = 0; i < n; ++i)
            p = juce::jmax (p, std::abs (in[0][i]));
        if (p > peak.load())
            peak = p;

        if (recording.load())
        {
            const juce::SpinLock::ScopedTryLockType lock (writerLock);
            if (lock.isLocked() && writer != nullptr)
            {
                writer->write (in, n);
                recordedSamples += n;
            }
        }
    }

    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr)
            juce::FloatVectorOperations::clear (out[c], n);

    if (renderer != nullptr && numOut > 0 && out[0] != nullptr)
    {
        renderer->setInput (in, numIn);
        renderer->render (out[0], numOut > 1 ? out[1] : nullptr, n);
    }

    if (! playing.load() || numOut < 1)
        return;

    const juce::SpinLock::ScopedTryLockType lock (playLock);
    if (! lock.isLocked() || playAudio == nullptr)
        return;

    const auto& a = *playAudio;
    const double step = crate::kSampleRate / sampleRate;
    double pos = playPos.load();
    const int last = a.getNumSamples() - 1;

    for (int i = 0; i < n; ++i)
    {
        if (pos >= playEnd)
        {
            playing = false;
            break;
        }
        const int i0 = juce::jlimit (0, last, (int) pos);
        const int i1 = juce::jmin (last, i0 + 1);
        const float frac = (float) (pos - (double) i0);
        for (int c = 0; c < numOut; ++c)
        {
            if (out[c] == nullptr)
                continue;
            const int src = juce::jmin (c, a.getNumChannels() - 1);
            const float* d = a.getReadPointer (src);
            out[c][i] += d[i0] + (d[i1] - d[i0]) * frac;
        }
        pos += step;
    }
    playPos = pos;
}
