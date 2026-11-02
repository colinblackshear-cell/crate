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
            b.appendChild (p, nullptr);
        }

        root.appendChild (b, nullptr);
    }

    root.appendChild (sounds, nullptr);

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
        }

        loaded.push_back (std::move (bank));
    }

    if (loaded.empty())
        return false;

    banks = std::move (loaded);
    current = juce::jlimit (0, (int) banks.size() - 1, (int) root["current"]);
    ramMegabytes = juce::jlimit (2, 32, (int) root.getProperty ("ram", 32));
    changed();
    return true;
}
