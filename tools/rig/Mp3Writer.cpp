#include "Mp3Writer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#if CRATE_HAS_SHINE
extern "C"
{
 #include <layer3.h>
}
#endif

namespace
{
// The sound at a rate the encoder takes (44.1 or 48 kHz), stereo.
juce::AudioBuffer<float> prepared (const juce::AudioBuffer<float>& audio, double rate, double& outRate)
{
    outRate = std::abs (rate - 44100.0) < 1.0 || std::abs (rate - 48000.0) < 1.0 ? rate : (rate < 46000.0 ? 44100.0 : 48000.0);
    const int inN = audio.getNumSamples();
    const double ratio = rate / outRate;
    const int outN = (int) std::ceil (inN / ratio);
    juce::AudioBuffer<float> out (2, juce::jmax (1, outN));
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1));
        if (std::abs (ratio - 1.0) < 1.0e-9)
            out.copyFrom (ch, 0, src, juce::jmin (inN, outN));
        else
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, src, out.getWritePointer (ch), outN, inN, 0);
        }
    }
    return out;
}
}

bool mp3::available()
{
#if CRATE_HAS_SHINE
    return true;
#else
    return false;
#endif
}

bool mp3::write (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, int kbps)
{
#if CRATE_HAS_SHINE
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0)
        return false;
    double rate = sampleRate;
    const auto pcm = prepared (audio, sampleRate, rate);
    shine_config_t config;
    shine_set_config_mpeg_defaults (&config.mpeg);
    config.wave.channels = 2;
    config.wave.samplerate = (int) rate;
    config.mpeg.mode = SHINE_JOINT_STEREO;
    config.mpeg.bitrate = kbps;
    if (shine_check_config (config.wave.samplerate, config.mpeg.bitrate) < 0)
        return false;
    shine_t enc = shine_initialise (&config);
    if (enc == nullptr)
        return false;
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
    {
        shine_close (enc);
        return false;
    }
    const int perPass = shine_samples_per_pass (enc);
    std::vector<int16_t> left ((size_t) perPass), right ((size_t) perPass);
    int16_t* channels[2] = { left.data(), right.data() };
    const int n = pcm.getNumSamples();
    const auto toInt = [] (float v) { return (int16_t) juce::jlimit (-32767, 32767, (int) std::lrint (v * 32767.0f)); };
    bool ok = true;
    for (int pos = 0; pos < n && ok; pos += perPass)
    {
        const int count = juce::jmin (perPass, n - pos);
        const float* l = pcm.getReadPointer (0, pos);
        const float* r = pcm.getReadPointer (1, pos);
        for (int i = 0; i < perPass; ++i)
        {
            left[(size_t) i] = i < count ? toInt (l[i]) : 0;
            right[(size_t) i] = i < count ? toInt (r[i]) : 0;
        }
        int written = 0;
        if (const unsigned char* data = shine_encode_buffer (enc, channels, &written); data != nullptr && written > 0)
            ok = out.write (data, (size_t) written);
    }
    int written = 0;
    if (const unsigned char* data = shine_flush (enc, &written); data != nullptr && written > 0)
        ok = out.write (data, (size_t) written) && ok;
    shine_close (enc);
    out.flush();
    return ok;
#else
    juce::ignoreUnused (file, audio, sampleRate, kbps);
    return false;
#endif
}
