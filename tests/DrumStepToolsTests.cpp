#include "TestHelpers.h"
#include "DrumProcessor.h"

using namespace crate;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// Plays the engine on a running clock for `beats` and records each sound's hits and their levels.
struct Played { std::vector<float> levels; int hits = 0; };
Played play (drums::Engine& e, double beats, int voice, double bpm = 120.0)
{
    Played out;
    drums::Clock clock;
    clock.playing = true;
    clock.bpm = bpm;
    juce::AudioBuffer<float> b (2, kBlock);
    juce::MidiBuffer none;
    const int blocks = (int) std::ceil (beats * 60.0 / bpm * kRate / kBlock);
    int seen = 0;
    for (int i = 0; i < blocks; ++i)
    {
        e.render (b.getWritePointer (0), b.getWritePointer (1), kBlock, none, clock);
        clock.ppq += kBlock * bpm / (60.0 * kRate);
        const int now = e.hitCounts[(size_t) voice].load();
        for (; seen < now; ++seen)
            out.levels.push_back (e.lastVelocity ((int) voice));
    }
    out.hits = e.hitCounts[(size_t) voice].load();
    return out;
}
}

struct DrumStepToolsTests : juce::UnitTest
{
    DrumStepToolsTests() : juce::UnitTest ("Drum machine: step tools", "Drums") {}

    void runTest() override
    {
        beginTest ("64 steps: a hit on the last step of bar 4 plays");
        {
            expectEquals (drums::kMaxSteps, 64);
            drums::Engine e;
            e.prepare (kRate);
            drums::Pattern p;
            p.length = 64;
            p.hits[drums::snare][63] = 1;
            e.setPatterns (&p, nullptr);
            expectEquals (play (e, 15.9, drums::snare).hits, 1, "step 64 is 15.75 beats in");
        }

        beginTest ("rolls go up to 8 hits in a step");
        {
            drums::Engine e;
            e.prepare (kRate);
            drums::Pattern p;
            p.hits[drums::closedHat][0] = 8;
            e.setPatterns (&p, nullptr);
            expectEquals (play (e, 0.3, drums::closedHat).hits, 8);
        }

        beginTest ("a plain roll fades, a building roll gets louder, a flam is a soft hit then a full one");
        {
            for (int shape : { (int) drums::plain, (int) drums::builds, (int) drums::flam })
            {
                drums::Engine e;
                e.prepare (kRate);
                drums::Pattern p;
                p.hits[drums::snare][0] = shape == drums::flam ? 1 : 4;
                p.shape[drums::snare][0] = (std::uint8_t) shape;
                e.setPatterns (&p, nullptr);
                auto heard = play (e, 0.3, drums::snare);
                expectEquals (heard.hits, shape == drums::flam ? 2 : 4);
                if (heard.levels.size() < 2)
                    continue;
                if (shape == drums::plain)
                    expect (heard.levels.back() < heard.levels.front(), "fades");
                else
                    expect (heard.levels.back() > heard.levels.front(), "builds / flam lands full");
            }
        }

        beginTest ("rolls and shapes are saved and load");
        {
            drums::Pattern p;
            p.length = 64;
            p.hits[drums::snare][40] = 7;
            p.shape[drums::snare][40] = drums::builds;
            p.hits[drums::clap][63] = 1;
            p.shape[drums::clap][63] = drums::flam;
            expect (p.hasLife());
            const auto back = DrumProcessor::patternFromTree (DrumProcessor::patternToTree (p));
            expect (back == p);
        }
    }
};

static DrumStepToolsTests drumStepToolsTests;
