#include "PocketApp.h"
#include "PocketFxChecks.h"

// The EFFECTS tool in the app: which board a tool's sound goes through, REC on EFFECTS, the boards in the song,
// and its checks and screenshots.
using namespace pocket;

int PocketApp::boardForTool (Tool t) const
{
    switch (t)
    {
        case Tool::drums: return boardDrums;
        case Tool::pads:
        case Tool::sampler: return boardPads;   // (what it samples plays on the pads)
        case Tool::synth: return boardSynth;
        case Tool::chords: return boardChords;
        case Tool::vocoder: return appSettings.singMode == 0 ? boardVoice : boardVocoder;
        case Tool::loops:
        {
            const int l = loopsScreen.selectedLayer();
            return l >= 0 ? layerBoardId (l) : -1;
        }
        case Tool::effects: return effects != nullptr ? effects->source() : -1;
        case Tool::bass: return boardBass;
        case Tool::drone: return boardDrone;
        default: return -1;
    }
}

Tool PocketApp::recTool() const
{
    if (tool != Tool::effects || effects == nullptr)
        return tool;
    switch (effects->source())
    {
        case boardDrums: return Tool::drums;
        case boardPads: return Tool::pads;
        case boardSynth: return Tool::synth;
        case boardChords: return Tool::chords;
        case boardBass: return Tool::bass;
        case boardDrone: return Tool::drone;
        default: return Tool::effects;   // VOICE, VOCODER and the resamples: EFFECTS' own
    }
}

void PocketApp::setupEffects()
{
    EffectsScreen::Model m;
    m.board = [this] (int id) -> Board& { return loopsScreen.engine().board (id); };
    m.shown = [this] (int id) { return ! isLayerBoard (id) || loopsScreen.hasLayer (id - boardLayer1); };
    m.picked = [this] (int id)
    {
        applyMic();
        if ((id == boardVoice || id == boardVocoder) && ! micOn)
            askForMic ({});
    };
    m.changed = [this] (int id)
    {
        if (isLayerBoard (id))
            loopsScreen.boardsChanged();   // kept with the layers (loops.json)
        else
            saveSong();
        singScreen.repaint();               // its FX lamp
    };
    m.toast = [this] (const juce::String& t) { toast (t); };
    m.ownFolder = [this] { return lib.getRoot().getChildFile ("Boards"); };
    m.micOn = [this] { return micOn; };
    m.hearing = [this] { return micOn && micHear; };
    m.route = [this] { return route; };
    m.micButton = [this] { if (micOn) micOff(); else askForMic ({}); };
    m.hearButton = [this] { setHearMyself (! micHear); };
    effects = std::make_unique<EffectsScreen> (std::move (m));
}

void PocketApp::openEffects (int boardId)
{
    if (boardId >= 0)
        effects->setSource (boardId);
    showTool (Tool::effects, false);
    if ((effects->source() == boardVoice || effects->source() == boardVocoder) && ! micOn)
        askForMic ({});
}

// REC on EFFECTS: the picked sound through its board onto the next layer, the same way its own tool records it.
// VOICE records the mic; a LAYER, ALL and MASTER record what you hear of them (a resample).
void PocketApp::recEffects()
{
    const int id = effects->source();
    const int bars = appSettings.recBars();
    auto startSong = [this, wasRunning = beats.engine().clockRunning()]
    {
        if (! wasRunning)
            togglePlay();   // the song starts; the take starts after the count-in (or at once without one)
    };
    switch (id)
    {
        case boardDrums: return recDrums();
        case boardPads: return recPads();
        case boardSynth:
        case boardBass:
            if (! synthLock())
                bassActive = id == boardBass;   // which part REC records
            return recSynth();
        case boardDrone: return recDrone();
        case boardChords: return recChords();
        case boardVoice:
        case boardVocoder:
            if (loopsScreen.recordFrom ((int) (id == boardVoice ? LoopSource::mic : LoopSource::vocoder), bars) >= 0)
                startSong();
            return;
        case boardAll:
            if (loopsScreen.layerCount() == 0)
                return toast ("No loops to record yet. Record a layer first, then ALL records them through this board.");
            if (loopsScreen.recordFrom ((int) LoopSource::loops, bars) >= 0)
                startSong();
            return;
        case boardMaster:
            if (loopsScreen.recordFrom ((int) LoopSource::master, bars) >= 0)
                startSong();
            return;
        default:
            if (isLayerBoard (id))
            {
                const int l = id - boardLayer1;
                if (! loopsScreen.hasLayer (l))
                    return toast ("Layer " + juce::String (l + 1) + " is empty.");
                if (loopsScreen.recordFrom ((int) LoopSource::layerTap, bars, -1, l) >= 0)
                    startSong();
            }
            return;
    }
}

namespace
{
struct SongBoard { int id; const char* key; };
const SongBoard songBoards[] = { { boardDrums, "drums" }, { boardPads, "pads" }, { boardSynth, "synth" }, { boardChords, "chords" },
                                 { boardVoice, "voice" }, { boardVocoder, "vocoder" }, { boardAll, "all" }, { boardMaster, "master" },
                                 { boardBass, "bass" }, { boardDrone, "drone" } };
}

juce::var PocketApp::boardsToVar() const
{
    auto& L = const_cast<PocketApp*> (this)->loopsScreen.engine();
    auto* o = new juce::DynamicObject();
    for (auto& sb : songBoards)
        if (const auto v = L.board (sb.id).toVar(); ! v.isVoid() && v.getDynamicObject() != nullptr)
            o->setProperty (sb.key, v);
    if (o->getProperties().isEmpty())
    {
        delete o;
        return {};
    }
    return juce::var (o);
}

void PocketApp::boardsFromVar (const juce::var& v)
{
    auto& L = loopsScreen.engine();
    for (auto& sb : songBoards)
        L.board (sb.id).fromVar (v.getProperty (sb.key, {}));
    if (effects != nullptr)
        effects->refresh();
}

//==============================================================================
// Through the app: an untouched song is bit for bit what it was; a SYNTH board changes only the synth; REC through
// a board gives a wet take and the layer's own board stays neutral; QUANTIZE keeps the board; boards come back
// with the song and a new song starts with none.
juce::String PocketApp::runEffectsChecks (juce::String& report)
{
    juce::StringArray problems;

    // 1. the engine's fingerprint
    {
        const auto untouched = mixFingerprint();
        const auto made = mixFingerprint ([] (LoopsEngine& L) { for (int b = 0; b < kBoards; ++b) L.board (b).fx(); });
        report << "untouched song: " << juce::String (untouched) << (untouched == kUntouchedFingerprint ? " (as before the boards)" : " (CHANGED)")
               << ", every board made but left alone: " << (made == untouched ? "identical" : "DIFFERENT") << "\n";
        if (untouched != kUntouchedFingerprint)
            problems.add ("Effects: an untouched song no longer renders bit for bit as before the boards");
        if (made != untouched)
            problems.add ("Effects: boards that were opened but left alone changed the sound");

        auto synthDelay = [] (LoopsEngine& L)
        {
            auto& fx = L.board (boardSynth).fx();
            pedalboard::setOn (fx, pedalboard::delay, true);
            pedalboard::set (fx, "delayMix", 0.5f);
        };
        const auto noSynth = mixFingerprint ({}, false), noSynthBoard = mixFingerprint (synthDelay, false), withBoard = mixFingerprint (synthDelay);
        report << "SYNTH board (delay on): with the synth playing the mix " << (withBoard != untouched ? "changes" : "DOES NOT change")
               << "; with the synth silent it is " << (noSynthBoard == noSynth ? "bit-identical (nothing else goes through it)" : "DIFFERENT") << "\n";
        if (withBoard == untouched)
            problems.add ("Effects: the SYNTH board's delay didn't change the synth");
        if (noSynthBoard != noSynth)
            problems.add ("Effects: the SYNTH board changed sounds other than the synth");
    }

    // 2. REC on EFFECTS (SYNTH, PATTERN) through a board that turns it down 12 dB
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, bpm = 97.0;
    const int barSamples = (int) (4.0 * 60.0 / bpm * sr);
    switchIdea (ideas.create ("Effects check"));
    const auto checkIdea = ideas.current().id;
    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    appSettings.countInBars = 0;
    appSettings.recLength = 0;   // 1 bar
    appSettings.click = false;
    L.setCountInBars (0);
    applyClick();
    loopsScreen.setSyncJobs (true);
    const bool wasDemo = loopsScreen.demoMode();
    loopsScreen.setDemoMode (false);   // the layers save, as in the app

    std::vector<float> in (512, 0.0f), outL (512), outR (512);
    auto pump = [&] (int samples)
    {
        for (int done = 0; done < samples; done += 512)
        {
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            L.render (outL.data(), outR.data(), 512);
            if (lockKind != LockKind::none)
                lockWatchTick();
        }
    };
    auto rms = [] (const float* d, int n)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i)
            s += (double) d[i] * d[i];
        return n > 0 ? std::sqrt (s / n) : 0.0;
    };
    pump (2048);

    for (int id = 0; id < kBoards; ++id)
        if (! L.board (id).isNeutral())
            problems.add ("Effects: a new song's " + boardTitle (id) + " board isn't empty");

    synthPart.reset();
    synthPart.setPreset (synthPresetIndex ("Pluck"));
    synthPart.mode = SynthPart::patternMode;
    for (int s : { 0, 4, 8, 12 })
        synthPart.pattern.setStep (s, s == 8 ? 64 : 57 + s / 2);
    synthPart.apply();
    synthPart.patternChanged();
    openEffects (boardSynth);
    pedalboard::set (L.board (boardSynth).fx(), "inputGain", -12.0f);
    const int layer = loopsScreen.nextLayer();
    recPressed();
    for (int g = 0; g < 12 * barSamples && L.state (layer) != LoopState::playing; g += 512)
        pump (512);
    pump (4096);
    auto take = L.takeOf (layer);
    if (L.state (layer) != LoopState::playing || take == nullptr || take->part == nullptr)
        problems.add ("REC on EFFECTS (SYNTH): no take with its notes landed on layer " + juce::String (layer + 1));
    else
    {
        const int n = take->length.load();
        const double got = rms (take->data.data(), n);
        auto dryPart = *take->part;
        dryPart.board = juce::var();
        const auto dry = renderPart (dryPart, take->quant, bpm, sr, n);
        const auto wet = renderPart (*take->part, take->quant, bpm, sr, n);
        const double toDry = got / juce::jmax (1.0e-9, rms (dry.data(), n)), toWet = got / juce::jmax (1.0e-9, rms (wet.data(), n));
        report << "REC on EFFECTS (SYNTH, Gain in -12 dB): the take is " << juce::String (juce::Decibels::gainToDecibels (toDry), 1) << " dB against the dry notes, "
               << juce::String (juce::Decibels::gainToDecibels (toWet), 1) << " dB against them through the stored board; layer " << (layer + 1) << "'s own board "
               << (L.board (layerBoardId (layer)).isNeutral() ? "empty" : "NOT EMPTY") << "\n";
        if (std::abs (juce::Decibels::gainToDecibels (toDry) + 12.0) > 1.5)
            problems.add ("REC on EFFECTS (SYNTH): the take didn't go through the board (" + juce::String (juce::Decibels::gainToDecibels (toDry), 1) + " dB, -12 expected)");
        if (std::abs (juce::Decibels::gainToDecibels (toWet)) > 1.5)
            problems.add ("REC on EFFECTS (SYNTH): the take's stored board doesn't match what was recorded");
        if (! L.board (layerBoardId (layer)).isNeutral())
            problems.add ("REC on EFFECTS (SYNTH): the layer's own board isn't empty (the board would be heard twice)");

        // QUANTIZE renders it again through the stored board
        LoopQuantize q;
        q.grid = 1;
        loopsScreen.quantizeLayer (layer, q);
        pump (2048);
        auto again = L.takeOf (layer);
        const double qDry = again != nullptr ? rms (again->data.data(), again->length.load()) / juce::jmax (1.0e-9, rms (dry.data(), n)) : 0.0;
        report << "QUANTIZE on it: " << juce::String (juce::Decibels::gainToDecibels (qDry), 1) << " dB against the dry notes\n";
        if (std::abs (juce::Decibels::gainToDecibels (qDry) + 12.0) > 1.5)
            problems.add ("QUANTIZE lost the board the take was recorded through");
    }
    togglePlay();
    pump (2048);

    // 3. boards come back with the song: a tool's (song.json) and a layer's (loops.json)
    {
        auto& drums = L.board (boardDrums);
        for (auto& c : boardChoices (boardDrums, {}))
            if (c.name == "Dub delay")
                applyChoice (drums, c);
        auto& lb = L.board (layerBoardId (layer));
        pedalboard::setOn (lb.fx(), pedalboard::chorus, true);
        pedalboard::set (lb.fx(), "chorusMix", 0.7f);
        auto& master = L.board (boardMaster);
        pedalboard::set (master.fx(), "inputGain", 3.0f);
        const auto dh = drums.hash(), lh = lb.hash(), mh = master.hash();
        const auto dn = drums.name;
        saveSong();
        loopsScreen.boardsChanged();
        switchIdea (ideas.create ("Effects check 2"));
        bool fresh = true;
        for (int id = 0; id < kBoards; ++id)
            fresh = fresh && L.board (id).isNeutral();
        switchIdea (checkIdea);
        pump (4096);
        const bool back = L.board (boardDrums).hash() == dh && L.board (boardDrums).name == dn
                       && L.board (layerBoardId (layer)).hash() == lh && L.board (boardMaster).hash() == mh;
        report << "boards with the song: " << (back ? "DRUMS (" + dn + "), layer " + juce::String (layer + 1) + " (chorus) and MASTER came back" : juce::String ("NOT BACK"))
               << ", another song's boards " << (fresh ? "empty" : "NOT EMPTY") << "\n";
        if (! back)
            problems.add ("Effects: the boards didn't come back with the song (DRUMS " + juce::String (L.board (boardDrums).hash() == dh ? "same" : "different")
                          + " \"" + L.board (boardDrums).name + "\", layer " + juce::String (L.board (layerBoardId (layer)).hash() == lh ? "same" : "different")
                          + ", MASTER " + juce::String (L.board (boardMaster).hash() == mh ? "same" : "different") + ")");
        if (! fresh)
            problems.add ("Effects: another song kept this song's boards");
    }
        // 4. the sound picker: every sound visible at once, big enough, and each chip does its job
    {
        const int sizes[2][2] = { { 390, 844 }, { 440, 956 } };
        const int oldW = getWidth(), oldH = getHeight();
        int emptyLayer = -1;
        for (int l = 0; l < kLayerBoards && emptyLayer < 0; ++l)
            if (! loopsScreen.hasLayer (l))
                emptyLayer = l;
        int fullLayer = -1;
        for (int l = 0; l < kLayerBoards && fullLayer < 0; ++l)
            if (loopsScreen.hasLayer (l))
                fullLayer = l;
        const int ids[] = { boardDrums, boardPads, boardBass, boardSynth, boardChords, boardVoice, boardVocoder, boardDrone,
                            layerBoardId (0), layerBoardId (1), layerBoardId (2), layerBoardId (3), layerBoardId (4), layerBoardId (5),
                            layerBoardId (6), layerBoardId (7), boardAll, boardMaster };
        for (auto& sz : sizes)
        {
            setSize (sz[0], sz[1]);
            openEffects (boardSynth);
            const auto screen = effects->getBounds().toFloat();
            for (int id : ids)
            {
                const auto r = effects->chipRect (id);
                if (r.isEmpty() || ! screen.withZeroOrigin().contains (r) || r.getHeight() < 44.0f || r.getWidth() < 34.0f)
                    problems.add ("Effects picker at " + juce::String (sz[0]) + "x" + juce::String (sz[1]) + ": the chip for " + boardLabel (id) + " is missing, off screen or too small");
                else if (id < boardLayer1 && r.getWidth() < 44.0f)
                    problems.add ("Effects picker: the " + boardLabel (id) + " chip is under 44pt wide");
            }
            if (effects->getBottom() > getHeight())
                problems.add ("Effects picker: the screen runs off the bottom");
        }
        setSize (oldW, oldH);
        openEffects (boardSynth);
        if (fullLayer >= 0)
        {
            effects->tapChip (layerBoardId (fullLayer));
            if (effects->source() != layerBoardId (fullLayer))
                problems.add ("Effects picker: tapping layer " + juce::String (fullLayer + 1) + " (it has a loop) didn't pick its board");
        }
        effects->setSource (boardSynth);
        if (emptyLayer >= 0)
        {
            lastToast = {};
            effects->tapChip (layerBoardId (emptyLayer));
            if (effects->source() != boardSynth)
                problems.add ("Effects picker: tapping an empty layer picked it");
            if (! lastToast.contains ("Record a loop on layer " + juce::String (emptyLayer + 1) + " first"))
                problems.add ("Effects picker: tapping an empty layer didn't say to record a loop first (said \"" + lastToast + "\")");
        }
        for (int id : { boardDrums, boardPads, boardBass, boardChords, boardVoice, boardVocoder, boardDrone, boardAll, boardMaster })
        {
            effects->tapChip (id);
            if (effects->source() != id)
                problems.add ("Effects picker: the " + boardLabel (id) + " chip didn't pick its board");
        }
        effects->setSource (boardSynth);
        report << "sound picker: 8 instruments + layers 1-8 + ALL + MIX on screen at 390x844 and 440x956, layer with a loop "
               << (fullLayer >= 0 ? juce::String (fullLayer + 1) : juce::String ("none")) << ", empty layer " << (emptyLayer >= 0 ? juce::String (emptyLayer + 1) : juce::String ("none")) << "\n";
    }
    loopsScreen.setSyncJobs (false);
    loopsScreen.setDemoMode (wasDemo);
    return problems.joinIntoString ("; ");
}

void PocketApp::writeEffectsSnapshots (const juce::File& folder)
{
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, bpm = 92.0;
    switchIdea (ideas.create ("Effects demo"));
    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    const bool oldMic = micOn, oldHear = micHear;

    double phase = 0.0;
    std::vector<float> in (512), l (512), r (512);
    auto pump = [&] (int blocks, float hz)
    {
        for (int k = 0; k < blocks; ++k)
        {
            for (auto& s : in)
            {
                s = hz > 0 ? 0.3f * (float) std::sin (phase) : 0.0f;
                phase += 2.0 * juce::MathConstants<double>::pi * hz / sr;
            }
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            L.render (l.data(), r.data(), 512);
        }
    };
    auto pick = [] (Board& board, int id, const char* name)
    {
        for (auto& c : boardChoices (id, {}))
            if (c.name == name)
                return applyChoice (board, c);
        return false;
    };

    // two loops on the layers, for the LAYER chips
    for (int layer = 0; layer < 2; ++layer)
    {
        const int n = (int) (8.0 * 60.0 / bpm * sr);
        auto t = LoopsEngine::makeTake (n, sr);
        for (int i = 0; i < n; ++i)
            t->data[(size_t) i] = 0.3f * (float) std::sin (i * (layer == 0 ? 0.02 : 0.035)) * (float) ((i / 5000) % 2);
        t->length = n;
        t->lengthBeats = 8.0;
        t->tool = (int) (layer == 0 ? LayerTool::drums : LayerTool::synth);
        L.setLayer (layer, t, false, 0.0);
    }
    pump (40, 0.0f);

    // SYNTH, its delay picked
    pick (L.board (boardSynth), boardSynth, "Dub delay");
    L.board (boardDrums).fx();
    pedalboard::set (L.board (boardDrums).fx(), "inputGain", 2.0f);   // a lamp on DRUMS
    openEffects (boardSynth);
    effects->selectPedal (pedalboard::delay);
    effects->refresh();
    savePng (folder.getChildFile ("17-effects-synth-delay.png"));
    setSize (440, 956);   // an iPhone 17 Pro Max
    openEffects (boardSynth);
    savePng (folder.getChildFile ("17-effects-synth-delay-440.png"));
    setSize (390, 844);
    openEffects (boardSynth);

    // VOICE: the mic banner, a guitar board, the amp picked
    micOn = micHear = true;
    route = Route::wired;
    pick (L.board (boardVoice), boardVoice, "Clean guitar");
    openEffects (boardVoice);
    effects->selectPedal (pedalboard::amp);
    applyMic();
    savePng (folder.getChildFile ("17b-effects-voice-banner.png"));

    // VOICE's tuner, singing an A
    effects->showTuner (true);
    pump (30, 220.0f);
    effects->pollTuner();
    savePng (folder.getChildFile ("17f-effects-tuner.png"));
    effects->showTuner (false);
    setSize (440, 956);
    openEffects (boardVoice);
    savePng (folder.getChildFile ("17b-effects-voice-banner-440.png"));
    setSize (390, 844);
    openEffects (boardVoice);

    // LAYER 2, chorus picked
    pick (L.board (layerBoardId (1)), layerBoardId (1), "Shimmer pad");
    openEffects (layerBoardId (1));
    effects->selectPedal (pedalboard::chorus);
    savePng (folder.getChildFile ("17c-effects-layer.png"));
    setSize (440, 956);
    openEffects (layerBoardId (1));
    savePng (folder.getChildFile ("17c-effects-layer-440.png"));
    setSize (390, 844);
    openEffects (layerBoardId (1));

    // the board list, with a board of your own
    effects->saveBoardAs ("My wide pad");
    effects->showList (true);
    savePng (folder.getChildFile ("17d-effects-board-list.png"));
    effects->showList (false);

    // the FX PAD, a finger on it
    effects->selectPedal (pedalboard::delay);
    effects->showPad (true);
    effects->padTouch (0.7f, 0.65f, true);
    savePng (folder.getChildFile ("17e-effects-fx-pad.png"));
    effects->padTouch (0.7f, 0.65f, false);
    effects->showPad (false);

    // a tool with its FX chip lit: VOCODER, with a board on your voice
    appSettings.singMode = 0;
    showTool (Tool::vocoder, false);
    savePng (folder.getChildFile ("17g-vocoder-fx-chip.png"));

    // your saved board stays out of the next run's list
    lib.getRoot().getChildFile ("Boards").getChildFile ("My wide pad.json").deleteFile();
    micOn = oldMic;
    micHear = oldHear;
    applyMic();
    stopEverything();
}
