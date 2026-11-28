#include "PocketApp.h"

using namespace pocket;

//==============================================================================
// FIX NOTES through the app: a BASS take on a layer, the take sheet's FIX NOTES, a note moved down to the key's next
// note and a step later, one added, UNDO, DELETE; each lands in the layer (its notes and its sound) and the loop's
// UNDO takes it back. Screenshots of the take sheet, the loop editor's button and the sheet itself.
juce::String PocketApp::runFixNotesChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, bpm = 100.0, spb = 60.0 / bpm * sr;

    showIdeasList();
    newIdea();
    setSongKey ({ 9, true });   // A minor
    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    loopsScreen.setSyncJobs (true);
    std::vector<float> in (512, 0.0f), outL (512), outR (512);
    auto pump = [&] (int samples)
    {
        for (int done = 0; done < samples; done += 512)
        {
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            L.render (outL.data(), outR.data(), 512);
        }
    };
    pump (40 * 512);

    // a one-bar BASS line: A1, then a misfired C2 on beat 2 (meant to be B1), then E2
    auto part = std::make_shared<NotePart>();
    part->kind = NotePart::synth;
    part->preset = 0;
    for (int i = 0; i < (int) synthPresets().size(); ++i)
        if (familyOfKind (synthPresets()[(size_t) i].kind) == bassFamily)
        {
            part->preset = i;
            break;
        }
    part->notes.bars = 1;
    part->notes.notes = { { 0.0, 0.5, 33 }, { 1.0, 0.5, 36 }, { 2.0, 0.5, 40 } };
    part->lengthBeats = 4.0;
    {
        const int n4 = (int) std::llround (4.0 * spb);
        auto take = LoopsEngine::makeTake (n4, sr);
        auto audio = renderPart (*part, {}, bpm, sr, n4);
        std::copy (audio.begin(), audio.end(), take->data.begin());
        take->length = n4;
        take->lengthBeats = 4.0;
        take->tool = (int) LayerTool::synth;
        take->source = (int) LoopSource::synth;
        take->part = part;
        L.setLayer (0, take, false);
    }
    pump (2048);
    L.setTransport (true);
    pump (4096);
    showTool (Tool::bass, false);

    expect (loopsScreen.notesOf (0) != nullptr && loopsScreen.notesOf (0)->describe() == "BASS", "the BASS layer's notes aren't offered to FIX NOTES");

    // the take sheet offers FIX NOTES on a BASS take
    openTakeSheet (0, true);
    savePng (folder.getChildFile ("18a-take-sheet-fix-notes.png"));
    expect (takeOpen, "the take sheet didn't open on the BASS take");
    if (takeSheet.onFixNotes)
        takeSheet.onFixNotes();
    expect (! takeOpen && notesOpen && notesSheet.layer() == 0 && notesSheet.pattern().notes.size() == 3,
            "FIX NOTES on the take sheet didn't open the sheet on the layer's 3 notes");

    auto layerNotes = [&] { auto p = loopsScreen.notesOf (0); return p != nullptr ? p->notes.notes : std::vector<SynthNote>(); };
    auto takeNow = [&] { return L.takeOf (0).get(); };

    // the misfired note: picked, DOWN (to B1, the key's next note down), LATER (a 1/16)
    const auto* before = takeNow();
    notesSheet.pick (1);
    notesSheet.press (FixNotesSheet::downId);
    pump (2048);
    {
        const auto n = layerNotes();
        report << "DOWN on C2 in A minor: " << (n.size() > 1 ? SongKey::noteName (n[1].note) : juce::String ("none")) << ", layer rendered again "
               << (takeNow() != before ? "yes" : "NO") << "\n";
        expect (n.size() == 3 && n[1].note == 35, "DOWN didn't move C2 to B1 (the key's next note down) in the layer");
        expect (takeNow() != before, "the layer wasn't rendered again from the fixed notes");
    }
    notesSheet.press (FixNotesSheet::laterId);
    pump (2048);
    {
        const auto n = layerNotes();
        expect (n.size() == 3 && std::abs (n[1].beat - 1.25) < 1.0e-9, "LATER didn't move the note a 1/16 later in the layer");
    }
    // the sound: a note at beat 1.25 now, silence where it used to start (beat 1 to 1.25, after the A1 has ended)
    {
        auto t = L.takeOf (0);
        auto rms = [&] (double fromBeat, double toBeat)
        {
            const int a = (int) (fromBeat * spb), b = (int) (toBeat * spb);
            double s = 0.0;
            for (int i = a; i < b && i < t->length.load(); ++i)
                s += (double) t->data[(size_t) i] * t->data[(size_t) i];
            return std::sqrt (s / juce::jmax (1, b - a));
        };
        const double gap = rms (1.02, 1.2), note = rms (1.3, 1.6);
        report << "the fixed note's sound: " << juce::String (gap, 4) << " RMS before it, " << juce::String (note, 4) << " on it\n";
        expect (note > 4.0 * gap + 1.0e-4, "the fixed note isn't where the layer sounds now");
    }
    savePng (folder.getChildFile ("18b-fix-notes.png"));

    // a tap on an empty spot adds a note; UNDO takes it out again; DELETE takes out the picked one
    notesSheet.tapCell (12, 45);
    pump (2048);
    expect (layerNotes().size() == 4, "a tap on an empty spot didn't add a note to the layer");
    notesSheet.press (FixNotesSheet::undoId);
    pump (2048);
    expect (layerNotes().size() == 3, "UNDO in FIX NOTES didn't take the added note out of the layer");
    notesSheet.pick (2);
    notesSheet.press (FixNotesSheet::deleteId);
    pump (2048);
    expect (layerNotes().size() == 2, "DELETE didn't take the picked note out of the layer");
    // the loop's own UNDO goes back a change at a time
    loopsScreen.undo();
    pump (2048);
    expect (layerNotes().size() == 3, "the loop's UNDO didn't bring the deleted note back");
    closeFixNotes();
    pump (2048);
    expect (! notesOpen, "DONE didn't close FIX NOTES");

    // the loop editor shows FIX NOTES on the layer and opens the sheet
    showTool (Tool::loops, false);
    loopsScreen.selectLayer (0);
    loopsScreen.openEditor (0);
    savePng (folder.getChildFile ("18c-loop-editor-fix-notes.png"));
    if (loopsScreen.editorSheet().onNotes)
        loopsScreen.editorSheet().onNotes();
    expect (notesOpen && notesSheet.layer() == 0, "the loop editor's FIX NOTES didn't open the sheet");
    closeFixNotes();

    // an audio layer (a voice take) isn't offered
    {
        auto take = LoopsEngine::makeTake (4096, sr);
        take->length = 4096;
        take->lengthBeats = 4.0;
        take->tool = (int) LayerTool::mic;
        L.setLayer (1, take, false);
        pump (2048);
        openFixNotes (1);
        expect (! notesOpen, "FIX NOTES opened on an audio layer");
    }

    L.setTransport (false);
    loopsScreen.setSyncJobs (false);
    stopEverything();
    report << (problems.isEmpty() ? "FIX NOTES: take sheet, DOWN in key, LATER, add, UNDO, DELETE, the loop's UNDO and the loop editor's button all OK\n"
                                  : "FIX NOTES problems: " + problems.joinIntoString ("; ") + "\n");
    return problems.joinIntoString ("; ");
}
