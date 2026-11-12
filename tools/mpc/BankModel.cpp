#include "BankModel.h"

#include "core/AudioFiles.h"
#include "core/SoundFx.h"

#include <juce_data_structures/juce_data_structures.h>

namespace SoundFx = crate::SoundFx;

double Bank::beatsFor (int index) const
{
    const auto& pad = pads[(size_t) index];
    if (! keepInTime || pad.playNormal || pad.isEmpty())
        return 0;
    return pad.beats > 0 ? pad.beats : SoundFx::guessBeats (pad.audio->getNumSamples(), bpm);
}

SharedAudio Bank::audioToPlay (int index) const
{
    const auto& pad = pads[(size_t) index];
    const auto beats = beatsFor (index);

    if (beats <= 0)
        return pad.audio;

    if (pad.timed == nullptr || pad.timedSource != pad.audio.get() || std::abs (pad.timedBeats - beats) > 1.0e-9 || std::abs (pad.timedBpm - bpm) > 1.0e-9)
    {
        pad.timed = SoundFx::fitToTempo (pad.audio, beats, bpm);
        pad.timedSource = pad.audio.get();
        pad.timedBeats = beats;
        pad.timedBpm = bpm;
    }

    return pad.timed;
}

size_t Bank::memoryBytes (int index) const
{
    const auto& pad = pads[(size_t) index];
    if (pad.isEmpty())
        return 0;

    // A fitted pad's length follows from the tempo, so no need to stretch it just to measure.
    const auto beats = beatsFor (index);
    const auto frames = beats > 0 ? (size_t) std::round (beats * 60.0 / bpm * kMpcSampleRate)
                                  : (size_t) pad.audio->getNumSamples();
    return frames * (size_t) pad.channelsOnMpc() * 2;
}

size_t Bank::memoryBytes() const
{
    size_t total = 0;
    for (int i = 0; i < mpc::kPadCount; ++i)
        total += memoryBytes (i);
    return total;
}

int Bank::usedPads() const
{
    int n = 0;
    for (const auto& p : pads)
        n += p.isEmpty() ? 0 : 1;
    return n;
}

juce::String padLabel (int pad)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + pad / kPadsPerBank))
         + juce::String (pad % kPadsPerBank + 1).paddedLeft ('0', 2);
}

int noteForPad (int pad)
{
    static const auto map = mpc::Program::defaultPadToNote();
    return map[(size_t) juce::jlimit (0, mpc::kPadCount - 1, pad)];
}

int padForNote (int note)
{
    for (int pad = 0; pad < mpc::kPadCount; ++pad)
        if (noteForPad (pad) == note)
            return pad;
    return -1;
}

Project::Project()
{
    addBank();
    addSequence();
    addSong();
}

void Project::setCurrentSong (int index)
{
    if (juce::isPositiveAndBelow (index, (int) songs.size()))
    {
        currentSongIndex = index;
        sequenceChanged();
    }
}

int Project::addSong (const juce::String& name)
{
    seq::Song s;
    s.name = name.isNotEmpty() ? name : "Song " + juce::String ((int) songs.size() + 1);
    songs.push_back (std::move (s));
    currentSongIndex = (int) songs.size() - 1;
    sequenceChanged();
    return currentSongIndex;
}

void Project::removeSong (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) songs.size()))
        return;
    songs.erase (songs.begin() + index);
    if (songs.empty())
        addSong();
    currentSongIndex = juce::jlimit (0, (int) songs.size() - 1, currentSongIndex);
    sequenceChanged();
}

void Project::setCurrent (int index)
{
    current = juce::jlimit (0, (int) banks.size() - 1, index);
    changed();
}

int Project::addBank (const juce::String& name)
{
    Bank bank;
    bank.name = name.isNotEmpty() ? name : "Kit " + juce::String ((int) banks.size() + 1);
    banks.push_back (std::move (bank));
    current = (int) banks.size() - 1;
    changed();
    return current;
}

void Project::setCurrentSequence (int index)
{
    if (juce::isPositiveAndBelow (index, (int) sequences.size()) && index != currentSeq)
    {
        currentSeq = index;
        sequenceChanged();
    }
}

int Project::addSequence (const juce::String& name)
{
    seq::Sequence s;
    s.name = name.isNotEmpty() ? name : "Sequence " + juce::String ((int) sequences.size() + 1);
    if (! banks.empty())
        s.bpm = currentBank().bpm;
    sequences.push_back (std::move (s));
    currentSeq = (int) sequences.size() - 1;
    sequenceChanged();
    return currentSeq;
}

void Project::removeSequence (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) sequences.size()))
        return;
    sequences.erase (sequences.begin() + index);
    for (auto& song : songs)
        song.sequenceRemoved (index);
    if (sequences.empty())
        addSequence();
    currentSeq = juce::jlimit (0, (int) sequences.size() - 1, currentSeq);
    sequenceChanged();
}

void Project::removeBank (int index)
{
    if (index < 0 || index >= (int) banks.size())
        return;

    banks.erase (banks.begin() + index);

    if (banks.empty())
        addBank();

    current = juce::jlimit (0, (int) banks.size() - 1, current);
    changed();
}

juce::MemoryBlock Project::save() const
{
    juce::ValueTree root ("MpcProject");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("current", current, nullptr);
    root.setProperty ("ram", ramMegabytes, nullptr);

    juce::ValueTree sounds ("Sounds");
    std::map<const void*, int> soundIndex;

    for (const auto& bank : banks)
    {
        juce::ValueTree b ("Bank");
        b.setProperty ("name", bank.name, nullptr);
        b.setProperty ("bpm", bank.bpm, nullptr);
        b.setProperty ("keepInTime", bank.keepInTime, nullptr);
        b.setProperty ("delayBeats", bank.delayBeats, nullptr);
        b.setProperty ("delayFeedback", bank.delayFeedback, nullptr);
        b.setProperty ("reverbSize", bank.reverbSize, nullptr);

        for (int i = 0; i < mpc::kPadCount; ++i)
        {
            const auto& pad = bank.pads[(size_t) i];
            if (pad.isEmpty())
                continue;

            auto [it, added] = soundIndex.try_emplace (pad.audio.get(), sounds.getNumChildren());
            if (added)
            {
                juce::ValueTree sound ("Sound");
                sound.setProperty ("wav", crate::AudioFiles::toWavData (*pad.audio), nullptr);
                sounds.appendChild (sound, nullptr);
            }

            juce::ValueTree p ("Pad");
            p.setProperty ("index", i, nullptr);
            p.setProperty ("sound", it->second, nullptr);
            p.setProperty ("name", pad.name, nullptr);
            p.setProperty ("volume", pad.volume, nullptr);
            p.setProperty ("pan", pad.pan, nullptr);
            p.setProperty ("tune", pad.tune, nullptr);
            p.setProperty ("mode", (int) pad.mode, nullptr);
            p.setProperty ("mono", pad.mono, nullptr);
            p.setProperty ("reverse", pad.reverse, nullptr);
            p.setProperty ("normalize", pad.normalize, nullptr);
            p.setProperty ("beats", pad.beats, nullptr);
            p.setProperty ("playNormal", pad.playNormal, nullptr);
            p.setProperty ("muteGroup", pad.muteGroup, nullptr);
            p.setProperty ("cutoff", pad.cutoff, nullptr);
            p.setProperty ("resonance", pad.resonance, nullptr);
            p.setProperty ("attack", pad.attack, nullptr);
            p.setProperty ("decay", pad.decay, nullptr);
            p.setProperty ("delaySend", pad.delaySend, nullptr);
            p.setProperty ("reverbSend", pad.reverbSend, nullptr);
            b.appendChild (p, nullptr);
        }

        root.appendChild (b, nullptr);
    }

    root.appendChild (sounds, nullptr);

    juce::ValueTree seqs ("Sequences");
    seqs.setProperty ("current", currentSeq, nullptr);
    for (const auto& sq : sequences)
        seqs.appendChild (sq.toValueTree(), nullptr);
    root.appendChild (seqs, nullptr);

    juce::ValueTree songTree ("Songs");
    songTree.setProperty ("current", currentSongIndex, nullptr);
    for (const auto& song : songs)
        songTree.appendChild (song.toValueTree(), nullptr);
    root.appendChild (songTree, nullptr);

    juce::MemoryOutputStream out;
    root.writeToStream (out);
    return out.getMemoryBlock();
}

bool Project::load (const void* data, size_t size)
{
    const auto root = juce::ValueTree::readFromData (data, size);
    if (! root.hasType ("MpcProject"))
        return false;

    std::vector<SharedAudio> sounds;
    for (const auto& sound : root.getChildWithName ("Sounds"))
        if (const auto* wav = sound.getProperty ("wav").getBinaryData())
            sounds.push_back (crate::AudioFiles::fromWavData (wav->getData(), wav->getSize()));
        else
            sounds.push_back (nullptr);

    std::vector<Bank> loaded;
    for (const auto& b : root)
    {
        if (! b.hasType ("Bank"))
            continue;

        Bank bank;
        bank.name = b["name"].toString();
        bank.bpm = juce::jlimit (40.0, 240.0, (double) b.getProperty ("bpm", 90.0));
        bank.keepInTime = b["keepInTime"];
        bank.delayBeats = juce::jlimit (0.125, 4.0, (double) b.getProperty ("delayBeats", 0.75));
        bank.delayFeedback = juce::jlimit (0, 90, (int) b.getProperty ("delayFeedback", 35));
        bank.reverbSize = juce::jlimit (0, 100, (int) b.getProperty ("reverbSize", 50));

        for (const auto& p : b)
        {
            const int index = p["index"];
            const int sound = p["sound"];
            if (! juce::isPositiveAndBelow (index, mpc::kPadCount) || ! juce::isPositiveAndBelow (sound, (int) sounds.size()))
                continue;

            auto& pad = bank.pads[(size_t) index];
            pad.audio = sounds[(size_t) sound];
            pad.name = p["name"].toString();
            pad.volume = juce::jlimit (0, 100, (int) p.getProperty ("volume", 100));
            pad.pan = juce::jlimit (0, 100, (int) p.getProperty ("pan", 50));
            pad.tune = juce::jlimit (-120, 120, (int) p["tune"]);
            pad.mode = (PadMode) juce::jlimit (0, 2, (int) p["mode"]);
            pad.mono = p["mono"];
            pad.reverse = p["reverse"];
            pad.normalize = p["normalize"];
            pad.beats = p["beats"];
            pad.playNormal = p["playNormal"];
            pad.muteGroup = juce::jlimit (0, 32, (int) p["muteGroup"]);
            pad.cutoff = juce::jlimit (20.0f, 20000.0f, (float) p.getProperty ("cutoff", 20000.0f));
            pad.resonance = juce::jlimit (0.0f, 1.0f, (float) p.getProperty ("resonance", 0.0f));
            pad.attack = juce::jlimit (0, 2000, (int) p["attack"]);
            pad.decay = juce::jlimit (0, 5000, (int) p["decay"]);
            pad.delaySend = juce::jlimit (0, 100, (int) p["delaySend"]);
            pad.reverbSend = juce::jlimit (0, 100, (int) p["reverbSend"]);
        }

        loaded.push_back (std::move (bank));
    }

    if (loaded.empty())
        return false;

    banks = std::move (loaded);
    current = juce::jlimit (0, (int) banks.size() - 1, (int) root["current"]);

    sequences.clear();
    const auto seqs = root.getChildWithName ("Sequences");
    for (const auto& sq : seqs)
        if (sq.hasType ("Sequence"))
            sequences.push_back (seq::Sequence::fromValueTree (sq));
    if (sequences.empty())
    {
        sequences.emplace_back();
        sequences.back().name = "Sequence 1";
        sequences.back().bpm = banks[(size_t) current].bpm;
    }
    currentSeq = juce::jlimit (0, (int) sequences.size() - 1, (int) seqs["current"]);

    songs.clear();
    const auto songTree = root.getChildWithName ("Songs");
    for (const auto& song : songTree)
        if (song.hasType ("Song"))
            songs.push_back (seq::Song::fromValueTree (song, (int) sequences.size()));
    if (songs.empty())
    {
        songs.emplace_back();
        songs.back().name = "Song 1";
    }
    currentSongIndex = juce::jlimit (0, (int) songs.size() - 1, (int) songTree["current"]);
    ramMegabytes = juce::jlimit (2, 32, (int) root.getProperty ("ram", 32));
    changed();
    return true;
}
