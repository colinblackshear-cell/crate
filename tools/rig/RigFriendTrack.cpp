#include "RigFriendTrack.h"

#include <cmath>

namespace
{
float faderToGain (float level)
{
    // As RigEngine::faderGain: 0.8 is unity, the top +6 dB, fading into silence at the bottom.
    level = juce::jlimit (0.0f, 1.0f, level);
    if (level <= 0.0f)
        return 0.0f;
    const float gain = juce::Decibels::decibelsToGain ((level - 0.8f) * 30.0f);
    return level < 0.1f ? gain * (level / 0.1f) : gain;
}
}

void RigFriendTrack::set (std::shared_ptr<const juce::AudioBuffer<float>> newAudio, const Info& newInfo)
{
    if (audio != nullptr)
        retired.push_back (audio);
    audio = newAudio != nullptr && newAudio->getNumSamples() > 0 && newAudio->getNumChannels() > 0 ? std::move (newAudio) : nullptr;
    info = newInfo;
    info.bpm = juce::jlimit (20.0, 400.0, info.bpm);
    info.firstBeat = juce::jmax (0.0, info.firstBeat);
    info.beatsPerBar = juce::jlimit (1, 16, info.beatsPerBar);
    id = audio != nullptr ? juce::Uuid().toDashedString() : juce::String();
    pendingFromShow = false;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        next = audio.get();
        nextInfo = info;
        ++version;
    }
    service();
}

void RigFriendTrack::setInfo (const Info& newInfo)
{
    info = newInfo;
    info.bpm = juce::jlimit (20.0, 400.0, info.bpm);
    info.firstBeat = juce::jmax (0.0, info.firstBeat);
    info.beatsPerBar = juce::jlimit (1, 16, info.beatsPerBar);
    const juce::SpinLock::ScopedLockType sl (lock);
    nextInfo = info;
    ++version;
}

double RigFriendTrack::lengthSeconds() const
{
    return audio != nullptr ? juce::jmax (0.0, audio->getNumSamples() / kRate - info.firstBeat) : 0.0;
}

double RigFriendTrack::lengthBars() const
{
    return lengthSeconds() * info.bpm / 60.0 / juce::jmax (1, info.beatsPerBar);
}

void RigFriendTrack::service()
{
    // The audio thread has the newest version: nothing older can be playing.
    int current;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        current = version;
    }
    if (seen.load() == current)
        retired.clear();
}

juce::ValueTree RigFriendTrack::toTree() const
{
    juce::ValueTree t (kTreeType);
    if (audio == nullptr)
        return t;
    t.setProperty ("name", info.name, nullptr);
    t.setProperty ("bpm", info.bpm, nullptr);
    t.setProperty ("firstBeat", info.firstBeat, nullptr);
    t.setProperty ("beatsPerBar", info.beatsPerBar, nullptr);
    t.setProperty ("key", info.key, nullptr);
    t.setProperty ("minor", info.minor, nullptr);
    t.setProperty ("level", level.load(), nullptr);
    t.setProperty ("mute", mute.load(), nullptr);
    t.setProperty ("id", id, nullptr);
    t.setProperty ("file", kAssetPath, nullptr);
    return t;
}

void RigFriendTrack::fromTree (const juce::ValueTree& t)
{
    clear();
    level = 0.8f;
    mute = false;
    if (! t.isValid() || ! t.hasProperty ("file"))
        return;
    info.name = t.getProperty ("name").toString();
    info.bpm = juce::jlimit (20.0, 400.0, (double) t.getProperty ("bpm", 120.0));
    info.firstBeat = juce::jmax (0.0, (double) t.getProperty ("firstBeat", 0.0));
    info.beatsPerBar = juce::jlimit (1, 16, (int) t.getProperty ("beatsPerBar", 4));
    info.key = (int) t.getProperty ("key", -1);
    info.minor = (bool) t.getProperty ("minor", false);
    level = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("level", 0.8f));
    mute = (bool) t.getProperty ("mute", false);
    id = t.getProperty ("id").toString();
    pendingFromShow = true;
}

void RigFriendTrack::setAudioFromShow (std::shared_ptr<const juce::AudioBuffer<float>> a)
{
    const auto keepId = id;
    const auto keepInfo = info;
    pendingFromShow = false;
    set (std::move (a), keepInfo);
    if (audio != nullptr)
        id = keepId.isNotEmpty() ? keepId : id;   // the same sound as was saved
}

float RigFriendTrack::gain() const
{
    return mute.load() ? 0.0f : faderToGain (level.load());
}

//==============================================================================
void RigFriendTrack::prepare (double rate)
{
    sampleRate = rate > 0 ? rate : 44100.0;
}

void RigFriendTrack::play (const juce::AudioBuffer<float>& a, const Info& in, double bpm, double rate, double beat,
                           float* const* out, int numSamples, float gainFrom, float gainTo)
{
    const int length = a.getNumSamples(), chans = a.getNumChannels();
    if (length < 2 || in.bpm <= 0 || bpm <= 0 || rate <= 0)
        return;
    double pos = (in.firstBeat + juce::jmax (0.0, beat) * 60.0 / in.bpm) * kRate;
    const double step = (bpm / in.bpm) * kRate / rate;
    if (pos >= length - 1)
        return;
    const float* left = a.getReadPointer (0);
    const float* right = a.getReadPointer (juce::jmin (1, chans - 1));
    const float dg = numSamples > 0 ? (gainTo - gainFrom) / (float) numSamples : 0.0f;
    float g = gainFrom;
    for (int i = 0; i < numSamples; ++i, pos += step, g += dg)
    {
        const int at = (int) pos;
        if (at + 1 >= length)
            break;
        const float frac = (float) (pos - at);
        out[0][i] += (left[at] + (left[at + 1] - left[at]) * frac) * g;
        out[1][i] += (right[at] + (right[at + 1] - right[at]) * frac) * g;
    }
}

void RigFriendTrack::render (juce::AudioBuffer<float>& out, int numSamples, double beat, double bpm, bool playing)
{
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (sl.isLocked() && liveVersion != version)
        {
            live = next;
            liveInfo = nextInfo;
            liveVersion = version;
            seen = version;
        }
    }
    const float target = gain();
    if (live == nullptr || ! playing || out.getNumChannels() < 2)
    {
        lastGain = playing ? target : 0.0f;   // (starts from silence when the clock starts)
        return;
    }
    float* const* ptrs = out.getArrayOfWritePointers();
    play (*live, liveInfo, bpm, sampleRate, beat, ptrs, numSamples, lastGain, target);
    lastGain = target;
    const float p = juce::jmax (out.getMagnitude (0, 0, numSamples), out.getMagnitude (1, 0, numSamples));
    if (p > peak.load (std::memory_order_relaxed))
        peak.store (p, std::memory_order_relaxed);
}

juce::AudioBuffer<float> RigFriendTrack::renderRange (const juce::AudioBuffer<float>& a, const Info& in, double bpm, double rate, int beatsPerBar,
                                                      double fromBar, double toBar, double tailSeconds, float g)
{
    const int bpb = juce::jmax (1, beatsPerBar);
    const double seconds = juce::jmax (0.0, (toBar - fromBar) * bpb * 60.0 / juce::jmax (1.0, bpm)) + juce::jmax (0.0, tailSeconds);
    juce::AudioBuffer<float> out (2, juce::jmax (0, (int) std::ceil (seconds * rate)));
    out.clear();
    if (out.getNumSamples() == 0)
        return out;
    play (a, in, bpm, rate, fromBar * bpb, out.getArrayOfWritePointers(), out.getNumSamples(), g, g);
    return out;
}
