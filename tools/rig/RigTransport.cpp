#include "RigTransport.h"

void RigTransport::play()                { request = 1; }
void RigTransport::stop()                { request = 2; }
void RigTransport::setBpm (double b)     { bpm = juce::jlimit (kMinBpm, kMaxBpm, b); }
void RigTransport::setBeatsPerBar (int b) { beatsPerBar = juce::jlimit (1, 16, b); }

void RigTransport::prepare (double rate)
{
    sampleRate = rate > 0 ? rate : 44100.0;
}

void RigTransport::beginBlock()
{
    switch (request.exchange (0))
    {
        case 1:
            playing = true;
            break;
        case 2:
            playing = false;
            ppq = 0;
            samplePosition = 0;
            break;
        default:
            break;
    }

    // (Only the first block after the host's position: a long host block played in pieces
    // moves on by itself.)
    if (hostValid && hostFresh)
    {
        hostFresh = false;
        // Logic's count-in (before bar 1) stays silent.
        if (hostPlaying && hostPpq >= 0.0)
        {
            playing = true;
            ppq = hostPpq;
            samplePosition = juce::jmax (0LL, hostSamples);
        }
        else if (hostWasPlaying && ! hostPlaying)
        {
            playing = false;
            ppq = 0;
            samplePosition = 0;
        }
        hostWasPlaying = hostPlaying;
    }
    playingNow = playing;
}

void RigTransport::setHostPosition (const juce::Optional<PositionInfo>& info)
{
    hostValid = info.hasValue();
    hosted = hostValid;
    hostFresh = hostValid;
    if (! hostValid)
        return;
    if (const auto b = info->getBpm(); b.hasValue() && *b > 0)
        bpm = juce::jlimit (kMinBpm, kMaxBpm, *b);
    if (const auto sig = info->getTimeSignature(); sig.hasValue() && sig->numerator > 0)
        beatsPerBar = juce::jlimit (1, 16, sig->numerator);
    hostPlaying = info->getIsPlaying();
    hostPpq = info->getPpqPosition().orFallback (0.0);
    hostSamples = info->getTimeInSamples().orFallback (0);
}

void RigTransport::endBlock (int numSamples, juce::AudioBuffer<float>* out)
{
    const double beatsPerSample = bpm.load() / (60.0 * sampleRate);
    const double from = ppq;

    if (playing)
    {
        // A click at each beat that starts inside this block.
        if (metronome.load())
        {
            const double firstBeat = std::ceil (from - 1.0e-9);
            const double offset = (firstBeat - from) / beatsPerSample;
            if (offset < numSamples)
            {
                clickAge = -(int) offset;
                clickActive = true;
                clickAccent = ((long long) firstBeat % juce::jmax (1, beatsPerBar.load())) == 0;
            }
        }
        ppq += numSamples * beatsPerSample;
        samplePosition += numSamples;
    }

    // The click itself: a short sine blip.
    if (clickActive)
    {
        const int length = (int) (0.03 * sampleRate);
        const double freq = clickAccent ? 1600.0 : 1100.0;
        const float gain = clickAccent ? 0.4f : 0.25f;
        for (int i = 0; out != nullptr && i < numSamples; ++i)
        {
            const int a = clickAge + i;
            if (a < 0)
                continue;
            if (a >= length)
                break;
            const auto v = gain * (float) (std::sin (juce::MathConstants<double>::twoPi * freq * a / sampleRate)
                                           * std::exp (-a / (0.006 * sampleRate)));
            for (int ch = 0; ch < out->getNumChannels(); ++ch)
                out->addSample (ch, i, v);
        }
        clickAge += numSamples;
        if (clickAge >= length)
            clickActive = false;
    }

    ppqNow = ppq;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> RigTransport::getPosition() const
{
    PositionInfo info;
    const int bar = juce::jmax (1, beatsPerBar.load());
    info.setBpm (bpm.load());
    info.setTimeSignature (TimeSignature { bar, 4 });
    info.setIsPlaying (playing);
    info.setPpqPosition (ppq);
    info.setPpqPositionOfLastBarStart (std::floor (ppq / bar) * bar);
    info.setTimeInSamples (samplePosition);
    info.setTimeInSeconds ((double) samplePosition / sampleRate);
    info.setBarCount ((long long) std::floor (ppq / bar));
    return info;
}
