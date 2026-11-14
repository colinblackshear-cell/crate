#include "SongGrid.h"

#include "RigColours.h"
#include "RigExport.h"
#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

namespace
{
constexpr int kNameWidth = 150, kColumn = 96, kHeader = 54, kRow = 28, kGap = 10;

juce::Colour loopColour (int i)
{
    static const juce::Colour colours[] = { juce::Colour (0xffff8a3d), juce::Colour (0xffb48cff), juce::Colour (0xff4fc3f7),
                                            juce::Colour (0xfff06292), juce::Colour (0xffffd54f), juce::Colour (0xff8fd3c8),
                                            juce::Colour (0xff81c784), juce::Colour (0xffe0e0e0) };
    return colours[i % 8];
}

juce::String barsLabel (int bars)
{
    return bars <= 0 ? juce::String ("hold") : juce::String (bars) + (bars == 1 ? " bar" : " bars");
}
}

const std::vector<SongGrid::RowInfo>& SongGrid::rowList()
{
    static const std::vector<RowInfo> rows = []
    {
        std::vector<RowInfo> r;
        for (int l = 0; l < RigLooper::kLayers; ++l)
            r.push_back ({ true, l });
        for (int s : { RigEngine::mpcStrip, RigEngine::bassStrip, RigEngine::synthStrip, RigEngine::vocoderStrip,
                       RigEngine::samplerStrip, RigEngine::inputStrip })
            r.push_back ({ false, s });
        return r;
    }();
    return rows;
}

int SongGrid::cellValue (const RigScene& scene, const RowInfo& row)
{
    const int mute = row.isLoop ? scene.state.loopMutes[(size_t) row.index] : scene.state.stripMutes[(size_t) row.index];
    return mute < 0 ? -1 : (mute == 1 ? 0 : 1);
}

void SongGrid::setCell (RigScene& scene, const RowInfo& row, int value)
{
    const int mute = value < 0 ? -1 : (value == 1 ? 0 : 1);
    if (row.isLoop)
        scene.state.loopMutes[(size_t) row.index] = mute;
    else
        scene.state.stripMutes[(size_t) row.index] = mute;
}

//==============================================================================
class SongGrid::Cells : public juce::Component
{
public:
    explicit Cells (SongGrid& o) : owner (o) {}

    int parts() const { return (int) owner.engine.getScenes().getScenes().size(); }
    juce::Rectangle<int> cellBounds (int part, int row) const
    {
        return { kNameWidth + part * kColumn, kHeader + row * kRow, kColumn, kRow };
    }
    void updateSize()
    {
        setSize (kNameWidth + juce::jmax (1, parts()) * kColumn + kGap, kHeader + (int) rowList().size() * kRow + kRow / 2 + 8);
    }

    void paint (juce::Graphics& g) override
    {
        auto& engine = owner.engine;
        auto& scenes = engine.getScenes();
        const auto& list = scenes.getScenes();
        const auto& rows = rowList();
        const int bpb = engine.getTransport().getBeatsPerBar();

        // Row names.
        for (size_t r = 0; r < rows.size(); ++r)
        {
            const auto& row = rows[r];
            auto area = juce::Rectangle<int> (0, kHeader + (int) r * kRow + (row.isLoop ? 0 : kRow / 2), kNameWidth - 8, kRow).reduced (0, 3);
            const auto colour = row.isLoop ? loopColour (row.index) : RigColours::forStrip (row.index);
            g.setColour (colour);
            g.fillRoundedRectangle (area.removeFromLeft (5).toFloat(), 2.0f);
            area.removeFromLeft (8);
            g.setColour (Theme::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            const juce::String name = row.isLoop ? "Loop " + juce::String (row.index + 1) : RigEngine::stripLabel (row.index);
            g.drawText (name, area, juce::Justification::centredLeft);
            if (row.isLoop)
            {
                const auto state = engine.getLooper().getState (row.index);
                const double beats = engine.getLooper().getLayer (row.index).lengthBeats.load();
                g.setColour (Theme::textDim);
                g.setFont (juce::FontOptions (11.0f));
                const juce::String what = state == RigLooper::State::empty ? "empty"
                                        : beats > 0 ? juce::String (beats / juce::jmax (1, bpb), std::abs (beats / juce::jmax (1, bpb) - std::round (beats / juce::jmax (1, bpb))) < 0.01 ? 0 : 1) + " bars"
                                                    : "recording";
                g.drawText (what, area, juce::Justification::centredRight);
            }
        }
        if (list.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (14.0f));
            g.drawFittedText ("No parts yet. Get some loops going, then press + Part to keep what's playing as part A. "
                              "Then switch cells on and off to build the next parts.",
                              juce::Rectangle<int> (kNameWidth, kHeader, getWidth() - kNameWidth, kRow * 3), juce::Justification::centredLeft, 3);
            return;
        }

        const bool blink = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
        for (int p = 0; p < (int) list.size(); ++p)
        {
            const auto& scene = list[(size_t) p];
            const bool isCurrent = p == scenes.getCurrent(), isQueued = p == scenes.getQueued();
            auto head = juce::Rectangle<int> (kNameWidth + p * kColumn, 0, kColumn, kHeader - 4).reduced (3, 2);
            g.setColour (isCurrent ? RigColours::song : (isQueued && blink) ? RigColours::song.withAlpha (0.45f) : Theme::raised);
            g.fillRoundedRectangle (head.toFloat(), 5.0f);
            g.setColour (isCurrent ? Theme::background : Theme::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            auto nameArea = head.removeFromTop (head.getHeight() / 2 + 2);
            g.drawText (scene.name, nameArea.reduced (4, 0), juce::Justification::centredBottom, true);
            g.setFont (juce::FontOptions (11.0f));
            g.setColour ((isCurrent ? Theme::background : Theme::textDim));
            g.drawText (barsLabel (scene.bars) + " v", head.reduced (4, 0), juce::Justification::centred);

            for (size_t r = 0; r < rows.size(); ++r)
            {
                auto cell = cellBounds (p, (int) r).translated (0, rows[r].isLoop ? 0 : kRow / 2).reduced (3, 3).toFloat();
                const int v = cellValue (scene, rows[r]);
                const auto colour = rows[r].isLoop ? loopColour (rows[r].index) : RigColours::forStrip (rows[r].index);
                if (isCurrent)
                {
                    g.setColour (RigColours::song.withAlpha (0.10f));
                    g.fillRect (cell.expanded (3, 3));
                }
                const bool emptyLoop = rows[r].isLoop && engine.getLooper().getState (rows[r].index) == RigLooper::State::empty;
                if (v == 1)
                {
                    g.setColour (colour.withAlpha (emptyLoop ? 0.18f : 0.85f));   // on, but nothing recorded there yet
                    g.fillRoundedRectangle (cell, 4.0f);
                }
                else
                {
                    g.setColour (Theme::background);
                    g.fillRoundedRectangle (cell, 4.0f);
                    g.setColour (Theme::outline);
                    g.drawRoundedRectangle (cell, 4.0f, 1.0f);
                    if (v < 0)
                    {
                        g.setColour (Theme::textDim);
                        g.drawHorizontalLine ((int) cell.getCentreY(), cell.getCentreX() - 6, cell.getCentreX() + 6);
                    }
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& list = owner.engine.getScenes().getScenes();
        const int part = (e.x - kNameWidth) / kColumn;
        if (e.x < kNameWidth || ! juce::isPositiveAndBelow (part, (int) list.size()))
            return;
        if (e.y < kHeader)
        {
            if (e.y < kHeader / 2 + 2 && ! e.mods.isPopupMenu())
                owner.engine.getScenes().launch (part);
            else
                partMenu (part);
            repaint();
            return;
        }
        const auto& rows = rowList();
        for (size_t r = 0; r < rows.size(); ++r)
            if (cellBounds (part, (int) r).translated (0, rows[r].isLoop ? 0 : kRow / 2).contains (e.getPosition()))
            {
                owner.clickCell (part, (int) r);
                return;
            }
    }

private:
    void partMenu (int part)
    {
        auto& scenes = owner.engine.getScenes();
        auto& list = scenes.getScenes();
        juce::PopupMenu lengths;
        for (int b : { 0, 1, 2, 4, 8, 16, 32 })
            lengths.addItem (b == 0 ? "Hold (stay until you move on)" : barsLabel (b), true, list[(size_t) part].bars == b,
                             [this, part, b] { auto& l = owner.engine.getScenes().getScenes(); if (part < (int) l.size()) l[(size_t) part].bars = b; repaint(); });
        juce::PopupMenu menu;
        menu.addSubMenu ("Length", lengths);
        menu.addItem ("Play this part", [this, part] { owner.engine.getScenes().launch (part); });
        menu.addItem ("Keep what's playing now in this part", [this, part]
                      {
                          auto& l = owner.engine.getScenes().getScenes();
                          if (part < (int) l.size())
                              l[(size_t) part].state = owner.engine.captureScene();
                          repaint();
                      });
        menu.addItem ("Copy as a new part", (int) list.size() < RigScenes::kMaxScenes, false, [this, part]
                      {
                          auto& l = owner.engine.getScenes().getScenes();
                          if (part < (int) l.size() && (int) l.size() < RigScenes::kMaxScenes)
                          {
                              auto copy = l[(size_t) part];
                              copy.name = "Part " + juce::String::charToString ((juce::juce_wchar) ('A' + (int) l.size() % 26));
                              l.insert (l.begin() + part + 1, copy);
                          }
                          owner.refresh();
                      });
        menu.addItem ("Delete this part", [this, part]
                      {
                          auto& l = owner.engine.getScenes().getScenes();
                          if (part < (int) l.size() && part != owner.engine.getScenes().getCurrent()
                              && part != owner.engine.getScenes().getQueued())
                              l.erase (l.begin() + part);
                          else
                              owner.say ("Can't delete the part that's playing or coming up next.");
                          owner.refresh();
                      });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (
            juce::Rectangle<int> (kNameWidth + part * kColumn, kHeader / 2, kColumn, kHeader / 2))));
    }

    SongGrid& owner;
};

//==============================================================================
SongGrid::SongGrid (RigEngine& e) : engine (e)
{
    cells = std::make_unique<Cells> (*this);
    viewport.setViewedComponent (cells.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    title.setText ("SONG", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, RigColours::song);
    help.setText ("Lit plays, dark is muted, a dash leaves it. Click a part's name to play it; "
                  "click its length to change it, delete it or copy it.",
                  juce::dontSendNotification);
    steps.setText (juce::String::fromUTF8 ("How to build a song:  1 Record a few loops   2 + Part keeps what's playing   "
                                           "3 Click cells to switch loops on or off in each part   4 Click a part's length to set its bars   "
                                           "5 Play song   6 Export for Logic"),
                   juce::dontSendNotification);
    steps.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    steps.setColour (juce::Label::textColourId, Theme::text);
    help.setFont (juce::FontOptions (13.0f));
    help.setColour (juce::Label::textColourId, Theme::textDim);

    addButton.setTooltip ("Keeps what's playing now (which loops and tools are on) as a new part.");
    addButton.onClick = [this] { addPart(); };
    songMode.setClickingTogglesState (true);
    songMode.setColour (juce::TextButton::buttonOnColourId, RigColours::song);
    songMode.setTooltip ("Parts with a length play that many bars, then the next part starts by itself.");
    songMode.onClick = [this] { engine.getScenes().songMode = songMode.getToggleState(); };
    playButton.setTooltip ("Plays the song from part A, in song mode.");
    playButton.onClick = [this] { playSong(); };
    exportButton.setTooltip ("Saves every loop as WAV files for Logic, all starting on bar 1: each loop on its own, and (if you've "
                             "built a song) each loop laid out over the whole song, with a tempo and part-marker MIDI file.");
    exportButton.onClick = [this] { exportForLogic(); };
    close.onClick = [this] { if (onClose) onClose(); };
    for (auto* c : std::initializer_list<juce::Component*> { &title, &help, &steps, &addButton, &songMode, &playButton, &exportButton, &close })
        addAndMakeVisible (c);
    refresh();
}

SongGrid::~SongGrid() = default;

void SongGrid::refresh()
{
    knownParts = (int) engine.getScenes().getScenes().size();
    songMode.setToggleState (engine.getScenes().songMode, juce::dontSendNotification);
    cells->updateSize();
    cells->repaint();
}

void SongGrid::tick()
{
    if ((int) engine.getScenes().getScenes().size() != knownParts)
        refresh();
    songMode.setToggleState (engine.getScenes().songMode, juce::dontSendNotification);
    cells->repaint();
}

void SongGrid::clickCell (int part, int row)
{
    auto& scenes = engine.getScenes();
    auto& list = scenes.getScenes();
    const auto& rows = rowList();
    if (! juce::isPositiveAndBelow (part, (int) list.size()) || ! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;
    const auto& info = rows[(size_t) row];
    const int value = nextValue (cellValue (list[(size_t) part], info));
    setCell (list[(size_t) part], info, value);
    // The part that's playing changes now.
    if (part == scenes.getCurrent())
    {
        if (info.isLoop)
            engine.getLooper().getLayer (info.index).mute = value == 0;
        else
            engine.getStrip (info.index).mute = value == 0;
    }
    cells->repaint();
}

void SongGrid::addPart()
{
    auto& list = engine.getScenes().getScenes();
    if ((int) list.size() >= RigScenes::kMaxScenes)
        return;
    RigScene scene;
    scene.name = "Part " + juce::String::charToString ((juce::juce_wchar) ('A' + (int) list.size() % 26));
    scene.bars = list.empty() ? 4 : list.back().bars;
    scene.state = engine.captureScene();
    list.push_back (scene);
    refresh();
    viewport.setViewPosition (cells->getWidth(), viewport.getViewPositionY());
    say (scene.name + " keeps what's playing now. Switch cells on and off to change it.");
}

void SongGrid::playSong()
{
    auto& scenes = engine.getScenes();
    if (scenes.getScenes().empty())
    {
        say ("Add a part first: + Part keeps what's playing now.");
        return;
    }
    scenes.songMode = true;
    engine.getTimeline().on = false;   // the parts run it, not the timeline
    songMode.setToggleState (true, juce::dontSendNotification);
    scenes.launch (0);
    if (! engine.getTransport().isPlaying())
        engine.getTransport().play();
    say (engine.getTransport().isPlaying() ? "The song starts from part A at the next bar." : "Playing the song from part A.");
}

void SongGrid::exportForLogic()
{
    const auto result = RigExport::exportForLogic (engine, RigExport::defaultFolder());
    if (result.error.isNotEmpty() && result.files == 0)
    {
        say (result.error);
        return;
    }
    say ("Exported " + juce::String (result.files) + " files to " + result.folder.getFullPathName()
         + (result.error.isNotEmpty() ? " (" + result.error + ")" : juce::String()) + ". Drag them into Logic at bar 1.");
    result.folder.revealToUser();
}

void SongGrid::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::outline);
    g.drawRect (getLocalBounds(), 1);
}

void SongGrid::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    auto top = area.removeFromTop (30);
    title.setBounds (top.removeFromLeft (70));
    close.setBounds (top.removeFromRight (80));
    top.removeFromRight (8);
    exportButton.setBounds (top.removeFromRight (140));
    top.removeFromRight (8);
    playButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    songMode.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    addButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (8);
    help.setBounds (top);
    area.removeFromTop (6);
    steps.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);
    viewport.setBounds (area);
}
