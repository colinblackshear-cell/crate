#include "SoundLibrary.h"

#include "AudioFiles.h"

namespace
{
    const juce::String tagsPrefix = "tags: ";
}

SoundLibrary::SoundLibrary (juce::AudioFormatManager& f, juce::File dir) : formats (f), folder (std::move (dir))
{
    folder.createDirectory();
    rescan();
}

juce::File SoundLibrary::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("Crate")
        .getChildFile ("Sounds");
}

std::unordered_map<juce::String, juce::String> SoundLibrary::toMetadata (const Info& info)
{
    std::unordered_map<juce::String, juce::String> m;
    using W = juce::WavAudioFormat;

    if (info.name.isNotEmpty())
        m[W::riffInfoTitle] = info.name;
    m[W::riffInfoSoftware] = "Crate" + (info.tool.isNotEmpty() ? " " + info.tool : juce::String());
    if (! info.tags.isEmpty())
        m[W::riffInfoKeywords] = info.tags.joinIntoString ("; ");

    if (info.rootNote >= 0)
    {
        m[W::acidRootSet] = "1";
        m[W::acidRootNote] = juce::String (info.rootNote);
    }
    if (info.bpm > 0)
    {
        m[W::acidTempo] = juce::String (info.bpm, 3);
        m[W::acidNumerator] = "4";
        m[W::acidDenominator] = "4";
        m[W::acidStretch] = "1";
    }
    if (info.beats > 0)
        m[W::acidBeats] = juce::String ((int) std::round (info.beats));
    if (info.bpm > 0 || info.rootNote >= 0 || info.beats > 0)
        m[W::acidOneShot] = info.bpm > 0 ? "0" : "1";   // no tempo: a hit, not a loop

    return m;
}

SoundLibrary::Info SoundLibrary::fromMetadata (const juce::StringPairArray& values, const juce::String& name)
{
    using W = juce::WavAudioFormat;
    Info info;
    info.name = name;

    const auto software = values[W::riffInfoSoftware];
    if (software.startsWith ("Crate"))
        info.tool = software.fromFirstOccurrenceOf ("Crate", false, false).trim();

    info.tags.addTokens (values[W::riffInfoKeywords], ";", {});
    info.tags.trim();
    info.tags.removeEmptyStrings();

    if (values[W::acidRootSet].getIntValue() != 0)
        info.rootNote = values[W::acidRootNote].getIntValue();
    info.bpm = values[W::acidTempo].getDoubleValue();
    info.beats = values[W::acidBeats].getDoubleValue();
    return info;
}

void SoundLibrary::rescan()
{
    entries.clear();

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false))
    {
        if (file.getFileName().startsWith ("."))
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0)
            continue;

        Entry e;
        e.file = file;
        e.info = fromMetadata (reader->metadataValues, file.getFileNameWithoutExtension());
        e.seconds = (double) reader->lengthInSamples / reader->sampleRate;
        e.channels = (int) reader->numChannels;
        e.modified = file.getLastModificationTime();
        entries.push_back (e);
    }

    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.modified > b.modified; });
    sendChangeMessage();
}

juce::File SoundLibrary::save (const juce::AudioBuffer<float>& audio, const Info& info)
{
    auto safe = juce::File::createLegalFileName (info.name.trim()).substring (0, 60).trim();
    if (safe.isEmpty())
        safe = "Sound";

    folder.createDirectory();
    auto file = folder.getNonexistentChildFile (safe, ".wav", false);

    auto named = info;
    named.name = file.getFileNameWithoutExtension();
    if (! crate::AudioFiles::write (file, audio, toMetadata (named)))
        return {};

    rescan();
    return file;
}

bool SoundLibrary::rename (const Entry& e, const juce::String& newName)
{
    auto safe = juce::File::createLegalFileName (newName.trim());
    if (safe.isEmpty())
        return false;

    auto target = folder.getNonexistentChildFile (safe, e.file.getFileExtension(), false);
    const bool ok = e.file.moveFileTo (target);
    rescan();
    return ok;
}

bool SoundLibrary::remove (const Entry& e)
{
    const bool ok = e.file.moveToTrash();
    rescan();
    return ok;
}
