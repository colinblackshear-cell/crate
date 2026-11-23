#include "RigShowFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

#if JUCE_MAC || JUCE_LINUX || JUCE_IOS || JUCE_ANDROID
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace rigshow
{
namespace
{
constexpr int kChunk = 1 << 16;                                // frames per write (and test hook step)

juce::String stagingPrefix (const juce::File& showFile) { return currentFolder (showFile).getFileName() + ".staging-"; }
juce::String revisionPrefix (const juce::File& showFile) { return currentFolder (showFile).getFileName() + ".r-"; }

juce::String hex (juce::uint64 v) { return juce::String::toHexString ((juce::int64) v).paddedLeft ('0', 16); }

// Flushes a file or folder to the disk. On macOS fsync() only reaches the drive's cache, so
// F_FULLFSYNC asks the drive to write it out (falls back to fsync where that isn't supported).
bool syncToDisk (const juce::File& f)
{
   #if JUCE_MAC || JUCE_LINUX || JUCE_IOS || JUCE_ANDROID
    const int fd = ::open (f.getFullPathName().toRawUTF8(), O_RDONLY);
    if (fd < 0)
        return false;
    bool ok = false;
   #if JUCE_MAC || JUCE_IOS
    ok = ::fcntl (fd, F_FULLFSYNC) != -1;
   #endif
    if (! ok)
        ok = ::fsync (fd) == 0;
    ::close (fd);
    return ok;
   #else
    juce::ignoreUnused (f);
    return true;
   #endif
}

std::filesystem::path fsPath (const juce::File& f)
{
    return std::filesystem::u8path (f.getFullPathName().toStdString());
}

bool renameFolder (const juce::File& from, const juce::File& to)
{
    if (to.exists())
        return false;   // never over anything
    std::error_code ec;
    std::filesystem::rename (fsPath (from), fsPath (to), ec);
    return ! ec;
}

struct Decoded
{
    juce::int64 frames = 0;
    int channels = 0;
    juce::uint64 key = 0;
    bool ok = false;
};

// Reads a WAV back the way loading does (two channels, as floats).
Decoded readBack (const juce::File& file)
{
    Decoded d;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
    if (reader == nullptr)
        return d;
    d.frames = (juce::int64) reader->lengthInSamples;
    d.channels = (int) reader->numChannels;
    juce::AudioBuffer<float> audio (2, (int) reader->lengthInSamples);
    d.ok = reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
    d.key = hashAudio (audio);
    return d;
}

// Writes a new WAV (24-bit stereo, as shows always have). Refuses a path that's already there,
// so no file that's saved is ever written again.
bool writeNewWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate, int n)
{
    if (file.exists() || ! file.getParentDirectory().createDirectory())
        return false;
    {
        auto stream = std::make_unique<juce::FileOutputStream> (file);
        if (! stream->openedOk())
            return false;
        std::unique_ptr<juce::OutputStream> out (stream.release());
        juce::WavAudioFormat wav;
        auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
        auto writer = wav.createWriterFor (out, options);
        if (writer == nullptr)
            return false;
        for (int start = 0, chunk = 0; start < audio.getNumSamples(); start += kChunk, ++chunk)
        {
            if (! reach ("wavChunk", n * 100000 + chunk))
                return false;
            if (! writer->writeFromAudioSampleBuffer (audio, start, juce::jmin (kChunk, audio.getNumSamples() - start)))
                return false;
        }
        // The writer finishes the header and closes the file here.
        if (! writer->flush())
            return false;
    }
    return syncToDisk (file);
}

juce::uint64 revisionOf (std::vector<Entry> entries, double rate)
{
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.path < b.path; });
    juce::String s ("rate " + juce::String (rate, 3));
    for (const auto& e : entries)
        s << "\n" << e.path << " " << e.frames << " " << e.channels << " " << hex (e.decodedKey);
    juce::uint64 h = 1469598103934665603ull;
    for (auto p = s.toRawUTF8(); *p != 0; ++p)
        h = (h ^ (juce::uint8) *p) * 1099511628211ull;
    return h;
}

std::unique_ptr<juce::XmlElement> readIndex (const juce::File& folder)
{
    auto xml = juce::parseXML (folder.getChildFile (kIndexName));
    if (xml == nullptr || ! xml->hasTagName ("KaleidoboxAudio"))
        return {};
    return xml;
}

const Entry* findSaved (const Committed& previous, const juce::AudioBuffer<float>& audio, juce::uint64 key)
{
    for (const auto& e : previous.entries)
        if ((e.sourceKey == key || e.decodedKey == key) && e.frames == audio.getNumSamples())
            return &e;
    return nullptr;
}
} // namespace

//==============================================================================
juce::File currentFolder (const juce::File& showFile)
{
    return showFile.getSiblingFile (showFile.getFileNameWithoutExtension() + " loops");
}

juce::File revisionFolder (const juce::File& showFile, const juce::String& revision)
{
    return showFile.getSiblingFile (revisionPrefix (showFile) + revision);
}

bool holdsRevision (const juce::File& folder, const juce::String& revision)
{
    if (revision.isEmpty() || ! folder.isDirectory())
        return false;
    auto index = readIndex (folder);
    if (index == nullptr || index->getStringAttribute ("revision") != revision)
        return false;
    for (auto* e : index->getChildWithTagNameIterator ("File"))
    {
        const auto file = folder.getChildFile (e->getStringAttribute ("path"));
        if (! file.existsAsFile() || file.getSize() != e->getStringAttribute ("bytes").getLargeIntValue())
            return false;
    }
    return true;
}

juce::File resolve (const juce::File& showFile, const juce::String& revision, juce::String* problem)
{
    if (revision.isEmpty())
        return currentFolder (showFile);   // a show from an older build
    if (revision != kUnsavedRevision)
    {
        if (holdsRevision (currentFolder (showFile), revision))
            return currentFolder (showFile);
        if (holdsRevision (revisionFolder (showFile, revision), revision))
            return revisionFolder (showFile, revision);
    }
    if (problem != nullptr)
        *problem = revision == kUnsavedRevision
                     ? juce::String ("This state's loops weren't saved (the disk was full or couldn't be written), so it opened without them.")
                     : "Couldn't find the loops saved with " + showFile.getFileNameWithoutExtension()
                         + " (\"" + revisionPrefix (showFile) + revision + "\" or \"" + currentFolder (showFile).getFileName()
                         + "\"), so it opened without them.";
    return {};
}

juce::uint64 hashAudio (const juce::AudioBuffer<float>& audio)
{
    juce::uint64 h = 1469598103934665603ull ^ ((juce::uint64) audio.getNumChannels() << 40) ^ (juce::uint64) audio.getNumSamples();
    for (int c = 0; c < audio.getNumChannels(); ++c)
    {
        const auto* d = audio.getReadPointer (c);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            juce::uint32 bits;
            std::memcpy (&bits, d + i, sizeof bits);
            h = (h ^ bits) * 0x100000001b3ull;
            h ^= h >> 29;
        }
    }
    return h;
}

//==============================================================================
bool stage (const juce::File& showFile, const std::vector<Asset>& assets, double sampleRate,
            const Committed& previous, Staged& out, Result& result)
{
    const double rate = sampleRate > 0 ? sampleRate : 44100.0;
    out = {};
    out.showFile = showFile;
    auto fail = [&result] (const juce::String& why) { result.ok = false; result.error = why; return false; };

    if (! reach ("stage"))
        return fail ("Couldn't start saving the loops.");

    std::vector<juce::uint64> keys;
    for (const auto& a : assets)
        keys.push_back (hashAudio (*a.audio));
    out.keys = keys;

    // Already saved as it is (same show, same files): nothing to write.
    const auto previousFolder = previous.showFile == juce::File() ? juce::File() : resolve (previous.showFile, previous.revision);
    if (previous.showFile == showFile && previous.revision.isNotEmpty() && previousFolder != juce::File()
        && assets.size() == previous.entries.size())
    {
        std::vector<Entry> same;
        for (size_t i = 0; i < assets.size(); ++i)
            for (const auto& e : previous.entries)
                if (e.path == assets[i].path && (e.sourceKey == keys[i] || e.decodedKey == keys[i]) && e.frames == assets[i].audio->getNumSamples())
                    same.push_back (e);
        if (same.size() == assets.size() && hex (revisionOf (same, rate)) == previous.revision
            && holdsRevision (previousFolder, previous.revision))
        {
            out.folder = previousFolder;
            out.revision = previous.revision;
            out.entries = std::move (same);
            result.unchanged = true;
            result.revision = out.revision;
            result.ok = true;
            return true;
        }
    }

    const auto parent = showFile.getParentDirectory();
    if (! parent.createDirectory())
        return fail ("Couldn't make the folder " + parent.getFullPathName() + ".");

    juce::File folder;
    for (int attempt = 0; attempt < 20 && folder == juce::File(); ++attempt)
    {
        auto f = parent.getChildFile (stagingPrefix (showFile) + juce::String::toHexString (juce::Time::currentTimeMillis())
                                      + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        if (! f.exists() && f.createDirectory())
            folder = f;
    }
    if (folder == juce::File())
        return fail ("Couldn't make a folder for the loops next to " + showFile.getFileName() + " (is the disk full or read-only?).");

    auto abandon = [&folder, &fail] (const juce::String& why) { folder.deleteRecursively(); return fail (why); };

    for (size_t i = 0; i < assets.size(); ++i)
    {
        const auto& a = assets[i];
        const auto target = folder.getChildFile (a.path);
        Entry entry;
        entry.path = a.path;

        // Unchanged audio: a link to the file already saved (it never changes), checked first.
        bool linked = false;
        if (previousFolder != juce::File())
            if (const auto* saved = findSaved (previous, *a.audio, keys[i]))
            {
                const auto source = previousFolder.getChildFile (saved->path);
                if (source.existsAsFile() && source.getSize() == saved->bytes
                    && source.getLastModificationTime().toMilliseconds() == saved->modified
                    && target.getParentDirectory().createDirectory())
                {
                    std::error_code ec;
                    std::filesystem::create_hard_link (fsPath (source), fsPath (target), ec);
                    if (ec)
                    {
                        ec.clear();   // (another disk, or links not allowed): a copy
                        linked = std::filesystem::copy_file (fsPath (source), fsPath (target), ec) && ! ec && syncToDisk (target);
                    }
                    else
                        linked = true;
                    if (linked && target.getSize() == saved->bytes)
                    {
                        entry = *saved;
                        entry.path = a.path;
                        entry.sourceKey = keys[i];
                        ++result.linked;
                    }
                    else
                    {
                        linked = false;
                        target.deleteFile();
                    }
                }
            }

        if (! linked)
        {
            if (! reach ("wav", (int) i) || ! writeNewWav (target, *a.audio, rate, (int) i))
                return abandon ("Couldn't write " + a.path + " (is the disk full?). The last saved show is unchanged.");
            const auto back = readBack (target);
            if (! back.ok || back.frames != a.audio->getNumSamples() || back.channels != 2)
                return abandon ("The saved " + a.path + " didn't read back right. The last saved show is unchanged.");
            entry.frames = back.frames;
            entry.channels = back.channels;
            entry.sourceKey = keys[i];
            entry.decodedKey = back.key;
            ++result.written;
        }
        entry.bytes = target.getSize();
        entry.modified = target.getLastModificationTime().toMilliseconds();
        out.entries.push_back (entry);
    }

    const auto revision = hex (revisionOf (out.entries, rate));
    out.revision = revision;

    // The index, last: a folder with it is complete.
    {
        juce::XmlElement index ("KaleidoboxAudio");
        index.setAttribute ("revision", revision);
        index.setAttribute ("sampleRate", rate);
        for (const auto& e : out.entries)
        {
            auto* f = index.createNewChildElement ("File");
            f->setAttribute ("path", e.path);
            f->setAttribute ("frames", juce::String (e.frames));
            f->setAttribute ("channels", e.channels);
            f->setAttribute ("bytes", juce::String (e.bytes));
            f->setAttribute ("key", hex (e.decodedKey));
        }
        const auto indexFile = folder.getChildFile (kIndexName);
        if (! reach ("index") || ! index.writeTo (indexFile) || ! syncToDisk (indexFile) || ! holdsRevision (folder, revision))
            return abandon ("Couldn't finish saving the loops (is the disk full?). The last saved show is unchanged.");
    }
    // The folders inside, then the staging folder itself (their names are on disk too).
    for (const auto& sub : folder.findChildFiles (juce::File::findDirectories, true, "*"))
        syncToDisk (sub);
    if (! syncToDisk (folder))
        return abandon ("Couldn't flush the loops to the disk. The last saved show is unchanged.");

    // The same audio may already be saved (in "<show> loops" or its own revision folder).
    for (const auto& existing : { currentFolder (showFile), revisionFolder (showFile, revision) })
        if (holdsRevision (existing, revision))
        {
            folder.deleteRecursively();
            out.folder = existing;
            out.created = false;
            result.revision = revision;
            result.ok = true;
            return true;
        }

    auto target = revisionFolder (showFile, revision);
    if (target.exists())
    {
        // A broken folder of that name (nothing can load it): kept, out of the way.
        if (! renameFolder (target, target.getSiblingFile (target.getFileName() + "-broken-" + juce::String (juce::Time::currentTimeMillis()))))
            return abandon ("Couldn't make room for the loops next to " + showFile.getFileName() + ". The last saved show is unchanged.");
    }
    if (! reach ("stageRename") || ! renameFolder (folder, target))
        return abandon ("Couldn't finish saving the loops. The last saved show is unchanged.");
    if (! syncToDisk (parent))
    {
        target.deleteRecursively();
        return fail ("Couldn't flush the loops to the disk. The last saved show is unchanged.");
    }
    out.folder = target;
    out.created = true;
    result.revision = revision;
    result.ok = true;
    return true;
}

void discard (const Staged& staged)
{
    if (staged.created && staged.folder != currentFolder (staged.showFile))
        staged.folder.deleteRecursively();
}

void finish (const Staged& staged, Result& result)
{
    const auto current = currentFolder (staged.showFile);
    if (staged.folder == current)
        return;

    // Older builds only know "<show> loops": the new revision goes there, and what was there
    // goes to its own folder (nothing is deleted).
    if (current.exists())
    {
        juce::String old;
        if (auto index = readIndex (current))
            old = index->getStringAttribute ("revision");
        auto aside = old.isNotEmpty() ? revisionFolder (staged.showFile, old)
                                      : revisionFolder (staged.showFile, "legacy-" + juce::String (juce::Time::currentTimeMillis()));
        if (aside.exists())
            aside = aside.getSiblingFile (aside.getFileName() + "-" + juce::String (juce::Time::currentTimeMillis()));
        if (! reach ("aside") || ! renameFolder (current, aside))
        {
            result.warning = "Saved. (The loops are in \"" + staged.folder.getFileName() + "\"; older versions of Kaleidobox won't find them.)";
            return;
        }
    }
    if (! reach ("promote") || ! renameFolder (staged.folder, current))
    {
        result.warning = "Saved. (The loops are in \"" + staged.folder.getFileName() + "\"; older versions of Kaleidobox won't find them.)";
        return;
    }
    syncToDisk (staged.showFile.getParentDirectory());
    reach ("done");
}

bool commitXml (const juce::XmlElement& xml, const juce::File& showFile)
{
    // A new file next to the show (same folder, so the rename can't cross disks), flushed to the
    // disk, then renamed over the show in one step: the show file is always the old or the new
    // one, never part of either.
    const auto parent = showFile.getParentDirectory();
    if (! parent.createDirectory())
        return false;
    const auto temp = parent.getChildFile ("." + showFile.getFileName() + ".saving-"
                                           + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    if (temp.exists())
        return false;
    bool ok = false;
    {
        juce::FileOutputStream out (temp);
        if (out.openedOk())
        {
            xml.writeTo (out);
            out.flush();
            ok = out.getStatus().wasOk();
        }
    }
    ok = ok && syncToDisk (temp) && reach ("commit");
    if (ok)
    {
        std::error_code ec;
        std::filesystem::rename (fsPath (temp), fsPath (showFile), ec);   // replaces the old show atomically
        ok = ! ec;
    }
    if (! ok)
    {
        temp.deleteFile();
        return false;
    }
    syncToDisk (parent);   // the new name, on the disk
    return true;
}

Committed committedFrom (const Staged& staged)
{
    return { staged.showFile, staged.revision, staged.entries };
}

juce::CriticalSection& saveLock()
{
    static juce::CriticalSection lock;
    return lock;
}

Hooks& hooks()
{
    static Hooks h;
    return h;
}

bool reach (const juce::String& point, int n)
{
    auto& h = hooks();
    return ! h.at || h.at (point, n);
}
} // namespace rigshow
