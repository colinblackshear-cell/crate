#include "SoundBank.h"

namespace
{
const juce::String indexName = "Sound bank index.tsv", foldersName = "Sound bank folders.txt";

// Names split into lowercase words: "KickHard_01-Dry" -> kick, hard, 01, dry.
juce::StringArray words (const juce::String& text)
{
    juce::StringArray out;
    juce::String current;
    auto flush = [&]
    {
        if (current.isNotEmpty())
            out.add (current.toLowerCase());
        current.clear();
    };
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (! juce::CharacterFunctions::isLetterOrDigit (c) && c != '#')
        {
            flush();
            continue;
        }
        if (current.isNotEmpty())
        {
            const auto last = current.getLastCharacter();
            const bool lowerToUpper = juce::CharacterFunctions::isLowerCase (last) && juce::CharacterFunctions::isUpperCase (c);
            const bool letterDigit = juce::CharacterFunctions::isLetter (last) != juce::CharacterFunctions::isLetter (c) && c != '#' && last != '#';
            if (lowerToUpper || letterDigit)
                flush();
        }
        current += c;
    }
    flush();
    return out;
}

struct KindWords
{
    int kind;
    std::initializer_list<const char*> exact;    // whole words
    std::initializer_list<const char*> inside;   // anywhere in a word (long enough to be safe)
};

// In order: the first kind that matches wins (so "bass drum" is a kick, not a bass).
const KindWords kindWords[] = {
    { SoundBank::kick,   { "kick", "kik", "kck", "bd", "bassdrum" }, { "kick", "bassdrum" } },
    { SoundBank::snare,  { "snare", "snr", "sd", "rim", "rimshot" }, { "snare" } },
    { SoundBank::clap,   { "clap", "clp", "snap", "snaps", "claps" }, { "clap" } },
    { SoundBank::hat,    { "hat", "hats", "hh", "hihat", "hihats", "oh", "ch" }, { "hihat" } },
    { SoundBank::cymbal, { "cymbal", "crash", "ride", "china", "splash", "cym" }, { "cymbal" } },
    { SoundBank::tom,    { "tom", "toms" }, {} },
    { SoundBank::perc,   { "perc", "percs", "percussion", "shaker", "conga", "bongo", "tamb", "tambourine", "cowbell", "clave", "woodblock", "triangle", "timbale", "guiro" }, { "shaker", "percussion" } },
    { SoundBank::vocal,  { "vocal", "vocals", "vox", "voice", "acapella", "adlib", "adlibs", "chant", "choir", "spoken" }, { "vocal" } },
    { SoundBank::fx,     { "fx", "sfx", "riser", "impact", "sweep", "noise", "uplifter", "downlifter", "transition", "whoosh", "foley", "texture" }, { "riser" } },
    { SoundBank::bass,   { "bass", "808", "808s", "sub", "reese", "bassline" }, {} },
    { SoundBank::keys,   { "piano", "keys", "rhodes", "organ", "ep", "wurli", "wurlitzer", "clav" }, { "piano" } },
    { SoundBank::guitar, { "guitar", "gtr", "guitars", "strum" }, { "guitar" } },
    { SoundBank::synth,  { "synth", "synths", "lead", "pad", "pads", "pluck", "plucks", "arp", "chord", "chords", "stab", "stabs", "brass", "strings", "bell", "bells" }, { "synth" } },
};

int kindOf (const juce::StringArray& split)
{
    // Pairs joined too, so "bass drum" and "hi hat" are found as one word.
    auto w = split;
    for (int i = 0; i + 1 < split.size(); ++i)
        w.add (split[i] + split[i + 1]);
    for (const auto& k : kindWords)
        for (const auto& word : w)
        {
            for (auto* e : k.exact)
                if (word == e)
                    return k.kind;
            for (auto* e : k.inside)
                if (word.contains (e))
                    return k.kind;
        }
    return -1;
}

// "F#min", "Cm", "Ebmaj", "A#" (an accidental or a mode, so a lone "a" isn't a key).
juce::String keyOf (const juce::String& word)
{
    const auto w = word.toLowerCase();
    if (w.isEmpty() || w[0] < 'a' || w[0] > 'g')
        return {};
    juce::String note = juce::String::charToString (juce::CharacterFunctions::toUpperCase (w[0]));
    auto rest = w.substring (1);
    bool marked = false;
    if (rest.startsWith ("#") || rest.startsWith ("sharp")) { note += "#"; rest = rest.fromFirstOccurrenceOf (rest.startsWith ("#") ? "#" : "sharp", false, false); marked = true; }
    else if (rest.startsWith ("b") && ! rest.startsWith ("bass")) { note += "b"; rest = rest.substring (1); marked = true; }
    if (rest == "m" || rest == "min" || rest == "minor")
        return note + " min";
    if (rest == "maj" || rest == "major")
        return note + " maj";
    return rest.isEmpty() && marked ? note : juce::String();
}
}

//==============================================================================
juce::String SoundBank::kindName (int k)
{
    static const char* names[] = { "Kicks", "Snares", "Claps", "Hats", "Cymbals", "Toms", "Percussion", "Bass",
                                   "Synths", "Keys", "Guitar", "Vocals", "FX", "Other" };
    return juce::isPositiveAndBelow (k, (int) numKinds) ? names[k] : "";
}

SoundBank::SoundBank (juce::File h)
    : juce::Thread ("Sound bank scan"), home (std::move (h))
{
    formats.registerBasicFormats();
    const auto saved = home.getChildFile (foldersName);
    if (saved.existsAsFile())
    {
        juce::StringArray lines;
        lines.addLines (saved.loadFileAsString());
        for (const auto& line : lines)
            if (line.trim().isNotEmpty())
                folders.add (juce::File (line.trim()));
    }
    else
    {
        folders = suggestedFolders();
    }
    loadIndex();
}

SoundBank::~SoundBank()
{
    stopThread (4000);
}

juce::File SoundBank::defaultHome()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate");
}

juce::Array<juce::File> SoundBank::suggestedFolders()
{
    const auto user = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto desktop = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
    juce::Array<juce::File> out;
    for (const auto& f : { music.getChildFile ("Crate").getChildFile ("Sounds"),
                           user.getChildFile ("Splice"), music.getChildFile ("Splice"), docs.getChildFile ("Splice"),
                           desktop.getChildFile ("Splice"), music.getChildFile ("Samples"), user.getChildFile ("Samples") })
        if (f.isDirectory())
            out.addIfNotAlreadyThere (f);
    return out;
}

juce::Array<juce::File> SoundBank::getFolders() const
{
    const juce::ScopedLock sl (lock);
    return folders;
}

void SoundBank::addFolder (const juce::File& f)
{
    {
        const juce::ScopedLock sl (lock);
        if (! f.isDirectory() || folders.contains (f))
            return;
        folders.add (f);
    }
    saveFolders();
    rescan();
}

void SoundBank::removeFolder (const juce::File& f)
{
    {
        const juce::ScopedLock sl (lock);
        folders.removeFirstMatchingValue (f);
        sounds.erase (std::remove_if (sounds.begin(), sounds.end(), [&] (const Sound& s) { return s.file.isAChildOf (f); }), sounds.end());
    }
    saveFolders();
    saveIndex();
    sendChangeMessage();
}

void SoundBank::rescan()
{
    if (isThreadRunning())
        again = true;
    else
        startThread (juce::Thread::Priority::low);
}

int SoundBank::size() const
{
    const juce::ScopedLock sl (lock);
    return (int) sounds.size();
}

//==============================================================================
void SoundBank::describe (Sound& s, const juce::String& relativePath)
{
    s.name = s.file.getFileNameWithoutExtension();
    s.where = relativePath.upToLastOccurrenceOf ("/", false, false);

    const auto nameWords = words (s.name);
    juce::StringArray folderWords;
    juce::StringArray parts;
    parts.addTokens (s.where, "/", {});
    for (int i = parts.size(); --i >= 0;)   // the nearest folder first
        folderWords.addArray (words (parts[i]));

    int k = kindOf (nameWords);
    if (k < 0)
        k = kindOf (folderWords);
    s.kind = k < 0 ? other : k;

    auto has = [] (const juce::StringArray& w, std::initializer_list<const char*> list)
    {
        for (auto* e : list)
            if (w.contains (e))
                return true;
        return false;
    };
    const bool saysLoop = has (nameWords, { "loop", "loops", "lp" }) || has (folderWords, { "loop", "loops" });
    const bool saysShot = has (nameWords, { "oneshot", "shot", "hit", "one" }) || has (folderWords, { "oneshot", "oneshots", "shots", "one" });

    // Tempo: "92bpm", "92 bpm", or a lone 60-200 number in a loop's name.
    if (s.bpm <= 0)
    {
        for (int i = 0; i < nameWords.size(); ++i)
        {
            const auto& w = nameWords[i];
            if (! w.containsOnly ("0123456789"))
                continue;
            const int v = w.getIntValue();
            const bool tagged = nameWords[i + 1] == "bpm";
            if (v >= 60 && v <= 200 && (tagged || saysLoop || ! saysShot))
            {
                if (tagged || saysLoop || w.length() >= 2)
                {
                    s.bpm = v;
                    if (tagged)
                        break;
                }
            }
        }
    }
    for (const auto& w : nameWords)
        if (const auto key = keyOf (w); key.isNotEmpty())
            s.key = key;
    // Words like "Fmin" split into "fmin" already; "F" + "min" come apart, so join pairs too.
    if (s.key.isEmpty())
        for (int i = 0; i + 1 < nameWords.size(); ++i)
            if (nameWords[i].length() <= 2)
                if (const auto key = keyOf (nameWords[i] + nameWords[i + 1]); key.isNotEmpty() && key.contains (" "))
                    s.key = key;

    s.loop = saysLoop || (! saysShot && s.bpm > 0 && s.seconds > 1.5f);
    if (s.loop && s.bpm <= 0)
        s.bpm = 0;
}

std::vector<SoundBank::Sound> SoundBank::search (const Query& q) const
{
    const auto terms = words (q.text);
    std::vector<Sound> out;
    const juce::ScopedLock sl (lock);
    for (const auto& s : sounds)
    {
        if (q.kind >= 0 && s.kind != q.kind)
            continue;
        if (q.length == Length::loops && ! s.loop)
            continue;
        if (q.length == Length::oneShots && s.loop)
            continue;
        if (q.nearBpm > 0)
        {
            if (! s.loop || s.bpm <= 0)
                continue;
            bool near = false;
            for (double f : { 0.5, 1.0, 2.0 })
                near = near || std::abs (s.bpm * f - q.nearBpm) <= q.nearBpm * 0.08;
            if (! near)
                continue;
        }
        if (! terms.isEmpty())
        {
            const auto hay = (s.name + " " + s.where + " " + kindName (s.kind) + " " + s.key).toLowerCase();
            bool all = true;
            for (const auto& t : terms)
                all = all && hay.contains (t);
            if (! all)
                continue;
        }
        out.push_back (s);
        if ((int) out.size() >= q.limit)
            break;
    }
    return out;
}

//==============================================================================
void SoundBank::run()
{
    do
    {
        again = false;
        const auto list = getFolders();

        // What we knew, by path, to skip files that haven't changed.
        std::unordered_map<juce::String, Sound> known;
        {
            const juce::ScopedLock sl (lock);
            for (const auto& s : sounds)
                known.emplace (s.file.getFullPathName(), s);
        }

        std::vector<Sound> next;
        found = 0;
        for (const auto& folder : list)
        {
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a",
                                                                    juce::File::findFiles))
            {
                if (threadShouldExit())
                    return;
                const auto& file = entry.getFile();
                Sound s;
                s.file = file;
                s.size = entry.getFileSize();
                s.modified = entry.getModificationTime().toMilliseconds();
                if (auto it = known.find (file.getFullPathName()); it != known.end() && it->second.size == s.size && it->second.modified == s.modified)
                {
                    next.push_back (it->second);
                }
                else
                {
                    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
                    {
                        s.seconds = reader->sampleRate > 0 ? (float) (reader->lengthInSamples / reader->sampleRate) : 0.0f;
                        const auto acid = reader->metadataValues[juce::WavAudioFormat::acidTempo].getDoubleValue();
                        if (acid >= 40 && acid <= 300)
                            s.bpm = acid;
                    }
                    describe (s, file.getRelativePathFrom (folder.getParentDirectory()).replaceCharacter ('\\', '/'));
                    next.push_back (std::move (s));
                }
                ++found;
            }
        }

        std::sort (next.begin(), next.end(), [] (const Sound& a, const Sound& b) { return a.name.compareNatural (b.name) < 0; });
        {
            const juce::ScopedLock sl (lock);
            sounds = std::move (next);
        }
        saveIndex();
        sendChangeMessage();
    }
    while (again.load() && ! threadShouldExit());
}

void SoundBank::loadIndex()
{
    juce::StringArray lines;
    lines.addLines (home.getChildFile (indexName).loadFileAsString());
    std::vector<Sound> loaded;
    for (const auto& line : lines)
    {
        juce::StringArray f;
        f.addTokens (line, "\t", {});
        if (f.size() < 9)
            continue;
        Sound s;
        s.file = juce::File (f[0]);
        s.name = f[1];
        s.where = f[2];
        s.kind = juce::jlimit (0, (int) numKinds - 1, f[3].getIntValue());
        s.loop = f[4] == "1";
        s.bpm = f[5].getDoubleValue();
        s.key = f[6];
        s.seconds = f[7].getFloatValue();
        s.size = f[8].getLargeIntValue();
        s.modified = f.size() > 9 ? f[9].getLargeIntValue() : 0;
        loaded.push_back (std::move (s));
    }
    const juce::ScopedLock sl (lock);
    sounds = std::move (loaded);
}

void SoundBank::saveIndex() const
{
    juce::MemoryOutputStream out;
    {
        const juce::ScopedLock sl (lock);
        for (const auto& s : sounds)
            out << s.file.getFullPathName() << "\t" << s.name << "\t" << s.where << "\t" << s.kind << "\t" << (s.loop ? "1" : "0")
                << "\t" << juce::String (s.bpm, 2) << "\t" << s.key << "\t" << juce::String (s.seconds, 3) << "\t" << juce::String (s.size)
                << "\t" << juce::String (s.modified) << "\n";
    }
    home.createDirectory();
    home.getChildFile (indexName).replaceWithData (out.getData(), out.getDataSize());
}

void SoundBank::saveFolders() const
{
    juce::StringArray lines;
    for (const auto& f : getFolders())
        lines.add (f.getFullPathName());
    home.createDirectory();
    home.getChildFile (foldersName).replaceWithText (lines.joinIntoString ("\n"));
}
