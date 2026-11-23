#include "SampleKits.h"

namespace crate::drums::kits
{
namespace
{
bool has (const juce::String& n, std::initializer_list<const char*> words)
{
    for (auto* w : words)
        if (n.contains (w))
            return true;
    return false;
}

// A short tag (bd, sd, hh, oh...) as a word of its own: "BD_01", "808 sd", "hh-closed".
bool tag (const juce::String& n, std::initializer_list<const char*> tags)
{
    juce::StringArray words;
    words.addTokens (n, " _-.()[]0123456789", "");
    for (auto* t : tags)
        if (words.contains (t))
            return true;
    return false;
}
}

int voiceForName (const juce::String& fileName, const std::array<bool, kVoices>& taken)
{
    const auto n = juce::File::createFileWithoutCheckingPath ("/" + fileName).getFileNameWithoutExtension().toLowerCase();
    auto firstFree = [&] (std::initializer_list<int> order)
    {
        for (int v : order)
            if (! taken[(size_t) v])
                return v;
        return *order.begin();
    };

    if (has (n, { "kick", "kik", "bassdrum", "bass drum" }) || tag (n, { "bd", "kk" }))
        return kick;
    if (has (n, { "clap", "clp" }) || tag (n, { "cp" }))
        return clap;
    if (has (n, { "rim", "sidestick", "side stick", "stick" }) || tag (n, { "rs" }))
        return rim;
    if (has (n, { "snare", "snr" }) || tag (n, { "sd", "sn" }))
        return snare;
    if (has (n, { "open" }) || tag (n, { "oh", "ohh", "ohat" }))
        return openHat;
    if (has (n, { "hat", "hihat", "hi-hat", "hh" }) || tag (n, { "ch", "chh" }))
        return firstFree ({ closedHat, openHat });
    if (has (n, { "crash", "ride", "cym", "splash", "china" }) || tag (n, { "cy" }))
        return cymbal;
    if (has (n, { "cowbell", "bell", "agogo" }) || tag (n, { "cb" }))
        return cowbell;
    if (has (n, { "clave", "wood", "block", "click" }) || tag (n, { "cl" }))
        return clave;
    if (has (n, { "tom", "conga", "bongo", "tabla", "djembe" }))
    {
        if (has (n, { "low", "floor" }) || tag (n, { "lo", "lt" }))
            return lowTom;
        if (has (n, { "high" }) || tag (n, { "hi", "ht" }))
            return highTom;
        if (has (n, { "mid" }) || tag (n, { "mt" }))
            return midTom;
        return firstFree ({ lowTom, midTom, highTom });
    }
    if (has (n, { "shaker", "shake", "tamb", "perc", "maraca", "cabasa" }))
        return firstFree ({ cowbell, clave, rim });
    return -1;
}

std::array<juce::File, kVoices> mapFiles (juce::Array<juce::File> files)
{
    struct ByName
    {
        static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()); }
    };
    ByName order;
    files.sort (order);

    std::array<juce::File, kVoices> out;
    std::array<bool, kVoices> taken {};
    juce::Array<juce::File> left;
    for (const auto& f : files)
    {
        const int v = voiceForName (f.getFileName(), taken);
        if (v >= 0 && ! taken[(size_t) v])
        {
            out[(size_t) v] = f;
            taken[(size_t) v] = true;
        }
        else
            left.add (f);
    }
    for (int v : { (int) clave, (int) cowbell, (int) rim, (int) cymbal, (int) clap, (int) highTom, (int) midTom, (int) lowTom })
        if (! taken[(size_t) v] && ! left.isEmpty())
        {
            out[(size_t) v] = left.removeAndReturn (0);
            taken[(size_t) v] = true;
        }
    return out;
}

juce::Array<juce::File> audioFilesIn (const juce::File& folder, juce::AudioFormatManager& formats)
{
    juce::Array<juce::File> out;
    if (! folder.isDirectory())
        return out;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
    {
        const auto& f = entry.getFile();
        if (f.getFileName().startsWithChar ('.'))
            continue;
        if (formats.findFormatForFileExtension (f.getFileExtension()) != nullptr)
            out.add (f);
        if (out.size() >= 512)
            break;
    }
    struct ByName
    {
        static int compareElements (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural (b.getFullPathName()); }
    };
    ByName order;
    out.sort (order);
    return out;
}

juce::File userKitsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Drum Kits");
}

juce::Array<juce::File> userKits()
{
    auto list = userKitsFolder().findChildFiles (juce::File::findFiles, false, "*.drumkit");
    struct ByName
    {
        static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()); }
    };
    ByName order;
    list.sort (order);
    return list;
}

bool save (const Kit& kit, const juce::File& file)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", kit.name);
    juce::Array<juce::var> sounds;
    for (int v = 0; v < kVoices; ++v)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("sound", voiceName (v));
        const auto& f = kit.files[(size_t) v];
        if (f != juce::File())
        {
            o->setProperty ("file", f.getFullPathName());
            // Also relative to the kit, so a folder that moves with its kit still works.
            o->setProperty ("relative", f.getRelativePathFrom (file.getParentDirectory()));
        }
        if (kit.hasVoices)
        {
            const auto& s = kit.voices[(size_t) v];
            o->setProperty ("level", s.level);
            o->setProperty ("tune", s.tune);
            o->setProperty ("decay", s.decay);
            o->setProperty ("tone", s.tone);
            o->setProperty ("pan", s.pan);
        }
        sounds.add (juce::var (o));
    }
    root->setProperty ("sounds", sounds);
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

bool load (const juce::File& file, Kit& kit)
{
    const auto json = juce::JSON::parse (file.loadFileAsString());
    auto* sounds = json.getProperty ("sounds", {}).getArray();
    if (sounds == nullptr)
        return false;
    kit = {};
    kit.name = json.getProperty ("name", file.getFileNameWithoutExtension()).toString();
    for (int v = 0; v < juce::jmin (kVoices, sounds->size()); ++v)
    {
        const auto& o = (*sounds)[v];
        juce::File f;
        const auto path = o.getProperty ("file", "").toString();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
            f = juce::File (path);
        const auto rel = o.getProperty ("relative", "").toString();
        if (! f.existsAsFile() && rel.isNotEmpty())
            f = file.getParentDirectory().getChildFile (rel);
        kit.files[(size_t) v] = path.isNotEmpty() || rel.isNotEmpty() ? f : juce::File();
        if (o.hasProperty ("level"))
        {
            kit.hasVoices = true;
            auto& s = kit.voices[(size_t) v];
            s.level = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("level", s.level));
            s.tune = juce::jlimit (-12.0f, 12.0f, (float) (double) o.getProperty ("tune", s.tune));
            s.decay = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("decay", s.decay));
            s.tone = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("tone", s.tone));
            s.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) o.getProperty ("pan", s.pan));
        }
    }
    return true;
}
}
