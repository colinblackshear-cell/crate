#include "PocketApp.h"

// The navigation checks: the song drawer, press and hold menus, sliding between tools, and the sheets that close with a
// downward drag. They drive the app itself with no sound card (screenshot mode), the way a finger would.
using namespace pocket;

juce::String PocketApp::runNavigationChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, bpm = 96.0;
    const bool wasDemo = loopsScreen.demoMode();
    loopsScreen.setDemoMode (false);   // the layers save and come back, as in the app
    loopsScreen.setSyncJobs (true);
    appSettings.click = false;
    applyClick();
    micOn = micHear = false;
    applyMic();
    hideToast();

    std::vector<float> in (512, 0.0f), outL (512), outR (512);
    auto pump = [&] (int blocks)
    {
        for (int k = 0; k < blocks; ++k)
        {
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            L.render (outL.data(), outR.data(), 512);
        }
    };
    auto putLoop = [&] (int layer, LayerTool t, double step)
    {
        const int n = (int) (8.0 * 60.0 / bpm * sr);
        auto take = LoopsEngine::makeTake (n, sr);
        for (int i = 0; i < n; ++i)
            take->data[(size_t) i] = 0.3f * (float) std::sin ((double) i * step) * (float) ((i / 5000) % 2);
        take->length = n;
        take->lengthBeats = 8.0;
        take->tool = (int) t;
        L.setLayer (layer, take, false, 0.0);
    };
    auto fake = [] (juce::Component& c, juce::Point<float> p)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p, now, 1, false);
    };
    auto tap = [&] (juce::Point<float> p)   // a tap on the header
    {
        mouseDown (fake (*this, p));
        mouseUp (fake (*this, p));
    };
    auto settle = [&]
    {
        holdCard.close();
        closeAllSheets();
        if (drawer.isVisible())
            drawer.animateTo (false);
        if (tour->isVisible())
            tour->setVisible (false);
        endLesson();
        loopsScreen.closeEditor();
        hideToast();
    };

    // ------------------------------------------------------------------ two songs, A with two loop layers, B empty
    showIdeasList();
    switchIdea (ideas.create ("Nav A"));
    const auto idA = ideas.current().id;
    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    pump (40);
    putLoop (0, LayerTool::drums, 0.02);
    putLoop (1, LayerTool::synth, 0.035);
    pump (40);
    expect (loopsScreen.layerCount() == 2, "Nav: song A didn't get its two layers");
    const auto idB = ideas.create ("Nav B");

    // ------------------------------------------------------------------ the drawer opens and closes
    showTool (Tool::loops, false);
    openDrawer();
    expect (drawer.isVisible() && drawer.isOpen() && drawer.progress() > 0.99f, "Drawer: it didn't open");
    expect (drawer.songRows() >= 2, "Drawer: YOUR SONGS is empty");
    closeDrawer();
    expect (! drawer.isVisible(), "Drawer: it didn't close");
    // a swipe left closes it (past a third of its width); a short pull springs back
    openDrawer();
    drawer.dragStart();
    drawer.dragTo (-40.0f);
    drawer.dragEnd (-40.0f, 0.0f);
    expect (drawer.isOpen(), "Drawer: a short pull left should leave it open");
    drawer.dragStart();
    drawer.dragTo (-250.0f);
    drawer.dragEnd (-250.0f, 0.0f);
    expect (! drawer.isVisible(), "Drawer: a swipe left didn't close it");
    // a tap on the dim closes it
    openDrawer();
    drawer.mouseUp (fake (drawer, { (float) getWidth() - 10.0f, 400.0f }));
    expect (! drawer.isVisible(), "Drawer: a tap on the dim didn't close it");
    // the header: the menu icon, the song's name, and the Mac link dot (still a tap)
    tap (menuRect().getCentre());
    expect (drawer.isVisible(), "Drawer: the header's menu icon didn't open it");
    closeDrawer();
    tap ({ 110.0f, (float) insets.getTop() + 20.0f });
    expect (drawer.isVisible(), "Drawer: a tap on the song's name didn't open it");
    closeDrawer();
    tap (chipRect().getCentre());
    expect (page == Page::more && morePageShown == 3, "Header: the Mac link dot no longer opens Mac link");
    goBack();
    expect (page == Page::tool && tool == Tool::loops, "Mac link: no way back to the tool it was opened from");
    tap (lcdRect().getCentre());
    expect (recSetupOpen, "Header: the display no longer opens Rec setup");
    closeRecSetup();

    // ------------------------------------------------------------------ switching songs from the drawer keeps the tool
    showTool (Tool::loops, false);
    openDrawer();
    pickSong (idB);
    pump (40);
    expect (ideas.current().id == idB && page == Page::tool && tool == Tool::loops && ! drawer.isVisible(), "Drawer: picking song B didn't switch to it and stay on LOOPS");
    expect (loopsScreen.layerCount() == 0, "Drawer: song B shows layers it doesn't have (" + juce::String (loopsScreen.layerCount()) + ")");
    showTool (Tool::pads, false);
    openDrawer();
    pickSong (idA);
    pump (60);
    expect (ideas.current().id == idA && page == Page::tool && tool == Tool::pads, "Drawer: picking song A didn't switch to it and stay on PADS");
    expect (loopsScreen.layerCount() == 2, "Drawer: song A came back with " + juce::String (loopsScreen.layerCount()) + " layers, not 2");
    showTool (Tool::loops, false);
    report << "drawer: opens from the menu icon, the song name and (below) the left edge; closes on a swipe left or a tap on the dim; "
              "song B (no layers) and song A (2 layers) switch with the tool kept\n";

    // ------------------------------------------------------------------ the edge swipe
    {
        const float y = 300.0f;
        edgeWatcher.begin ({ 10.0f, y }, 0);
        edgeWatcher.move ({ 60.0f, y + 4.0f }, 0);
        edgeWatcher.move ({ 200.0f, y + 8.0f }, 0);
        edgeWatcher.end (0);
        expect (drawer.isOpen(), "Edge swipe: a swipe from the left edge on LOOPS didn't open the drawer");
        closeDrawer();
        // a vertical drag from the edge is not ours
        edgeWatcher.begin ({ 10.0f, y }, 0);
        edgeWatcher.move ({ 12.0f, y + 40.0f }, 0);
        edgeWatcher.move ({ 150.0f, y + 50.0f }, 0);
        edgeWatcher.end (0);
        expect (! drawer.isVisible(), "Edge swipe: a vertical drag from the edge opened the drawer");
        // not from further in
        edgeWatcher.begin ({ 60.0f, y }, 0);
        edgeWatcher.move ({ 200.0f, y }, 0);
        edgeWatcher.end (0);
        expect (! drawer.isVisible(), "Edge swipe: a drag that started 60 px in opened the drawer");
        // not on the thumb bar or the tool row
        expect (! edgeSwipeAllowed ({ 10.0f, (float) thumb.getY() + 20.0f }) && ! edgeSwipeAllowed ({ 10.0f, (float) toolRow.getY() + 20.0f }),
                "Edge swipe: allowed on the thumb bar or the tool row");

        // SYNTH: not inside the keys
        showTool (Tool::synth, false);
        const auto keys = getLocalArea (&synthScreen, synthScreen.keysBounds());
        const float kx = juce::jmin (20.0f, (float) keys.getX() + 2.0f), ky = (float) keys.getCentreY();
        expect (! edgeSwipeAllowed ({ kx, ky }), "Edge swipe: allowed inside the SYNTH keys");
        edgeWatcher.begin ({ kx, ky }, 0);
        edgeWatcher.move ({ kx + 60.0f, ky }, 0);
        edgeWatcher.move ({ kx + 200.0f, ky }, 0);
        edgeWatcher.end (0);
        expect (! drawer.isVisible(), "Edge swipe: a drag along the SYNTH keys pulled the drawer out");
        expect (edgeSwipeAllowed ({ 10.0f, (float) insets.getTop() + 60.0f }), "Edge swipe: not allowed at the left edge above the SYNTH keys");
        report << "edge swipe: starts within 22 px of the left edge (not the thumb bar, the tool row, the SYNTH keys at x=" << keys.getX() << ".." << keys.getRight()
               << ", y=" << keys.getY() << ".." << keys.getBottom() << ", sheets or menus); a vertical drag does not open it\n";
        showTool (Tool::loops, false);
    }

    // ------------------------------------------------------------------ sliding between tools
    {
        auto cellX = [this] (Tool t)
        {
            for (int i = 0; i < (int) toolRow.tools.size(); ++i)
                if (toolRow.tools[(size_t) i] == t)
                    return toolRow.cellRect (i).getCentreX();
            return 0.0f;
        };
        toolRow.slideStart (cellX (Tool::loops));
        toolRow.slideTo (cellX (Tool::drums));
        toolRow.slideTo (cellX (Tool::sampler));
        expect (toolRow.slidingTo() >= 0 && toolRow.tools[(size_t) toolRow.slidingTo()] == Tool::sampler, "Tool row: the finger on SAMPLER didn't light SAMPLER");
        toolRow.slideTo (cellX (Tool::synth));
        expect (toolRow.slidingTo() >= 0 && toolRow.tools[(size_t) toolRow.slidingTo()] == Tool::synth, "Tool row: the finger on SYNTH didn't light SYNTH");
        expect (tool == Tool::loops, "Tool row: the tool changed before the finger lifted");
        toolRow.slideEnd (cellX (Tool::synth), 10.0f);
        expect (page == Page::tool && tool == Tool::synth, "Tool row: lifting on SYNTH didn't pick it");
        toolRow.slideStart (cellX (Tool::synth));
        toolRow.slideTo (cellX (Tool::chords));
        toolRow.slideEnd (cellX (Tool::chords), -200.0f);   // a finger that went up the screen calls it off
        expect (tool == Tool::synth, "Tool row: a slide taken up and away still picked a tool");
        toolRow.slideStart (cellX (Tool::chords));
        toolRow.slideEnd (cellX (Tool::chords), 10.0f);
        expect (tool == Tool::chords, "Tool row: a plain tap on CHORDS no longer picks it");
        // the header bar: a swipe left is the next tool, right the previous
        showTool (Tool::loops, false);
        mouseDown (fake (*this, { 300.0f, (float) insets.getTop() + 20.0f }));
        mouseDrag (fake (*this, { 240.0f, (float) insets.getTop() + 22.0f }));
        mouseDrag (fake (*this, { 180.0f, (float) insets.getTop() + 22.0f }));
        mouseUp (fake (*this, { 180.0f, (float) insets.getTop() + 22.0f }));
        expect (tool == Tool::drums, "Header swipe: a swipe left on LOOPS didn't go to DRUMS");
        mouseDown (fake (*this, { 120.0f, (float) insets.getTop() + 20.0f }));
        mouseDrag (fake (*this, { 200.0f, (float) insets.getTop() + 22.0f }));
        mouseUp (fake (*this, { 200.0f, (float) insets.getTop() + 22.0f }));
        expect (tool == Tool::loops, "Header swipe: a swipe right on DRUMS didn't go back to LOOPS");
        report << "tools: sliding a finger along the row lights the tool under it and picks it on lift (up and away calls it off); "
                  "a swipe on the header goes to the next or previous tool\n";
        // the amber outline: only the brief flash, and never in a demo
        toolRow.flash();
        expect (toolRow.flashing(), "Tool row: flash() didn't flash");
        toolRow.slideStart (cellX (Tool::loops));
        expect (! toolRow.flashing(), "Tool row: touching the row didn't end the flash");
        toolRow.slideEnd (cellX (Tool::loops), 10.0f);
    }

    // ------------------------------------------------------------------ every sheet closes on a downward drag
    {
        auto sheetCheck = [&] (const juce::String& name, std::function<void()> open, pocket::DragSheet& sheet, std::function<bool()> isOpen)
        {
            open();
            if (! isOpen())
                return problems.add ("Sheet " + name + ": didn't open for the check");
            const auto home = sheet.getBounds();
            const float h = (float) sheet.getHeight();
            sheet.dragStart();
            sheet.dragMove (h / 6.0f);
            const bool gone1 = sheet.dragEnd (h / 6.0f, 0.0f);
            if (gone1 || ! isOpen() || sheet.getBounds() != home)
                problems.add ("Sheet " + name + ": a short drag should spring back");
            sheet.dragStart();
            sheet.dragMove (h / 3.0f + 14.0f);
            const bool gone2 = sheet.dragEnd (h / 3.0f + 14.0f, 0.0f);
            if (! gone2 || isOpen())
                problems.add ("Sheet " + name + ": a drag past a third of its height didn't close it");
            open();
            sheet.dragStart();
            sheet.dragMove (50.0f);
            const bool gone3 = sheet.dragEnd (50.0f, 1.3f);
            if (! gone3 || isOpen())
                problems.add ("Sheet " + name + ": a downward flick didn't close it");
            closeAllSheets();
        };
        showTool (Tool::pads, false);
        sheetCheck ("Tempo", [this] { openTempo(); }, tempoSheet, [this] { return tempoOpen; });
        sheetCheck ("Rec setup", [this] { openRecSetup(); }, recSetup, [this] { return recSetupOpen; });
        sheetCheck ("Sounds", [this] { openSoundBank(); }, soundBank, [this] { return bankOpen; });
        sheetCheck ("Pad", [this] { openPadSheet (9); }, padSheet, [this] { return padSheetOpen; });
        sheetCheck ("Send to Mac", [this] { sendSoundToMac (lib.items().front().id, "sounds"); }, *sendSheet, [this] { return sendOpen; });
        sheetCheck ("Headphones", [this] { micOn = false; openHeadphones ({}); }, headphones, [this] { return phonesOpen; });
        expect (! micOn, "Sheet Headphones: closing it with a drag turned the mic on");
        {
            auto take = lib.load (lib.items().front().id);
            showPage (Page::recorder);
            record.showDemoTake (take, "Nav", false);
            sheetCheck ("Effects + Chop", [this] { openSheet(); }, sheet, [this] { return sheetOpen; });
            showTool (Tool::loops, false);
        }
        sheetCheck ("Loop editor", [this] { loopsScreen.openEditor (0); }, loopsScreen.editorSheet(), [this] { return loopsScreen.editorSheet().isVisible(); });
        loopsScreen.closeEditor();
        showTool (Tool::loops, false);
        report << "sheets: Tempo, Rec setup, Sounds, Pad, Send to Mac, Headphones (the mic stays off), Effects + Chop and the loop editor all spring back "
                  "from a short drag and close on a drag past a third of their height or a flick\n";
    }

    // ------------------------------------------------------------------ the hold menu on a loop layer
    {
        showTool (Tool::loops, false);
        pump (4);
        auto run = [&] (int item, const juce::String& what)
        {
            loopsScreen.holdLayer (0);
            if (! holdCard.isOpen() || holdCard.itemCount() != 8)
                return problems.add ("Hold menu on a layer: " + what + ": the menu didn't open with its 8 rows");
            holdCard.choose (item);
            if (holdCard.isOpen())
                problems.add ("Hold menu on a layer: " + what + ": the menu stayed open after a pick");
        };
        loopsScreen.holdLayer (0);
        expect (holdCard.isOpen() && holdCard.current().title.startsWith ("LAYER 1"), "Hold menu on a layer: no menu, or the wrong title");
        expect (loopsScreen.selectedLayer() == 0, "Hold menu on a layer: the layer isn't picked, so its panel doesn't show it");
        const auto spec = holdCard.current();
        holdCard.close();
        expect (spec.items.size() == 8 && spec.items[0].text.startsWith ("IN / OUT") && spec.items[1].text == "FIT" && spec.items[2].text == "EFFECTS"
                    && spec.items[3].text == "WARP" && spec.items[4].text == "DUB OVER IT" && spec.items[5].text.startsWith ("OPEN") && spec.items[6].text == "SEND TO MAC"
                    && spec.items[7].text == "CLEAR" && spec.items[7].danger, "Hold menu on a layer: the rows are not the agreed ones");
        run (0, "IN / OUT");
        expect (loopsScreen.editorSheet().isVisible() && loopsScreen.editorSheet().page() == LoopEditSheet::shapePage, "Hold menu on a layer: IN / OUT didn't open the editor");
        loopsScreen.closeEditor();
        run (1, "FIT");
        expect (loopsScreen.editorSheet().isVisible(), "Hold menu on a layer: FIT didn't open the editor");
        loopsScreen.closeEditor();
        run (2, "EFFECTS");
        expect (loopsScreen.editorSheet().isVisible() && loopsScreen.editorSheet().page() == LoopEditSheet::effectsPage, "Hold menu on a layer: EFFECTS didn't open the effects page");
        loopsScreen.closeEditor();
        run (3, "WARP");
        expect (loopsScreen.editorSheet().isVisible() && loopsScreen.editorSheet().page() == LoopEditSheet::warpPage, "Hold menu on a layer: WARP didn't open the Warp page");
        loopsScreen.closeEditor();
        run (5, "OPEN");
        expect (page == Page::tool && tool == Tool::drums, "Hold menu on a layer: OPEN DRUMS didn't open DRUMS");
        showTool (Tool::loops, false);
        run (6, "SEND TO MAC");
        expect (sendOpen, "Hold menu on a layer: SEND TO MAC didn't open the Send to Mac sheet");
        closeSendSheet();
        run (4, "DUB OVER IT");
        pump (2);
        expect (L.state (0) == LoopState::overdubWait || L.state (0) == LoopState::overdubbing, "Hold menu on a layer: DUB OVER IT didn't start a dub");
        loopsScreen.holdLayer (0);
        expect (holdCard.current().items[4].text == "STOP DUB", "Hold menu on a layer: the dub row doesn't turn into STOP DUB");
        holdCard.choose (4);
        pump (500);
        expect (L.state (0) == LoopState::playing, "Hold menu on a layer: STOP DUB didn't end the dub");
        run (7, "CLEAR");
        pump (4);
        expect (L.state (0) == LoopState::empty, "Hold menu on a layer: CLEAR didn't clear the layer");
        loopsScreen.undo();
        pump (40);
        expect (L.state (0) == LoopState::playing && loopsScreen.layerCount() == 2, "Hold menu on a layer: UNDO didn't bring the cleared layer back");
        // an empty layer has no menu
        loopsScreen.holdLayer (5);
        expect (! holdCard.isOpen(), "Hold menu: an empty layer opened a menu");
        report << "hold menu on a layer: IN / OUT, FIT (QUANTIZE on note layers), EFFECTS, WARP, DUB OVER IT (then STOP DUB), OPEN, SEND TO MAC and CLEAR each run the panel's action; "
                  "CLEAR then UNDO brings the layer back\n";
        settle();
    }

    // ------------------------------------------------------------------ the hold menu on a pad, and the sound that never waits for it
    {
        showTool (Tool::pads, false);
        auto& kit = beats.kit();
        std::vector<float> pl (512), pr (512);
        auto renderPads = [&] (int blocks)
        {
            for (int k = 0; k < blocks; ++k)
            {
                std::fill (pl.begin(), pl.end(), 0.0f);
                std::fill (pr.begin(), pr.end(), 0.0f);
                beats.engine().render (pl.data(), pr.data(), 512);
            }
        };
        beats.engine().stopPads();
        renderPads (2);
        expect (! kit.pads[9].empty(), "Hold menu on a pad: pad A10 has no sound to hold");
        beats.selectPad (9);
        beats.engine().stopPads();
        renderPads (2);
        // a touch sounds at once, with no menu; the hold then opens the menu and the sound plays on until the finger lifts
        beats.testTouchPad (9);
        renderPads (1);
        expect ((beats.engine().soundingPads() >> 9 & 1) != 0, "Pad: a touch didn't sound the pad in the first block");
        expect (! holdCard.isOpen(), "Pad: the hold menu came before the hold time");
        beats.testHoldElapsed();
        expect (holdCard.isOpen() && holdCard.itemCount() == 7, "Hold menu on a pad: no menu after the hold");
        renderPads (1);
        expect ((beats.engine().soundingPads() >> 9 & 1) != 0, "Pad: the hold menu cut the sound");
        beats.testLiftPad();
        holdCard.close();
        // the rows
        beats.holdPad (9);
        const auto spec = holdCard.current();
        holdCard.close();
        expect (spec.items.size() == 7 && spec.items[0].text == "SOUND PAGE" && spec.items[1].text.startsWith ("SWAP SOUND") && spec.items[2].text == "RECORD A NEW SOUND"
                    && spec.items[3].text.startsWith ("VOLUME") && spec.items[4].text == "16 LEVELS" && spec.items[5].text == "COPY TO ANOTHER PAD"
                    && spec.items[6].text == "REMOVE SOUND" && spec.items[6].danger && spec.title.startsWith ("PAD A10"), "Hold menu on a pad: the rows are not the agreed ones");
        auto choose = [&] (int item)
        {
            beats.holdPad (9);
            holdCard.choose (item);
        };
        choose (3);
        expect (padSheetOpen && padSheet.pad() == 9, "Hold menu on a pad: VOLUME · PAN · TUNE didn't open the pad sheet");
        closePadSheet();
        choose (0);
        expect (page == Page::tool && tool == Tool::sampler && sampler.target() == 9, "Hold menu on a pad: SOUND PAGE didn't open the SAMPLER on pad A10");
        showTool (Tool::pads, false);
        choose (2);
        expect (page == Page::tool && tool == Tool::sampler && sampler.target() == 9 && ! sampler.hasSound(),
                "Hold menu on a pad: RECORD A NEW SOUND didn't open an empty SAMPLER aimed at pad A10");
        showTool (Tool::pads, false);
        // SWAP SOUND: your sounds in the same card; the pad keeps its volume
        kit.pads[9].volume = 77;
        const auto oldId = kit.pads[9].soundId;
        choose (1);
        expect (holdCard.isOpen() && holdCard.current().title.startsWith ("SOUND FOR PAD A10") && holdCard.itemCount() == (int) lib.items().size(), "Hold menu on a pad: SWAP SOUND didn't list the sounds");
        {
            auto items = lib.items();
            std::stable_sort (items.begin(), items.end(), [] (const SoundItem& a, const SoundItem& b) { return a.date > b.date; });
            int pick = 0;
            while (pick + 1 < (int) items.size() && items[(size_t) pick].id == oldId)
                ++pick;
            const auto wanted = items[(size_t) pick].id;
            holdCard.choose (pick);
            expect (kit.pads[9].soundId == wanted && wanted != oldId && kit.pads[9].volume == 77, "Hold menu on a pad: SWAP SOUND didn't put the picked sound on the pad (keeping its volume)");
        }
        // 16 LEVELS: the bank's pads play the sound quiet to loud
        choose (4);
        expect (beats.levelsPad() == 9, "Hold menu on a pad: 16 LEVELS didn't start");
        beats.engine().stopPads();
        renderPads (2);
        beats.testTouchPad (2);
        renderPads (1);
        expect ((beats.engine().soundingPads() >> 9 & 1) != 0, "16 LEVELS: a pad didn't play the picked sound");
        beats.testLiftPad();
        beats.endPadMode();
        expect (beats.levelsPad() < 0, "16 LEVELS: EXIT didn't leave it");
        // COPY TO ANOTHER PAD, then UNDO
        const auto before = kit.pads[12].soundId, source = kit.pads[9].soundId;
        choose (5);
        expect (beats.copySource() == 9, "Hold menu on a pad: COPY TO ANOTHER PAD didn't start");
        beats.copyPadTo (12);
        expect (kit.pads[12].soundId == source && beats.copySource() < 0, "Copy: the target pad didn't get the sound");
        expect (toastHasAction(), "Copy: no UNDO on the note");
        pressToastAction();
        expect (kit.pads[12].soundId == before, "Copy: UNDO didn't put the other pad's sound back");
        // REMOVE SOUND, then UNDO
        choose (6);
        expect (kit.pads[9].empty(), "Hold menu on a pad: REMOVE SOUND didn't remove it");
        expect (toastHasAction(), "Remove sound: no UNDO on the note");
        pressToastAction();
        expect (! kit.pads[9].empty() && kit.pads[9].soundId == source, "Remove sound: UNDO didn't bring the sound back");
        // an empty pad's menu
        const auto keep = kit.pads[9];
        kit.clear (9);
        beats.holdPad (9);
        expect (holdCard.itemCount() == 2 && holdCard.current().items[0].text.startsWith ("PUT A SOUND"), "Hold menu on an empty pad: the rows are not the agreed ones");
        holdCard.close();
        kit.pads[9] = keep;
        beats.padsChanged();
        report << "hold menu on a pad: a touch sounds the pad in the very first block and the menu opens after the hold with the sound still playing; "
                  "VOLUME · PAN · TUNE, SOUND PAGE (the SAMPLER on the pad), RECORD A NEW SOUND (an empty SAMPLER for the pad), SWAP SOUND (keeps volume), 16 LEVELS (the pads play quiet to loud), COPY (UNDO) and REMOVE SOUND (UNDO) work\n";
        settle();
    }

    // ------------------------------------------------------------------ the hold menu on a chord pad
    {
        showTool (Tool::chords, false);
        chordsTool.reset();
        chordsTool.setKey (songKey);
        chordsScreen.toolChanged();
        chordsScreen.pressPad (3, true);
        expect (chordsTool.heldPad() == 3, "Chord pad: a touch didn't play the chord at once");
        chordsScreen.holdPad (3);
        expect (holdCard.isOpen() && holdCard.itemCount() == chordsTool.progLength + 2 && holdCard.current().items[0].text == "EDIT CHORD"
                    && holdCard.current().items[2].text == "PUT IN PROGRESSION BOX 1",
                "Hold menu on a chord pad: no menu, or not EDIT CHORD, COPY CHORD TO and one row per box");
        expect (chordsTool.heldPad() == 3, "Chord pad: the hold menu let the chord go");
        holdCard.choose (3);
        expect (chordsTool.progression[1] == 3, "Hold menu on a chord pad: PUT IN PROGRESSION BOX 2 didn't put the chord there");
        chordsScreen.pressPad (3, false);
        chordsTool.reset();
        chordsTool.setKey (songKey);
        chordsScreen.toolChanged();
        report << "hold menu on a chord pad: the chord plays on the touch and keeps playing under the menu; EDIT CHORD and COPY CHORD TO lead the menu; PUT IN PROGRESSION BOX n puts it there\n";
        settle();
    }

    // ------------------------------------------------------------------ the hold menu on a song (the drawer and Home)
    {
        showTool (Tool::loops, false);
        openDrawer();
        int other = -1;
        for (int i = 0; i < drawer.songRows(); ++i)
            if (drawer.songIdAt (i) == idB)
                other = i;
        expect (other >= 0, "Song menu: song B isn't in the drawer");
        drawer.holdRow (other);
        expect (holdCard.isOpen() && holdCard.itemCount() == 3 && holdCard.current().title == "NAV B", "Song menu: holding a song in the drawer didn't open STAR / RENAME / DELETE");
        const auto s = songHoldSpec (idB);
        expect (s.items[0].text == "STAR" && s.items[0].action != nullptr && s.items[1].text == "RENAME" && s.items[1].action != nullptr
                && s.items[2].text == "DELETE" && s.items[2].danger && s.items[2].enabled && s.items[2].action != nullptr,
                "Song menu: RENAME and a red DELETE aren't both there for another song");
        const auto cur = songHoldSpec (idA);
        expect (cur.items[2].enabled && cur.items[2].action != nullptr, "Song menu: the open song's DELETE is off");
        holdCard.close();
        settle();
        showIdeasList();
        ideasScreen->holdRow (0);
        expect (holdCard.isOpen() && holdCard.itemCount() == 3, "Song menu: holding a song on Home didn't open STAR / RENAME / DELETE");
        settle();
        report << "hold menu on a song (drawer and Home): RENAME and a red DELETE (the open song's too)\n";
    }

    // ------------------------------------------------------------------ everything the old More list offered is in the drawer
    {
        const int tiles[5] = { SongDrawer::sounds, SongDrawer::macLink, SongDrawer::remote, SongDrawer::settings, SongDrawer::record };
        const int pages[5] = { 1, 3, 2, 4, -1 };
        for (int k = 0; k < 5; ++k)
        {
            showTool (Tool::loops, false);
            openDrawer();
            drawerTile (tiles[k]);
            const bool ok = tiles[k] == SongDrawer::record ? page == Page::recorder : (page == Page::more && morePageShown == pages[k]);
            expect (ok && ! drawer.isVisible(), "Drawer: tile " + juce::String (tiles[k]) + " didn't open its page");
            goBack();
            expect (page == Page::tool && tool == Tool::loops, "Drawer: tile " + juce::String (tiles[k]) + " has no way back to the tool");
        }
        expect (phoneSummary().contains ("sounds"), "Settings: the ON THIS IPHONE line is empty");
        showTool (Tool::loops, false);
        openDrawer();
        drawerTile (SongDrawer::tourLesson);
        expect (holdCard.isOpen() && holdCard.itemCount() == 2 && holdCard.current().items[0].text == "TAKE THE TOUR", "Drawer: TOUR · LESSON didn't open its small menu");
        holdCard.choose (0);
        expect (tour->isVisible(), "Drawer: TAKE THE TOUR didn't start the tour");
        settle();
        showTool (Tool::loops, false);
        openDrawer();
        drawerTile (SongDrawer::tourLesson);
        holdCard.choose (1);
        expect (lessonOn, "Drawer: SHOW ME: MAKE A BEAT didn't start the lesson");
        settle();
        report << "drawer: SOUNDS, RECORD A SOUND, MAC LINK, REMOTE, SETTINGS (each with a way back to the tool) and TOUR · LESSON (the tour, the lesson) are all reachable; "
                  "the old ··· button and More list are gone\n";
    }

    // ------------------------------------------------------------------ the amber outline on the tool row
    {
        toolRow.stopFlash();
        newIdea();
        expect (! toolRow.flashing(), "Tool row: + NEW SONG outlined the tools in a demo run");
        toolRow.flash();
        expect (toolRow.flashing(), "Tool row: the flash is gone");
        report << "tool row: the amber outline is a 2.8 second time stamp (and + NEW SONG doesn't start it in a demo run), so it can't stay on\n";
        toolRow.stopFlash();
    }

    settle();
    showIdeasList();
    loopsScreen.setSyncJobs (false);
    loopsScreen.setDemoMode (wasDemo);
    juce::ignoreUnused (folder);
    return problems.joinIntoString ("\n");
}

// Deleting songs through their menus: another song, the open one (on Home and from the drawer over a tool), and the last one.
juce::String PocketApp::runSongDeleteChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    hideToast();
    holdCard.close();
    auto rowOf = [this] (const juce::String& id)
    {
        for (int i = 0; i < drawer.songRows(); ++i)
            if (drawer.songIdAt (i) == id)
                return i;
        return -1;
    };
    auto homeRowOf = [this] (const juce::String& id)
    {
        for (int i = 0; i < (int) ideas.list().size(); ++i)
            if (ideasScreen->songIdAtRow (i) == id)
                return i;
        return -1;
    };
    // the song's menu, then the confirm card: DELETE, DELETE SONG
    auto deleteThroughMenu = [&] (const juce::String& id, bool fromDrawer, const juce::String& what)
    {
        if (fromDrawer)
        {
            openDrawer();
            drawer.refresh();
            const int row = rowOf (id);
            if (row < 0)
                return problems.add (what + ": the song isn't in the drawer");
            drawer.holdRow (row);
        }
        else
        {
            const int row = homeRowOf (id);
            if (row < 0)
                return problems.add (what + ": the song isn't on Home");
            ideasScreen->holdRow (row);
        }
        if (! holdCard.isOpen() || holdCard.itemCount() != 3 || holdCard.current().items[2].text != "DELETE" || ! holdCard.current().items[2].enabled)
            return problems.add (what + ": no DELETE in the song's menu");
        holdCard.choose (2);
        if (! holdCard.isOpen() || ! holdCard.current().title.startsWith ("DELETE ") || holdCard.current().items[0].text != "DELETE SONG")
            return problems.add (what + ": DELETE didn't ask first");
        holdCard.choose (0);
    };

    // another song (not open), from Home
    showIdeasList();
    const auto keepId = ideas.current().id;
    const auto other = ideas.create ("Delete me");
    ideasScreen->refresh();
    deleteThroughMenu (other, false, "Another song");
    expect (ideas.find (other) == nullptr && ! ideas.folderFor (other).exists(), "Another song: it's still there");
    expect (ideas.current().id == keepId, "Another song: deleting it changed the open song");

    // the open song, from Home: your most recent other song opens in its place; KEEP IT keeps it
    const auto older = ideas.create ("Delete older");
    const auto openId = ideas.create ("Delete open");
    switchIdea (openId);
    showIdeasList();
    juce::String expectNext;
    for (auto& i : ideas.list())
        if (i.id != openId && expectNext.isEmpty())
            expectNext = i.id;
    ideasScreen->holdRow (homeRowOf (openId));
    holdCard.choose (2);
    expect (holdCard.isOpen() && holdCard.current().items[1].text == "KEEP IT", "The open song: the confirm has no KEEP IT");
    holdCard.choose (1);
    expect (ideas.find (openId) != nullptr && ideas.current().id == openId, "The open song: KEEP IT deleted it");
    deleteThroughMenu (openId, false, "The open song (Home)");
    expect (ideas.find (openId) == nullptr && ! ideas.folderFor (openId).exists(), "The open song (Home): it's still there");
    expect (ideas.current().id == expectNext, "The open song (Home): the most recent other song didn't open");
    expect (page == Page::songs, "The open song (Home): it left Home");
    report << "Home: another song deletes after its confirm; the open song deletes too and " << (ideas.find (expectNext) != nullptr ? ideas.find (expectNext)->name : juce::String ("?"))
           << " (the most recent other) opens; KEEP IT keeps it\n";

    // the open song, from the drawer over a tool: the tool stays, on the next song
    switchIdea (older);
    showTool (Tool::chords, false);
    deleteThroughMenu (older, true, "The open song (drawer)");
    expect (ideas.find (older) == nullptr && ideas.current().id != older && ideas.current().id.isNotEmpty(), "The open song (drawer): it's still open");
    expect (page == Page::tool && tool == Tool::chords, "The open song (drawer): it didn't stay on CHORDS");
    closeDrawer();
    report << "drawer over CHORDS: the open song deletes and the next one opens on CHORDS\n";

    // the last song: a new empty one opens (the Street kit on its pads)
    showIdeasList();
    for (int guard = 0; guard < 200 && ideas.list().size() > 1; ++guard)
        for (auto& i : ideas.list())
            if (i.id != ideas.current().id)
            {
                removeIdea (i.id);
                break;
            }
    expect (ideas.list().size() == 1, "The last song: couldn't clear the others (" + juce::String ((int) ideas.list().size()) + " left)");
    const auto last = ideas.current().id;
    showIdeasList();
    deleteThroughMenu (last, false, "The last song");
    {
        std::vector<float> l (512), r (512);
        for (int i = 0; i < 40; ++i)
            loopsScreen.engine().render (l.data(), r.data(), 512);   // the empty layers reach the engine and settle
    }
    expect (ideas.list().size() == 1 && ideas.find (last) == nullptr && ideas.current().id != last && ideas.current().id.isNotEmpty(),
            "The last song: no new song in its place (" + juce::String ((int) ideas.list().size()) + " songs)");
    expect (beats.kit().usedPads() == 16 && loopsScreen.layerCount() == 0, "The last song: the new song isn't empty with the Street kit ("
                                                                             + juce::String (beats.kit().usedPads()) + " pads, " + juce::String (loopsScreen.layerCount()) + " layers)");
    showIdeasList();
    savePng (folder.getChildFile ("24d-after-deleting-the-last-song.png"));
    report << "the last song: deletes, and " << ideas.current().name << " (new, empty, the Street kit) opens\n";
    holdCard.close();
    hideToast();
    return problems.joinIntoString ("\n");
}

//==============================================================================
// EDIT CHORD in the app: CHORDS > hold a pad > EDIT CHORD, ROOT and TYPE, the keys, DONE; the pad plays it (synth and
// vocoder), it's marked on the pad, COPY CHORD TO, and it stays after the song is saved and opened again and the key changes.
juce::String PocketApp::runOwnChordChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    auto notesText = [] (const juce::Array<int>& n)
    {
        juce::StringArray s;
        for (int v : n)
            s.add (SongKey::noteName (v));
        return s.joinIntoString (" ");
    };
    auto pcsOf = [] (const juce::Array<int>& n)
    {
        juce::Array<int> s;
        for (int v : n)
            s.addIfNotAlreadyThere (((v % 12) + 12) % 12);
        s.sort();
        return s;
    };
    auto typeIndex = [] (const char* label)
    {
        const auto& t = ownChordTypes();
        for (int i = 0; i < (int) t.size(); ++i)
            if (juce::String (t[(size_t) i].label) == label)
                return i;
        return 0;
    };
    // what the pad really plays: a touch, a few blocks of the engine, the notes it sent the vocoder
    auto played = [this] (int pad)
    {
        auto& e = chordsTool.engine();
        std::vector<float> l (256), r (256);
        chordsTool.releaseAll();   // (a HEAR may still have the pad on: a touch would let it go)
        for (int i = 0; i < 4; ++i)
            e.renderAt (l.data(), r.data(), 256, false, 0.0, 120.0);
        const int before = e.vocoderSends();
        chordsTool.padDown (pad, false);
        for (int i = 0; i < 8 && e.vocoderSends() == before; ++i)
            e.renderAt (l.data(), r.data(), 256, false, 0.0, 120.0);
        auto got = e.vocoderSends() != before ? e.lastVocoderNotes() : juce::Array<int>();
        chordsTool.padUp (pad);
        chordsTool.releaseAll();
        return got;
    };
    hideToast();
    holdCard.close();
    showIdeasList();
    switchIdea (ideas.create ("Own chord check"));
    const auto songId = ideas.current().id;
    setSongKey ({ 9, true });
    hideToast();
    showTool (Tool::chords, false);
    const int pad = 2;

    // ---------------------------------------------------------------- hold > EDIT CHORD > ROOT G, TYPE MAJ7
    chordsScreen.holdPad (pad);
    if (holdCard.isOpen() && holdCard.itemCount() > 0 && holdCard.current().items[0].text == "EDIT CHORD")
        holdCard.choose (0);
    else
        problems.add ("the chord pad's hold menu has no EDIT CHORD");
    expect (chordEditOpen && chordEdit.isVisible() && chordEdit.pad() == pad, "EDIT CHORD didn't open the chord sheet on pad 3");
    chordEdit.pickRoot (7);
    chordEdit.pickType (typeIndex ("MAJ7"));
    hideToast();
    savePng (folder.getChildFile ("40-edit-chord-type.png"));
    const auto gmaj7 = chordmath::buildOwn (7, typeIndex ("MAJ7"), 0, 0);
    expect (chordsTool.isOwn (pad) && chordsTool.padNotes (pad) == gmaj7, "ROOT G + TYPE MAJ7: pad 3 plays " + notesText (chordsTool.padNotes (pad)) + ", not " + notesText (gmaj7));
    expect (chordsTool.padName (pad).startsWith ("G"), "ROOT G + TYPE MAJ7: pad 3 is called " + chordsTool.padName (pad));
    {
        const auto got = played (pad);
        report << "EDIT CHORD G MAJ7 on pad 3: named " << chordsTool.padName (pad) << ", pad notes " << notesText (chordsTool.padNotes (pad)) << ", sent to the vocoder " << notesText (got) << "\n";
        expect (pcsOf (got) == pcsOf (gmaj7), "pad 3's touch sent " + notesText (got) + " to the vocoder, not G MAJ7's notes");
    }
    // INV + : the B on the bottom, the same notes
    chordEdit.invert (1);
    expect (pcsOf (chordsTool.padNotes (pad)) == pcsOf (gmaj7) && chordsTool.padNotes (pad).getFirst() == gmaj7[1], "INV + didn't put the 3rd on the bottom ("
                                                                                                                        + notesText (chordsTool.padNotes (pad)) + ")");
    chordEdit.invert (-1);

    // ---------------------------------------------------------------- the keys: C MAJ, then C# and D added: CUSTOM
    chordEdit.pickRoot (0);
    chordEdit.pickType (typeIndex ("MAJ"));
    const int low = chordsTool.padNotes (pad).getFirst();
    expect (! chordEdit.keyRect (low + 1).isEmpty(), "the keys don't show the chord's notes");
    chordEdit.toggleNote (low + 1);
    chordEdit.toggleNote (low + 2);
    hideToast();
    savePng (folder.getChildFile ("40a-edit-chord-keys-custom.png"));
    juce::Array<int> cluster { low, low + 1, low + 2, low + 4, low + 7 };
    expect (chordsTool.padNotes (pad) == cluster, "toggling C# and D on the keys gave " + notesText (chordsTool.padNotes (pad)));
    expect (chordsTool.padName (pad) == "CUSTOM", "a C C# D E G cluster is called " + chordsTool.padName (pad) + ", not CUSTOM");
    expect (pcsOf (played (pad)) == pcsOf (cluster), "the custom chord isn't what the pad sends the vocoder");
    // a toggled voicing that is a chord gets its name: take C# and D out, add the B: C MAJ7
    chordEdit.toggleNote (low + 1);
    chordEdit.toggleNote (low + 2);
    chordEdit.toggleNote (low + 11);
    expect (chordsTool.padName (pad) == chordmath::nameNotes (chordsTool.padNotes (pad)) && chordsTool.padName (pad).startsWith ("C"),
            "C E G B on the keys is called " + chordsTool.padName (pad));
    report << "the keys: C C# D E G shows CUSTOM; C E G B shows " << chordsTool.padName (pad) << "\n";
    chordEdit.toggleNote (low + 11);
    chordEdit.toggleNote (low + 1);
    chordEdit.toggleNote (low + 2);
    chordEdit.onDone();
    expect (! chordEditOpen, "DONE didn't close the chord sheet");
    hideToast();
    chordsTool.releaseAll();   // (the pads as they rest: the own chord's mark on a pad that isn't playing)
    {
        std::vector<float> l (256), r (256);
        chordsTool.engine().renderAt (l.data(), r.data(), 256, false, 0.0, 120.0);
    }
    chordsScreen.repaint();
    savePng (folder.getChildFile ("40b-chord-pad-own.png"));

    // ---------------------------------------------------------------- COPY CHORD TO pad 6 (the sub-menu), and the pad's menu
    chordsScreen.holdPad (pad);
    if (holdCard.isOpen() && holdCard.itemCount() > 1 && holdCard.current().items[1].text.startsWith ("COPY CHORD TO"))
        holdCard.choose (1);
    expect (holdCard.isOpen() && holdCard.current().title.startsWith ("COPY"), "COPY CHORD TO didn't open the list of pads");
    if (holdCard.isOpen() && holdCard.itemCount() >= 5 && holdCard.current().items[4].text == "PAD 6")
        holdCard.choose (4);
    else
        problems.add ("COPY CHORD TO: no PAD 6 in its list");
    expect (chordsTool.isOwn (5) && chordsTool.padNotes (5) == cluster, "COPY CHORD TO PAD 6 didn't give pad 6 the chord");
    hideToast();

    // ---------------------------------------------------------------- saved: another song and back, then a new key
    saveSong();
    {
        const auto other = ideas.create ("Own chord check B");
        switchIdea (other);
        expect (! chordsTool.isOwn (pad), "a new song's pad 3 has the other song's own chord");
        switchIdea (songId);
        ideas.remove (other);
    }
    expect (chordsTool.isOwn (pad) && chordsTool.padNotes (pad) == cluster && chordsTool.isOwn (5), "the own chords didn't come back when the song was opened again ("
                                                                                                        + notesText (chordsTool.padNotes (pad)) + ")");
    expect (pcsOf (played (pad)) == pcsOf (cluster), "after reopening, pad 3 doesn't play its own chord");
    const auto keyPad0 = chordsTool.padName (0);
    setSongKey ({ 0, false });
    hideToast();
    expect (chordsTool.padNotes (pad) == cluster && chordsTool.isOwn (5), "the key change (A minor to C major) changed the own chords");
    expect (chordsTool.padName (0) != keyPad0, "the key change didn't change the pads that follow the key");
    expect (pcsOf (played (pad)) == pcsOf (cluster), "after the key change, pad 3 doesn't play its own chord");
    report << "own chords stay after another song and back, and after A minor > C major (pad 1 went " << keyPad0 << " > " << chordsTool.padName (0) << ")\n";

    // ---------------------------------------------------------------- RESET TO SONG KEY (the menu, then the sheet)
    chordsScreen.holdPad (5);
    if (holdCard.isOpen() && holdCard.itemCount() > 2 && holdCard.current().items[2].text == "RESET TO SONG KEY")
        holdCard.choose (2);
    else
        problems.add ("an own chord's menu has no RESET TO SONG KEY");
    expect (! chordsTool.isOwn (5) && chordsTool.padNotes (5) == chordsTool.keyNotes (5), "RESET TO SONG KEY didn't give pad 6 the key's chord back");
    openChordEdit (pad);
    chordEdit.resetToKey();
    expect (! chordsTool.isOwn (pad), "the sheet's RESET TO SONG KEY didn't take the own chord off");
    closeChordEdit();
    // the progression plays an own chord too
    {
        auto own = chordsTool.ownChord (pad);
        own.on = true;
        own.notes = cluster;
        own.type = -1;
        chordsTool.setOwnChord (pad, own);
        chordsTool.progression = { pad, -1, -1, -1, -1, -1, -1, -1 };
        chordsTool.progressionOn = true;
        chordsTool.apply();
        auto& e = chordsTool.engine();
        const int before = e.vocoderSends();
        std::vector<float> l (256), r (256);
        double beat = 0.0;
        for (int i = 0; i < 20 && e.vocoderSends() == before; ++i, beat += 256 * 120.0 / (60.0 * 44100.0))
            e.renderAt (l.data(), r.data(), 256, true, beat, 120.0);
        expect (e.vocoderSends() != before && pcsOf (e.lastVocoderNotes()) == pcsOf (cluster), "the progression doesn't play pad 3's own chord");
        chordsTool.progressionOn = false;
        chordsTool.progression = { -1, -1, -1, -1, -1, -1, -1, -1 };
        chordsTool.apply();
        chordsTool.resetToKey (pad);
        e.renderAt (l.data(), r.data(), 256, false, 0.0, 120.0);
    }
    report << "COPY CHORD TO PAD 6, RESET TO SONG KEY (menu and sheet) and the progression playing an own chord work\n";

    showIdeasList();
    removeIdea (songId);
    hideToast();
    holdCard.close();
    return problems.joinIntoString ("\n");
}
