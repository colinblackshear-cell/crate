#include "PocketChordPlayer.h"

using namespace pocket;

ChordPlayer::ChordPlayer()
{
    for (auto& n : nextNotes)
        n = -1;
}

void ChordPlayer::setSound (crate::SharedAudio s, double tuneSemitones)
{
    if (sound != nullptr)
        keepAlive.push_back (sound);   // a voice may still be reading it
    if (keepAlive.size() > 8)
        keepAlive.erase (keepAlive.begin());
    sound = std::move (s);
    tune = tuneSemitones;
    live = sound.get();
}

void ChordPlayer::play (const juce::Array<int>& notes)
{
    for (int v = 0; v < kVoices; ++v)
        nextNotes[(size_t) v] = v < notes.size() ? notes[v] : -1;
    releasing = false;
    ++serial;
}

void ChordPlayer::release()
{
    releasing = true;
}

void ChordPlayer::render (float* left, float* right, int n)
{
    const auto* buf = live.load();
    if (buf == nullptr || buf->getNumSamples() < 2)
    {
        for (auto& v : voices)
            v.on = false;
        return;
    }

    const int s = serial.load();
    if (s != heard)
    {
        heard = s;
        const double rate = (double) crate::kSampleRate / sampleRate;
        int count = 0;
        for (auto& n_ : nextNotes)
            count += n_.load() >= 0 ? 1 : 0;
        for (int v = 0; v < kVoices; ++v)
        {
            const int note = nextNotes[(size_t) v].load();
            auto& voice = voices[(size_t) v];
            if (note < 0)
            {
                voice.target = 0.0f;
                continue;
            }
            voice.on = true;
            voice.pos = 0;
            voice.gain = 0.0f;
            voice.target = 0.8f / std::sqrt ((float) juce::jmax (1, count));
            voice.step = rate * std::pow (2.0, ((double) note - 60.0 + tune.load()) / 12.0);
        }
    }
    if (releasing.load())
        for (auto& v : voices)
            v.target = 0.0f;

    const float* d = buf->getReadPointer (0);
    const float* d2 = buf->getNumChannels() > 1 ? buf->getReadPointer (1) : d;
    const int len = buf->getNumSamples();
    const float attack = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));
    const float fall = 1.0f - std::exp (-1.0f / (0.12f * (float) sampleRate));
    for (auto& v : voices)
    {
        if (! v.on)
            continue;
        for (int i = 0; i < n; ++i)
        {
            const int i0 = (int) v.pos;
            if (i0 >= len - 1)
            {
                v.on = false;
                break;
            }
            const float f = (float) (v.pos - i0);
            v.gain += (v.target - v.gain) * (v.target > v.gain ? attack : fall);
            const float l = (d[i0] + (d[i0 + 1] - d[i0]) * f) * v.gain;
            const float r = (d2[i0] + (d2[i0 + 1] - d2[i0]) * f) * v.gain;
            left[i] += l;
            if (right != nullptr)
                right[i] += r;
            v.pos += v.step;
        }
        if (v.target == 0.0f && v.gain < 1.0e-4f)
            v.on = false;
    }
}
