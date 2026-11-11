#include "RigRecorder.h"

RigRecorder::RigRecorder()
{
    writerThread.startThread();
}

RigRecorder::~RigRecorder()
{
    if (getState() != State::idle)
    {
        // Closing with a take running: let the audio thread go, then save what there is.
        state = (int) State::finishing;
        finish();
    }
    writerThread.stopThread (2000);
}

bool RigRecorder::start (const juce::File& f, const juce::StringArray& stems, const juce::StringArray& tracks, double rate)
{
    if (getState() != State::idle || ! f.createDirectory())
        return false;

    folder = f;
    stemNames = stems;
    trackNames = tracks;
    sampleRate = rate;
    saved.clear();
    writers.clear();
    stemFiles.clear();
    numStems = stems.size();
    stemPeaks.reset (new std::atomic<float>[(size_t) juce::jmax (1, numStems)]);

    juce::WavAudioFormat wav;
    for (int i = 0; i < numStems; ++i)
    {
        const auto file = folder.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + " " + stems[i] + ".wav");
        stemPeaks[(size_t) i] = 0.0f;
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
        if (out == nullptr)
            return false;
        auto* writer = wav.createWriterFor (out.get(), rate, 2, 24, {}, 0);
        if (writer == nullptr)
            return false;
        out.release();   // the writer owns it now
        // About 4 seconds of room between the audio thread and the disk.
        writers.push_back (std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, writerThread, (int) (rate * 4)));
        stemFiles.push_back (file);
    }

    sequences.assign ((size_t) tracks.size(), {});
    eventFifo.reset();
    samplesWritten = 0;
    stopRequested = false;
    state = (int) State::armed;   // the audio thread may use the writers from here
    return true;
}

void RigRecorder::stop()
{
    if (getState() == State::armed || getState() == State::recording)
        stopRequested = true;
}

double RigRecorder::getRecordedSeconds() const
{
    return (double) samplesWritten.load() / juce::jmax (1.0, sampleRate);
}

void RigRecorder::addMidi (int track, const juce::MidiMessage& m, int sample, const Clock& clock)
{
    if (getState() != State::recording || m.getRawDataSize() > 3 || ! juce::isPositiveAndBelow (track, (int) sequences.size()))
        return;
    if (! (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isChannelPressure() || m.isAftertouch()))
        return;

    const double beat = clock.beat + sample * clock.bpm / (60.0 * clock.sampleRate) - startBeat.load();
    if (beat < 0)
        return;
    const auto scope = eventFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return;   // full: the message thread is behind, so this one is lost
    auto& e = events[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    e.track = track;
    e.beat = beat;
    e.size = m.getRawDataSize();
    std::memcpy (e.data, m.getRawData(), (size_t) e.size);
}

void RigRecorder::process (const Clock& clock, const std::vector<const juce::AudioBuffer<float>*>& stems, int numSamples)
{
    auto current = getState();
    if (current != State::armed && current != State::recording)
        return;

    if (stopRequested.load() || (current == State::recording && ! clock.playing))
    {
        state = (int) State::finishing;   // the writers are the message thread's again
        return;
    }

    int from = 0;
    if (current == State::armed)
    {
        if (! clock.playing)
            return;   // waits for the clock
        // The next bar line (or this one, if the block starts on it).
        const double beatsPerSample = clock.bpm / (60.0 * clock.sampleRate);
        const int bpb = juce::jmax (1, clock.beatsPerBar);
        const double bar = std::ceil (clock.beat / bpb - 1.0e-9) * bpb;
        const double offset = (bar - clock.beat) / beatsPerSample;
        if (offset >= numSamples)
            return;
        from = juce::jlimit (0, numSamples - 1, (int) std::round (offset));
        startBeat = bar;
        startBpm = clock.bpm;
        startBeatsPerBar = bpb;
        state = (int) State::recording;
    }

    const int count = numSamples - from;
    for (int i = 0; i < numStems && i < (int) stems.size(); ++i)
    {
        const auto* buffer = stems[(size_t) i];
        if (buffer == nullptr)
            continue;
        const float* channels[2] = { buffer->getReadPointer (0, from), buffer->getReadPointer (juce::jmin (1, buffer->getNumChannels() - 1), from) };
        writers[(size_t) i]->write (channels, count);
        const float peak = juce::jmax (buffer->getMagnitude (0, from, count), buffer->getMagnitude (juce::jmin (1, buffer->getNumChannels() - 1), from, count));
        if (peak > stemPeaks[(size_t) i].load())
            stemPeaks[(size_t) i] = peak;
    }
    samplesWritten += count;
}

bool RigRecorder::service()
{
    // Played notes, into their tracks.
    const auto scope = eventFifo.read (eventFifo.getNumReady());
    auto take = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& e = events[(size_t) i];
            juce::MidiMessage m (e.data, e.size, e.beat);
            sequences[(size_t) e.track].addEvent (m);
        }
    };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);

    if (getState() != State::finishing)
        return false;
    finish();
    return true;
}

void RigRecorder::finish()
{
    writers.clear();   // flushes and closes each WAV

    // Silent strips aren't worth a track in Logic.
    for (size_t i = 0; i < stemFiles.size(); ++i)
    {
        if (stemPeaks[i].load() < 1.0e-4f || samplesWritten.load() == 0)
            stemFiles[i].deleteFile();
        else
            saved.add (stemFiles[i]);
    }

    // What was played, a track for each tool, at the take's tempo (960 ticks a beat).
    constexpr int ticks = 960;
    const double bpm = startBpm.load();
    const int bpb = startBeatsPerBar.load();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticks);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
    midi.addTrack (tempo);
    bool anyNotes = false;
    for (size_t t = 0; t < sequences.size(); ++t)
    {
        auto& seq = sequences[t];
        if (seq.getNumEvents() == 0)
            continue;
        juce::MidiMessageSequence track;
        auto name = juce::MidiMessage::textMetaEvent (3, trackNames[(int) t]);
        track.addEvent (name, 0);
        for (const auto* e : seq)
        {
            auto m = e->message;
            m.setTimeStamp (std::round (m.getTimeStamp() * ticks));
            track.addEvent (m);
        }
        track.updateMatchedPairs();
        midi.addTrack (track);
        anyNotes = true;
    }
    if (anyNotes)
    {
        const auto file = folder.getChildFile ("Played.mid");
        if (auto out = file.createOutputStream())
            if (midi.writeTo (*out))
                saved.add (file);
    }

    const auto readMe = folder.getChildFile ("Open in Logic.txt");
    readMe.replaceWithText ("Recorded in Kaleidobox, " + juce::Time::getCurrentTime().toString (true, true) + ".\n\n"
                            "In Logic: set the project tempo to " + juce::String (bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                            + " BPM (" + juce::String (bpb) + "/4), then drag every file here in at bar 1. They all start on the same beat.\n"
                            "The WAVs are each tool as you heard it (after its effects and fader); Mix is everything together.\n"
                            "Played.mid is what you played on the keys and pads, a track for each tool.\n");
    saved.add (readMe);

    sequences.clear();
    stopRequested = false;
    state = (int) State::idle;
}
