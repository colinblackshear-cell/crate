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

juce::String PocketApp::runSamplerChecks (juce::String& report)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    hideToast();

    // ------------------------------------------------------------------ the sample page (PADS > hold > SOUND PAGE): KEEP, then ‹ PADS
    showIdeasList();
    switchIdea (ideas.create ("Trim check"));
    const auto songId = ideas.current().id;
    auto& kit = beats.kit();
    const int pad = 0;
    if (kit.pads[(size_t) pad].empty())
        return "Trim: a new song has no sound on pad A01";
    const auto factoryId = kit.pads[(size_t) pad].soundId;
    const auto original = kit.pads[(size_t) pad].audio;
    const int n = original->getNumSamples();
    const juce::Range<int> keep (n / 4, n * 3 / 4);
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

    showTool (Tool::pads, false);
    openSamplePage (pad);
    expect (page == Page::recorder && record.padTarget() == pad, "Trim: SOUND PAGE didn't open pad A01's sound");
    record.testSelect (keep);
    record.testKeep();
    record.testBack();   // ‹ PADS, without SAVE TO PAD: what the phone's owner did
    expect (page == Page::tool && tool == Tool::pads, "Trim: ‹ PADS didn't go back to the pads");
    padIsTrimmed ("right after KEEP and < PADS");
    expect (kit.pads[(size_t) pad].soundId != factoryId, "Trim: the pad still plays the factory sound, not the edited copy");
    if (const auto* f = lib.find (factoryId))
        expect (std::abs (f->seconds - (double) n / crate::kSampleRate) < 0.01, "Trim: the factory kit's sound got shorter for every song");

    // the song is saved, another one opened, and this one opened again
    const auto other = ideas.create ("Trim check B");
    switchIdea (other);
    expect (beats.kit().pads[(size_t) pad].audio != nullptr && beats.kit().pads[(size_t) pad].audio->getNumSamples() == n,
            "Trim: a new song's kit got the trimmed sound, not the factory one");
    switchIdea (songId);
    padIsTrimmed ("after the song was closed and opened again");

    // leaving by the header's back arrow (or the tool row) keeps an edit too
    {
        openSamplePage (pad);
        const int m = kit.pads[(size_t) pad].audio->getNumSamples();
        record.testSelect ({ 0, m / 2 });
        record.testKeep();
        goBack();
        expect (kit.pads[(size_t) pad].audio->getNumSamples() == m / 2, "Trim: leaving the sample page by the back arrow dropped the edit ("
                                                                          + juce::String (kit.pads[(size_t) pad].audio->getNumSamples()) + " samples, not " + juce::String (m / 2) + ")");
    }

    showIdeasList();
    ideas.remove (other);
    return problems.joinIntoString ("\n");
}
