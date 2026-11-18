#include "PocketKit.h"

#include "mpc/BankWriter.h"

#include <algorithm>
#include <cstdio>
#include <map>

using namespace pocket;

namespace
{
std::int16_t toInt16 (float v) { return (std::int16_t) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, v) * 32767.0f); }

mpc::VoiceOverlap overlapFor (PadMode m)
{
    return m == PadMode::mono ? mpc::VoiceOverlap::mono : m == PadMode::noteOff ? mpc::VoiceOverlap::noteOff : mpc::VoiceOverlap::poly;
}

const char* const modeNames[] = { "oneShot", "mono", "noteOff" };

std::vector<std::uint8_t> readBytes (const juce::File& f)
{
    juce::MemoryBlock block;
    f.loadFileAsData (block);
    auto* p = static_cast<const std::uint8_t*> (block.getData());
    return std::vector<std::uint8_t> (p, p + block.getSize());
}
}

Kit::Kit (Library& l) : library (l) {}

juce::String Kit::padLabel (int pad)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + pad / 16)) + juce::String (pad % 16 + 1).paddedLeft ('0', 2);
}

juce::String Kit::mpcName (const juce::String& in, int maxLength)
{
    juce::String out;
    for (auto c : in.toUpperCase())
    {
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_')
            out << juce::String::charToString (c);
        if (out.length() >= maxLength)
            break;
    }
    return out.trim();
}

juce::String Kit::lengthText (double seconds)
{
    const int s = juce::roundToInt (seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

int Kit::usedPads() const
{
    int n = 0;
    for (auto& p : pads)
        n += p.empty() ? 0 : 1;
    return n;
}

size_t Kit::memoryBytes() const
{
    size_t bytes = 0;
    for (auto& p : pads)
        if (! p.empty())
            bytes += (size_t) p.audio->getNumSamples() * (size_t) juce::jmin (2, p.audio->getNumChannels()) * 2;
    return bytes;
}

int Kit::nextEmpty (int bank) const
{
    for (int i = 0; i < 16; ++i)
        if (pads[(size_t) (bank * 16 + i)].empty())
            return bank * 16 + i;
    return -1;
}

crate::SharedAudio Kit::sliceOf (const SoundItem& item, const juce::AudioBuffer<float>& whole, int from, int to) const
{
    const int n = whole.getNumSamples();
    from = juce::jlimit (0, juce::jmax (0, n - 1), from);
    to = to < 0 ? n : juce::jlimit (from + 1, n, to);
    juce::AudioBuffer<float> part (whole.getNumChannels(), to - from);
    for (int c = 0; c < whole.getNumChannels(); ++c)
        part.copyFrom (c, 0, whole, c, from, to - from);
    // What you hear on the Record tab with the effects on is what the pad plays.
    if (item.fx.anyOn())
        return crate::share (crate::SoundFx::apply (part, item.fx.toSettings()));
    return crate::share (std::move (part));
}

void Kit::assign (int pad, const SoundItem& item, crate::SharedAudio whole, int from, int to, const juce::String& padName)
{
    if (whole == nullptr || pad < 0 || pad >= kPads)
        return;
    KitPad p;
    p.soundId = item.id;
    p.name = padName.isNotEmpty() ? padName : item.name;
    p.from = from;
    p.to = to;
    p.audio = sliceOf (item, *whole, from, to);
    pads[(size_t) pad] = std::move (p);
}

bool Kit::assign (int pad, const juce::String& soundId, int from, int to, const juce::String& padName)
{
    auto* item = library.find (soundId);
    auto whole = library.load (soundId);
    if (item == nullptr || whole == nullptr)
        return false;
    assign (pad, *item, whole, from, to, padName);
    return true;
}

int Kit::chop (int first, const juce::String& soundId, bool onlyEmpty)
{
    auto* item = library.find (soundId);
    auto whole = library.load (soundId);
    if (item == nullptr || whole == nullptr)
        return 0;
    const int n = whole->getNumSamples();
    std::vector<int> cuts { 0 };
    auto markers = item->markers;
    std::sort (markers.begin(), markers.end());
    for (int m : markers)
        if (m > cuts.back() + 64 && m < n - 64)
            cuts.push_back (m);
    cuts.push_back (n);

    const int bankEnd = (first / 16 + 1) * 16;
    int used = 0;
    size_t slice = 0;
    for (int pad = first; pad < bankEnd && slice + 1 < cuts.size(); ++pad)
    {
        if (onlyEmpty && ! pads[(size_t) pad].empty())
            continue;
        assign (pad, *item, whole, cuts[slice], cuts[slice + 1], item->name + " " + juce::String ((int) slice + 1));
        ++slice;
        ++used;
    }
    return used;
}

//==============================================================================
juce::File Kit::kitsFolder() const { return library.getRoot().getChildFile ("Kits"); }

void Kit::save() const
{
    auto folder = kitsFolder();
    folder.createDirectory();
    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    juce::Array<juce::var> list;
    for (int i = 0; i < kPads; ++i)
    {
        auto& p = pads[(size_t) i];
        if (p.empty())
            continue;
        auto* o = new juce::DynamicObject();
        o->setProperty ("pad", i);
        o->setProperty ("sound", p.soundId);
        o->setProperty ("file", library.fileFor (p.soundId).getFileName());
        o->setProperty ("name", p.name);
        o->setProperty ("from", p.from);
        o->setProperty ("to", p.to);
        o->setProperty ("volume", p.volume);
        o->setProperty ("pan", p.pan);
        o->setProperty ("tune", p.tune);
        o->setProperty ("mode", modeNames[(int) p.mode]);
        list.add (juce::var (o));
    }
    root->setProperty ("pads", list);
    auto file = folder.getChildFile (juce::File::createLegalFileName (name) + ".json");
    file.replaceWithText (juce::JSON::toString (juce::var (root)));
    folder.getChildFile ("current.txt").replaceWithText (file.getFileName());
}

void Kit::load()
{
    auto current = kitsFolder().getChildFile ("current.txt");
    if (! current.existsAsFile())
        return;
    auto file = kitsFolder().getChildFile (current.loadFileAsString().trim());
    auto v = juce::JSON::parse (file);
    if (! v.isObject())
        return;
    name = v.getProperty ("name", name).toString();
    pads = {};
    std::map<juce::String, crate::SharedAudio> loaded;
    if (auto* list = v.getProperty ("pads", {}).getArray())
        for (auto& o : *list)
        {
            const int pad = o.getProperty ("pad", -1);
            const auto id = o["sound"].toString();
            auto* item = library.find (id);
            if (pad < 0 || pad >= kPads || item == nullptr)
                continue;
            auto& whole = loaded[id];
            if (whole == nullptr)
                whole = library.load (id);
            if (whole == nullptr)
                continue;
            assign (pad, *item, whole, (int) o.getProperty ("from", 0), (int) o.getProperty ("to", -1), o["name"].toString());
            auto& p = pads[(size_t) pad];
            p.volume = juce::jlimit (0, 100, (int) o.getProperty ("volume", 100));
            p.pan = juce::jlimit (0, 100, (int) o.getProperty ("pan", 50));
            p.tune = juce::jlimit (-120, 120, (int) o.getProperty ("tune", 0));
            const auto mode = o["mode"].toString();
            p.mode = mode == "mono" ? PadMode::mono : mode == "noteOff" ? PadMode::noteOff : PadMode::oneShot;
        }
}

//==============================================================================
Kit::Export Kit::exportForMpc (const juce::File& work) const
{
    Export out;
    mpc::Bank bank;
    bank.name = mpcName (name).toStdString();
    if (bank.name.empty())
        bank.name = "KIT";

    for (int i = 0; i < kPads; ++i)
    {
        auto& p = pads[(size_t) i];
        if (p.empty())
            continue;
        const auto& a = *p.audio;
        const int n = a.getNumSamples();
        mpc::BankPad bp;
        bp.pad = i;
        bp.sound.name = mpcName (p.name).toStdString();
        if (bp.sound.name.empty())
            bp.sound.name = "SOUND";
        bp.sound.left.resize ((size_t) n);
        for (int s = 0; s < n; ++s)
            bp.sound.left[(size_t) s] = toInt16 (a.getSample (0, s));
        if (a.getNumChannels() > 1)
        {
            bp.sound.right.resize ((size_t) n);
            for (int s = 0; s < n; ++s)
                bp.sound.right[(size_t) s] = toInt16 (a.getSample (1, s));
        }
        bp.sound.end = (std::uint32_t) n;
        bp.volume = (std::uint8_t) juce::jlimit (0, 100, p.volume);
        bp.pan = (std::uint8_t) juce::jlimit (0, 100, p.pan);
        bp.tune = (std::int16_t) juce::jlimit (-120, 120, p.tune);
        bp.voiceOverlap = overlapFor (p.mode);
        bank.pads.push_back (std::move (bp));
        ++out.pads;
    }
    if (bank.pads.empty())
    {
        out.error = "Put some sounds on pads first.";
        return out;
    }

    std::vector<std::string> taken;
    auto files = mpc::buildBank (bank, taken);
    work.createDirectory();
    auto result = mpc::writeBankFolder (std::filesystem::path (work.getFullPathName().toStdString()), files);
    if (! result.ok)
    {
        out.error = juce::String (result.error);
        return out;
    }
    out.bytes = result.memoryBytes;
    out.folderName = juce::String (files.folderName);
    out.folder = work.getChildFile (out.folderName);

    // The zip holds the kit's folder, so unzipping it on the card gives KITNAME/KITNAME.PGM and the .SND files.
    juce::ZipFile::Builder zip;
    for (auto& f : out.folder.findChildFiles (juce::File::findFiles, false))
        if (! f.getFileName().startsWith ("."))
            zip.addFile (f, 0, out.folderName + "/" + f.getFileName());
    out.zip = work.getChildFile (out.folderName + ".zip");
    out.zip.deleteFile();
    if (auto stream = out.zip.createOutputStream())
    {
        double progress = 0;
        zip.writeToStream (*stream, &progress);
        stream->flush();
    }
    if (! out.zip.existsAsFile() || out.zip.getSize() == 0)
    {
        out.error = "Couldn't make the zip file.";
        return out;
    }
    out.ok = true;
    return out;
}

juce::String Kit::verify (const Export& e) const
{
    if (! e.ok)
        return e.error;

    // From the zip, the way the Files app would unzip it onto a card.
    auto check = e.zip.getParentDirectory().getChildFile ("verify-" + e.folderName);
    check.deleteRecursively();
    juce::ZipFile zip (e.zip);
    if (! zip.uncompressTo (check).wasOk())
        return "The zip can't be unzipped.";
    auto folder = check.getChildFile (e.folderName);

    std::string why;
    auto program = mpc::readPgm (readBytes (folder.getChildFile (e.folderName + ".PGM")), &why);
    if (! program)
        return "The .PGM can't be read back: " + juce::String (why);
    if (program->name != mpcName (name).toStdString())
        return "The program name changed to " + juce::String (program->name);

    int found = 0;
    for (int i = 0; i < kPads; ++i)
    {
        auto& p = pads[(size_t) i];
        if (p.empty())
            continue;
        auto& note = program->noteForPad (i);
        if (note.soundIndex == mpc::kNoSound || note.soundIndex >= program->soundNames.size())
            return padLabel (i) + " has no sound in the program.";
        auto sndFile = folder.getChildFile (juce::String (program->soundNames[note.soundIndex]) + ".SND");
        auto snd = mpc::readSnd (readBytes (sndFile), &why);
        if (! snd)
            return sndFile.getFileName() + " can't be read back: " + juce::String (why);
        if ((int) snd->frameCount() != p.audio->getNumSamples())
            return sndFile.getFileName() + " has the wrong length.";
        if (note.voiceOverlap != overlapFor (p.mode) || note.tune != p.tune)
            return padLabel (i) + " lost its settings.";
        if (program->mixers[(size_t) (program->padToNote[(size_t) i] - mpc::kFirstNote)].volume != p.volume)
            return padLabel (i) + " lost its volume.";
        ++found;
    }
    check.deleteRecursively();
    return found == usedPads() ? juce::String() : juce::String ("Some pads are missing from the program.");
}
