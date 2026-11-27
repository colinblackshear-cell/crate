#include "ExportSheet.h"

#include "Mp3Writer.h"

using namespace pocket;

namespace
{
constexpr int kChipH = 46, kSide = 24, kGap = 8;
}

ExportSheet::ExportSheet()
{
    setDragZone (60);
    song.onClick = [this] { press (whatSong); };
    part.onClick = [this] { press (whatPart); };
    looper.onClick = [this] { press (whatLoops); };
    mix.onClick = [this] { press (splitMix); };
    stems.onClick = [this] { press (splitStems); };
    wav.onClick = [this] { press (fmtWav); };
    aiff.onClick = [this] { press (fmtAiff); };
    mp3.onClick = [this] { press (fmtMp3); };
    tailOffB.onClick = [this] { press (tailOff); };
    tail2B.onClick = [this] { press (tail2); };
    tail4B.onClick = [this] { press (tail4); };
    exportB.onClick = [this] { press (exportId); };
    toMac.onClick = [this] { press (toMacId); };
    for (auto* b : { &song, &part, &looper, &mix, &stems, &wav, &aiff, &mp3, &tailOffB, &tail2B, &tail4B, &exportB, &toMac })
    {
        b->setFontHeight (16.0f);
        addAndMakeVisible (*b);
    }
    exportB.setFontHeight (22.0f);
    toMac.setFontHeight (18.0f);
    mp3.setVisible (mp3Here());
}

bool ExportSheet::mp3Here() const
{
    return mp3::available();
}

juce::Component* ExportSheet::chip (int id)
{
    switch (id)
    {
        case whatSong: return &song;
        case whatPart: return &part;
        case whatLoops: return &looper;
        case splitMix: return &mix;
        case splitStems: return &stems;
        case fmtWav: return &wav;
        case fmtAiff: return &aiff;
        case fmtMp3: return &mp3;
        case tailOff: return &tailOffB;
        case tail2: return &tail2B;
        case tail4: return &tail4B;
        case exportId: return &exportB;
        case toMacId: return &toMac;
        default: break;
    }
    return nullptr;
}

void ExportSheet::show (SongScreen::Bounce::What what)
{
    settings.sendBack = false;
    settings.withTrack = true;
    settings.what = what;
    if (settings.what == SongScreen::Bounce::part && ! partPicked())
        settings.what = SongScreen::Bounce::song;
    if (! mp3Here())
        settings.mp3 = false;
    if (! settings.wav && ! settings.aiff && ! settings.mp3)
        settings.wav = true;
    refresh();
}

void ExportSheet::press (int id)
{
    auto& s = settings;
    switch (id)
    {
        case whatSong: s.what = SongScreen::Bounce::song; break;
        case whatPart:
            if (! partPicked())
            {
                if (toast)
                    toast ("Close this, tap a part on the ruler first, then EXPORT.");
                return;
            }
            s.what = SongScreen::Bounce::part;
            break;
        case whatLoops: s.what = SongScreen::Bounce::loops; break;
        case splitMix: s.eachLayer = false; break;
        case splitStems: s.eachLayer = true; break;
        case fmtWav: if (! s.wav || s.aiff || s.mp3) s.wav = ! s.wav; break;
        case fmtAiff: if (! s.aiff || s.wav || s.mp3) s.aiff = ! s.aiff; break;
        case fmtMp3: if (mp3Here() && (! s.mp3 || s.wav || s.aiff)) s.mp3 = ! s.mp3; break;
        case tailOff: s.tailSeconds = 0.0; break;
        case tail2: s.tailSeconds = 2.0; break;
        case tail4: s.tailSeconds = 4.0; break;
        case exportId:
            if (onExport)
                onExport (s);
            return;
        case toMacId:
            if (onToMac)
                onToMac();
            return;
        default: return;
    }
    refresh();
}

void ExportSheet::refresh()
{
    const auto& s = settings;
    auto pick = [] (PButton& b, bool on) { b.setLook (on ? Look::amber : Look::cream); b.setToggled (on); };
    pick (song, s.what == SongScreen::Bounce::song);
    pick (looper, s.what == SongScreen::Bounce::loops);
    const auto name = partName != nullptr ? partName() : juce::String();
    part.setText (name.isNotEmpty() ? name.toUpperCase() : juce::String ("A PART"));
    pick (part, s.what == SongScreen::Bounce::part && name.isNotEmpty());
    if (name.isEmpty())
        part.setLook (Look::ghost);
    pick (mix, ! s.eachLayer);
    pick (stems, s.eachLayer);
    pick (wav, s.wav);
    pick (aiff, s.aiff);
    pick (mp3, s.mp3);
    pick (tailOffB, s.tailSeconds <= 0.0);
    pick (tail2B, s.tailSeconds > 0.0 && s.tailSeconds <= 2.0);
    pick (tail4B, s.tailSeconds > 2.0);
    mp3.setVisible (mp3Here());
    resized();
    repaint();
}

void ExportSheet::resized()
{
    const int w = getWidth(), inner = w - 2 * kSide;
    int y = 56;
    auto row = [&] (juce::Rectangle<int>& label, std::initializer_list<PButton*> chips, int extra = 0)
    {
        label = { kSide, y, inner, 18 };
        y += 20;
        int shown = 0;
        for (auto* c : chips)
            shown += c->isVisible() ? 1 : 0;
        const int each = (inner - kGap * (juce::jmax (1, shown) - 1)) / juce::jmax (1, shown);
        int x = kSide;
        for (auto* c : chips)
            if (c->isVisible())
            {
                c->setBounds (x, y, each, kChipH);
                x += each + kGap;
            }
        y += kChipH + extra + 8;
    };
    row (whatLabel, { &song, &part, &looper }, 0);
    hintArea = { kSide, y - 8, inner, 16 };
    y += 12;
    row (splitLabel, { &mix, &stems });
    row (formatLabel, { &wav, &aiff, &mp3 });
    row (tailLabel, { &tailOffB, &tail2B, &tail4B });
    summaryArea = { kSide, y, inner, 44 };
    y += 44 + 12;
    const int bigW = (inner - kGap) * 62 / 100;
    exportB.setBounds (kSide, y, bigW, 56);
    toMac.setBounds (kSide + bigW + kGap, y, inner - bigW - kGap, 56);
}

void ExportSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (26.0f));
    g.drawText ("EXPORT", kSide, 16, 200, 36, juce::Justification::centredLeft);

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (14.0f)));
    g.drawText ("WHAT", whatLabel, juce::Justification::centredLeft);
    g.drawText ("SPLIT", splitLabel, juce::Justification::centredLeft);
    g.drawText ("FORMAT", formatLabel, juce::Justification::centredLeft);
    g.drawText ("ECHO TAIL", tailLabel, juce::Justification::centredLeft);
    if (! partPicked())
    {
        g.setColour (col::inkFaint);
        g.setFont (small (13.0f));
        g.drawText ("tap a part on the ruler first", hintArea, juce::Justification::centredLeft);
    }
    auto box = summaryArea.toFloat();
    drawWell (g, box, 12.0f);
    g.setColour (col::lcdAmber);
    g.setFont (condensed (16.0f));
    g.drawFittedText (summary != nullptr ? summary (settings).toUpperCase() : juce::String(), summaryArea.reduced (12, 4),
                      juce::Justification::centredLeft, 2, 0.9f);
}
