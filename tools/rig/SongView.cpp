#include "SongView.h"
#include "RigColours.h"
#include "RigExport.h"
#include "Vintage.h"

using namespace vintage;

namespace
{
// The grid's sizes.
constexpr int kNames = 178, kCol = 156, kColGap = 8, kHead = 80, kRow = 20, kRowGap = 3, kGroup = 16;

juce::Colour loopColour() { return RigColours::loops; }

juce::String barsWord (int bars) { return bars == 1 ? "bar" : "bars"; }
}

//==============================================================================
const std::vector<SongView::Track>& SongView::tracks()
{
    static const std::vector<Track> list = []
    {
        std::vector<Track> t;
        for (int s : { RigEngine::mpcStrip, RigEngine::drumsStrip, RigEngine::bassStrip, RigEngine::synthStrip, RigEngine::vocoderStrip,
                       RigEngine::samplerStrip, RigEngine::inputStrip })
            t.push_back ({ false, s });
        for (int l = 0; l < RigLooper::kLayers; ++l)
            t.push_back ({ true, l });
        return t;
    }();
    return list;
}

juce::String SongView::trackName (const Track& t)
{
    if (t.isLoop)
        return "Loop " + juce::String (t.index + 1);
    switch (t.index)
    {
        case RigEngine::mpcStrip:     return "MPC";
        case RigEngine::drumsStrip:   return "Drum machine";
        case RigEngine::bassStrip:    return "Bass";
        case RigEngine::synthStrip:   return "Synth";
        case RigEngine::vocoderStrip: return "Vocoder";
        case RigEngine::samplerStrip: return "Sampler";
        case RigEngine::inputStrip:   return "Line in";
        default:                      return RigEngine::stripName (t.index);
    }
}

int SongView::cellValue (const RigScene& scene, const Track& t)
{
    const int mute = t.isLoop ? scene.state.loopMutes[(size_t) t.index] : scene.state.stripMutes[(size_t) t.index];
    return mute < 0 ? -1 : (mute == 1 ? 0 : 1);
}

void SongView::setCell (RigScene& scene, const Track& t, int value)
{
    const int mute = value < 0 ? -1 : (value == 1 ? 0 : 1);
    if (t.isLoop)
        scene.state.loopMutes[(size_t) t.index] = mute;
    else
        scene.state.stripMutes[(size_t) t.index] = mute;
}

juce::String SongView::lengthText (int bars)
{
    return bars <= 0 ? juce::String ("Hold") : juce::String (bars) + " " + barsWord (bars);
}

juce::String SongView::letterFor (int part)
{
    return part < 26 ? juce::String::charToString ((juce::juce_wchar) ('A' + part)) : juce::String (part + 1);
}

//==============================================================================
// The grid: track names down the left, a column per part, and a last column that saves a new one.
class SongView::Grid : public juce::Component
{
public:
    explicit Grid (SongView& o) : owner (o)
    {
        nameEditor.setFont (cond (17.0f, Weight::bold));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.onReturnKey = [this] { commitName(); };
        nameEditor.onFocusLost = [this] { commitName(); };
        nameEditor.onEscapeKey = [this] { editing = -1; nameEditor.setVisible (false); };
        addChildComponent (nameEditor);
    }

    static int numTools()
    {
        int n = 0;
        for (const auto& t : tracks())
            n += t.isLoop ? 0 : 1;
        return n;
    }

    int parts() const { return (int) scenes().getScenes().size(); }

    void updateSize()
    {
        const int rows = (int) tracks().size();
        setSize (kNames + (parts() + 1) * (kCol + kColGap), kHead + 6 + 2 * kGroup + rows * (kRow + kRowGap) + 4);
    }

    // Where things are.
    int rowY (int row) const
    {
        const int tools = numTools();
        return kHead + 6 + kGroup + row * (kRow + kRowGap) + (row >= tools ? kGroup : 0);
    }
    juce::Rectangle<int> column (int part) const { return { kNames + part * (kCol + kColGap), 0, kCol, getHeight() }; }
    juce::Rectangle<int> cell (int part, int row) const { return column (part).reduced (8, 0).withY (rowY (row)).withHeight (kRow); }
    juce::Rectangle<int> header (int part) const { return column (part).withHeight (kHead).reduced (8, 7); }
    juce::Rectangle<int> letterBox (int part) const { return header (part).removeFromTop (36).removeFromLeft (36); }
    juce::Rectangle<int> nameBox (int part) const { return header (part).removeFromTop (36).withTrimmedLeft (44); }
    juce::Rectangle<int> lengthBox (int part) const { return header (part).removeFromBottom (26).removeFromLeft (72); }
    juce::Rectangle<int> playBox (int part) const { return header (part).removeFromBottom (26).withTrimmedLeft (78).withTrimmedRight (34); }
    juce::Rectangle<int> menuBox (int part) const { return header (part).removeFromBottom (26).removeFromRight (28); }
    // Over the part's loops: which loop bank it plays (click to pick).
    juce::Rectangle<int> bankBox (int part) const { return column (part).reduced (8, 0).withY (rowY (numTools()) - kGroup).withHeight (kGroup); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto& list = scenes().getScenes();
        const auto& rows = tracks();
        const int current = scenes().getCurrent(), queued = scenes().getQueued();
        const bool pulse = (juce::Time::getMillisecondCounter() / 350) % 2 == 0;

        // The key, in the corner above the names.
        {
            auto key = juce::Rectangle<int> (6, 4, kNames - 12, kHead - 8);
            const int h = key.getHeight() / 3;
            const std::pair<int, const char*> keys[] = { { 1, "plays" }, { 0, "muted" }, { -1, "left alone" } };
            for (auto [value, what] : keys)
            {
                auto r = key.removeFromTop (h);
                paintCell (g, r.removeFromLeft (64).reduced (0, 2).toFloat(), value, RigColours::loops, false);
                r.removeFromLeft (8);
                g.setColour (p.ink2);
                g.setFont (barlow (13.0f));
                g.drawText (what, r, juce::Justification::centredLeft);
            }
        }

        // Track names, in two groups.
        engraved (g, "Tools", { 6, kHead + 6, kNames - 12, kGroup }, juce::Justification::centredLeft, 12.0f);
        engraved (g, "Loops", { 6, rowY (numTools()) - kGroup, kNames - 12, kGroup }, juce::Justification::centredLeft, 12.0f);
        for (int r = 0; r < (int) rows.size(); ++r)
        {
            const auto& t = rows[(size_t) r];
            auto area = juce::Rectangle<int> (6, rowY (r), kNames - 12, kRow);
            const auto colour = t.isLoop ? loopColour() : RigColours::forStrip (t.index);
            g.setColour (colour);
            g.fillRoundedRectangle (area.removeFromLeft (6).toFloat(), 2.0f);
            area.removeFromLeft (8);
            juce::String status;
            if (t.isLoop)
            {
                const auto& layer = owner.engine.getLooper().getLayer (t.index);
                const double beats = layer.lengthBeats.load();
                const int bpb = juce::jmax (1, owner.engine.getTransport().getBeatsPerBar());
                status = owner.engine.getLooper().getState (t.index) == RigLooper::State::empty || beats <= 0.0
                           ? juce::String ("empty")
                           : juce::String (beats / bpb, std::abs (beats / bpb - std::round (beats / bpb)) < 0.05 ? 0 : 1) + " "
                                 + barsWord ((int) std::round (beats / bpb));
            }
            g.setColour (status == "empty" ? p.ink2 : p.ink);
            g.setFont (cond (17.0f, Weight::bold, 0.03f));
            g.drawFittedText (trackName (t).toUpperCase(), area.removeFromLeft (118), juce::Justification::centredLeft, 1, 0.75f);
            g.setColour (p.ink2);
            g.setFont (barlow (12.0f));
            g.drawText (status, area, juce::Justification::centredRight);
        }

        // The parts.
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& scene = list[(size_t) i];
            const auto col = column (i).toFloat();
            const bool isCurrent = i == current, isQueued = i == queued;

            // The column's card.
            g.setColour (juce::Colours::black.withAlpha (vintage::isStage() ? 0.28f : 0.06f));
            g.fillRoundedRectangle (col, 10.0f);
            if (isCurrent)
                glow (g, col, 10.0f, colours::play, 10.0f, 0.35f);
            if (isCurrent || (isQueued && pulse))
            {
                g.setColour (isCurrent ? colours::play : colours::next);
                g.drawRoundedRectangle (col.reduced (1.0f), 10.0f, 2.5f);
            }

            // Letter, name and what else it changes.
            const auto letter = letterBox (i).toFloat();
            drawButton (g, letter, isCurrent ? Lit::play : isQueued ? Lit::next : Lit::none, false, hovered == i, false, 8.0f);
            g.setColour (isCurrent || isQueued ? juce::Colour (0xff1c1305) : p.btnInk);
            g.setFont (cond (24.0f, Weight::extra));
            g.drawText (letterFor (i), letter.withTrimmedBottom (3.0f).toNearestInt(), juce::Justification::centred);

            auto name = nameBox (i);
            if (editing != i)
            {
                g.setColour (p.ink);
                g.setFont (cond (18.0f, Weight::extra, 0.04f));
                g.drawFittedText (scene.name.toUpperCase(), name.removeFromTop (20), juce::Justification::centredLeft, 1, 0.7f);
                g.setColour (isCurrent ? colours::play.darker (0.3f) : isQueued ? colours::next.darker (0.3f) : p.ink2);
                g.setFont (cond (12.0f, Weight::bold, 0.1f));
                g.drawFittedText (isCurrent ? juce::String::fromUTF8 ("\xe2\x96\xb6 Playing now") : isQueued ? juce::String ("Next, at the bar")
                                                                                                         : summary (scene.state),
                                  name, juce::Justification::centredLeft, 1, 0.7f);
            }

            // Length (an LCD), Play and the menu.
            const auto len = lengthBox (i).toFloat();
            drawLcd (g, len, 6.0f);
            lcdText (g, scene.bars <= 0 ? juce::String ("HOLD") : juce::String (scene.bars) + " BAR" + (scene.bars == 1 ? "" : "S"), len, 20.0f);
            const auto play = playBox (i).toFloat();
            drawButton (g, play, isCurrent ? Lit::play : isQueued ? Lit::next : Lit::none, false, false, false, 7.0f);
            {
                // A play triangle (lit green while it plays, amber when it's next).
                const auto ic = play.withTrimmedBottom (3.0f).withSizeKeepingCentre (12.0f, 13.0f);
                juce::Path tri;
                tri.addTriangle (ic.getX(), ic.getY(), ic.getRight(), ic.getCentreY(), ic.getX(), ic.getBottom());
                g.setColour (isCurrent || isQueued ? juce::Colour (0xff1c1305) : p.btnInk);
                g.fillPath (tri);
            }
            const auto menu = menuBox (i).toFloat();
            drawButton (g, menu, Lit::none, false, false, false, 7.0f);
            g.setColour (p.btnInk);
            g.setFont (cond (18.0f, Weight::extra));
            g.drawText (juce::String::fromUTF8 ("\xe2\x8b\xaf"), menu.withTrimmedBottom (3.0f).toNearestInt(), juce::Justification::centred);

            // The part's loop bank, over its loops.
            {
                const auto& engine = owner.engine;
                const int bank = scene.state.loopBank;
                const auto text = bank >= 0 && bank < engine.getNumLoopBanks() ? engine.getLoopBankName (bank) : juce::String ("Loops as they are");
                g.setColour (hovered == i ? p.ink : p.ink2);
                g.setFont (cond (12.0f, Weight::bold, 0.06f));
                g.drawFittedText (juce::String::fromUTF8 ("\xe2\x96\xbe ") + text, bankBox (i), juce::Justification::centredLeft, 1, 0.7f);
            }

            // The cells.
            for (int r = 0; r < (int) rows.size(); ++r)
                paintCell (g, cell (i, r).toFloat(), cellValue (scene, rows[(size_t) r]),
                           rows[(size_t) r].isLoop ? loopColour() : RigColours::forStrip (rows[(size_t) r].index),
                           hoverCell == juce::Point<int> (i, r));
        }

        // The last column: save what's playing as a new part.
        if ((int) list.size() < RigScenes::kMaxScenes)
        {
            auto add = column ((int) list.size()).toFloat().reduced (2.0f);
            g.setColour (hovered == (int) list.size() ? p.ink : p.line);
            juce::Path outline;
            outline.addRoundedRectangle (add, 10.0f);
            const float dashes[] = { 7.0f, 5.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (outline, outline, dashes, 2);
            g.fillPath (outline);
            auto text = add.reduced (14.0f).toNearestInt();
            g.setColour (p.ink);
            g.setFont (cond (54.0f, Weight::extra));
            g.drawText ("+", text.removeFromTop (kHead + 20), juce::Justification::centredBottom);
            g.setFont (cond (17.0f, Weight::extra, 0.06f));
            g.drawFittedText ("NEW PART " + letterFor ((int) list.size()), text.removeFromTop (26), juce::Justification::centred, 1);
            g.setColour (p.ink2);
            g.setFont (barlow (13.0f));
            g.drawFittedText ("Click to keep what's playing now as the next part of the song.\n\nThen switch tracks on and off "
                              "in its column.",
                              text.removeFromTop (110), juce::Justification::centredTop, 7);
        }
    }

    void paintCell (juce::Graphics& g, juce::Rectangle<float> r, int value, juce::Colour colour, bool over)
    {
        const auto& p = pal();
        if (value == 1)
        {
            glow (g, r, 6.0f, colour, 6.0f, 0.3f);
            g.setColour (over ? colour.brighter (0.15f) : colour);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colour (0xff1c1305));
            g.setFont (cond (12.0f, Weight::extra, 0.12f));
            g.drawText ("ON", r.toNearestInt(), juce::Justification::centred);
        }
        else if (value == 0)
        {
            drawWell (g, r, 6.0f);
            drawLamp (g, { r.getX() + 13.0f, r.getCentreY() }, Lit::mute, 10.0f);
            g.setColour (over ? p.wellInk : p.wellInk2);
            g.setFont (cond (12.0f, Weight::extra, 0.12f));
            g.drawText ("OFF", r.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            g.setColour (over ? p.ink2 : p.line);
            g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 1.5f);
            g.setColour (p.ink2);
            g.setFont (barlow (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94  as is"), r.toNearestInt(), juce::Justification::centred);
        }
    }

    // A part's other settings, in a few words.
    juce::String summary (const RigScene::State& s) const
    {
        juce::StringArray parts;
        auto& sequences = owner.engine.getMpc().getProject().getSequences();
        if (juce::isPositiveAndBelow (s.mpcSequence, (int) sequences.size()))
            parts.add ("Seq " + juce::String (s.mpcSequence + 1));
        if (s.bassSequencer == 0)
            parts.add ("Bass seq off");
        else if (s.bassPattern >= 0)
            parts.add ("Bass pat " + juce::String (s.bassPattern + 1));
        if (s.arp == 1)
            parts.add ("Arp");
        if (s.vocoderOn == 0)
            parts.add ("Voc off");
        return parts.isEmpty() ? juce::String ("Click a cell to change") : parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
    }

    //==========================================================================
    int partAt (juce::Point<int> pos) const
    {
        for (int i = 0; i <= parts(); ++i)
            if (column (i).contains (pos))
                return i;
        return -1;
    }
    int rowAt (int part, juce::Point<int> pos) const
    {
        for (int r = 0; r < (int) tracks().size(); ++r)
            if (cell (part, r).expanded (0, kRowGap / 2).contains (pos))
                return r;
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int part = partAt (e.getPosition());
        const juce::Point<int> c = part >= 0 && part < parts() ? juce::Point<int> (part, rowAt (part, e.getPosition())) : juce::Point<int> (-1, -1);
        if (part != hovered || c != hoverCell)
        {
            hovered = part;
            hoverCell = c;
            setMouseCursor (part >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = -1;
        hoverCell = { -1, -1 };
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;
        const auto pos = e.getPosition();
        const int part = partAt (pos);
        if (part < 0)
            return;
        if (part == parts())
        {
            owner.addPart();
            return;
        }
        const bool popup = e.mods.isPopupMenu();
        const int row = rowAt (part, pos);
        if (row >= 0)
        {
            if (popup)
                cellMenu (part, row);
            else
                owner.clickCell (part, row);
        }
        else if (bankBox (part).contains (pos))
            bankMenu (part, bankBox (part));
        else if (menuBox (part).contains (pos) || (popup && pos.y < kHead))
            partMenu (part);
        else if (lengthBox (part).contains (pos))
            lengthMenu (part);
        else if (nameBox (part).contains (pos) && e.getNumberOfClicks() >= 2)
            startRename (part);
        else if (playBox (part).contains (pos) || letterBox (part).contains (pos) || nameBox (part).contains (pos))
            scenes().launch (part);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int part = partAt (e.getPosition());
        if (part >= 0 && part < parts() && nameBox (part).contains (e.getPosition()))
            startRename (part);
    }

    void startRename (int part)
    {
        editing = part;
        nameEditor.setText (scenes().getScenes()[(size_t) part].name, false);
        nameEditor.setBounds (nameBox (part).removeFromTop (24));
        nameEditor.setVisible (true);
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
        repaint();
    }

    void commitName()
    {
        auto& list = scenes().getScenes();
        if (juce::isPositiveAndBelow (editing, (int) list.size()) && nameEditor.getText().trim().isNotEmpty())
            list[(size_t) editing].name = nameEditor.getText().trim();
        editing = -1;
        nameEditor.setVisible (false);
        repaint();
    }

    void lengthMenu (int part)
    {
        juce::PopupMenu menu;
        addLengths (menu, part);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (lengthBox (part))));
    }

    void addLengths (juce::PopupMenu& menu, int part)
    {
        const int now = scenes().getScenes()[(size_t) part].bars;
        for (int b : { 0, 1, 2, 4, 8, 12, 16, 24, 32, 64 })
            menu.addItem (b == 0 ? "Hold (stays until you move on)" : lengthText (b), true, now == b,
                          [this, part, b]
                          {
                              auto& l = scenes().getScenes();
                              if (part < (int) l.size())
                                  l[(size_t) part].bars = b;
                              owner.repaint();
                          });
    }

    void cellMenu (int part, int row)
    {
        const auto& t = tracks()[(size_t) row];
        const int now = cellValue (scenes().getScenes()[(size_t) part], t);
        juce::PopupMenu menu;
        menu.addSectionHeader (trackName (t) + " in " + scenes().getScenes()[(size_t) part].name);
        menu.addItem ("On: plays in this part", true, now == 1, [this, part, row] { owner.setCellTo (part, row, 1); });
        menu.addItem ("Off: muted in this part", true, now == 0, [this, part, row] { owner.setCellTo (part, row, 0); });
        menu.addItem ("As is: this part leaves it alone", true, now == -1, [this, part, row] { owner.setCellTo (part, row, -1); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cell (part, row))));
    }

    void partMenu (int part)
    {
        auto& rigScenes = scenes();
        const int count = parts();
        juce::PopupMenu lengths;
        addLengths (lengths, part);
        juce::PopupMenu menu;
        menu.addSectionHeader ("Part " + letterFor (part) + ": " + rigScenes.getScenes()[(size_t) part].name);
        menu.addItem ("Play this part (at the next bar)", [this, part] { scenes().launch (part); });
        menu.addItem ("Save what's playing now into this part", [this, part]
                      {
                          auto& l = scenes().getScenes();
                          if (part < (int) l.size())
                          {
                              l[(size_t) part].state = owner.engine.captureScene();
                              owner.say ("\"" + l[(size_t) part].name + "\" now has what's playing.");
                          }
                          owner.repaint();
                      });
        menu.addItem ("Rename...", [this, part] { startRename (part); });
        menu.addSubMenu ("Length", lengths);
        menu.addSubMenu ("Loop bank", bankItems (part));
        menu.addSeparator();
        menu.addItem (juce::String::fromUTF8 ("\xe2\x97\x80 Move earlier"), part > 0, false, [this, part] { scenes().move (part, part - 1); owner.refresh(); });
        menu.addItem (juce::String::fromUTF8 ("Move later \xe2\x96\xb6"), part + 1 < count, false, [this, part] { scenes().move (part, part + 1); owner.refresh(); });
        menu.addItem ("Copy (puts the copy right after it)", count < RigScenes::kMaxScenes, false, [this, part]
                      {
                          scenes().duplicate (part);
                          owner.refresh();
                      });
        menu.addSeparator();
        menu.addItem ("Delete this part", [this, part]
                      {
                          const auto name = scenes().getScenes()[(size_t) part].name;
                          scenes().remove (part);
                          owner.say ("Deleted \"" + name + "\".");
                          owner.refresh();
                      });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (menuBox (part))));
    }

    juce::PopupMenu bankItems (int part)
    {
        juce::PopupMenu menu;
        const int now = scenes().getScenes()[(size_t) part].state.loopBank;
        auto& engine = owner.engine;
        for (int b = 0; b < engine.getNumLoopBanks(); ++b)
            menu.addItem (juce::String (b + 1) + ". " + engine.getLoopBankName (b), true, now == b, [this, part, b] { owner.setPartLoopBank (part, b); });
        menu.addSeparator();
        menu.addItem ("Leave the loops as they are", true, now < 0, [this, part] { owner.setPartLoopBank (part, -1); });
        return menu;
    }

    void bankMenu (int part, juce::Rectangle<int> where)
    {
        bankItems (part).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (where)));
    }

    RigScenes& scenes() const { return owner.engine.getScenes(); }

    SongView& owner;
    juce::TextEditor nameEditor;
    int editing = -1, hovered = -1;
    juce::Point<int> hoverCell { -1, -1 };
};

//==============================================================================
SongView::SongView (RigEngine& e) : engine (e)
{
    grid = std::make_unique<Grid> (*this);
    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    addButton.sub = "Save what's playing";
    addButton.setTooltip ("Keeps what's playing now (which tracks and loops play, the MPC's sequence, the bass pattern, the arp, "
                          "the vocoder) as the next part of the song.");
    addButton.onClick = [this] { addPart(); };
    playButton.sub = "From part A";
    playButton.icon = rigpanels::PanelButton::Icon::none;
    playButton.setTooltip ("Plays the song from part A, each part for its length, in order.");
    playButton.onClick = [this] { playSong(); };
    inOrder.sub = "Parts move on";
    inOrder.setTooltip ("On: a part with a length plays that many bars, then the next part starts by itself (song mode). "
                        "Off: a part plays until you launch another.");
    inOrder.onClick = [this]
    {
        engine.getScenes().songMode = ! engine.getScenes().songMode;
        say (engine.getScenes().songMode ? "In order: each part plays its bars, then the next one starts."
                                         : "Parts now stay until you launch another one.");
    };
    back.sub = juce::String::fromUTF8 ("\xe2\x97\x80 Part");
    next.sub = juce::String::fromUTF8 ("Part \xe2\x96\xb6");
    back.onClick = [this] { engine.getScenes().back(); };
    next.onClick = [this] { engine.getScenes().next(); };
    back.setTooltip ("The part before this one, at the next bar.");
    next.setTooltip ("The next part, at the next bar.");
    keys.sub = "Next / Back";
    keys.setTooltip ("Put Next and Back on buttons of your keyboard (the Launchkey's scene arrows, say).");
    keys.onClick = [this] { showKeyboardMenu(); };
    exportButton.sub = "For Logic";
    exportButton.setTooltip ("Saves every loop as WAV files for Logic, all starting on bar 1, laid out over the song, with a tempo "
                             "and part-marker MIDI file.");
    exportButton.onClick = [this]
    {
        if (onExport)
        {
            onExport();
            return;
        }
        const auto result = RigExport::exportForLogic (engine, RigExport::defaultFolder());
        say (result.files > 0 ? "Exported " + juce::String (result.files) + " files to " + result.folder.getFullPathName() : result.error);
    };
    for (auto* b : { &addButton, &playButton, &inOrder, &back, &next, &keys, &exportButton })
    {
        b->fontSize = 17.0f;
        b->subSize = 11.0f;
        addAndMakeVisible (b);
    }
    addButton.fontSize = 19.0f;

    bigAdd.sub = "Keeps which tracks and loops play now";
    bigAdd.fontSize = 26.0f;
    bigAdd.subSize = 12.0f;
    bigAdd.lit = Lit::next;
    bigAdd.setTooltip (addButton.getTooltip());
    bigAdd.onClick = [this] { addPart(); };
    addChildComponent (bigAdd);

    refresh();
    startTimerHz (15);
}

SongView::~SongView() { stopTimer(); }

void SongView::refresh()
{
    knownParts = (int) engine.getScenes().getScenes().size();
    grid->updateSize();
    resized();
    repaint();
}

void SongView::addPart()
{
    auto& list = engine.getScenes().getScenes();
    if ((int) list.size() >= RigScenes::kMaxScenes)
    {
        say ("A song can have up to " + juce::String (RigScenes::kMaxScenes) + " parts.");
        return;
    }
    static const char* names[] = { "Intro", "Verse", "Chorus", "Verse 2", "Bridge", "Drop", "Break", "Outro" };
    RigScene scene;
    scene.name = list.size() < std::size (names) ? names[list.size()] : "Part " + letterFor ((int) list.size());
    scene.bars = list.empty() ? 8 : list.back().bars;
    scene.state = engine.captureScene();
    list.push_back (scene);
    refresh();
    viewport.setViewPosition (grid->getWidth(), viewport.getViewPositionY());
    say ("Part " + letterFor ((int) list.size() - 1) + " (\"" + scene.name + "\") keeps what's playing now. Click its cells to switch "
         "tracks on or off; double-click its name to rename it.");
}

void SongView::playSong()
{
    auto& scenes = engine.getScenes();
    if (scenes.getScenes().empty())
    {
        say ("Save a part first: get something playing, then + New part.");
        return;
    }
    scenes.songMode = true;
    engine.getTimeline().on = false;   // the parts run it, not the timeline
    scenes.launch (0);
    const bool wasPlaying = engine.getTransport().isPlaying();
    if (! wasPlaying)
        engine.getTransport().play();
    say (wasPlaying ? "The song starts from part A at the next bar." : "Playing the song from part A.");
}

void SongView::clickCell (int part, int row)
{
    auto& list = engine.getScenes().getScenes();
    if (! juce::isPositiveAndBelow (part, (int) list.size()) || ! juce::isPositiveAndBelow (row, (int) tracks().size()))
        return;
    setCellTo (part, row, cellValue (list[(size_t) part], tracks()[(size_t) row]) == 1 ? 0 : 1);
}

void SongView::setCellTo (int part, int row, int value)
{
    auto& scenes = engine.getScenes();
    auto& list = scenes.getScenes();
    if (! juce::isPositiveAndBelow (part, (int) list.size()) || ! juce::isPositiveAndBelow (row, (int) tracks().size()))
        return;
    const auto& t = tracks()[(size_t) row];
    setCell (list[(size_t) part], t, value);
    // The part that's playing changes now.
    if (part == scenes.getCurrent() && value >= 0)
    {
        if (t.isLoop)
            engine.getLooper().getLayer (t.index).mute = value == 0;
        else
            engine.getStrip (t.index).mute = value == 0;
    }
    grid->repaint();
}

void SongView::setPartLoopBank (int part, int bank)
{
    auto& scenes = engine.getScenes();
    auto& list = scenes.getScenes();
    if (! juce::isPositiveAndBelow (part, (int) list.size()) || bank >= engine.getNumLoopBanks())
        return;
    auto& scene = list[(size_t) part];
    scene.state.loopBank = juce::jmax (-1, bank);
    if (bank >= 0)
    {
        if (part == scenes.getCurrent())
            say (engine.switchLoopBank (bank));
        else
            say ("Part " + letterFor (part) + " plays the loops in \"" + engine.getLoopBankName (bank) + "\".");
    }
    grid->repaint();
}

void SongView::showKeyboardMenu()
{
    juce::PopupMenu menu;
    for (auto [control, what] : { std::pair<int, const char*> { RigRouting::nextScene, "Next" }, { RigRouting::previousScene, "Back" } })
    {
        const auto [channel, cc] = engine.getRouting().getAssignment (control);
        juce::ignoreUnused (channel);
        const juce::String label (what);
        menu.addItem (cc >= 0 ? label + " is on CC " + juce::String (cc) + ": forget it" : "Put " + label + " on a keyboard button",
                      [this, control, cc, label]
                      {
                          if (cc >= 0)
                          {
                              engine.getRouting().forget (control);
                              say (label + " forgotten.");
                          }
                          else
                          {
                              engine.getRouting().learn (control);
                              say ("Press the button on your keyboard for " + label + ".");
                          }
                      });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keys));
}

void SongView::timerCallback()
{
    if ((int) engine.getScenes().getScenes().size() != knownParts)
        refresh();
    const auto lit = engine.getScenes().songMode ? Lit::current : Lit::none;
    if (inOrder.lit != lit)
    {
        inOrder.lit = lit;
        inOrder.repaint();
    }
    const auto playLit = engine.getScenes().songMode && engine.getTransport().isPlaying() && engine.getScenes().getCurrent() >= 0
                             ? Lit::play : Lit::none;
    if (playButton.lit != playLit)
    {
        playButton.lit = playLit;
        playButton.repaint();
    }
    grid->repaint();
    repaint (nowArea.getUnion (stripArea).expanded (6));
}

//==============================================================================
void SongView::paint (juce::Graphics& g)
{
    const auto& p = pal();
    drawFace (g, getLocalBounds());

    // The title.
    auto title = titleArea;
    g.setColour (p.ink);
    g.setFont (cond (30.0f, Weight::extra, 0.06f));
    g.drawText ("SONG BUILDER", title.removeFromTop (34), juce::Justification::bottomLeft);
    g.setColour (p.ink2);
    g.setFont (barlow (13.0f));
    g.drawFittedText ("Columns are the parts of your song, rows are tracks. Click a cell to switch it.", title,
                      juce::Justification::topLeft, 2, 1.0f);

    paintStrip (g);

    if (engine.getScenes().getScenes().empty())
        paintSteps (g, stepsArea);
}

void SongView::paintSteps (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& p = pal();
    struct Step { const char* title; const char* body; juce::Colour colour; };
    const Step steps[] = {
        { "Get something playing", "Make a beat on Groove (F1), play the Bass or Synth, or record loops on the Looper (F3). "
                                   "Mute what shouldn't play yet.", RigColours::mpc },
        { "Save it as a part", "Press + New part. It remembers which tracks and loops play, the MPC sequence, the bass "
                               "pattern and the arp. Change what's playing and save again for the next part.", RigColours::song },
        { "Play them in order", "Give each part a length in bars and press Play song. Or jump to any part live with "
                                "keys 1-8 or the A-H buttons along the bottom.", colours::play },
    };
    const int gap = 18;
    const int w = (area.getWidth() - 2 * gap) / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto card = area.removeFromLeft (w).toFloat();
        area.removeFromLeft (gap);
        drawWell (g, card, 14.0f);
        auto inner = card.reduced (22.0f, 18.0f);
        auto top = inner.removeFromTop (64.0f);
        auto disc = top.removeFromLeft (64.0f);
        g.setColour (steps[i].colour);
        g.fillEllipse (disc);
        g.setColour (juce::Colour (0xff1c1305));
        g.setFont (cond (40.0f, Weight::extra));
        g.drawText (juce::String (i + 1), disc.toNearestInt(), juce::Justification::centred);
        top.removeFromLeft (16.0f);
        g.setColour (p.wellInk);
        g.setFont (cond (26.0f, Weight::extra, 0.05f));
        g.drawFittedText (juce::String (steps[i].title).toUpperCase(), top.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
        inner.removeFromTop (14.0f);
        g.setColour (p.wellInk2);
        g.setFont (barlow (16.0f));
        g.drawFittedText (steps[i].body, inner.toNearestInt(), juce::Justification::topLeft, 6, 1.0f);

        // An arrow to the next step.
        if (i < 2)
        {
            const float x = card.getRight() + gap * 0.5f, y = card.getCentreY();
            juce::Path arrow;
            arrow.addTriangle (x - 5.0f, y - 9.0f, x + 6.0f, y, x - 5.0f, y + 9.0f);
            g.setColour (p.ink2);
            g.fillPath (arrow);
        }
    }
}

void SongView::paintStrip (juce::Graphics& g)
{
    const auto& p = pal();
    auto& scenes = engine.getScenes();
    const auto& list = scenes.getScenes();
    const auto& transport = engine.getTransport();
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const int current = scenes.getCurrent(), queued = scenes.getQueued();

    // Now: which part, and how far into it.
    const auto lcd = nowArea.toFloat();
    drawLcd (g, lcd);
    juce::String line1, line2;
    double progress = -1.0;
    if (list.empty())
        line1 = "NO PARTS YET", line2 = "PRESS + NEW PART";
    else if (juce::isPositiveAndBelow (current, (int) list.size()))
    {
        const auto& scene = list[(size_t) current];
        line1 = "NOW " + letterFor (current) + " " + scene.name.toUpperCase();
        const double into = juce::jmax (0.0, transport.getPositionInBeats() - scenes.getCurrentStartBeat());
        const int bar = (int) std::floor (into / bpb) + 1;
        if (scene.bars > 0)
        {
            line2 = "BAR " + juce::String (juce::jmin (bar, scene.bars)) + " OF " + juce::String (scene.bars);
            progress = juce::jlimit (0.0, 1.0, into / (scene.bars * bpb));
        }
        else
            line2 = "BAR " + juce::String (bar) + " - HOLDING";
        if (juce::isPositiveAndBelow (queued, (int) list.size()))
            line2 << juce::String::fromUTF8 (" \xe2\x86\x92 ") << letterFor (queued);
        if (! transport.isPlaying())
            line2 = "STOPPED";
    }
    else
        line1 = juce::String (list.size()) + " PARTS", line2 = juce::isPositiveAndBelow (queued, (int) list.size()) ? "STARTING " + letterFor (queued)
                                                                                                                      : "PRESS PLAY SONG";
    auto text = lcd.reduced (12.0f, 4.0f);
    lcdText (g, line1, text.removeFromTop (text.getHeight() * 0.55f), 26.0f, juce::Justification::centredLeft);
    lcdText (g, line2, text, 19.0f, juce::Justification::centredLeft, colours::lcdGreen);

    // The song to scale.
    const auto strip = stripArea.toFloat();
    drawWell (g, strip, 10.0f);
    auto inner = strip.reduced (8.0f, 7.0f);
    if (list.empty())
    {
        g.setColour (p.wellInk2);
        g.setFont (barlow (14.0f));
        g.drawText ("Your song shows up here, part by part, to scale.", inner.toNearestInt(), juce::Justification::centred);
        return;
    }
    int totalBars = 0;
    bool anyHold = false;
    for (const auto& s : list)
    {
        totalBars += s.bars > 0 ? s.bars : 0;
        anyHold = anyHold || s.bars <= 0;
    }
    // The total, at the right.
    const double seconds = totalBars * bpb * 60.0 / juce::jmax (1.0, transport.getBpm());
    auto total = inner.removeFromRight (120.0f);
    g.setColour (p.wellInk);
    g.setFont (cond (16.0f, Weight::extra, 0.06f));
    g.drawText (juce::String (totalBars) + " BARS" + (anyHold ? "+" : ""), total.removeFromTop (total.getHeight() * 0.55f).toNearestInt(),
                juce::Justification::centredRight);
    g.setColour (p.wellInk2);
    g.setFont (barlow (12.0f));
    g.drawText (juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2) + (anyHold ? " +" : ""),
                total.toNearestInt(), juce::Justification::centredRight);
    inner.removeFromRight (10.0f);

    const auto weight = [] (const RigScene& s) { return (float) (s.bars > 0 ? s.bars : 4); };
    float sum = 0;
    for (const auto& s : list)
        sum += weight (s);
    float x = inner.getX();
    const bool pulse = (juce::Time::getMillisecondCounter() / 350) % 2 == 0;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const float w = inner.getWidth() * weight (list[(size_t) i]) / sum;
        auto seg = juce::Rectangle<float> (x, inner.getY(), w, inner.getHeight()).reduced (1.5f, 0.0f);
        x += w;
        const bool isCurrent = i == current, isQueued = i == queued;
        const auto colour = isCurrent ? colours::play : isQueued && pulse ? colours::next : juce::Colour (0xff4a433d);
        g.setColour (colour.withAlpha (isCurrent ? 0.35f : 1.0f));
        g.fillRoundedRectangle (seg, 5.0f);
        if (isCurrent && progress >= 0.0 && transport.isPlaying())
        {
            g.setColour (colours::play);
            g.fillRoundedRectangle (seg.withWidth (seg.getWidth() * (float) progress), 5.0f);
        }
        else if (isCurrent)
        {
            g.setColour (colours::play);
            g.fillRoundedRectangle (seg, 5.0f);
        }
        if (list[(size_t) i].bars <= 0)
        {
            // Hold: a dashed end, it stays until you move on.
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            for (float y = seg.getY() + 2.0f; y < seg.getBottom() - 2.0f; y += 6.0f)
                g.fillRect (seg.getRight() - 4.0f, y, 2.0f, 3.0f);
        }
        g.setColour (isCurrent || (isQueued && pulse) ? juce::Colour (0xff1c1305) : p.wellInk);
        g.setFont (cond (15.0f, Weight::extra, 0.05f));
        g.drawFittedText (letterFor (i) + (seg.getWidth() > 70.0f ? "  " + list[(size_t) i].name.toUpperCase() : juce::String()),
                          seg.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    }
}

int SongView::partAtStrip (juce::Point<int> pos) const
{
    const auto& list = engine.getScenes().getScenes();
    if (list.empty() || ! stripArea.contains (pos))
        return -1;
    auto inner = stripArea.toFloat().reduced (8.0f, 7.0f);
    inner.removeFromRight (130.0f);
    float sum = 0;
    for (const auto& s : list)
        sum += (float) (s.bars > 0 ? s.bars : 4);
    float x = inner.getX();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        x += inner.getWidth() * (float) (list[(size_t) i].bars > 0 ? list[(size_t) i].bars : 4) / sum;
        if ((float) pos.x < x)
            return i;
    }
    return -1;
}

void SongView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;
    const int part = partAtStrip (e.getPosition());
    if (part >= 0)
        engine.getScenes().launch (part);
}

void SongView::resized()
{
    auto area = getLocalBounds().reduced (18, 12);

    // The top: title, then the buttons.
    auto top = area.removeFromTop (58);
    const int buttonW = juce::jlimit (92, 128, (top.getWidth() - 360) / 7 - 8);
    for (auto* b : { &exportButton, &keys, &next, &back, &inOrder, &playButton })
    {
        b->setBounds (top.removeFromRight (buttonW));
        top.removeFromRight (8);
    }
    top.removeFromRight (6);
    addButton.setBounds (top.removeFromRight (juce::jmin (176, buttonW + 40)));
    top.removeFromRight (16);
    titleArea = top;

    area.removeFromTop (10);
    auto strip = area.removeFromTop (50);
    nowArea = strip.removeFromLeft (290);
    strip.removeFromLeft (12);
    stripArea = strip;
    setMouseCursor (juce::MouseCursor::NormalCursor);

    area.removeFromTop (8);
    const bool empty = engine.getScenes().getScenes().empty();
    viewport.setVisible (! empty);
    bigAdd.setVisible (empty);
    if (empty)
    {
        auto cta = area.removeFromBottom (juce::jmin (76, area.getHeight() / 4));
        bigAdd.setBounds (cta.withSizeKeepingCentre (juce::jmin (520, cta.getWidth()), cta.getHeight()));
        area.removeFromBottom (14);
        stepsArea = area;
        return;
    }
    stepsArea = {};
    viewport.setBounds (area);
}
