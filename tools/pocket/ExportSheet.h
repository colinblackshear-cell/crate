#pragma once

#include "PocketSheet.h"
#include "PocketTheme.h"
#include "SongScreen.h"

// EXPORT: the song (a part of it, or the layers playing in the looper) out as files, to Logic or anywhere else.
// A bottom sheet of big chips: WHAT (the song, a part, the looper now), SPLIT (the mix, or each layer as its own
// stem + the mix), FORMAT (WAV, AIFF, MP3: any mix of them, one at least) and the ECHO TAIL, with a line saying
// what it will write, EXPORT (writes the files, then the Share sheet) and TO MAC (the last export).
// The app (PocketApp) shows it, writes the files and does the sharing; the choices live in `settings`.
class ExportSheet : public pocket::DragSheet
{
public:
    ExportSheet();

    SongScreen::Bounce settings;
    // The app's
    std::function<juce::String()> partName;                       // the picked part on SONG's ruler ("" when none)
    std::function<juce::String (const SongScreen::Bounce&)> summary;
    std::function<void (const SongScreen::Bounce&)> onExport;
    std::function<void()> onToMac;
    std::function<void (const juce::String&)> toast;

    enum Id
    {
        none = 0, whatSong = 1, whatPart, whatLoops, splitMix, splitStems, fmtWav, fmtAiff, fmtMp3,
        tailOff, tail2, tail4, exportId, toMacId
    };
    // What the chips do (the self-checks use these too).
    void press (int id);
    juce::Component* chip (int id);
    // Opens with WHAT set (loops: from the looper; song: from SONG, keeping a part if one is picked).
    void show (SongScreen::Bounce::What what);
    void refresh();
    int preferredHeight() const { return 520; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    bool partPicked() const { return partName != nullptr && partName().isNotEmpty(); }
    bool mp3Here() const;

    pocket::PButton song { "THE SONG" }, part { "A PART" }, looper { "LOOPER NOW" };
    pocket::PButton mix { "MIX" }, stems { "STEMS + MIX" };
    pocket::PButton wav { "WAV" }, aiff { "AIFF" }, mp3 { "MP3" };
    pocket::PButton tailOffB { "OFF" }, tail2B { "2 S" }, tail4B { "4 S" };
    pocket::PButton exportB { "EXPORT", pocket::Look::amber }, toMac { "TO MAC", pocket::Look::green };
    juce::Rectangle<int> whatLabel, splitLabel, formatLabel, tailLabel, hintArea, summaryArea;
};
