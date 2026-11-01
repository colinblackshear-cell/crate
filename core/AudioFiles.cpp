#include "AudioFiles.h"

namespace crate::AudioFiles
{

juce::String wildcard (juce::AudioFormatManager& formats)
{
    return formats.getWildcardForAllFormats();
}

bool isAudioFile (const juce::File& file, juce::AudioFormatManager& formats)
{
    return file.existsAsFile() && formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& source, double sourceRate)
{
    if (sourceRate <= 0 || std::abs (sourceRate - kSampleRate) < 0.5)
        return source;

    const double ratio = sourceRate / kSampleRate;
    const int outLength = juce::jmax (1, (int) std::ceil (source.getNumSamples() / ratio));
    // the interpolator looks a few samples ahead, so give it silence past the end
    constexpr int padding = 64;

    juce::AudioBuffer<float> out (source.getNumChannels(), outLength);
    std::vector<float> in ((size_t) (source.getNumSamples() + padding), 0.0f);

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        std::copy (source.getReadPointer (ch), source.getReadPointer (ch) + source.getNumSamples(), in.begin());
        juce::WindowedSincInterpolator interpolator;
        interpolator.process (ratio, in.data(), out.getWritePointer (ch), outLength);
    }

    return out;
}

namespace
{
    SharedAudio readAll (juce::AudioFormatReader& reader, juce::String& error, const juce::String& name)
    {
        if (reader.sampleRate <= 0 || reader.lengthInSamples <= 0)
        {
            error = name + " has no audio in it.";
            return nullptr;
        }

        if ((double) reader.lengthInSamples / reader.sampleRate > kMaxSeconds)
        {
            error = name + " is longer than " + juce::String ((int) (kMaxSeconds / 60)) + " minutes.";
            return nullptr;
        }

        const int fileChannels = (int) juce::jmax (1u, reader.numChannels);
        const int n = (int) reader.lengthInSamples;
        juce::AudioBuffer<float> raw (fileChannels, n);
        reader.read (&raw, 0, n, 0, true, fileChannels > 1);

        juce::AudioBuffer<float> audio (juce::jmin (2, fileChannels), n);
        if (fileChannels <= 2)
        {
            audio.makeCopyOf (raw);
        }
        else
        {
            // fold odd channels to the left and even ones to the right
            audio.clear();
            const float gain = 2.0f / (float) fileChannels;
            for (int ch = 0; ch < fileChannels; ++ch)
                audio.addFrom (ch % 2, 0, raw, ch, 0, n, gain);
        }

        return share (resample (audio, reader.sampleRate));
    }
}

SharedAudio load (const juce::File& file, juce::AudioFormatManager& formats, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Couldn't read " + file.getFileName() + ". Try WAV, AIFF, MP3 or FLAC.";
        return nullptr;
    }
    return readAll (*reader, error, file.getFileName());
}

juce::Array<juce::File> expandDropped (const juce::StringArray& paths, juce::AudioFormatManager& formats)
{
    juce::Array<juce::File> files;
    for (const auto& path : paths)
    {
        juce::File f (path);
        if (f.isDirectory())
        {
            auto inside = f.findChildFiles (juce::File::findFiles, true);
            inside.sort();
            for (const auto& child : inside)
                if (isAudioFile (child, formats))
                    files.add (child);
        }
        else if (isAudioFile (f, formats))
        {
            files.add (f);
        }
    }
    return files;
}

bool write (const juce::File& file, const juce::AudioBuffer<float>& audio,
            const std::unordered_map<juce::String, juce::String>& metadata)
{
    std::unique_ptr<juce::AudioFormat> format;
    if (file.hasFileExtension ("aif;aiff"))
        format = std::make_unique<juce::AiffAudioFormat>();
    else
        format = std::make_unique<juce::WavAudioFormat>();

    juce::TemporaryFile temp (file);
    {
        std::unique_ptr<juce::OutputStream> stream (temp.getFile().createOutputStream().release());
        if (stream == nullptr)
            return false;

        auto options = juce::AudioFormatWriterOptions{}
                           .withSampleRate (kSampleRate)
                           .withNumChannels (audio.getNumChannels())
                           .withBitsPerSample (24);
        if (dynamic_cast<juce::WavAudioFormat*> (format.get()) != nullptr)
            options = options.withMetadataValues (metadata);

        auto writer = format->createWriterFor (stream, options);
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return false;
    }
    return temp.overwriteTargetFileWithTemporary();
}

juce::MemoryBlock toWavData (const juce::AudioBuffer<float>& audio)
{
    juce::MemoryBlock block;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (kSampleRate)
                                                       .withNumChannels (audio.getNumChannels())
                                                       .withBitsPerSample (24));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return {};
    }
    return block;
}

SharedAudio fromWavData (const void* data, size_t size)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
    if (reader == nullptr)
        return nullptr;
    juce::String error;
    return readAll (*reader, error, "Saved sound");
}

} // namespace crate::AudioFiles
