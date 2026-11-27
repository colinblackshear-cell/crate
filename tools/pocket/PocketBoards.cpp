#include "PocketBoards.h"

namespace pocket
{
juce::String boardLabel (int id)
{
    if (isLayerBoard (id))
        return "LAYER " + juce::String (id - boardLayer1 + 1);
    switch (id)
    {
        case boardDrums: return "DRUMS";
        case boardPads: return "PADS";
        case boardSynth: return "SYNTH";
        case boardChords: return "CHORDS";
        case boardVoice: return "VOICE";
        case boardVocoder: return "VOCODER";
        case boardAll: return "ALL";
        case boardMaster: return "MASTER";
        case boardBass: return "BASS";
        case boardDrone: return "DRONE";
        default: return {};
    }
}

juce::String boardTitle (int id)
{
    if (isLayerBoard (id))
        return "Layer " + juce::String (id - boardLayer1 + 1);
    switch (id)
    {
        case boardDrums: return "Drums";
        case boardPads: return "Pads";
        case boardSynth: return "Synth";
        case boardChords: return "Chords";
        case boardVoice: return "Voice";
        case boardVocoder: return "Vocoder";
        case boardAll: return "All loops";
        case boardMaster: return "Master";
        case boardBass: return "Bass";
        case boardDrone: return "Drone";
        default: return {};
    }
}

int gridPedal (int cell) { return juce::jlimit (0, kGridPedals - 1, cell) + 1; }   // gate .. repeat

const PedalPage& pedalPage (int pedal)
{
    using namespace pedalboard;
    static const std::vector<PedalPage> pages {
        /* mic */     { { { "micDistance", "DISTANCE" }, { "micWarmth", "WARMTH" }, { "micAir", "AIR" }, { "micAmount", "AMOUNT" } }, {} },
        /* gate */    { { { "gateThreshold", "THRESHOLD" } }, {} },
        /* comp */    { { { "compAmount", "SQUEEZE" }, { "compLevel", "LEVEL" } }, {} },
        /* wah */     { { { "wahSense", "SENSE" }, { "wahRange", "RANGE" }, { "wahPeak", "PEAK" } }, {} },
        /* filter */  { { { "filter", "FILTER" }, { "resonance", "RESONANCE" } }, {} },
        /* dirt */    { { { "drive", "DRIVE" }, { "crush", "CRUSH" } }, {} },
        /* amp */     { { { "ampGain", "GAIN" }, { "ampTone", "TONE" }, { "ampLevel", "LEVEL" } },
                        { { "ampType", { "CLEAN", "CRUNCH", "LEAD", "FUZZ", "BASS" } } } },
        /* eq */      { { { "eqLow", "BASS" }, { "eqMid", "MIDDLE" }, { "eqHigh", "TREBLE" } }, {} },
        /* chorus */  { { { "chorusMix", "MIX" }, { "chorusRate", "RATE" }, { "chorusDepth", "DEPTH" } }, {} },
        /* phaser */  { { { "phaserMix", "MIX" }, { "phaserRate", "SPEED" }, { "phaserDepth", "DEPTH" }, { "phaserFeedback", "FEEDBACK" } }, {} },
        /* trem */    { { { "tremDepth", "DEPTH" } },
                        { { "tremRate", { "1/4", "1/8", "1/8T", "1/16" } }, { "tremShape", { "SMOOTH", "CHOPPY" } } } },
        /* delay */   { { { "delayMix", "MIX" }, { "feedback", "REPEATS" } },
                        { { "delayTime", { "1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2" } } } },
        /* reverb */  { { { "reverbMix", "MIX" }, { "reverbSize", "SIZE" } }, {} },
        /* repeat */  { {}, { { "repeat", { "OFF", "1/4", "1/8", "1/16", "1/32" } } } },
    };
    static const PedalPage gain { { { "inputGain", "GAIN IN" } }, {} };
    if (pedal < 0 || pedal >= (int) pages.size())
        return gain;
    return pages[(size_t) pedal];
}

juce::String pedalShortName (int pedal)
{
    if (pedal == pedalboard::tremolo)
        return "TREM";
    if (pedal < 0 || pedal >= pedalboard::numPedals)
        return "GAIN IN";
    return juce::String (pedalboard::pedals()[(size_t) pedal].name).toUpperCase();
}

juce::Optional<juce::AudioPlayHead::PositionInfo> BoardClock::getPosition() const
{
    PositionInfo p;
    p.setBpm (bpm.load());
    p.setPpqPosition (ppq.load());
    p.setIsPlaying (playing.load());
    p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    return p;
}

//==============================================================================
juce::var settingsToVar (const StripFx& fx)
{
    auto* o = new juce::DynamicObject();
    for (auto* p : fx.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const float v = r->convertFrom0to1 (r->getValue()), def = r->convertFrom0to1 (r->getDefaultValue());
            if (std::abs (v - def) > 1.0e-6f)
                o->setProperty (r->paramID, std::round (v * 10000.0f) / 10000.0f);
        }
    if (o->getProperties().isEmpty())
    {
        delete o;
        return {};
    }
    return juce::var (o);
}

void settingsFromVar (StripFx& fx, const juce::var& v)
{
    for (auto* p : fx.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            float value = r->convertFrom0to1 (r->getDefaultValue());
            if (v.hasProperty (r->paramID))
                value = (float) (double) v.getProperty (r->paramID, value);
            const float n = r->convertTo0to1 (value);
            if (std::abs (r->getValue() - n) > 1.0e-7f)
                r->setValueNotifyingHost (n);
        }
}

bool settingsAreNeutral (const juce::var& v)
{
    auto* o = v.getDynamicObject();
    return o == nullptr || o->getProperties().isEmpty();
}

//==============================================================================
Board::Board()
{
    dryL.assign ((size_t) kMaxBlock, 0.0f);
    dryR.assign ((size_t) kMaxBlock, 0.0f);
}

StripFx& Board::fx()
{
    if (owned == nullptr)
    {
        auto f = std::make_unique<StripFx>();
        if (head != nullptr)
            f->setPlayHead (head);
        f->prepareToPlay (rate.load(), kMaxBlock);
        gainIn = f->params.getRawParameterValue ("inputGain");
        owned = std::move (f);
        live.store (owned.get());
    }
    return *owned;
}

bool Board::isNeutral() const
{
    return owned == nullptr || settingsAreNeutral (settingsToVar (*owned));
}

bool Board::audibleNow (const StripFx& f, const std::atomic<float>* g)
{
    return f.isActive() || (g != nullptr && std::abs (g->load()) > 0.05f) || f.tunerListening.load();
}

bool Board::audible() const
{
    return owned != nullptr && (owned->isActive() || (gainIn != nullptr && std::abs (gainIn->load()) > 0.05f));
}

int Board::pedalsOn() const
{
    if (owned == nullptr || owned->params.getRawParameterValue ("bypass")->load() > 0.5f)
        return 0;
    int n = 0;
    for (int c = 0; c < kGridPedals; ++c)
        n += pedalboard::isOn (*owned, gridPedal (c)) ? 1 : 0;
    return n;
}

void Board::reset()
{
    if (owned == nullptr)
        return;
    for (auto* p : owned->getParameters())
        if (std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-7f)
            p->setValueNotifyingHost (p->getDefaultValue());
}

juce::var Board::settings() const
{
    return owned != nullptr ? settingsToVar (*owned) : juce::var();
}

void Board::setSettings (const juce::var& v)
{
    if (settingsAreNeutral (v))
        reset();
    else
        settingsFromVar (fx(), v);
}

juce::var Board::toVar() const
{
    const auto s = settings();
    if (s.isVoid() && name.isEmpty())
        return {};
    auto* o = new juce::DynamicObject();
    if (name.isNotEmpty() && ! edited())
        o->setProperty ("name", name);
    if (! s.isVoid())
        o->setProperty ("fx", s);
    return juce::var (o);
}

void Board::fromVar (const juce::var& v)
{
    setSettings (v.getProperty ("fx", {}));
    setName (v.getProperty ("name", {}).toString());
}

void Board::copyFrom (const Board& other)
{
    setSettings (other.settings());
    name = other.name;
    namedHash = other.namedHash;
}

juce::uint32 Board::hash() const
{
    // from the saved (rounded) settings, so a board read back from a song hashes the same as before it was saved
    if (owned == nullptr)
        return 2166136261u;
    const auto v = settingsToVar (*owned);
    return v.isVoid() ? 2166136261u : (juce::uint32) juce::JSON::toString (v, true).hashCode();
}

void Board::setTuner (bool on)
{
    if (on)
        fx().tunerListening = true;
    else if (owned != nullptr)
        owned->tunerListening = false;
}

void Board::prepare (double sampleRate)
{
    rate = sampleRate;
    running = false;
    quiet = 0;
    if (auto* f = live.load())
        f->prepareToPlay (sampleRate, kMaxBlock);
}

bool Board::willRun() const
{
    auto* f = live.load();
    return f != nullptr && (running || audibleNow (*f, gainIn));
}

void Board::process (float* left, float* right, int n)
{
    auto* f = live.load();
    if (f == nullptr || left == nullptr || right == nullptr)
        return;
    const bool on = audibleNow (*f, gainIn);
    if (on)
    {
        running = true;
        quiet = 0;
    }
    else if (! running)
        return;   // nothing on and nothing ringing: the sound goes by untouched

    for (int done = 0; done < n; done += kMaxBlock)
    {
        const int m = juce::jmin (kMaxBlock, n - done);
        float* l = left + done;
        float* r = right + done;
        if (! on)
        {
            std::copy (l, l + m, dryL.begin());
            std::copy (r, r + m, dryR.begin());
        }
        float* chans[2] = { l, r };
        juce::AudioBuffer<float> buffer (chans, 2, m);
        f->processBlock (buffer, noMidi);
        if (! on)
        {
            // switched off: the echoes and the reverb ring out, then the board is skipped again
            float diff = 0.0f;
            for (int i = 0; i < m; ++i)
                diff = juce::jmax (diff, std::abs (l[i] - dryL[(size_t) i]), std::abs (r[i] - dryR[(size_t) i]));
            quiet = diff < 1.0e-6f ? quiet + m : 0;
        }
    }
    if (! on && quiet > (int) (rate.load() * 4.5))
        running = false;
}

//==============================================================================
OfflineBoard::OfflineBoard (const juce::var& settings, double sampleRate, double bpm, double startBeat)
{
    if (settingsAreNeutral (settings))
        return;
    fx = std::make_unique<StripFx>();
    settingsFromVar (*fx, settings);
    clock.bpm = juce::jmax (20.0, bpm);
    clock.ppq = startBeat;
    clock.playing = true;
    fx->setPlayHead (&clock);
    fx->prepareToPlay (sampleRate, Board::kMaxBlock);
    beatsPerSample = clock.bpm.load() / (60.0 * sampleRate);
    if (! fx->isActive())
        fx.reset();
}

void OfflineBoard::process (float* left, float* right, int n)
{
    if (fx == nullptr)
        return;
    for (int done = 0; done < n; done += Board::kMaxBlock)
    {
        const int m = juce::jmin (Board::kMaxBlock, n - done);
        float* chans[2] = { left + done, right + done };
        juce::AudioBuffer<float> buffer (chans, 2, m);
        fx->processBlock (buffer, noMidi);
        clock.ppq = clock.ppq.load() + m * beatsPerSample;
    }
}

void applyBoardToLoop (juce::AudioBuffer<float>& loop, const juce::var& settings, double sampleRate, double bpm)
{
    OfflineBoard board (settings, sampleRate, bpm, 0.0);
    if (! board.active() || loop.getNumSamples() < 2)
        return;
    if (loop.getNumChannels() < 2)
    {
        loop.setSize (2, loop.getNumSamples(), true);
        loop.copyFrom (1, 0, loop, 0, 0, loop.getNumSamples());
    }
    juce::AudioBuffer<float> first (loop);
    board.process (first.getWritePointer (0), first.getWritePointer (1), first.getNumSamples());
    board.process (loop.getWritePointer (0), loop.getWritePointer (1), loop.getNumSamples());
}

//==============================================================================
std::vector<BoardChoice> boardChoices (int boardId, const juce::File& ownFolder)
{
    std::vector<BoardChoice> out;
    BoardChoice empty;
    empty.name = "Empty board";
    empty.empty = true;
    out.push_back (empty);

    auto fits = [boardId] (const juce::String& forWhat)
    {
        if (isLayerBoard (boardId) || boardId == boardAll || boardId == boardMaster || forWhat == "Anything")
            return true;
        switch (boardId)
        {
            case boardDrums:
            case boardPads: return forWhat == "Drums";
            case boardSynth: return forWhat == "Keys and synths" || forWhat == "Bass";
            case boardBass: return forWhat == "Bass" || forWhat == "Keys and synths";
            case boardDrone: return forWhat == "Keys and synths";
            case boardChords:
            case boardVocoder: return forWhat == "Keys and synths";
            case boardVoice: return forWhat == "Guitar" || forWhat == "Bass";
            default: return true;
        }
    };
    const auto& presets = pedalboard::presets();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (fits (presets[(size_t) i].forWhat))
        {
            BoardChoice c;
            c.name = presets[(size_t) i].name;
            c.preset = i;
            out.push_back (c);
        }
    if (ownFolder.isDirectory())
    {
        auto files = ownFolder.findChildFiles (juce::File::findFiles, false, "*.json");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareIgnoreCase (b.getFileNameWithoutExtension()) < 0; });
        for (auto& f : files)
        {
            BoardChoice c;
            c.name = juce::JSON::parse (f).getProperty ("name", f.getFileNameWithoutExtension()).toString();
            c.file = f;
            out.push_back (c);
        }
    }
    return out;
}

bool applyChoice (Board& board, const BoardChoice& c)
{
    if (c.empty)
    {
        board.reset();
        board.setName ({});
        return true;
    }
    if (juce::isPositiveAndBelow (c.preset, (int) pedalboard::presets().size()))
    {
        pedalboard::applyPreset (board.fx(), pedalboard::presets()[(size_t) c.preset]);
        board.setName (c.name);
        return true;
    }
    if (c.file.existsAsFile())
    {
        const auto v = juce::JSON::parse (c.file);
        if (! v.isObject())
            return false;
        board.setSettings (v.getProperty ("fx", {}));
        board.setName (c.name);
        return true;
    }
    return false;
}

juce::File saveOwnBoard (const Board& board, const juce::String& name, const juce::File& ownFolder)
{
    const auto clean = name.trim().substring (0, 40);
    if (clean.isEmpty())
        return {};
    ownFolder.createDirectory();
    auto file = ownFolder.getChildFile (juce::File::createLegalFileName (clean) + ".json");
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", clean);
    o->setProperty ("fx", board.settings());
    if (! file.replaceWithText (juce::JSON::toString (juce::var (o))))
        return {};
    return file;
}

int choiceIndexOf (const std::vector<BoardChoice>& choices, const Board& board)
{
    if (board.name.isEmpty())
        return board.isNeutral() ? 0 : -1;
    for (int i = 0; i < (int) choices.size(); ++i)
        if (! choices[(size_t) i].empty && choices[(size_t) i].name == board.name)
            return i;
    return -1;
}

//==============================================================================
namespace
{
struct TestRack
{
    BoardClock clock;
    StripFx fx;
    TestRack (double bpm)
    {
        clock.bpm = bpm;
        clock.playing = true;
        fx.setPlayHead (&clock);
        fx.prepareToPlay (44100.0, Board::kMaxBlock);
    }
    void run (std::vector<float>& l, std::vector<float>& r)
    {
        juce::MidiBuffer none;
        for (int done = 0; done < (int) l.size(); done += Board::kMaxBlock)
        {
            const int m = juce::jmin (Board::kMaxBlock, (int) l.size() - done);
            float* chans[2] = { l.data() + done, r.data() + done };
            juce::AudioBuffer<float> b (chans, 2, m);
            fx.processBlock (b, none);
            clock.ppq = clock.ppq.load() + m * clock.bpm.load() / (60.0 * 44100.0);
        }
    }
};

// Two seconds of bursts with near-silent gaps (so the gate has something to shut), louder and softer notes
// (for the comp and the wah) and a little hiss.
void testSignal (std::vector<float>& l, std::vector<float>& r)
{
    const int n = 88200;
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    juce::uint32 seed = 1234567;
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float hiss = ((float) (seed >> 9) / 8388608.0f - 1.0f) * 1.0e-4f;
        const int burst = i / 5512;   // eighth notes at 120 BPM
        const bool sounding = (i % 5512) < 3800;
        const float level = burst % 3 == 0 ? 0.7f : 0.25f;
        const float env = sounding ? level * std::exp (-(float) (i % 5512) / 2000.0f) : 0.0f;
        const float tone = (float) std::sin (i * 2.0 * juce::MathConstants<double>::pi * (110.0 + 55.0 * (burst % 4)) / 44100.0);
        l[(size_t) i] = env * tone + hiss + 0.001f;
        r[(size_t) i] = env * tone * 0.9f + hiss + 0.0012f;
    }
}

float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        d = juce::jmax (d, std::abs (a[i] - b[i]));
    return d;
}

bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

int firstEcho (const std::vector<float>& l, int from)
{
    for (int i = from; i < (int) l.size(); ++i)
        if (std::abs (l[(size_t) i]) > 0.2f)
            return i;
    return -1;
}
}

juce::String boardsSelfCheck (juce::String& report)
{
    juce::StringArray problems;
    std::vector<float> inL, inR;
    testSignal (inL, inR);

    // A fresh rack with everything off: the sound comes out bit for bit.
    {
        TestRack rack (120.0);
        auto l = inL, r = inR;
        rack.run (l, r);
        if (! sameBits (l, inL) || ! sameBits (r, inR))
            problems.add ("a rack with every pedal off changed the sound");
    }

    for (int cell = 0; cell < kGridPedals; ++cell)
    {
        const int pedal = gridPedal (cell);
        const juce::String name = pedalShortName (pedal);
        // on: the sound changes
        float changed = 0.0f;
        {
            TestRack rack (120.0);
            pedalboard::setOn (rack.fx, pedal, true);
            if (pedal == pedalboard::gate)
                pedalboard::set (rack.fx, "gateThreshold", -20.0f);   // its start only shuts the hiss between notes (-60 dB)
            auto l = inL, r = inR;
            rack.run (l, r);
            changed = juce::jmax (maxDiff (l, inL), maxDiff (r, inR));
        }
        // on, then off again: bit-transparent, and a Board with that history is skipped
        bool transparent = false, skipped = false;
        {
            TestRack rack (120.0);
            pedalboard::setOn (rack.fx, pedal, true);
            pedalboard::setOn (rack.fx, pedal, false);
            auto l = inL, r = inR;
            rack.run (l, r);
            transparent = sameBits (l, inL) && sameBits (r, inR);
            Board b;
            pedalboard::setOn (b.fx(), pedal, true);
            pedalboard::setOn (b.fx(), pedal, false);
            skipped = ! b.willRun() && ! b.audible();
        }
        report << name << ": on changes it by " << juce::String (changed, 4) << ", off " << (transparent ? "bit-transparent" : "NOT transparent")
               << (skipped ? ", board skipped" : ", board NOT skipped") << "\n";
        if (changed < 1.0e-3f)
            problems.add (name + " on didn't change the sound");
        if (! transparent)
            problems.add (name + " switched off wasn't bit-transparent");
        if (! skipped)
            problems.add (name + " switched off left the board running");
    }

    // Delay at 1/4 follows the tempo: 22050 samples at 120 BPM, 44100 after a change to 60 BPM.
    {
        TestRack rack (120.0);
        auto& fx = rack.fx;
        pedalboard::set (fx, "delayMix", 1.0f);
        pedalboard::set (fx, "feedback", 0.0f);
        pedalboard::set (fx, "delayTime", 3.0f);   // 1/4
        std::vector<float> l (66150, 0.0f), r (66150, 0.0f);
        l[100] = r[100] = 1.0f;
        rack.run (l, r);
        const int at120 = firstEcho (l, 200) - 100;
        rack.clock.bpm = 60.0;
        std::vector<float> settle (44100, 0.0f), settleR (44100, 0.0f);
        rack.run (settle, settleR);   // the time glides to the new tempo, the old echo rings out
        std::vector<float> l2 (88200, 0.0f), r2 (88200, 0.0f);
        l2[100] = r2[100] = 1.0f;
        rack.run (l2, r2);
        const int at60 = firstEcho (l2, 200) - 100;
        report << "Delay 1/4: echo after " << at120 << " samples at 120 BPM (22050 expected), " << at60 << " at 60 BPM (44100 expected)\n";
        if (std::abs (at120 - 22050) > 2 || std::abs (at60 - 44100) > 2)
            problems.add ("the 1/4 delay didn't follow the tempo (" + juce::String (at120) + " at 120 BPM, " + juce::String (at60) + " at 60 BPM)");
    }

    // A board switched off keeps running until its echoes have died away, then it is skipped again.
    {
        Board b;
        b.prepare (44100.0);
        pedalboard::set (b.fx(), "delayMix", 0.5f);
        pedalboard::set (b.fx(), "feedback", 0.6f);
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        l[0] = r[0] = 0.5f;
        b.process (l.data(), r.data(), 512);
        pedalboard::set (b.fx(), "delayMix", 0.0f);
        bool ringing = false;
        int blocks = 0;
        for (; blocks < 2000 && b.willRun(); ++blocks)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            b.process (l.data(), r.data(), 512);
            for (auto v : l)
                ringing = ringing || std::abs (v) > 1.0e-3f;
        }
        report << "A delay switched off rang out (" << (ringing ? "echoes heard" : "no echoes") << ") and the board was skipped after "
               << juce::String (blocks * 512 / 44100.0, 1) << " s\n";
        if (! ringing || b.willRun())
            problems.add ("a board switched off didn't ring out and then stop");
    }

    // The settings survive the trip through a song file.
    {
        Board a, b, e;
        applyChoice (a, { "Dub delay", 15, {}, false });
        b.fromVar (juce::JSON::parse (juce::JSON::toString (a.toVar())));
        if (b.hash() != a.hash() || b.name != "Dub delay" || b.isNeutral())
            problems.add ("a factory board didn't come back the same from its saved settings");
        pedalboard::set (a.fx(), "eqOn", 1.0f);   // edited: its own settings now
        e.fromVar (juce::JSON::parse (juce::JSON::toString (a.toVar())));
        if (e.hash() != a.hash() || ! a.edited())
            problems.add ("an edited board didn't come back the same from its saved settings");
        Board c;
        c.fromVar ({});
        if (! c.isNeutral() || c.existing() != nullptr)
            problems.add ("an old song (no board) didn't load a neutral, unmade board");
    }

    return problems.joinIntoString ("; ");
}
}
