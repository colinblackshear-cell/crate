// The overdub/undo benchmark: what the audio thread's work costs when an overdub starts and is
// undone, on long loops, at every buffer size and sample rate asked for.
//
//   CrateTests --overdub-undo-bench synthetic [--minutes M] [--out report.txt]
//   CrateTests --overdub-undo-bench device    [--minutes M] [--out report.txt]
//   CrateTests --overdub-undo-bench all       [--minutes M] [--out report.txt]
//
// Two parts, reported separately and never mixed:
//
// SYNTHETIC: the looper driven by a test callback, block after block as fast as it goes, at
// 16, 32, 64, 128 and 256 samples and 44.1, 48, 96 and 192 kHz. It shows what the code costs.
// DEVICE: the real audio device driving the looper, at only the sizes and rates the device
// itself lists. It shows what a player would get. (The output is silent: the loops are played
// into the block and then cleared, so nothing loud comes out.)
//
// Each case is one of: overdub start, immediate undo, undo while the capture runs, undo after it
// (by swap, and by restoration when the undo room is bigger than the loop's buffer), each alone
// and with a save copying the loops at the same time. For each: the blocks' wall-clock and
// audio-thread CPU time (median, 99th percentile, most), late blocks (longer than they play
// for), loop blocks missed (a layer skipped because its lock was busy), pages copied that had
// to be (mandatory) and in order (discretionary), and memory.

#include "OverdubUndoHelpers.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <fstream>
#include <iostream>
#include <thread>

#if JUCE_LINUX
 #include <fstream>
#endif
#if JUCE_MAC
 #include <mach/mach.h>
 #include <sys/resource.h>
#endif

namespace
{
constexpr int kBuffers[] = { 16, 32, 64, 128, 256 };
constexpr double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
constexpr int kToneBins[] = { 6, 11, 17, 23 };

// Peak resident memory (MB): since the last reset where the system allows it (Linux), else
// since the process started.
double peakMemoryMB()
{
#if JUCE_LINUX
    std::ifstream f ("/proc/self/status");
    std::string line;
    while (std::getline (f, line))
        if (line.rfind ("VmHWM:", 0) == 0)
            return std::atof (line.c_str() + 6) / 1024.0;
    return -1;
#elif JUCE_MAC
    rusage u {};
    getrusage (RUSAGE_SELF, &u);
    return u.ru_maxrss / 1048576.0;
#else
    return -1;
#endif
}
void resetPeakMemory()
{
#if JUCE_LINUX
    std::ofstream ("/proc/self/clear_refs") << "5";
#endif
}

struct Result
{
    juce::String part, kase;
    double rate = 0;
    int block = 0;
    odu::Times wall, cpu;
    int late = 0;
    juce::int64 blocks = 0, missing = 0;
    juce::int64 mandatoryBytes = 0, discretionaryBytes = 0, wholeBytes = 0;
    int maxMandatoryPages = 0, maxDiscretionaryPages = 0;
    int swaps = 0, restorations = 0, roomsMost = 0;
    double peakMB = 0;
    bool undoOffered = false, exact = true;
};

juce::String row (const Result& r)
{
    auto f = [] (double v, int d = 1) { return juce::String (v, d); };
    juce::String path = r.wholeBytes > 0 ? "whole copy" : r.swaps > 0 ? "swap" : r.restorations > 0 ? "restoration" : "-";
    return juce::String (r.rate / 1000.0, 1).paddedLeft (' ', 5) + " " + juce::String (r.block).paddedLeft (' ', 4) + "  "
           + r.kase.paddedRight (' ', 30) + f (r.wall.median()).paddedLeft (' ', 8) + f (r.wall.p99()).paddedLeft (' ', 9)
           + f (r.wall.most()).paddedLeft (' ', 10) + f (r.cpu.median()).paddedLeft (' ', 8) + f (r.cpu.p99()).paddedLeft (' ', 9)
           + f (r.cpu.most()).paddedLeft (' ', 10) + juce::String (r.late).paddedLeft (' ', 6) + juce::String (r.missing).paddedLeft (' ', 6)
           + juce::String (r.mandatoryBytes / 1024).paddedLeft (' ', 10) + juce::String (r.discretionaryBytes / 1024).paddedLeft (' ', 10)
           + juce::String (r.wholeBytes / 1024).paddedLeft (' ', 10) + juce::String (r.maxMandatoryPages).paddedLeft (' ', 5)
           + juce::String (r.maxDiscretionaryPages).paddedLeft (' ', 6) + juce::String (r.roomsMost).paddedLeft (' ', 4)
           + juce::String (juce::roundToInt (r.peakMB)).paddedLeft (' ', 7) + "  " + path + (r.undoOffered ? "" : " (no undo)") + (r.exact ? "" : " NOT EXACT");
}

juce::String header()
{
    return "  kHz  buf  case                            wall us: med      p99       max  cpu us: med   p99      max  late  miss  mand KB  disc KB  whole KB mxMd mxDsc rms  pkMB  undo path";
}

// The loops: 0 is the longest (its buffer is the undo room's size: undo by swap); 1 is 5%
// shorter (the room is bigger than its buffer: undo by restoration); 2 and 3 are short tones.
struct Loops
{
    std::array<juce::AudioBuffer<float>, 4> audio;
    Loops (double rate, double minutes)
    {
        const int big = (int) (rate * 60.0 * minutes) / 256 * 256;
        audio[0] = odu::toneLoop (big, kToneBins[0]);
        audio[1] = odu::toneLoop ((int) (big * 0.95) / 256 * 256, kToneBins[1]);
        audio[2] = odu::toneLoop ((int) rate * 8 / 256 * 256, kToneBins[2]);
        audio[3] = odu::toneLoop ((int) rate * 6 / 256 * 256, kToneBins[3]);
    }
};

enum class Kase { start, immediate, partial, completeSwap, completeRestore };
const char* kaseName (Kase k)
{
    switch (k)
    {
        case Kase::start: return "overdub start";
        case Kase::immediate: return "immediate undo";
        case Kase::partial: return "undo, capture half done";
        case Kase::completeSwap: return "undo, capture done (swap)";
        case Kase::completeRestore: return "undo, capture done (restore)";
    }
    return "";
}

// Something that drives the looper's blocks: the test callback or the device. `run` plays
// `seconds` of audio, measuring every block if `measure`.
struct Driver
{
    virtual ~Driver() = default;
    virtual RigLooper& looper() = 0;
    virtual void run (double seconds, bool measure) = 0;
    virtual void oneBlock() = 0;   // at least one block (so a press is handled before an undo)
    virtual void collect (Result&) = 0;
    double rate = 48000.0;
    int block = 256;
};

struct SyntheticDriver : Driver
{
    std::unique_ptr<odu::Drive> d;
    SyntheticDriver (double r, int b)
    {
        rate = r;
        block = b;
        d = std::make_unique<odu::Drive> (r, b, 120.0);
        d->input = [] (juce::int64, int) { return 1.0f / 1024.0f; };
    }
    RigLooper& looper() override { return d->looper; }
    void run (double seconds, bool measure) override
    {
        d->timing = measure;
        d->seconds (seconds);
        d->timing = false;
    }
    void oneBlock() override { d->block(); }
    void collect (Result& r) override
    {
        r.wall = d->wall;
        r.cpu = d->cpu;
        r.late = d->late;
        d->resetTiming();
    }
};

// The device: its callback plays the looper; the main thread asks for presses and undos and
// waits in real time.
struct DeviceDriver : Driver, juce::AudioIODeviceCallback
{
    RigLooper l;
    juce::AudioDeviceManager& manager;
    juce::AudioBuffer<float> in, out;
    std::atomic<double> beat { 0 };
    std::atomic<bool> measuring { false };
    juce::CriticalSection timesLock;
    odu::Times wall, cpu;
    std::atomic<int> late { 0 };

    DeviceDriver (juce::AudioDeviceManager& m, double r, int b) : manager (m)
    {
        rate = r;
        block = b;
        l.prepare (r, b);
        in.setSize (2, juce::jmax (b, 4096));
        out.setSize (2, juce::jmax (b, 4096));
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (in.getWritePointer (ch), 1.0f / 1024.0f, in.getNumSamples());
        wall.v.reserve (1 << 20);
        cpu.v.reserve (1 << 20);
    }
    RigLooper& looper() override { return l; }
    void start() { manager.addAudioCallback (this); }
    void stop() { manager.removeAudioCallback (this); }
    void run (double seconds, bool measure) override
    {
        measuring = measure;
        juce::Thread::sleep ((int) (seconds * 1000.0));
        measuring = false;
    }
    void oneBlock() override { juce::Thread::sleep (juce::jmax (2, (int) std::ceil (3000.0 * block / rate))); }
    void collect (Result& r) override
    {
        const juce::ScopedLock sl (timesLock);
        r.wall = wall;
        r.cpu = cpu;
        r.late = late.exchange (0);
        wall.v.clear();
        cpu.v.clear();
    }
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        n = juce::jmin (n, out.getNumSamples());
        const double w0 = juce::Time::getMillisecondCounterHiRes();
        const double c0 = odu::threadCpuMicros();
        RigLooper::Clock clock;
        clock.beat = beat.load();
        clock.bpm = 120.0;
        clock.playing = true;
        juce::AudioBuffer<float> inView (in.getArrayOfWritePointers(), 2, n), outView (out.getArrayOfWritePointers(), 2, n);
        std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
        sources[RigLooper::input] = &inView;
        l.process (clock, sources, outView);
        beat = clock.beat + n * 120.0 / (60.0 * rate);
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], n);   // silent
        if (measuring)
        {
            const double us = (juce::Time::getMillisecondCounterHiRes() - w0) * 1000.0;
            const double c = odu::threadCpuMicros() - c0;
            if (us > 1.0e6 * n / rate)
                ++late;
            const juce::ScopedTryLock sl (timesLock);
            if (sl.isLocked() && wall.v.size() < wall.v.capacity())
            {
                wall.v.push_back (us);
                cpu.v.push_back (c);
            }
        }
    }
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

// Runs one case on a driver whose looper has the loops on it, and returns its figures.
Result runCase (Driver& drv, const Loops& loops, Kase kase, bool withSave, const juce::String& part)
{
    auto& looper = drv.looper();
    // Fresh loops each time (an undo by swap leaves a loop in another buffer), then the room.
    for (int t = 0; t < 4; ++t)
    {
        looper.setLayerAudio (t, loops.audio[(size_t) t], loops.audio[(size_t) t].getNumSamples() / (drv.rate * 0.5));
        looper.getLayer (t).source = RigLooper::input;
    }
    looper.maintain();
    drv.run (0.2, false);
    looper.maintain();
    drv.run (0.1, false);
    const int layer = kase == Kase::completeRestore ? 1 : 0;
    const int pages = (loops.audio[(size_t) layer].getNumSamples() + 1023) / 1024;
    const double captureSeconds = pages / 48000.0 * 1.2;   // at the budget's rate, with margin
    const double window = juce::jmax (0.5, captureSeconds + 0.3);

    std::atomic<bool> saving { withSave };
    std::thread saver;
    if (withSave)
        saver = std::thread ([&]
        {
            while (saving)
                for (int t = 0; t < 4 && saving; ++t)
                    looper.copyLayerAudio (t);
        });

    resetPeakMemory();
    looper.resetCopyCounters();
    const auto skipped0 = looper.getCopyCounters().skippedLayerBlocks;
    Result r;
    r.part = part;
    r.kase = juce::String (kaseName (kase)) + (withSave ? " + save" : "");
    r.rate = drv.rate;
    r.block = drv.block;
    switch (kase)
    {
        case Kase::start:
            looper.pressRecord (layer);
            drv.run (window, true);
            r.undoOffered = looper.canUndo (layer);
            looper.pressRecord (layer);
            break;
        case Kase::immediate:
            looper.pressRecord (layer);
            drv.oneBlock();
            r.undoOffered = looper.canUndo (layer);
            looper.undo (layer);
            drv.run (window, true);
            break;
        case Kase::partial:
            looper.pressRecord (layer);
            drv.oneBlock();
            drv.run (captureSeconds / 2.4, true);
            r.undoOffered = looper.canUndo (layer);
            looper.undo (layer);
            drv.run (window, true);
            break;
        case Kase::completeSwap:
        case Kase::completeRestore:
            looper.pressRecord (layer);
            drv.run (window, false);
            looper.pressRecord (layer);
            drv.run (0.05, false);
            r.undoOffered = looper.canUndo (layer);
            looper.undo (layer);
            drv.run (window, true);
            break;
    }
    saving = false;
    if (saver.joinable())
        saver.join();
    drv.collect (r);
    const auto c = looper.getCopyCounters();
    for (int k = 0; k < RigLooper::numCopyKinds; ++k)
        (k == RigLooper::wholeLoop ? r.wholeBytes : RigLooper::isMandatoryCopy (k) ? r.mandatoryBytes : r.discretionaryBytes) += c.bytes[(size_t) k];
    r.maxMandatoryPages = c.maxMandatory;
    r.maxDiscretionaryPages = c.maxDiscretionary;
    r.missing = c.skippedLayerBlocks - skipped0;
    r.swaps = c.swaps;
    r.restorations = c.restorations;
    r.roomsMost = c.roomBuffersMost;
    r.peakMB = peakMemoryMB();
    if (kase != Kase::start)
    {
        drv.run (window, false);   // anything left to do
        r.exact = odu::same (looper.copyLayerAudio (layer), loops.audio[(size_t) layer]) || ! r.undoOffered;
    }
    else
        drv.run (0.2, false);
    return r;
}

void runAll (Driver& drv, const Loops& loops, const juce::String& part, std::ostream& out)
{
    for (const auto kase : { Kase::start, Kase::immediate, Kase::partial, Kase::completeSwap, Kase::completeRestore })
        for (const bool save : { false, true })
        {
            const auto r = runCase (drv, loops, kase, save, part);
            out << row (r) << std::endl;
            std::cout << row (r) << std::endl;
        }
}

void synthetic (double minutes, std::ostream& out)
{
    out << "\n==== SYNTHETIC: the looper driven by a test callback (shows what the code costs) ====\n";
    out << "Loops: " << minutes << " min (undo by swap), " << minutes * 0.95 << " min (undo by restoration), and two short ones.\n";
    out << header() << std::endl;
    for (const double rate : kRates)
    {
        const Loops loops (rate, minutes);
        for (const int block : kBuffers)
        {
            SyntheticDriver drv (rate, block);
            runAll (drv, loops, "synthetic", out);
        }
    }
}

void device (double minutes, std::ostream& out)
{
    out << "\n==== DEVICE: the real audio device driving the looper (what a player would get) ====\n";
    juce::AudioDeviceManager manager;
    const auto error = manager.initialise (0, 2, nullptr, true);
    auto* dev = manager.getCurrentAudioDevice();
    if (error.isNotEmpty() || dev == nullptr)
    {
        out << "No audio device could be opened: " << (error.isNotEmpty() ? error : juce::String ("none found")) << std::endl;
        return;
    }
    out << "Device: " << dev->getName() << " (" << manager.getCurrentAudioDeviceType() << ")\n";
    out << "It lists rates:";
    for (auto r : dev->getAvailableSampleRates())
        out << " " << r;
    out << "\nand buffer sizes:";
    for (auto b : dev->getAvailableBufferSizes())
        out << " " << b;
    out << "\n" << header() << std::endl;
    const auto rates = dev->getAvailableSampleRates();
    const auto sizes = dev->getAvailableBufferSizes();
    for (const double rate : kRates)
    {
        if (! rates.contains (rate))
        {
            out << "  (" << rate / 1000.0 << " kHz: not offered by the device)\n";
            continue;
        }
        const Loops loops (rate, minutes);
        for (const int block : kBuffers)
        {
            if (! sizes.contains (block))
            {
                out << "  (" << rate / 1000.0 << " kHz, " << block << ": buffer size not offered by the device)\n";
                continue;
            }
            auto setup = manager.getAudioDeviceSetup();
            setup.sampleRate = rate;
            setup.bufferSize = block;
            if (const auto e = manager.setAudioDeviceSetup (setup, true); e.isNotEmpty())
            {
                out << "  (" << rate / 1000.0 << " kHz, " << block << ": " << e << ")\n";
                continue;
            }
            auto* now = manager.getCurrentAudioDevice();
            if (now == nullptr || (int) now->getCurrentSampleRate() != (int) rate || now->getCurrentBufferSizeSamples() != block)
            {
                out << "  (" << rate / 1000.0 << " kHz, " << block << ": the device ran at "
                    << (now != nullptr ? juce::String (now->getCurrentSampleRate()) + " Hz, " + juce::String (now->getCurrentBufferSizeSamples()) : juce::String ("nothing"))
                    << " instead: skipped)\n";
                continue;
            }
            DeviceDriver drv (manager, rate, block);
            drv.start();
            runAll (drv, loops, "device", out);
            drv.stop();
        }
    }
}
}

// From TestMain: argv[1] is --overdub-undo-bench.
int runOverdubUndoBench (int argc, char** argv)
{
    juce::String part = argc > 2 ? juce::String (argv[2]) : juce::String ("synthetic");
    double minutes = 10.0;
    juce::File report;
    for (int a = 3; a + 1 < argc; ++a)
    {
        if (juce::String (argv[a]) == "--minutes")
            minutes = juce::String (argv[a + 1]).getDoubleValue();
        if (juce::String (argv[a]) == "--out")
            report = juce::File::getCurrentWorkingDirectory().getChildFile (argv[a + 1]);
    }
    if (report == juce::File())
        report = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                     .getChildFile ("Overdub undo benchmark " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M") + ".txt");
    std::ofstream out (report.getFullPathName().toStdString());
    out << "Overdub and undo benchmark, " << juce::Time::getCurrentTime().toString (true, true) << "\n"
        << juce::SystemStats::getOperatingSystemName() << ", " << juce::SystemStats::getCpuModel() << ", "
        << juce::SystemStats::getNumCpus() << " cores\n"
        << "Undo made " << (RigLooper::kIncrementalUndo ? "a slice at a time (new)" : "by whole copies (old)") << "\n"
        << "Times in microseconds a block. late: blocks longer than they play for. miss: loop blocks not played (a layer's lock busy).\n"
        << "mand/disc KB: copied because it had to be / in order. whole KB: whole-loop copies. mxMd/mxDsc: most pages in one block.\n"
        << "rms: undo-room-sized buffers alive at once (most). pkMB: peak resident memory during the case.\n";
    if (part == "synthetic" || part == "all")
        synthetic (minutes, out);
    if (part == "device" || part == "all")
        device (minutes, out);
    out << "\nEnd of report.\n";
    std::cout << "Report written to " << report.getFullPathName() << std::endl;
    return 0;
}
