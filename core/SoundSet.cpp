#include "SoundSet.h"

#include "AudioFiles.h"

namespace
{
const char* const kJson = "set.json";

juce::String padLabel (int index)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 3, index / 16))) + juce::String (index % 16 + 1).paddedLeft ('0', 2);
}

std::optional<SoundSet> readJson (const juce::File& folder)
{
    const auto dir = folder.getFileName() == kJson ? folder.getParentDirectory() : folder;
    const auto json = juce::JSON::parse (dir.getChildFile (kJson));
    if (! json.isObject() || (int) json.getProperty ("version", 0) < 1)
        return std::nullopt;
    SoundSet set;
    set.name = json.getProperty ("name", dir.getFileName()).toString();
    set.wholeKit = (bool) json.getProperty ("wholeKit", false);
    set.bpm = (double) json.getProperty ("bpm", 0.0);
    set.madeOn = json.getProperty ("madeOn", {}).toString();
    set.saved = juce::Time ((juce::int64) json.getProperty ("saved", (juce::int64) 0));
    if (auto* pads = json.getProperty ("pads", {}).getArray())
        for (auto& p : *pads)
        {
            SoundSet::Pad pad;
            pad.index = juce::jlimit (0, set.wholeKit ? 63 : 15, (int) p.getProperty ("pad", 0));
            pad.name = p.getProperty ("name", {}).toString();
            pad.volume = juce::jlimit (0, 100, (int) p.getProperty ("volume", 100));
            pad.pan = juce::jlimit (0, 100, (int) p.getProperty ("pan", 50));
            pad.tune = juce::jlimit (-120, 120, (int) p.getProperty ("tune", 0));
            pad.mode = juce::jlimit (0, 2, (int) p.getProperty ("mode", 0));
            pad.mono = (bool) p.getProperty ("mono", false);
            pad.muteGroup = juce::jlimit (0, 32, (int) p.getProperty ("muteGroup", 0));
            pad.cutoff = juce::jlimit (20.0f, 20000.0f, (float) p.getProperty ("cutoff", 20000.0));
            pad.resonance = juce::jlimit (0.0f, 1.0f, (float) p.getProperty ("resonance", 0.0));
            pad.attack = juce::jlimit (0, 2000, (int) p.getProperty ("attack", 0));
            pad.decay = juce::jlimit (0, 5000, (int) p.getProperty ("decay", 0));
            pad.beats = juce::jmax (0.0, (double) p.getProperty ("beats", 0.0));
            // the file name rides along in "file"; the audio is read by load()
            pad.audio = nullptr;
            set.pads.push_back (pad);
        }
    return set;
}
}

juce::String SoundSet::describe() const
{
    juce::String s = juce::String (padCount()) + (padCount() == 1 ? " pad" : " pads");
    s << juce::String::fromUTF8 (" \xc2\xb7 ") << (wholeKit ? "whole kit" : "one bank");
    if (madeOn.isNotEmpty())
        s << juce::String::fromUTF8 (" \xc2\xb7 from the ") << madeOn.toLowerCase();
    return s;
}

juce::File SoundSet::macFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Sets");
}

juce::String SoundSet::fileNameFor (const juce::String& name)
{
    auto safe = name.removeCharacters ("/\\:*?\"<>|").trim();
    while (safe.startsWithChar ('.'))
        safe = safe.substring (1);
    return safe.isEmpty() ? juce::String ("Set") : safe.substring (0, 60).trim();
}

juce::File SoundSet::save (const juce::File& root, juce::String& error) const
{
    if (pads.empty())
    {
        error = "There's nothing on the pads to save.";
        return {};
    }
    if (! root.createDirectory())
    {
        error = "Couldn't make the Sets folder (" + root.getFullPathName() + ").";
        return {};
    }
    const auto base = fileNameFor (name);
    auto dir = root.getChildFile (base);
    for (int n = 2; dir.exists(); ++n)
        dir = root.getChildFile (base + " " + juce::String (n));
    // written into a hidden folder first, so a half-written set never shows in a list
    const auto temp = root.getChildFile ("." + dir.getFileName() + ".saving");
    temp.deleteRecursively();
    if (! temp.createDirectory())
    {
        error = "Couldn't write in " + root.getFullPathName() + ".";
        return {};
    }
    juce::Array<juce::var> list;
    for (const auto& pad : pads)
    {
        if (pad.audio == nullptr || pad.audio->getNumSamples() == 0)
            continue;
        const auto file = padLabel (pad.index) + (pad.name.isNotEmpty() ? " " + fileNameFor (pad.name) : juce::String()) + ".wav";
        if (! crate::AudioFiles::write (temp.getChildFile (file), *pad.audio))
        {
            temp.deleteRecursively();
            error = "Couldn't save pad " + padLabel (pad.index) + "'s sound.";
            return {};
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("pad", pad.index);
        o->setProperty ("name", pad.name);
        o->setProperty ("file", file);
        o->setProperty ("volume", pad.volume);
        o->setProperty ("pan", pad.pan);
        o->setProperty ("tune", pad.tune);
        o->setProperty ("mode", pad.mode);
        o->setProperty ("mono", pad.mono);
        o->setProperty ("muteGroup", pad.muteGroup);
        o->setProperty ("cutoff", pad.cutoff);
        o->setProperty ("resonance", pad.resonance);
        o->setProperty ("attack", pad.attack);
        o->setProperty ("decay", pad.decay);
        o->setProperty ("beats", pad.beats);
        list.add (juce::var (o));
    }
    auto* root_ = new juce::DynamicObject();
    root_->setProperty ("version", 1);
    root_->setProperty ("name", name);
    root_->setProperty ("wholeKit", wholeKit);
    root_->setProperty ("bpm", bpm);
    root_->setProperty ("madeOn", madeOn);
    root_->setProperty ("saved", (juce::int64) (saved.toMilliseconds() > 0 ? saved : juce::Time::getCurrentTime()).toMilliseconds());
    root_->setProperty ("pads", list);
    if (! temp.getChildFile (kJson).replaceWithText (juce::JSON::toString (juce::var (root_))) || ! temp.moveFileTo (dir))
    {
        temp.deleteRecursively();
        error = "Couldn't save the set in " + root.getFullPathName() + ".";
        return {};
    }
    return dir;
}

std::optional<SoundSet> SoundSet::peek (const juce::File& folder)
{
    return readJson (folder);
}

std::optional<SoundSet> SoundSet::load (const juce::File& folder, juce::String& error)
{
    const auto dir = folder.getFileName() == kJson ? folder.getParentDirectory() : folder;
    auto set = readJson (dir);
    if (! set)
    {
        error = "That isn't a set (no set.json in " + dir.getFileName() + ").";
        return std::nullopt;
    }
    const auto json = juce::JSON::parse (dir.getChildFile (kJson));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::vector<Pad> kept;
    auto* list = json.getProperty ("pads", {}).getArray();
    for (size_t i = 0; i < set->pads.size() && list != nullptr && (int) i < list->size(); ++i)
    {
        const auto file = (*list)[(int) i].getProperty ("file", {}).toString();
        if (file.isEmpty() || file.containsAnyOf ("/\\"))
            continue;
        juce::String why;
        auto audio = crate::AudioFiles::load (dir.getChildFile (file), formats, why);
        if (audio == nullptr)
            continue;
        auto pad = set->pads[i];
        pad.audio = audio;
        kept.push_back (pad);
    }
    set->pads = std::move (kept);
    if (set->pads.empty())
    {
        error = "The set " + set->name + " has no sounds left in its folder.";
        return std::nullopt;
    }
    return set;
}

juce::Array<juce::File> SoundSet::list (const juce::File& root)
{
    juce::Array<juce::File> sets;
    for (const auto& entry : juce::RangedDirectoryIterator (root, false, "*", juce::File::findDirectories))
        if (! entry.getFile().getFileName().startsWithChar ('.') && entry.getFile().getChildFile (kJson).existsAsFile())
            sets.add (entry.getFile());
    std::sort (sets.begin(), sets.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getChildFile (kJson).getLastModificationTime() > b.getChildFile (kJson).getLastModificationTime(); });
    return sets;
}
