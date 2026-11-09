#include "RigExport.h"

#include "RigEngine.h"

namespace
{
constexpr int kHoldBars = 4;   // a part with no length ("hold") is exported this long

// What a fresh copy of the effects needs to know the tempo (for the delay and the repeat).
struct FixedTempo : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (true);
        return info;
    }
};

// The loop's sound at a beat of it (0 .. lengthBeats), read the way the looper plays it.
float sampleAt (const juce::AudioBuffer<float>& audio, double lengthBeats, int ch, double beat)
{
    const int n = audio.getNumSamples();
    if (n < 2 || lengthBeats <= 0)
        return 0.0f;
    double p = std::fmod (beat, lengthBeats);
    if (p < 0)
        p += lengthBeats;
    const double pos = p / lengthBeats * n;
    const int i0 = juce::jlimit (0, n - 1, (int) pos);
    const int i1 = (i0 + 1) % n;
    const float frac = (float) (pos - i0);
    const float* d = audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1));
    return d[i0] + (d[i1] - d[i0]) * frac;
}

// Runs `buffer` through a fresh copy of the layer's built-in effects (not its plugin).
// `warmUp` is played through first and thrown away, so a loop's echoes carry round.
void applyEffects (RigEngine& engine, int layer, double bpm, double rate, juce::AudioBuffer<float>& buffer,
                   const juce::AudioBuffer<float>* warmUp)
{
    auto& source = engine.getLoopFx (layer);
    if (! source.isActive())
        return;
    StripFx fx;
    for (auto* p : source.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (auto* target = fx.params.getParameter (withId->paramID))
                target->setValueNotifyingHost (p->getValue());
    FixedTempo tempo;
    tempo.bpm = bpm;
    fx.setPlayHead (&tempo);
    const int block = 512;
    fx.setRateAndBufferSizeDetails (rate, block);
    fx.prepareToPlay (rate, block);
    juce::MidiBuffer none;
    const auto run = [&] (juce::AudioBuffer<float>& b)
    {
        for (int start = 0; start < b.getNumSamples(); start += block)
        {
            const int n = juce::jmin (block, b.getNumSamples() - start);
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, start, n);
            fx.processBlock (view, none);
        }
    };
    if (warmUp != nullptr)
    {
        juce::AudioBuffer<float> copy (*warmUp);
        run (copy);
    }
    run (buffer);
    fx.setPlayHead (nullptr);
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
    if (auto writer = wav.createWriterFor (stream, options))
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    return false;
}

juce::String safeName (juce::String s)
{
    return s.removeCharacters ("/\\:*?\"<>|").trim();
}

bool hasLoop (RigEngine& engine, int layer)
{
    const auto state = engine.getLooper().getState (layer);
    return state == RigLooper::State::playing || state == RigLooper::State::overdubbing;
}
}

juce::File RigExport::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig").getChildFile ("Exports");
}

juce::AudioBuffer<float> RigExport::renderLoop (RigEngine& engine, int layer, double bpm, double rate, bool withEffects)
{
    const auto audio = engine.getLooper().copyLayerAudio (layer);
    const double beats = engine.getLooper().getLayer (layer).lengthBeats.load();
    if (audio.getNumSamples() == 0 || beats <= 0)
        return {};
    const double samplesPerBeat = rate * 60.0 / bpm;
    const int n = juce::jmax (1, (int) std::round (beats * samplesPerBeat));
    juce::AudioBuffer<float> out (2, n);
    const float level = engine.getLooper().getLayer (layer).level.load();
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch)
            out.setSample (ch, i, sampleAt (audio, beats, ch, i / samplesPerBeat) * (withEffects ? level : 1.0f));
    if (withEffects)
    {
        const juce::AudioBuffer<float> warm (out);
        applyEffects (engine, layer, bpm, rate, out, &warm);
    }
    return out;
}

int RigExport::songBars (RigEngine& engine)
{
    int bars = 0;
    for (const auto& part : engine.getScenes().getScenes())
        bars += part.bars > 0 ? part.bars : kHoldBars;
    return bars;
}

juce::AudioBuffer<float> RigExport::renderSongStem (RigEngine& engine, int layer, double bpm, double rate, bool withEffects)
{
    const auto& parts = engine.getScenes().getScenes();
    const auto audio = engine.getLooper().copyLayerAudio (layer);
    const double beats = engine.getLooper().getLayer (layer).lengthBeats.load();
    if (parts.empty() || audio.getNumSamples() == 0 || beats <= 0)
        return {};
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const double samplesPerBeat = rate * 60.0 / bpm;
    const int total = (int) std::round (songBars (engine) * bpb * samplesPerBeat);
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    const float level = withEffects ? engine.getLooper().getLayer (layer).level.load() : 1.0f;

    // Like the Rig: a part leaves a loop as it was (-1), mutes it, or plays it; a loop that
    // comes in with a part starts from its top on the part's first beat.
    bool on = ! engine.getLooper().getLayer (layer).mute.load();
    double startBeat = 0, partBeat = 0;
    for (const auto& part : parts)
    {
        const int value = part.state.loopMutes[(size_t) layer];
        if (value == 0 && ! on)
            startBeat = partBeat;
        if (value >= 0)
            on = value == 0;
        const double partBeats = (part.bars > 0 ? part.bars : kHoldBars) * bpb;
        if (on)
        {
            const int from = (int) std::round (partBeat * samplesPerBeat);
            const int to = juce::jmin (total, (int) std::round ((partBeat + partBeats) * samplesPerBeat));
            for (int i = from; i < to; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    out.setSample (ch, i, sampleAt (audio, beats, ch, i / samplesPerBeat - startBeat) * level);
            // A short fade where it stops, so it doesn't click.
            const int fade = juce::jmin (96, (to - from) / 4);
            if (fade > 0 && to < total)
                for (int ch = 0; ch < 2; ++ch)
                    out.applyGainRamp (ch, to - fade, fade, 1.0f, 0.0f);
        }
        partBeat += partBeats;
    }
    if (withEffects)
        applyEffects (engine, layer, bpm, rate, out, nullptr);
    return out;
}

int RigExport::timelineBars (RigEngine& engine)
{
    return (int) std::ceil (engine.getTimeline().lengthBars() - 1.0e-6);
}

juce::AudioBuffer<float> RigExport::renderTimelineStem (RigEngine& engine, int layer, double bpm, double rate, bool withEffects)
{
    const auto& timeline = engine.getTimeline();
    const auto audio = engine.getLooper().copyLayerAudio (layer);
    const double beats = engine.getLooper().getLayer (layer).lengthBeats.load();
    const int lane = RigTimeline::laneForLoop (layer);
    if (timeline.isEmpty() || audio.getNumSamples() == 0 || beats <= 0)
        return {};
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const double samplesPerBeat = rate * 60.0 / bpm;
    const int total = (int) std::round (timelineBars (engine) * bpb * samplesPerBeat);
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    const float level = withEffects ? engine.getLooper().getLayer (layer).level.load() : 1.0f;
    const auto& blocks = timeline.getBlocks();
    const bool used = timeline.laneUsed (lane);
    // Like the Rig: the loop starts from its top at each block, and moves smoothly to a new
    // level (a few milliseconds where a block starts or stops without a fade, so it can't click).
    const float slew = 1.0f / 96.0f;
    float gain = 0;
    for (int i = 0; i < total; ++i)
    {
        const double beat = i / samplesPerBeat;
        const double bar = beat / bpb;
        const float target = RigTimeline::gainAt (blocks.data(), (int) blocks.size(), lane, bar, used);
        gain = target > gain ? juce::jmin (target, gain + slew) : juce::jmax (target, gain - slew);
        if (gain <= 0.0f)
            continue;
        const double start = RigTimeline::blockStartAt (blocks.data(), (int) blocks.size(), lane, bar);
        const double from = start >= 0 ? start * bpb : 0.0;
        for (int ch = 0; ch < 2; ++ch)
            out.setSample (ch, i, sampleAt (audio, beats, ch, beat - from) * level * gain);
    }
    if (withEffects)
        applyEffects (engine, layer, bpm, rate, out, nullptr);
    return out;
}

RigExport::Result RigExport::exportForLogic (RigEngine& engine, const juce::File& parent)
{
    Result result;
    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    const double bpm = engine.getTransport().getBpm();
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    bool anyLoop = false;
    for (int i = 0; i < RigLooper::kLayers; ++i)
        anyLoop = anyLoop || hasLoop (engine, i);
    if (! anyLoop)
    {
        result.error = "There are no loops to export yet.";
        return result;
    }

    result.folder = parent.getNonexistentChildFile ("Rig export " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M"), {}, false);
    if (! result.folder.createDirectory())
    {
        result.error = "Couldn't make the folder " + result.folder.getFullPathName();
        return result;
    }
    const juce::String tempoText = juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1) + " BPM";
    bool ok = true;

    const auto loops = result.folder.getChildFile ("Loops");
    loops.createDirectory();
    const auto dryLoops = loops.getChildFile ("Dry");
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        if (! hasLoop (engine, i))
            continue;
        const double beats = engine.getLooper().getLayer (i).lengthBeats.load();
        const double bars = beats / bpb;
        const juce::String name = "Loop " + juce::String (i + 1) + " - " + safeName (RigLooper::sourceName (engine.getLooper().getLayer (i).source.load()))
                                + " - " + juce::String (bars, std::abs (bars - std::round (bars)) < 0.01 ? 0 : 2) + " bars - " + tempoText;
        ok = writeWav (loops.getChildFile (name + ".wav"), renderLoop (engine, i, bpm, rate, true), rate) && ok;
        dryLoops.createDirectory();
        ok = writeWav (dryLoops.getChildFile (name + " (dry).wav"), renderLoop (engine, i, bpm, rate, false), rate) && ok;
        result.files += 2;
    }

    const auto& parts = engine.getScenes().getScenes();
    const auto& timeline = engine.getTimeline();
    const bool useTimeline = ! timeline.isEmpty();
    if (useTimeline)
    {
        const auto song = result.folder.getChildFile ("Song");
        song.createDirectory();
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            if (! hasLoop (engine, i) || ! timeline.laneUsed (RigTimeline::laneForLoop (i)))
                continue;
            ok = writeWav (song.getChildFile ("Song - Loop " + juce::String (i + 1) + " - " + tempoText + ".wav"),
                           renderTimelineStem (engine, i, bpm, rate, true), rate) && ok;
            ++result.files;
        }
        juce::MidiMessageSequence track;
        const int ppq = 960;
        track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
        for (const auto& m : timeline.getMarkers())
            track.addEvent (juce::MidiMessage::textMetaEvent (6, m.name), m.bar * bpb * ppq);
        track.addEvent (juce::MidiMessage::endOfTrack(), timelineBars (engine) * bpb * ppq);
        track.sort();
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ppq);
        midi.addTrack (track);
        if (juce::FileOutputStream stream (song.getChildFile ("Song markers.mid")); stream.openedOk())
            ok = midi.writeTo (stream) && ok;
        ++result.files;
    }
    else if (! parts.empty())
    {
        const auto song = result.folder.getChildFile ("Song");
        song.createDirectory();
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            if (! hasLoop (engine, i))
                continue;
            ok = writeWav (song.getChildFile ("Song - Loop " + juce::String (i + 1) + " - " + tempoText + ".wav"),
                           renderSongStem (engine, i, bpm, rate, true), rate) && ok;
            ++result.files;
        }

        // Tempo and part markers, for Logic.
        juce::MidiMessageSequence track;
        const int ppq = 960;
        track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
        double beat = 0;
        for (const auto& part : parts)
        {
            auto marker = juce::MidiMessage::textMetaEvent (6, part.name);
            track.addEvent (marker, beat * ppq);
            beat += (part.bars > 0 ? part.bars : kHoldBars) * bpb;
        }
        track.addEvent (juce::MidiMessage::endOfTrack(), beat * ppq);
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ppq);
        midi.addTrack (track);
        const auto midiFile = song.getChildFile ("Song markers.mid");
        if (juce::FileOutputStream stream (midiFile); stream.openedOk())
            ok = midi.writeTo (stream) && ok;
        ++result.files;
    }

    juce::String notes;
    notes << "Exported from Crate Rig, " << juce::Time::getCurrentTime().toString (true, true) << "\n\n"
          << "Tempo: " << tempoText << ", " << bpb << "/4\n\n"
          << "Set Logic's project to this tempo, then drag the files in at bar 1. Every file starts on bar 1.\n"
          << "Loops: each loop once (as heard, and dry). Loop them in Logic.\n";
    if (useTimeline)
    {
        notes << "Song: each loop on the timeline, the whole song long, playing in its blocks (with their fades).\n"
              << "Song markers.mid: drag it in too for the tempo and the timeline's markers:\n";
        for (const auto& m : timeline.getMarkers())
            notes << "  bar " << juce::String (m.bar + 1.0, std::abs (m.bar - std::round (m.bar)) < 0.01 ? 0 : 2) << ": " << m.name << "\n";
        notes << "The tools (MPC, Bass, Synth, vocals, Input) play live, so they aren't in these files: in the Timeline, "
                 "Record song plays the song once and records every tool as its own WAV.\n";
    }
    else if (! parts.empty())
    {
        notes << "Song: each loop laid out over the whole song, playing in the parts where it's on.\n"
              << "Song markers.mid: drag it in too for the tempo and a marker for each part:\n";
        double bar = 1;
        for (const auto& part : parts)
        {
            notes << "  bar " << (int) bar << ": " << part.name << "\n";
            bar += part.bars > 0 ? part.bars : kHoldBars;
        }
        notes << "(A part set to hold is exported 4 bars long.)\n";
    }
    notes << "\nYour own plugins on a loop aren't in these files; use Record set for those.\n";
    ok = result.folder.getChildFile ("Read me.txt").replaceWithText (notes) && ok;
    if (! ok)
        result.error = "Some files couldn't be written.";
    return result;
}
