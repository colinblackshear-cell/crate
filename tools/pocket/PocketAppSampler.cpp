#include "PocketApp.h"

#include "PocketFactory.h"
#include "core/SoundSet.h"

// The sampler: a pad's sound trimmed (or otherwise edited) is the sound the pad plays, at once and after the song is
// opened again. The checks drive the app itself with no sound card (screenshot mode), the way a finger would.
using namespace pocket;

namespace
{
// The largest difference between `got` and `want` from sample `from` of `want`, over `got`'s length (-1: they don't fit).
float differenceFrom (const juce::AudioBuffer<float>& got, const juce::AudioBuffer<float>& want, int from)
{
    if (from < 0 || from + got.getNumSamples() > want.getNumSamples())
        return -1.0f;
    float worst = 0.0f;
    for (int c = 0; c < got.getNumChannels(); ++c)
    {
        const auto* g = got.getReadPointer (c);
        const auto* w = want.getReadPointer (juce::jmin (c, want.getNumChannels() - 1));
        for (int i = 0; i < got.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (g[i] - w[from + i]));
    }
    return worst;
}
}

//==============================================================================
void PocketApp::showSampleToolMenu()
{
    pocket::HoldSpec s;
    s.title = "SAMPLE A TOOL";
    const auto& screen = loopsScreen;
    for (int l = 0; l < pocket::kLoopLayers; ++l)
    {
        juce::String name;
        double beatsLong = 0;
        if (screen.layerSound (l, &name, &beatsLong) == nullptr)
            continue;
        const double bars = beatsLong / 4.0;
        s.items.push_back ({ "LAYER " + juce::String (l + 1) + U8(" \xc2\xb7 ") + name.fromLastOccurrenceOf (" ", false, false).toUpperCase(),
                             juce::String (bars, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + (bars == 1.0 ? " bar" : " bars"),
                             false, true, [this, l] { sampleLayer (l); } });
    }
    if (screen.layersWithLoops() > 1)
        s.items.push_back ({ "ALL THE LOOPS", "mixed, as you hear them", false, true, [this] { sampleLayer (-1); } });
    s.items.push_back ({ "RECORD A TOOL FIRST", U8("pick it \xc2\xb7 REC \xc2\xb7 back here"), false, true, [this]
    {
        toast ("Pick a tool below (DRUMS, SYNTH, CHORDS...) and press REC: its take lands on a loop layer. Then SAMPLER > A TOOL brings it here.");
    } });
    holdMenu (s, sampler, sampler.getLocalBounds().getCentre().toFloat().withY (60.0f), {});
}

void PocketApp::showSetsMenu (juce::Component* anchor)
{
    pocket::HoldSpec s;
    const int bank = beats.currentBank();
    const auto bankName = juce::String::charToString ((juce::juce_wchar) ('A' + bank));
    s.title = "SETS";
    auto ask = [this, bankName] (bool whole)
    {
        auto* w = new juce::AlertWindow (whole ? "Save the whole kit as a set" : "Save bank " + bankName + " as a set",
                                         "Its sounds and pad settings, named:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", beats.kit().name + (whole ? juce::String() : " " + bankName));
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PocketApp> (this), w, whole] (int r)
        {
            if (safe != nullptr && r == 1)
                safe->saveBankAsSet (w->getTextEditorContents ("name"), whole);
        }), true);
    };
    s.items.push_back ({ "SAVE BANK " + bankName + " AS A SET", "its 16 pads and settings", false, true, [ask] { ask (false); } });
    s.items.push_back ({ "SAVE THE WHOLE KIT", "all 64 pads", false, true, [ask] { ask (true); } });
    for (const auto& folder : SoundSet::list (setsFolder()))
        if (const auto set = SoundSet::peek (folder))
            s.items.push_back ({ "LOAD " + set->name.toUpperCase(), set->describe() + (set->wholeKit ? juce::String() : U8(" \xc2\xb7 onto bank ") + bankName),
                                 false, true, [this, folder] { loadSetFolder (folder); } });
    auto& over = anchor != nullptr ? *anchor : static_cast<juce::Component&> (soundBank);
    holdMenu (s, over, over.getLocalBounds().getCentre().toFloat().withY (40.0f), {});
}

juce::File PocketApp::saveBankAsSet (const juce::String& name, bool wholeKit)
{
    const auto& kit = beats.kit();
    SoundSet set;
    set.name = name.trim().isNotEmpty() ? name.trim() : kit.name;
    set.wholeKit = wholeKit;
    set.bpm = beats.engine().getBpm();
    set.madeOn = "Phone";
    set.saved = juce::Time::getCurrentTime();
    const int first = wholeKit ? 0 : beats.currentBank() * 16, count = wholeKit ? kPads : 16;
    for (int i = 0; i < count; ++i)
    {
        const auto& p = kit.pads[(size_t) (first + i)];
        if (p.empty())
            continue;
        SoundSet::Pad s;
        s.index = i;
        s.name = p.name;
        s.audio = p.audio;   // as it plays (its part of the sound, with the sound's effects)
        s.volume = p.volume;
        s.pan = p.pan;
        s.tune = p.tune;
        s.mode = p.mode == pocket::PadMode::noteOff ? 2 : p.mode == pocket::PadMode::mono ? 1 : 0;
        set.pads.push_back (s);
    }
    juce::String error;
    const auto folder = set.save (setsFolder(), error);
    toast (folder.exists() ? "Saved the set " + set.name + U8(" (") + set.describe() + "). PADS > SOUNDS > SETS loads it." : error);
    return folder;
}

bool PocketApp::loadSetFolder (const juce::File& folder)
{
    juce::String error;
    const auto set = SoundSet::load (folder, error);
    if (! set)
    {
        toast (error);
        return false;
    }
    auto& kit = beats.kit();
    const auto before = kit.pads;
    const int first = set->wholeKit ? 0 : beats.currentBank() * 16, count = set->wholeKit ? kPads : 16;
    for (int i = 0; i < count; ++i)
    {
        beats.engine().padOff (first + i);
        kit.clear (first + i);
    }
    for (const auto& s : set->pads)
    {
        const int pad = first + s.index;
        if (! juce::isPositiveAndBelow (pad, kPads) || s.index >= count || s.audio == nullptr)
            continue;
        const auto id = lib.add (s.name.isNotEmpty() ? s.name : set->name + " " + Kit::padLabel (pad), pocket::kTagImported, *s.audio, {}, {});
        const auto* item = lib.find (id);
        if (item == nullptr)
            continue;
        kit.assign (pad, *item, s.audio, 0, -1, s.name);
        auto& p = kit.pads[(size_t) pad];
        p.volume = s.volume;
        p.pan = s.pan;
        p.tune = s.tune;
        p.mode = s.mode == 2 ? pocket::PadMode::noteOff : s.mode == 1 ? pocket::PadMode::mono : pocket::PadMode::oneShot;
    }
    beats.padsChanged();
    closeSoundBank();
    toastAction ("The set " + set->name + " is on " + (set->wholeKit ? juce::String ("the whole kit") : "bank " + juce::String::charToString ((juce::juce_wchar) ('A' + first / 16))),
                 "UNDO", [this, before]
                 {
                     beats.kit().pads = before;
                     beats.padsChanged();
                 });
    return true;
}

void PocketApp::sampleLayer (int layer)
{
    juce::String name;
    double beatsLong = 0;
    auto sound = loopsScreen.layerSound (layer, &name, &beatsLong);
    if (sound == nullptr)
        return toast ("That layer has no loop");
    showTool (Tool::sampler, false);
    sampler.useTake (sound, name.replace ("Pocket loop", "Loop"));
    toast (name.replace ("Pocket loop", "Loop") + " is on the SAMPLER. Shape it, then CHOP TO PADS or SEND TO PAD.");
}

//==============================================================================
void PocketApp::setupSampler()
{
    sampler.onPadSet = [this] (int pad) { beats.padsChanged(); beats.selectPad (pad); };
    sampler.onRepad = [this] (const juce::String& oldId, const juce::String& newId, int shift) { repadSound (oldId, newId, shift); };
    sampler.onPickPad = [this] { openPadPicker(); };
    sampler.currentBank = [this] { return beats.currentBank(); };
    sampler.onSampleTool = [this] { showSampleToolMenu(); };
    sampler.onShowPads = [this] { showTool (Tool::pads, false); };
    sampler.onSaveSet = [this] { showSetsMenu (&sampler); };
    sampler.onMoreEdits = [this] (const juce::String& id, int pad)
    {
        // the sound recorder: fades, normalize, reverse, effects, chop and the name. On the pad's own sound it is the
        // pad's sample page (leaving it saves onto the pad); either way BACK comes here with the edited sound.
        showPage (Page::recorder);
        if (pad >= 0 && beats.kit().pads[(size_t) pad].soundId == id)
            record.editForPad (pad, id);
        else
            record.loadSound (id);
    };
    padPicker.onPick = [this] (int pad)
    {
        closePadPicker();
        sampler.sendTo (pad);
    };
    padPicker.onCancel = [this] { closePadPicker(); };
    padPicker.onDismiss = [this] { closePadPicker(); };
}

// After a TRIM (or its UNDO) saved the sound: every pad of this song that played `oldId` plays `newId`, its part of the
// sound moved by `shift` samples (a part the trim cut away whole leaves the pad on the whole new sound).
void PocketApp::repadSound (const juce::String& oldId, const juce::String& newId, int shift)
{
    auto& kit = beats.kit();
    const auto* item = lib.find (newId);
    auto whole = lib.load (newId);
    if (item == nullptr || whole == nullptr)
        return;
    const int n = whole->getNumSamples();
    for (int p = 0; p < kPads; ++p)
    {
        const auto old = kit.pads[(size_t) p];
        if (old.empty() || old.soundId != oldId)
            continue;
        int from = juce::jmax (0, old.from + shift), to = old.to < 0 ? -1 : old.to + shift;
        if (to >= n)
            to = -1;
        if (from >= n || (to >= 0 && to <= from))
        {
            from = 0;
            to = -1;
        }
        kit.assign (p, *item, whole, from, to, old.name);
        auto& now = kit.pads[(size_t) p];
        now.volume = old.volume;
        now.pan = old.pan;
        now.tune = old.tune;
        now.mode = old.mode;
    }
    beats.padsChanged();
}

juce::Rectangle<int> PocketApp::padPickerBounds() const
{
    const int h = padPicker.wantedHeight() + insets.getBottom();
    return { 0, getHeight() - h, getWidth(), h };
}

//==============================================================================
juce::String PocketApp::runSamplerChecks (juce::String& report)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    hideToast();

    showIdeasList();
    switchIdea (ideas.create ("Trim check"));
    const auto songId = ideas.current().id;
    auto& kit = beats.kit();
    const int pad = 0;
    if (kit.pads[(size_t) pad].empty())
        return "Trim: a new song has no sound on pad A01";
    const auto factoryId = kit.pads[(size_t) pad].soundId;
    auto original = kit.pads[(size_t) pad].audio;
    int n = original->getNumSamples();
    juce::Range<int> keep (n / 4, n * 3 / 4);
    auto padIsTrimmed = [&] (const juce::String& when)
    {
        const auto& now = kit.pads[(size_t) pad];
        if (now.empty())
            return problems.add ("Trim: pad A01 is empty " + when);
        const float diff = differenceFrom (*now.audio, *original, keep.getStart());
        report << "trim " << when << ": pad A01 plays " << now.audio->getNumSamples() << " samples (was " << n << ", trimmed to " << keep.getLength()
               << "), largest difference from the kept part " << juce::String (diff, 6) << "\n";
        expect (now.audio->getNumSamples() == keep.getLength(), "Trim: " + when + " pad A01 plays " + juce::String (now.audio->getNumSamples())
                                                                    + " samples, not the trimmed " + juce::String (keep.getLength()));
        expect (diff >= 0.0f && diff < 1.0e-3f, "Trim: " + when + " pad A01 doesn't play the part that was kept (difference " + juce::String (diff, 5) + ")");
    };
    auto reopen = [&]
    {
        const auto other = ideas.create ("Trim check B");
        switchIdea (other);
        const bool fresh = beats.kit().pads[(size_t) pad].audio != nullptr && beats.kit().pads[(size_t) pad].audio->getNumSamples() == lib.load (factoryId)->getNumSamples();
        switchIdea (songId);
        ideas.remove (other);
        return fresh;
    };

    // ------------------------------------------------------------------ SAMPLER (PADS > hold > SOUND PAGE): IN, OUT, TRIM
    showTool (Tool::pads, false);
    beats.holdPad (pad);
    if (holdCard.isOpen() && holdCard.itemCount() > 1 && holdCard.current().items[1].text == "SOUND PAGE")
        holdCard.choose (1);
    else
        problems.add ("Sampler: the pad's hold menu has no SOUND PAGE");
    expect (page == Page::tool && tool == Tool::sampler && sampler.target() == pad && sampler.soundId() == factoryId,
            "Sampler: SOUND PAGE didn't open pad A01's sound on the SAMPLER, aimed at A01");
    sampler.setSelection (keep);
    sampler.trim();
    padIsTrimmed ("on the SAMPLER, right after TRIM");
    expect (kit.pads[(size_t) pad].soundId != factoryId, "Trim: the pad still plays the factory sound, not your trimmed copy");
    if (const auto* f = lib.find (factoryId))
        expect (std::abs (f->seconds - (double) n / crate::kSampleRate) < 0.01, "Trim: the factory kit's sound got shorter for every song");
    expect (reopen(), "Trim: a new song's kit got the trimmed sound, not the factory one");
    padIsTrimmed ("on the SAMPLER, after the song was closed and opened again");
    // UNDO puts the whole sound back on the pad
    showTool (Tool::sampler, false);
    sampler.editForPad (pad);
    {
        const int m = kit.pads[(size_t) pad].audio->getNumSamples();
        sampler.setSelection ({ 0, m / 2 });
        sampler.trim();
        expect (kit.pads[(size_t) pad].audio->getNumSamples() == m / 2, "Sampler: a second TRIM didn't reach the pad");
        sampler.undo();
        expect (kit.pads[(size_t) pad].audio->getNumSamples() == m, "Sampler: UNDO didn't put the untrimmed sound back on the pad ("
                                                                      + juce::String (kit.pads[(size_t) pad].audio->getNumSamples()) + " samples, not " + juce::String (m) + ")");
    }

    // SEND TO PAD: IN..OUT onto another pad (from the 4 x 4 picker), as that pad's part of the sound; UNDO takes it back
    {
        const int to = 5;
        const auto before = kit.pads[(size_t) to];
        const int m = sampler.sound()->getNumSamples();
        sampler.setSelection ({ m / 10, m / 2 });
        openPadPicker();
        expect (padPickOpen && padPicker.isVisible(), "Sampler: PICK PAD didn't open the pad grid");
        padPicker.onPick (to);
        expect (! padPickOpen, "Sampler: picking a pad didn't close the grid");
        expect (kit.pads[(size_t) to].soundId == sampler.soundId() && kit.pads[(size_t) to].audio->getNumSamples() == m / 2 - m / 10,
                "Sampler: SEND TO PAD A06 didn't put IN..OUT on it");
        expect (sampler.target() == to, "Sampler: the pad it was sent to isn't the target now");
        expect (toastHasAction(), "Sampler: no UNDO after SEND TO PAD");
        pressToastAction();
        expect (kit.pads[(size_t) to].soundId == before.soundId && kit.pads[(size_t) to].audio == before.audio, "Sampler: UNDO didn't give pad A06 its sound back");
    }

    // a new take (what REC hands over when it stops; there's no mic here) goes on the pad, and into Sounds
    {
        const int soundCount = (int) lib.items().size();
        sampler.useTake (lib.load (lib.items().back().id));
        expect (sampler.soundId().isEmpty() && sampler.hasSound(), "Sampler: a new take isn't on the page");
        expect (sampler.sendTo (pad), "Sampler: SEND TO PAD refused the new take");
        expect ((int) lib.items().size() == soundCount + 1 && kit.pads[(size_t) pad].soundId == sampler.soundId(), "Sampler: the new take didn't go into Sounds and onto the pad");
        hideToast();
    }
    report << "sampler: SOUND PAGE opens the pad's sound aimed at it; TRIM reaches the pad at once and after reopening (a factory sound becomes your copy); "
              "UNDO puts the uncut sound back on the pad; SEND TO PAD (picked on the 4 x 4 grid) puts IN..OUT on it, with UNDO; a new take goes into Sounds and onto the pad\n";

    // ------------------------------------------------------------------ the sound recorder in pad mode (SAMPLER > MORE EDITS): KEEP, then BACK
    {
        original = kit.pads[(size_t) pad].audio;
        n = original->getNumSamples();
        keep = { n / 4, n * 3 / 4 };
        showTool (Tool::sampler, false);
        sampler.editForPad (pad);
        sampler.onMoreEdits (sampler.soundId(), pad);
        expect (page == Page::recorder && record.padTarget() == pad, "Recorder: MORE EDITS didn't open pad A01's sound in pad mode");
        record.testSelect (keep);
        record.testKeep();
        record.testBack();   // < SAMPLER, without SAVE TO PAD: what the phone's owner did on the old sample page
        expect (page == Page::tool && tool == Tool::sampler, "Recorder: < SAMPLER didn't go back to the SAMPLER");
        padIsTrimmed ("in the sound recorder, right after KEEP and BACK");
        expect (sampler.sound() != nullptr && sampler.sound()->getNumSamples() == keep.getLength(), "Recorder: the SAMPLER doesn't show the edited sound");
        reopen();
        padIsTrimmed ("in the sound recorder, after the song was closed and opened again");
        // leaving by the header's back arrow keeps an edit too
        sampler.editForPad (pad);
        sampler.onMoreEdits (sampler.soundId(), pad);
        const int m = kit.pads[(size_t) pad].audio->getNumSamples();
        record.testSelect ({ 0, m / 2 });
        record.testKeep();
        goBack();
        expect (kit.pads[(size_t) pad].audio->getNumSamples() == m / 2, "Recorder: leaving by the back arrow dropped the edit ("
                                                                          + juce::String (kit.pads[(size_t) pad].audio->getNumSamples()) + " samples, not " + juce::String (m / 2) + ")");
    }

    // ------------------------------------------------------------------ FACTORY SOUND BACK, CLEAR, CHOP TO PADS
    {
        showIdeasList();
        const auto fresh = ideas.create ("Chop check");
        switchIdea (fresh);
        auto& k = beats.kit();
        const int a03 = 2;
        const auto factoryA03 = k.pads[(size_t) a03].soundId;
        expect (factoryA03.isNotEmpty(), "Factory: a new song has no sound on A03");

        // your own sound over a factory pad, then FACTORY SOUND BACK from the pad's hold menu
        showTool (Tool::sampler, false);
        sampler.useTake (lib.load (lib.items().back().id));
        expect (sampler.sendTo (a03), "Factory: SEND TO PAD A03 refused");
        hideToast();
        expect (k.pads[(size_t) a03].soundId != factoryA03, "Factory: A03 still plays the factory sound after SEND");
        showTool (Tool::pads, false);
        beats.holdPad (a03);
        int back = -1;
        if (holdCard.isOpen())
            for (int i = 0; i < holdCard.itemCount(); ++i)
                if (holdCard.current().items[(size_t) i].text == "FACTORY SOUND BACK")
                    back = i;
        expect (back >= 0, "Factory: A03's hold menu has no FACTORY SOUND BACK");
        if (back >= 0)
            holdCard.choose (back);
        expect (k.pads[(size_t) a03].soundId == factoryA03, "Factory: FACTORY SOUND BACK didn't put the kit's own sound back on A03");
        hideToast();

        // CLEAR empties the page (the pad keeps its sound), UNDO brings it back
        showTool (Tool::sampler, false);
        sampler.editForPad (a03);
        const auto shown = sampler.sound();
        sampler.clearSound();
        expect (! sampler.hasSound(), "Clear: the sampler still has a sound");
        expect (k.pads[(size_t) a03].soundId == factoryA03 && ! k.pads[(size_t) a03].empty(), "Clear: the pad lost its sound");
        expect (toastHasAction(), "Clear: no UNDO after CLEAR");
        pressToastAction();
        expect (sampler.hasSound() && sampler.sound() == shown, "Clear: UNDO didn't bring the sound back");
        hideToast();

        // CHOP TO PADS: 16 equal slices onto A01-A16, each pad its own part of one sound; UNDO puts the kit back
        sampler.useTake (lib.load (lib.items().back().id));
        sampler.setTarget (-1);
        const int whole = sampler.sound()->getNumSamples();
        const auto before = k.pads[0].soundId;
        expect (sampler.chopToPads (16) == 16, "Chop: 16 EQUAL SLICES didn't fill 16 pads");
        const auto id = sampler.soundId();
        int sum = 0;
        bool same = true;
        for (int p = 0; p < 16; ++p)
        {
            same = same && k.pads[(size_t) p].soundId == id;
            sum += k.pads[(size_t) p].audio != nullptr ? k.pads[(size_t) p].audio->getNumSamples() : 0;
        }
        expect (same, "Chop: the 16 pads don't all play slices of the one sound");
        expect (std::abs (sum - whole) < 16, "Chop: the slices don't add up to the whole sound (" + juce::String (sum) + " of " + juce::String (whole) + ")");
        expect (k.pads[(size_t) 16].soundId.isEmpty() || k.pads[(size_t) 16].soundId != id, "Chop: a slice went past A16");
        // a slice's pad opens that slice on its sample page
        sampler.editForPad (4);
        expect (sampler.selection().getStart() == k.pads[4].from && sampler.selection().getLength() > 0, "Chop: A05's sample page doesn't show its slice");
        expect (toastHasAction(), "Chop: no UNDO after CHOP TO PADS");
        if (toastHasAction())
            pressToastAction();
        expect (k.pads[0].soundId == before, "Chop: UNDO didn't put A01 back");
        hideToast();

        // EDIT THIS SLICE: a chopped pad's part becomes its own sound, open alone in the sound editor aimed at the pad
        sampler.useTake (lib.load (lib.items().back().id));
        sampler.setTarget (-1);
        expect (sampler.chopToPads (8) == 8, "Edit slice: 8 EQUAL SLICES didn't fill 8 pads");
        hideToast();
        {
            const auto chopId = k.pads[4].soundId;
            const int length = k.pads[4].audio != nullptr ? k.pads[4].audio->getNumSamples() : 0;
            showTool (Tool::pads, false);
            beats.holdPad (4);
            const bool hasRow = holdCard.isOpen() && holdCard.itemCount() > 0 && holdCard.current().items[0].text == "EDIT THIS SLICE";
            expect (hasRow, "Edit slice: a pad's hold menu doesn't start with EDIT THIS SLICE");
            if (hasRow)
                holdCard.choose (0);
            else
                holdCard.close();
            const auto& pad = k.pads[4];
            expect (pad.soundId != chopId && pad.from == 0 && pad.to < 0, "Edit slice: A05 doesn't play its slice as its own whole sound");
            expect (pad.audio != nullptr && std::abs (pad.audio->getNumSamples() - length) < 4, "Edit slice: A05's own sound isn't the length of its slice");
            expect (k.pads[5].soundId == chopId, "Edit slice: the other slices moved off the chopped sound");
            expect (page == Page::recorder && record.padTarget() == 4 && record.soundId() == pad.soundId, "Edit slice: the sound editor isn't open on A05's slice, aimed at A05");
            expect (toastHasAction(), "Edit slice: no UNDO");
            if (toastHasAction())
                pressToastAction();
            expect (k.pads[4].soundId == chopId && k.pads[4].from > 0, "Edit slice: UNDO didn't put A05 back on its slice");
            hideToast();
            // a pad that already plays its own whole sound opens that sound, no copy
            const auto ownId = k.pads[20].soundId;
            if (! k.pads[20].empty() && lib.find (ownId) != nullptr)
            {
                int sharing = 0;
                for (auto& q : k.pads)
                    sharing += (! q.empty() && q.soundId == ownId) ? 1 : 0;
                const auto* own = lib.find (ownId);
                if (sharing == 1 && k.pads[20].from == 0 && k.pads[20].to < 0 && own->tag != pocket::kTagFactory)
                {
                    const auto count = lib.items().size();
                    editPadSlice (20);
                    expect (lib.items().size() == count && record.soundId() == ownId, "Edit slice: a pad's own whole sound got copied");
                }
            }
            showTool (Tool::pads, false);
        }

        // A TOOL: a loop layer (a SYNTH take) comes onto the SAMPLER, ready to chop
        {
            auto& L = loopsScreen.engine();
            const double sr = 44100.0;
            const int n = (int) (8.0 * 60.0 / L.bpm() * sr);
            auto t = pocket::LoopsEngine::makeTake (n, sr);
            for (int i = 0; i < n; ++i)
                t->data[(size_t) i] = 0.3f * (float) std::sin (i * 0.03);
            t->length = n;
            t->lengthBeats = 8.0;
            t->tool = (int) pocket::LayerTool::synth;
            L.setLayer (0, t, false, 0.0);
            std::vector<float> l (512), r (512);
            for (int b = 0; b < 8; ++b)
                L.render (l.data(), r.data(), 512);
            showTool (Tool::sampler, false);
            sampler.clear();
            showSampleToolMenu();
            const bool hasLayer = holdCard.isOpen() && holdCard.itemCount() >= 2 && holdCard.current().items[0].text.startsWith ("LAYER 1")
                                  && holdCard.current().items[0].text.endsWith ("SYNTH");
            expect (hasLayer, "A tool: the card doesn't list LAYER 1 (the SYNTH take)");
            if (hasLayer)
                holdCard.choose (0);
            else
                holdCard.close();
            expect (sampler.hasSound() && sampler.soundName().startsWith ("Loop 1"), "A tool: LAYER 1 didn't come onto the SAMPLER");
            if (sampler.hasSound())
                expect (std::abs (sampler.sound()->getNumSamples() - (int) (8.0 * 60.0 / L.bpm() * crate::kSampleRate)) < 64,
                        "A tool: the layer came in at the wrong length");
            hideToast();
            L.setLayer (0, nullptr, false);
            for (int b = 0; b < 4; ++b)
                L.render (l.data(), r.data(), 512);
            sampler.clear();
        }

        // SETS: bank A saved as a set, loaded back onto an empty bank A with its settings; UNDO
        {
            showTool (Tool::pads, false);
            beats.selectPad (0);
            sampler.chopToPads (8);
            hideToast();
            k.pads[2].volume = 63;
            k.pads[2].tune = 25;
            const int length3 = k.pads[2].audio != nullptr ? k.pads[2].audio->getNumSamples() : 0;
            const auto name3 = k.pads[2].name;
            const auto folder = saveBankAsSet ("Check set", false);
            hideToast();
            const auto peeked = SoundSet::peek (folder);
            int used = 0;
            for (int p = 0; p < 16; ++p)
                used += k.pads[(size_t) p].empty() ? 0 : 1;
            expect (folder.isDirectory() && peeked && peeked->padCount() == used, "Sets: SAVE BANK A didn't save one sound a pad");
            for (int p = 0; p < 16; ++p)
                k.clear (p);
            beats.padsChanged();
            expect (loadSetFolder (folder), "Sets: LOAD didn't load the set");
            expect (! k.pads[2].empty() && std::abs (k.pads[2].audio->getNumSamples() - length3) < 4 && k.pads[2].volume == 63 && k.pads[2].tune == 25 && k.pads[2].name == name3,
                    "Sets: A03 didn't come back with its sound and settings");
            expect (toastHasAction(), "Sets: no UNDO after LOAD");
            if (toastHasAction())
                pressToastAction();
            expect (k.pads[2].empty(), "Sets: UNDO didn't put bank A back");
            hideToast();
            folder.deleteRecursively();
        }

        // the steps: 1 GET lit on an empty page, 4 PADS after CHOP TO PADS, and 1 GET opens its menu
        {
            showTool (Tool::sampler, false);
            sampler.clear();
            expect (sampler.currentStep() == 0, "Steps: an empty SAMPLER doesn't light 1 GET");
            sampler.pressStep (0);
            expect (holdCard.isOpen() && holdCard.itemCount() == 3, "Steps: 1 GET didn't offer the mic, a tool and sounds");
            holdCard.close();
            sampler.useTake (lib.load (lib.items().back().id));
            expect (sampler.currentStep() == 1, "Steps: a sound on the page doesn't light 2 SHAPE");
            sampler.chopToPads (4);
            hideToast();
            expect (sampler.currentStep() == 3, "Steps: after CHOP TO PADS, 4 PADS isn't lit");
            sampler.pressStep (3);
            expect (page == Page::tool && tool == Tool::pads, "Steps: 4 PADS didn't go to the pads");
        }

        // Rec setup > PADS RECORD INTO > THE PAD PATTERN: REC on PADS plays into the pattern and never arms a layer
        {
            const int keepTo = appSettings.padsRecTo;
            appSettings.padsRecTo = 1;
            showTool (Tool::pads, false);
            const int layersBefore = loopsScreen.layerCount();
            recPads();
            expect (lockKind == LockKind::pads && playingIn() && lockStart > 1.0e14, "Pads into the pattern: REC didn't start playing into the pad pattern");
            lockWatchTick();
            stopRec();
            expect (lockKind == LockKind::none && ! beats.recordingPads() && loopsScreen.busyLayer() < 0 && loopsScreen.layerCount() == layersBefore,
                    "Pads into the pattern: REC again didn't stop, or a layer got armed");
            appSettings.padsRecTo = keepTo;
            hideToast();
        }
        showIdeasList();
        switchIdea (songId);
        ideas.remove (fresh);
        report << "sampler: FACTORY SOUND BACK on a pad's hold menu; CLEAR empties the page (the pad keeps it), UNDO brings it back; "
                  "CHOP TO PADS puts 16 slices of one sound on A01-A16, each pad's sample page shows its slice, UNDO puts the kit back; "
                  "EDIT THIS SLICE makes a chop its pad's own sound and opens it alone in the sound editor (UNDO puts the chop back); "
                  "A TOOL brings a loop layer (any tool's take) onto the SAMPLER at its length; SETS saves bank A and loads it back with its pad settings (UNDO); PADS RECORD INTO THE PAD PATTERN plays REC into the pattern only (no layer)\n";
    }

    showIdeasList();
    hideToast();
    return problems.joinIntoString ("\n");
}
