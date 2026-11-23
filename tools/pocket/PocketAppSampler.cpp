#include "PocketApp.h"

#include "PocketFactory.h"

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
void PocketApp::setupSampler()
{
    sampler.onPadSet = [this] (int pad) { beats.padsChanged(); beats.selectPad (pad); };
    sampler.onRepad = [this] (const juce::String& oldId, const juce::String& newId, int shift) { repadSound (oldId, newId, shift); };
    sampler.onPickPad = [this] { openPadPicker(); };
    sampler.currentBank = [this] { return beats.currentBank(); };
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
    if (holdCard.isOpen() && holdCard.itemCount() > 0 && holdCard.current().items[0].text == "SOUND PAGE")
        holdCard.choose (0);
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
        showIdeasList();
        switchIdea (songId);
        ideas.remove (fresh);
        report << "sampler: FACTORY SOUND BACK on a pad's hold menu; CLEAR empties the page (the pad keeps it), UNDO brings it back; "
                  "CHOP TO PADS puts 16 slices of one sound on A01-A16, each pad's sample page shows its slice, UNDO puts the kit back\n";
    }

    showIdeasList();
    hideToast();
    return problems.joinIntoString ("\n");
}
