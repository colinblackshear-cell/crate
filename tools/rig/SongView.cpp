#include "SongView.h"
#include "RigColours.h"
#include "RigExport.h"
#include "TimelineView.h"
#include "Vintage.h"

#include <deque>

using namespace vintage;

namespace
{
constexpr int kHead = 46, kRuler = 18, kNamesW = 158, kInspectorW = 286;
constexpr double kEps = 1.0e-6;

juce::String dot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }

// Bars as a person says them in a fade: "1 beat", "2 bars".
juce::String fadeText (double bars, int bpb)
{
    if (bars <= kEps)
        return "Straight";
    if (bars < 1.0 - kEps)
    {
        const int beats = juce::roundToInt (bars * bpb);
        return juce::String (beats) + (beats == 1 ? " beat" : " beats");
    }
    return rigsong::barsText (bars);
}
}

juce::Colour SongView::laneColour (int lane)
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return RigColours::song;
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    return info.isLoop ? RigColours::loops : RigColours::forStrip (info.index);
}

//==============================================================================
namespace
{
// A row of chips to pick from, wrapping onto more lines (the panel's choices).
class Chips
{
public:
    struct Item
    {
        juce::String label;
        int value;
    };
    std::vector<Item> items;
    int chosen = -100000;
    std::function<void (int)> onPick;

    // Lays them out from `area`'s top-left; returns the height used.
    int layout (juce::Rectangle<int> area)
    {
        rects.clear();
        int x = area.getX(), y = area.getY();
        const int h = 26, gap = 5;
        for (const auto& item : items)
        {
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (cond (15.0f, Weight::bold), item.label)) + 16;
            if (x + w > area.getRight() && x > area.getX())
            {
                x = area.getX();
                y += h + gap;
            }
            rects.push_back ({ x, y, juce::jmin (w, area.getWidth()), h });
            x += w + gap;
        }
        return items.empty() ? 0 : y + h - area.getY();
    }

    void paint (juce::Graphics& g, juce::Colour colour) const
    {
        for (size_t i = 0; i < items.size() && i < rects.size(); ++i)
        {
            const bool on = items[i].value == chosen;
            const auto r = rects[i].toFloat();
            drawButton (g, r, Lit::none, false, false, false, 6.0f);
            if (on)
            {
                g.setColour (colour);
                g.fillRoundedRectangle (r.reduced (2.0f), 5.0f);
            }
            g.setColour (on ? juce::Colour (0xff1a1410) : pal().btnInk);
            g.setFont (cond (15.0f, Weight::bold));
            g.drawFittedText (items[i].label, rects[i].reduced (6, 0), juce::Justification::centred, 1, 0.8f);
        }
    }

    bool click (juce::Point<int> p)
    {
        for (size_t i = 0; i < items.size() && i < rects.size(); ++i)
            if (rects[i].contains (p))
            {
                if (onPick)
                    onPick (items[i].value);
                return true;
            }
        return false;
    }

private:
    std::vector<juce::Rectangle<int>> rects;
};
}

//==============================================================================
// The track names down the left, beside the board (they stay put while the board scrolls).
class SongView::Names : public juce::Component
{
public:
    explicit Names (SongView& o) : owner (o) {}

    int laneH = 40;

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawWell (g, getLocalBounds().toFloat(), 8.0f);
        g.setColour (p.wellInk2);
        g.setFont (cond (12.0f, Weight::extra, 0.12f));
        g.drawText ("TRACKS", getLocalBounds().removeFromTop (kHead).reduced (12, 6), juce::Justification::topLeft);
        g.setFont (cond (11.0f, Weight::bold, 0.08f));
        g.drawText ("BAR", getLocalBounds().withY (kHead).withHeight (kRuler).reduced (12, 0), juce::Justification::centredLeft);
        const auto lanes = owner.shownLanes();
        for (size_t i = 0; i < lanes.size(); ++i)
        {
            const int lane = lanes[i];
            auto r = juce::Rectangle<int> (0, kHead + kRuler + (int) i * laneH, getWidth(), laneH);
            const bool picked = lane == owner.selLane;
            if (picked)
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.fillRect (r);
            }
            g.setColour (laneColour (lane));
            g.fillRoundedRectangle (r.removeFromLeft (10).reduced (3, 6).toFloat(), 2.0f);
            r.removeFromLeft (6);
            g.setColour (p.wellInk);
            g.setFont (cond (laneH >= 36 ? 17.0f : 15.0f, Weight::bold));
            if (laneH >= 34)
            {
                g.drawText (rigsong::laneName (lane), r.removeFromTop (r.getHeight() / 2 + 2).withTrimmedTop (3), juce::Justification::bottomLeft);
                g.setColour (p.wellInk2);
                g.setFont (barlow (12.0f));
                g.drawText (rigsong::laneHint (owner.engine, lane), r, juce::Justification::topLeft);
            }
            else
                g.drawText (rigsong::laneName (lane), r, juce::Justification::centredLeft);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawHorizontalLine (kHead + kRuler + (int) (i + 1) * laneH - 1, 0.0f, (float) getWidth());
        }
    }

private:
    SongView& owner;
};

//==============================================================================
// The sections, the ruler and the boxes. It sits in a viewport, so a long song scrolls.
class SongView::Board : public juce::Component
{
public:
    explicit Board (SongView& o) : owner (o) {}

    double px = 24.0;   // per bar
    int laneH = 40;

    RigTimeline& tl() const { return owner.timeline(); }

    void updateSize (int viewWidth, int viewHeight)
    {
        const double bars = juce::jmax (8.0, rigsong::songBars (tl()) + 2.0);
        px = juce::jmax (16.0, (viewWidth - 8) / bars);
        setSize (juce::jmax (viewWidth, (int) std::ceil (bars * px) + 8), viewHeight);
    }

    int laneY (int i) const { return kHead + kRuler + i * laneH; }
    float xFor (double bar) const { return (float) (bar * px); }
    double barAt (int x) const { return x / px; }

    juce::Rectangle<int> sectionHead (const rigsong::Section& s) const
    {
        return juce::Rectangle<int> ((int) xFor (s.start), 0, (int) (s.bars * px), kHead);
    }
    juce::Rectangle<int> boxRect (const rigsong::Section& s, int row) const
    {
        return juce::Rectangle<int> ((int) xFor (s.start), laneY (row), (int) (s.bars * px), laneH).reduced (2, 4);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.setColour (p.well);
        g.fillRect (getLocalBounds());
        const auto list = rigsong::sections (tl());
        const auto lanes = owner.shownLanes();
        const auto& engine = owner.engine;
        auto& transport = owner.engine.getTransport();
        const int bpb = juce::jmax (1, transport.getBeatsPerBar());
        const double nowBar = transport.getPositionInBeats() / bpb;
        const bool playing = transport.isPlaying() && tl().on.load();
        const int nowSection = playing ? rigsong::sectionAt (tl(), nowBar) : -1;
        const double pending = tl().getPendingJump();
        const int nextSection = playing && pending >= 0 ? rigsong::sectionAt (tl(), pending) : -1;
        juce::ignoreUnused (engine);

        // The sections along the top.
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& s = list[(size_t) i];
            auto r = sectionHead (s);
            const bool picked = i == owner.selSection && owner.selLane < 0;
            g.setColour (i == nowSection ? juce::Colour (0xff3a3220) : picked ? juce::Colour (0xff332c24) : juce::Colour (0xff221e1a));
            g.fillRect (r);
            g.setColour (juce::Colours::black);
            g.fillRect (r.removeFromLeft (2));
            auto text = r.reduced (6, 4);
            auto top = text.removeFromTop (20);
            auto badge = top.removeFromLeft (20).toFloat();
            g.setColour (i == nowSection ? colours::play : i == nextSection ? colours::next : RigColours::song);
            g.fillRoundedRectangle (badge, 4.0f);
            g.setColour (juce::Colour (0xff1a1410));
            g.setFont (cond (15.0f, Weight::extra));
            g.drawText (rigsong::letterFor (i), badge, juce::Justification::centred);
            top.removeFromLeft (6);
            g.setColour (p.wellInk);
            g.setFont (cond (16.0f, Weight::extra, 0.04f));
            g.drawFittedText (s.name.toUpperCase(), top, juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (p.wellInk2);
            g.setFont (barlow (12.0f));
            juce::String sub = rigsong::barsText (s.bars);
            if (i == nowSection)
                sub = juce::String::fromUTF8 ("\xe2\x96\xb6 ") + sub;
            else if (i == nextSection)
                sub = "next" + dot() + sub;
            if (i < (int) tl().getMarkers().size() && tl().getMarkers()[(size_t) i].loopBank >= 0)
                sub += dot() + owner.engine.getLoopBankName (tl().getMarkers()[(size_t) i].loopBank);
            g.drawFittedText (sub, text, juce::Justification::centredLeft, 1, 0.8f);
            if (picked)
            {
                g.setColour (colours::selected);
                g.drawRect (sectionHead (s), 2);
            }
            // The drag handle on its right edge.
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            const auto edge = sectionHead (s).getRight();
            g.fillRect (edge - 4, 12, 2, kHead - 24);
        }

        // The ruler.
        {
            auto ruler = juce::Rectangle<int> (0, kHead, getWidth(), kRuler);
            g.setColour (juce::Colour (0xff181512));
            g.fillRect (ruler);
            const int every = px >= 30 ? 1 : px >= 15 ? 2 : 4;
            g.setFont (barlow (11.0f));
            const int maxBar = (int) std::ceil (getWidth() / px);
            for (int b = 0; b <= maxBar; ++b)
            {
                const float x = xFor (b);
                g.setColour (juce::Colours::white.withAlpha (b % 4 == 0 ? 0.25f : 0.1f));
                g.drawVerticalLine ((int) x, (float) kHead + (b % every == 0 ? 6.0f : 12.0f), (float) (kHead + kRuler));
                if (b % every == 0)
                {
                    g.setColour (p.wellInk2);
                    g.drawText (juce::String (b + 1), (int) x + 3, kHead, 30, kRuler, juce::Justification::centredLeft);
                }
            }
            if (tl().cycle.load())
            {
                g.setColour (RigColours::song.withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (xFor (tl().cycleStart.load()), (float) kHead + kRuler - 4,
                                                    (float) ((tl().cycleEnd.load() - tl().cycleStart.load()) * px), 4.0f));
            }
            // Where Play starts.
            const float sx = xFor (owner.engine.songStartBar);
            juce::Path tri;
            tri.addTriangle (sx, (float) kHead + kRuler - 1, sx - 6, (float) kHead + 4, sx + 6, (float) kHead + 4);
            g.setColour (colours::play);
            g.fillPath (tri);
        }

        // The boxes.
        const auto& blocks = tl().getBlocks();
        for (int row = 0; row < (int) lanes.size(); ++row)
        {
            const int lane = lanes[(size_t) row];
            const auto colour = laneColour (lane);
            const int y = laneY (row);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawHorizontalLine (y + laneH - 1, 0.0f, (float) getWidth());
            // Empty boxes: a dashed outline and a +.
            for (int i = 0; i < (int) list.size(); ++i)
            {
                if (rigsong::boxBlock (tl(), i, lane) >= 0)
                    continue;
                const auto r = boxRect (list[(size_t) i], row).toFloat();
                juce::Path outline;
                outline.addRoundedRectangle (r.reduced (1.0f), 5.0f);
                juce::Path dashed;
                const float dashes[] = { 4.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (juce::Colours::white.withAlpha (0.13f));
                g.fillPath (dashed);
                g.setColour (juce::Colours::white.withAlpha (0.2f));
                g.setFont (cond (18.0f, Weight::bold));
                g.drawText ("+", r, juce::Justification::centred);
            }
            // The blocks themselves (a box is one; Bars view can make more).
            for (const auto& b : blocks)
            {
                if (b.lane != lane)
                    continue;
                auto r = juce::Rectangle<float> (xFor (b.start) + 2.0f, (float) y + 4.0f, (float) (b.length * px) - 4.0f, (float) laneH - 8.0f);
                if (r.getWidth() < 2.0f)
                    continue;
                juce::ColourGradient grad (colour, r.getX(), 0, colour, r.getRight(), 0, false);
                const float alpha = 0.55f + 0.45f * b.level;
                const auto c = colour.withAlpha (alpha);
                grad = juce::ColourGradient (c, r.getX(), 0, c, r.getRight(), 0, false);
                if (b.fadeIn > kEps)
                {
                    grad.clearColours();
                    grad.addColour (0.0, c.withAlpha (0.08f));
                    grad.addColour (juce::jlimit (0.01, 0.99, b.fadeIn / b.length), c);
                    grad.addColour (1.0, c);
                }
                if (b.fadeOut > kEps)
                {
                    const double from = 1.0 - b.fadeOut / b.length;
                    if (b.fadeIn <= kEps)
                    {
                        grad.clearColours();
                        grad.addColour (0.0, c);
                    }
                    grad.addColour (juce::jlimit (0.02, 0.98, from), c);
                    grad.addColour (1.0, c.withAlpha (0.08f));
                }
                g.setGradientFill (grad);
                g.fillRoundedRectangle (r, 5.0f);
                auto text = r.toNearestInt().reduced (7, 3);
                if (b.fadeIn > kEps)
                    text.removeFromLeft (juce::jmin (text.getWidth() / 3, (int) (b.fadeIn * px * 0.5)));
                g.setColour (juce::Colour (0xff1a1410));
                g.setFont (cond (laneH >= 36 ? 15.0f : 13.0f, Weight::bold));
                const auto name = rigsong::clipName (owner.engine, lane, b.clip);
                if (laneH >= 36)
                {
                    g.drawFittedText (name, text.removeFromTop (text.getHeight() / 2 + 2), juce::Justification::bottomLeft, 1, 0.75f);
                    juce::StringArray extra;
                    if (b.fadeIn > kEps)
                        extra.add ("fades in");
                    if (b.fadeOut > kEps)
                        extra.add ("fades out");
                    if (b.level < 0.99f)
                        extra.add (juce::String (juce::roundToInt (b.level * 100)) + "%");
                    g.setFont (barlow (11.0f));
                    g.setColour (juce::Colour (0xff1a1410).withAlpha (0.75f));
                    g.drawFittedText (extra.joinIntoString (dot()), text, juce::Justification::topLeft, 1, 0.8f);
                }
                else
                    g.drawFittedText (name, text, juce::Justification::centredLeft, 1, 0.75f);
            }
        }

        // The picked box.
        if (owner.selLane >= 0 && juce::isPositiveAndBelow (owner.selSection, (int) list.size()))
        {
            const auto it = std::find (lanes.begin(), lanes.end(), owner.selLane);
            if (it != lanes.end())
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (boxRect (list[(size_t) owner.selSection], (int) (it - lanes.begin())).toFloat().expanded (1.0f), 6.0f, 2.5f);
            }
        }

        // Where it's playing.
        if (transport.isPlaying())
        {
            const float x = xFor (nowBar);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (x - 1.0f, (float) kHead, 2.0f, (float) getHeight() - kHead);
        }
    }

    // Which section is under x (-1 past the end).
    int sectionAtX (int x) const { return rigsong::sectionAt (tl(), barAt (x)); }
    int rowAtY (int y) const { return y < kHead + kRuler ? -1 : (y - kHead - kRuler) / juce::jmax (1, laneH); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (edgeAt (e.getPosition()) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    // The section whose right edge is under the mouse, on the section row.
    int edgeAt (juce::Point<int> p) const
    {
        if (p.y >= kHead)
            return -1;
        const auto list = rigsong::sections (tl());
        for (int i = 0; i < (int) list.size(); ++i)
            if (std::abs (p.x - sectionHead (list[(size_t) i]).getRight()) <= 5)
                return i;
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = edgeAt (e.getPosition());
        if (dragging >= 0)
            return;
        const auto pos = e.getPosition();
        if (pos.y >= kHead && pos.y < kHead + kRuler)
        {
            // The ruler: where Play starts (while the song plays, it goes there at the next bar).
            owner.engine.songStartBar = juce::jmax (0.0, std::floor (barAt (pos.x)));
            if (owner.engine.getTransport().isPlaying() && tl().on.load())
                tl().jumpTo (owner.engine.songStartBar);
            repaint();
            return;
        }
        const int section = sectionAtX (pos.x);
        if (section < 0)
        {
            owner.select (-1, -1);
            return;
        }
        if (pos.y < kHead)
        {
            owner.select (section, -1);
            if (e.mods.isPopupMenu())
                owner.showSectionMenu (section);
            else if (e.getNumberOfClicks() >= 2)
                owner.renameSection (section);
            return;
        }
        const auto lanes = owner.shownLanes();
        const int row = rowAtY (pos.y);
        if (! juce::isPositiveAndBelow (row, (int) lanes.size()))
            return;
        owner.select (section, lanes[(size_t) row]);
        if (e.mods.isPopupMenu())
            owner.showBoxMenu (section, lanes[(size_t) row]);
        else if (e.getNumberOfClicks() >= 2 && rigsong::boxBlock (tl(), section, lanes[(size_t) row]) < 0)
        {
            // Double-click an empty box: it plays what that track has on now.
            const auto choices = rigsong::clipChoices (owner.engine, lanes[(size_t) row]);
            owner.pickClip (choices.size() > 1 ? choices[1].first : choices.front().first);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        const auto list = rigsong::sections (tl());
        if (! juce::isPositiveAndBelow (dragging, (int) list.size()))
            return;
        const double bars = juce::jmax (1.0, std::round (barAt (e.x) - list[(size_t) dragging].start));
        if (std::abs (bars - list[(size_t) dragging].bars) > kEps)
        {
            rigsong::setSectionBars (tl(), dragging, bars);
            owner.edited();
        }
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

private:
    SongView& owner;
    int dragging = -1;
};

//==============================================================================
// The panel on the right: what the picked box plays and how, or the picked section.
class SongView::Inspector : public juce::Component
{
public:
    explicit Inspector (SongView& o) : owner (o)
    {
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 22);
        level.setRange (0.0, 100.0, 1.0);
        level.setTextValueSuffix ("%");
        level.onValueChange = [this] { owner.setLevel ((float) level.getValue() / 100.0f); };
        addChildComponent (level);
        for (auto* b : { &edit, &sameAs, &playHere, &loopIt, &copy, &moveLeft, &moveRight, &remove, &rename })
        {
            b->fontSize = 15.0f;
            addChildComponent (b);
        }
        edit.onClick = [this] { if (owner.onGoTo) owner.onGoTo (owner.pageForLane (owner.selLane)); };
        sameAs.onClick = [this] { owner.showBoxMenu (owner.selSection, owner.selLane); };
        playHere.onClick = [this] { owner.engine.launchSection (owner.selSection); };
        loopIt.onClick = [this] { owner.loopSection (owner.selSection); };
        copy.onClick = [this]
        {
            const int i = rigsong::duplicateSection (owner.timeline(), owner.selSection);
            owner.edited();
            if (i >= 0)
                owner.select (i, -1);
        };
        moveLeft.onClick = [this] { move (-1); };
        moveRight.onClick = [this] { move (1); };
        remove.onClick = [this]
        {
            const auto name = rigsong::sections (owner.timeline())[(size_t) owner.selSection].name;
            rigsong::removeSection (owner.timeline(), owner.selSection);
            owner.select (-1, -1);
            owner.edited();
            owner.say ("Deleted " + name + ". Cmd+Z in Bars view brings it back.");
        };
        rename.onClick = [this] { owner.renameSection (owner.selSection); };
    }

    void move (int by)
    {
        const int n = (int) rigsong::sections (owner.timeline()).size();
        const int to = juce::jlimit (0, n - 1, owner.selSection + by);
        rigsong::moveSection (owner.timeline(), owner.selSection, to);
        owner.select (to, -1);
        owner.edited();
    }

    void update()
    {
        groups.clear();
        const auto list = rigsong::sections (owner.timeline());
        const int s = owner.selSection, lane = owner.selLane;
        const bool haveSection = juce::isPositiveAndBelow (s, (int) list.size());
        const int bpb = juce::jmax (1, owner.engine.getTransport().getBeatsPerBar());
        for (auto* b : { &edit, &sameAs, &playHere, &loopIt, &copy, &moveLeft, &moveRight, &remove, &rename })
            b->setVisible (false);
        level.setVisible (false);
        title.clear();
        sub.clear();
        if (! haveSection)
        {
            resized();
            repaint();
            return;
        }
        const auto& sec = list[(size_t) s];
        auto& tl = owner.timeline();
        if (lane < 0)
        {
            title = rigsong::letterFor (s) + dot() + sec.name.toUpperCase();
            sub = "Bars " + juce::String ((int) sec.start + 1) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String ((int) std::ceil (sec.end() - kEps))
                + dot() + rigsong::barsText (sec.bars);
            auto& length = addGroup ("LENGTH", RigColours::song);
            for (int b : { 1, 2, 4, 8, 12, 16, 32 })
                length.items.push_back ({ juce::String (b), b });
            length.chosen = juce::roundToInt (sec.bars);
            length.onPick = [this] (int v) { owner.setSectionLength (v); };
            if (owner.engine.getNumLoopBanks() > 1)
            {
                auto& bank = addGroup ("LOOP BANK", RigColours::loops);
                bank.items.push_back ({ "As it is", -1 });
                for (int b = 0; b < owner.engine.getNumLoopBanks(); ++b)
                    bank.items.push_back ({ owner.engine.getLoopBankName (b), b });
                bank.chosen = s < (int) tl.getMarkers().size() ? tl.getMarkers()[(size_t) s].loopBank : -1;
                bank.onPick = [this] (int v) { owner.setSectionBank (v); };
            }
            for (auto* b : { &playHere, &loopIt, &copy, &moveLeft, &moveRight, &remove, &rename })
                b->setVisible (true);
            moveLeft.setEnabled (s > 0);
            moveRight.setEnabled (s < (int) list.size() - 1);
            loopIt.lit = tl.cycle.load() && std::abs (tl.cycleStart.load() - sec.start) < kEps ? Lit::current : Lit::none;
            resized();
            repaint();
            return;
        }

        const auto colour = laneColour (lane);
        title = sec.name.toUpperCase() + dot() + rigsong::laneName (lane).toUpperCase();
        const int bi = rigsong::boxBlock (tl, s, lane);
        const RigTimeline::Block* b = bi >= 0 ? &tl.getBlocks()[(size_t) bi] : nullptr;
        if (b == nullptr)
            sub = "Empty: quiet in this section. Pick what it plays.";
        else if (b->start < sec.start - kEps)
            sub = "Carries on from bar " + juce::String ((int) b->start + 1);
        else
            sub = rigsong::barsText (juce::jmin (b->end(), sec.end()) - b->start) + " from bar " + juce::String ((int) b->start + 1);

        auto& plays = addGroup ("PLAYS", colour);
        for (const auto& [clip, name] : rigsong::clipChoices (owner.engine, lane))
            plays.items.push_back ({ name, clip });
        plays.items.push_back ({ "Silent", kSilent });
        plays.chosen = b != nullptr ? b->clip : kSilent;
        plays.onPick = [this] (int v) { owner.pickClip (v); };
        edit.setVisible (! RigTimeline::lanes()[(size_t) lane].isLoop || true);
        sameAs.setVisible (true);
        if (b != nullptr)
        {
            const double len = b->length;
            auto& in = addGroup ("FADES IN", colour);
            auto& out = addGroup ("FADES OUT", colour);
            for (double f : { 0.0, 1.0 / bpb, 1.0, 2.0, 4.0 })
                if (f < len - kEps || f == 0.0)
                {
                    in.items.push_back ({ fadeText (f, bpb), juce::roundToInt (f * 1000) });
                    out.items.push_back ({ fadeText (f, bpb), juce::roundToInt (f * 1000) });
                }
            in.chosen = juce::roundToInt (b->fadeIn * 1000);
            out.chosen = juce::roundToInt (b->fadeOut * 1000);
            in.onPick = [this] (int v) { owner.setFadeIn (v / 1000.0); };
            out.onPick = [this] (int v) { owner.setFadeOut (v / 1000.0); };
            if (sec.bars > 1.0 + kEps && b->start >= sec.start - kEps)
            {
                auto& starts = addGroup ("COMES IN ON BAR", colour);
                for (int bar = 1; bar <= juce::jmin (8, (int) std::ceil (sec.bars - kEps)); ++bar)
                    starts.items.push_back ({ juce::String (bar), bar });
                starts.chosen = juce::roundToInt (b->start - sec.start) + 1;
                starts.onPick = [this] (int v) { owner.setStartBar (v); };
            }
            level.setVisible (true);
            level.setValue (b->level * 100.0, juce::dontSendNotification);
        }
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawWell (g, getLocalBounds().toFloat(), 8.0f);
        auto r = getLocalBounds().reduced (14, 12);
        if (title.isEmpty())
        {
            g.setColour (p.wellInk);
            g.setFont (cond (18.0f, Weight::extra, 0.04f));
            g.drawText ("PICK A BOX", r.removeFromTop (26), juce::Justification::topLeft);
            g.setColour (p.wellInk2);
            g.setFont (barlow (14.0f));
            g.drawFittedText ("Click a box to choose what that track plays in that section, and how it comes in and goes out.\n\n"
                              "Click a section's name for its length, name and loop bank. Drag its right edge for more bars.\n\n"
                              "Double-click an empty box: it plays what that track has on now.",
                              r.removeFromTop (220), juce::Justification::topLeft, 12, 1.0f);
            return;
        }
        g.setColour (p.wellInk);
        g.setFont (cond (19.0f, Weight::extra, 0.05f));
        g.drawFittedText (title, r.removeFromTop (24), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (p.wellInk2);
        g.setFont (barlow (13.0f));
        g.drawFittedText (sub, r.removeFromTop (22), juce::Justification::topLeft, 1, 0.8f);
        for (const auto& grp : groups)
        {
            g.setColour (p.wellInk2);
            g.setFont (cond (11.5f, Weight::extra, 0.14f));
            g.drawText (grp.label, grp.labelArea, juce::Justification::bottomLeft);
            grp.chips.paint (g, grp.colour);
        }
        if (level.isVisible())
        {
            g.setColour (p.wellInk2);
            g.setFont (cond (11.5f, Weight::extra, 0.14f));
            g.drawText ("LEVEL", level.getBounds().withY (level.getY() - 18).withHeight (16), juce::Justification::bottomLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        r.removeFromTop (24 + 22);
        for (auto& grp : groups)
        {
            grp.labelArea = r.removeFromTop (16);
            r.removeFromTop (3);
            const int h = grp.chips.layout (r);
            r.removeFromTop (h + 6);
        }
        if (level.isVisible())
        {
            r.removeFromTop (16);
            level.setBounds (r.removeFromTop (24));
            r.removeFromTop (8);
        }
        auto row = [&r] (std::initializer_list<rigpanels::PanelButton*> bs)
        {
            int visible = 0;
            for (auto* b : bs)
                visible += b->isVisible() ? 1 : 0;
            if (visible == 0)
                return;
            auto line = r.removeFromTop (32);
            const int w = (line.getWidth() - (visible - 1) * 6) / visible;
            for (auto* b : bs)
                if (b->isVisible())
                {
                    b->setBounds (line.removeFromLeft (w));
                    line.removeFromLeft (6);
                }
            r.removeFromTop (6);
        };
        row ({ &edit, &sameAs });
        row ({ &playHere, &loopIt });
        row ({ &rename, &copy });
        row ({ &moveLeft, &moveRight });
        row ({ &remove });
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (auto& grp : groups)
            if (grp.chips.click (e.getPosition()))
                return;
    }

private:
    struct Group
    {
        juce::String label;
        juce::Colour colour;
        Chips chips;
        juce::Rectangle<int> labelArea;
    };
    Chips& addGroup (const juce::String& label, juce::Colour colour)
    {
        groups.push_back ({ label, colour, {}, {} });
        return groups.back().chips;
    }

    SongView& owner;
    std::deque<Group> groups;
    juce::String title, sub;
    juce::Slider level;
    rigpanels::PanelButton edit { juce::String::fromUTF8 ("Edit \xe2\x86\x97") }, sameAs { "Same as..." },
        playHere { juce::String::fromUTF8 ("\xe2\x96\xb6 Play here") }, loopIt { "Loop it" }, copy { "Copy" },
        moveLeft { juce::String::fromUTF8 ("\xe2\x97\x80 Move") }, moveRight { juce::String::fromUTF8 ("Move \xe2\x96\xb6") },
        remove { "Delete section" }, rename { "Rename" };
};

//==============================================================================
SongView::SongView (RigEngine& e) : engine (e)
{
    board = std::make_unique<Board> (*this);
    names = std::make_unique<Names> (*this);
    inspector = std::make_unique<Inspector> (*this);
    viewport.setViewedComponent (board.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (*names);
    addAndMakeVisible (*inspector);

    bars = std::make_unique<TimelineView> (engine);
    bars->onSay = [this] (const juce::String& s) { say (s); };
    bars->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongView> (this)]
                                                             { if (safe != nullptr) safe->showBars (false); }); };
    addChildComponent (*bars);

    addButton.sub = "empty, or what's playing";
    addButton.setTooltip ("Adds a section at the end of the song: an empty one to fill in, or one that plays what's on now.");
    addButton.onClick = [this] { showAddMenu(); };
    jamButton.sub = "Play: what's on";
    jamButton.setTooltip ("Play at the top plays whatever you've switched on, as you jam. The song waits.");
    jamButton.onClick = [this] { setSongMode (false); };
    songButton.sub = "Play: this song";
    songButton.setTooltip ("Play at the top plays this song, section by section, from the green marker on the ruler.");
    songButton.onClick = [this] { setSongMode (true); };
    playButton.sub = "over the click";
    playButton.setTooltip ("Plays the song from bar 1 (Song mode on).");
    playButton.onClick = [this] { playFromStart(); };
    loopButton.sub = "practise one part";
    loopButton.setTooltip ("Plays the picked section over and over (pick one first). Again to stop looping.");
    loopButton.onClick = [this] { loopSection (selSection); };
    barsButton.sub = "fine edits, fades";
    barsButton.setTooltip ("The same song as lanes of blocks over every bar: move, stretch and fade blocks anywhere.");
    barsButton.onClick = [this] { showBars (! barsShown); };
    recButton.sub = "stems for Logic";
    recButton.setTooltip ("Plays the song once from bar 1 and records every track as its own WAV (and the mix, and MIDI). Sing along!");
    recButton.onClick = [this] { if (bars != nullptr) bars->recordSong(); };
    exportButton.sub = "loops for Logic";
    exportButton.setTooltip ("Saves the loops, each on its own and laid out over the song, for Logic.");
    exportButton.onClick = [this]
    {
        if (onExport)
            onExport();
        else
        {
            const auto result = RigExport::exportForLogic (engine, RigExport::defaultFolder());
            say (result.files > 0 ? "Exported " + juce::String (result.files) + " files to " + result.folder.getFullPathName() : result.error);
        }
    };
    moreButton.setTooltip ("More: keyboard buttons for Next and Back, clear the song.");
    moreButton.onClick = [this] { showMoreMenu(); };
    for (auto* b : { &addButton, &jamButton, &songButton, &playButton, &loopButton, &barsButton, &recButton, &exportButton, &moreButton })
    {
        b->fontSize = 16.0f;
        b->subSize = 10.5f;
        addAndMakeVisible (b);
    }
    addButton.fontSize = 18.0f;
    moreButton.fontSize = 22.0f;

    bigAdd.sub = "the tracks and patterns on now become its boxes";
    bigEmpty.sub = "then click its boxes to fill them";
    for (auto* b : { &bigAdd, &bigEmpty })
    {
        b->fontSize = 22.0f;
        b->subSize = 12.0f;
        addChildComponent (b);
    }
    bigAdd.lit = Lit::next;
    bigAdd.onClick = [this] { addSectionFromPlaying(); };
    bigEmpty.onClick = [this] { addEmptySection(); };

    refresh();
    startTimerHz (30);
}

SongView::~SongView() { stopTimer(); }

//==============================================================================
std::vector<int> SongView::shownLanes() const
{
    // Tools first (the order you'd build a song in), then the loops.
    static const int order[] = { RigEngine::mpcStrip, RigEngine::drumsStrip, RigEngine::bassStrip, RigEngine::synthStrip,
                                 RigEngine::samplerStrip, RigEngine::vocoderStrip, RigEngine::inputStrip };
    std::vector<int> out;
    for (int strip : order)
    {
        const int lane = RigTimeline::laneForStrip (strip);
        if (lane >= 0 && (showingAllLanes || rigsong::laneHasSomething (engine, lane)))
            out.push_back (lane);
    }
    for (int l = 0; l < RigLooper::kLayers; ++l)
        if (showingAllLanes || rigsong::laneHasSomething (engine, RigTimeline::laneForLoop (l)))
            out.push_back (RigTimeline::laneForLoop (l));
    return out;
}

juce::String SongView::pageForLane (int lane) const
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return "Groove";
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    if (info.isLoop)
        return "Looper";
    switch (info.index)
    {
        case RigEngine::vocoderStrip: return "Vocoder";
        case RigEngine::samplerStrip: return "Sampler";
        case RigEngine::inputStrip:   return "Effects";
        default:                      return "Groove";
    }
}

void SongView::refresh()
{
    if (bars != nullptr)
        bars->refresh();
    edited();
}

void SongView::edited()
{
    const auto list = rigsong::sections (timeline());
    if (selSection >= (int) list.size())
        select (-1, -1);
    resized();
    inspector->update();
    board->repaint();
    names->repaint();
    repaint();
}

void SongView::select (int section, int lane)
{
    selSection = section;
    selLane = section < 0 ? -1 : lane;
    inspector->update();
    board->repaint();
    names->repaint();
}

//==============================================================================
int SongView::addEmptySection()
{
    const auto list = rigsong::sections (timeline());
    const double bars = list.empty() ? 4.0 : juce::jmax (4.0, list.back().bars);
    const auto name = rigsong::suggestName (timeline());
    const int i = rigsong::addSection (timeline(), name, list.empty() ? 4.0 : bars);
    if (i < 0)
    {
        say ("A song can have up to " + juce::String (rigsong::kMaxSections) + " sections.");
        return -1;
    }
    select (i, -1);
    edited();
    viewport.setViewPosition (board->getWidth(), 0);
    say (rigsong::letterFor (i) + " (" + name + ") added. Click its boxes to say what plays; double-click its name to rename it.");
    return i;
}

int SongView::addSectionFromPlaying()
{
    const auto list = rigsong::sections (timeline());
    const auto name = rigsong::suggestName (timeline());
    const int i = rigsong::addSectionFromWhatsPlaying (engine, name, list.empty() ? 8.0 : juce::jmax (4.0, list.back().bars));
    if (i < 0)
    {
        say ("A song can have up to " + juce::String (rigsong::kMaxSections) + " sections.");
        return -1;
    }
    select (i, -1);
    edited();
    viewport.setViewPosition (board->getWidth(), 0);
    say (rigsong::letterFor (i) + " (" + name + ") plays what's on now. Click a box to change it.");
    return i;
}

void SongView::setSongMode (bool song)
{
    timeline().on = song;
    if (! song)
        timeline().cycle = false;
    say (song ? "Song: Play (and space) plays this song from the green marker." : "Jam: Play runs whatever you've switched on. The song waits here.");
    repaint();
}

void SongView::playFromStart()
{
    if (timeline().isEmpty() && timeline().getMarkers().empty())
    {
        say ("Add a section first (+ Section).");
        return;
    }
    timeline().cycle = false;
    engine.songStartBar = 0;
    engine.getTransport().stop();
    engine.playTimeline (0);
    say ("Playing the song from bar 1.");
}

void SongView::loopSection (int section)
{
    const auto list = rigsong::sections (timeline());
    if (! juce::isPositiveAndBelow (section, (int) list.size()))
    {
        say ("Click a section's name first, then Loop section plays it over and over.");
        return;
    }
    const auto& s = list[(size_t) section];
    auto& tl = timeline();
    if (tl.cycle.load() && std::abs (tl.cycleStart.load() - s.start) < kEps && std::abs (tl.cycleEnd.load() - s.end()) < kEps)
    {
        tl.cycle = false;
        say ("Looping off: the song carries on.");
    }
    else
    {
        tl.cycleStart = s.start;
        tl.cycleEnd = s.end();
        tl.cycle = true;
        engine.songStartBar = s.start;
        engine.playTimeline (s.start);
        say ("Looping " + s.name + " (" + rigsong::barsText (s.bars) + "). Press Loop again to let the song go on.");
    }
    inspector->update();
    board->repaint();
}

void SongView::showBars (bool show)
{
    barsShown = show;
    if (bars != nullptr)
    {
        bars->refresh();
        bars->setVisible (show);
        if (show)
            bars->grabKeyboardFocus();
    }
    barsButton.lit = show ? Lit::current : Lit::none;
    barsButton.text = show ? "Sections view" : "Bars view";
    barsButton.repaint();
    edited();
}

//==============================================================================
void SongView::pickClip (int clip)
{
    if (selSection < 0 || selLane < 0)
        return;
    if (clip == kSilent)
        rigsong::clearBox (timeline(), selSection, selLane);
    else
        rigsong::setBox (timeline(), selSection, selLane, clip);
    edited();
}

namespace
{
RigTimeline::Block* pickedBlock (RigTimeline& tl, int section, int lane)
{
    const int i = rigsong::boxBlock (tl, section, lane);
    return i >= 0 ? &tl.getBlocks()[(size_t) i] : nullptr;
}
}

void SongView::setFadeIn (double barsIn)
{
    if (auto* b = pickedBlock (timeline(), selSection, selLane))
    {
        b->fadeIn = barsIn;
        timeline().changed();
        edited();
    }
}

void SongView::setFadeOut (double barsOut)
{
    if (auto* b = pickedBlock (timeline(), selSection, selLane))
    {
        b->fadeOut = barsOut;
        timeline().changed();
        edited();
    }
}

void SongView::setStartBar (int bar)
{
    const auto list = rigsong::sections (timeline());
    auto* b = pickedBlock (timeline(), selSection, selLane);
    if (b == nullptr || ! juce::isPositiveAndBelow (selSection, (int) list.size()))
        return;
    const auto& s = list[(size_t) selSection];
    const double end = b->end();
    b->start = juce::jlimit (s.start, s.end() - 0.25, s.start + bar - 1);
    b->length = juce::jmax (0.25, end - b->start);
    timeline().changed();
    edited();
}

void SongView::setLevel (float level)
{
    if (auto* b = pickedBlock (timeline(), selSection, selLane))
    {
        b->level = level;
        timeline().changed();
        board->repaint();
    }
}

void SongView::setSectionLength (double barsLong)
{
    rigsong::setSectionBars (timeline(), selSection, barsLong);
    edited();
}

void SongView::setSectionBank (int bank)
{
    auto& markers = timeline().getMarkers();
    if (! juce::isPositiveAndBelow (selSection, (int) markers.size()))
        return;
    markers[(size_t) selSection].loopBank = bank;
    timeline().changed();
    edited();
}

//==============================================================================
void SongView::showAddMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Empty section (" + rigsong::suggestName (timeline()) + ")", [this] { addEmptySection(); });
    menu.addItem ("Section from what's playing now", [this] { addSectionFromPlaying(); });
    if (juce::isPositiveAndBelow (selSection, (int) rigsong::sections (timeline()).size()))
        menu.addItem ("Copy " + rigsong::sections (timeline())[(size_t) selSection].name + " to the end", [this]
                      {
                          const int n = (int) rigsong::sections (timeline()).size();
                          const int i = rigsong::duplicateSection (timeline(), selSection);
                          if (i >= 0)
                              rigsong::moveSection (timeline(), i, n);
                          edited();
                      });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton));
}

void SongView::showMoreMenu()
{
    juce::PopupMenu menu;
    menu.addItem (showingAllLanes ? "Show only the tracks in use" : "Show every track", [this]
                  {
                      showingAllLanes = ! showingAllLanes;
                      edited();
                  });
    for (auto [control, what] : { std::pair<int, const char*> { RigRouting::nextScene, "Next section" }, { RigRouting::previousScene, "Back a section" } })
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
    menu.addSeparator();
    menu.addItem ("Clear the whole song", ! timeline().isEmpty() || ! timeline().getMarkers().empty(), false, [this]
                  {
                      timeline().clear();
                      select (-1, -1);
                      edited();
                      say ("The song is empty. Your loops and patterns are all still there.");
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton));
}

void SongView::showSectionMenu (int section)
{
    const auto list = rigsong::sections (timeline());
    if (! juce::isPositiveAndBelow (section, (int) list.size()))
        return;
    juce::PopupMenu menu, length;
    for (int b : { 1, 2, 4, 8, 12, 16, 24, 32 })
        length.addItem (rigsong::barsText (b), true, std::abs (list[(size_t) section].bars - b) < kEps, [this, section, b]
                        {
                            rigsong::setSectionBars (timeline(), section, b);
                            edited();
                        });
    menu.addItem (juce::String::fromUTF8 ("\xe2\x96\xb6 Play from here"), [this, section] { engine.launchSection (section); });
    menu.addItem ("Loop it", [this, section] { loopSection (section); });
    menu.addSeparator();
    menu.addItem ("Rename...", [this, section] { renameSection (section); });
    menu.addSubMenu ("Length", length);
    menu.addItem ("Copy (right after it)", [this, section] { rigsong::duplicateSection (timeline(), section); edited(); });
    menu.addItem ("Move left", section > 0, false, [this, section] { rigsong::moveSection (timeline(), section, section - 1); select (section - 1, -1); edited(); });
    menu.addItem ("Move right", section < (int) list.size() - 1, false, [this, section]
                  { rigsong::moveSection (timeline(), section, section + 1); select (section + 1, -1); edited(); });
    menu.addSeparator();
    menu.addItem ("Delete", [this, section] { rigsong::removeSection (timeline(), section); select (-1, -1); edited(); });
    menu.showMenuAsync (juce::PopupMenu::Options());
}

void SongView::showBoxMenu (int section, int lane)
{
    const auto list = rigsong::sections (timeline());
    if (! juce::isPositiveAndBelow (section, (int) list.size()) || lane < 0)
        return;
    juce::PopupMenu menu, same, plays;
    for (const auto& [clip, name] : rigsong::clipChoices (engine, lane))
        plays.addItem (name, [this, section, lane, c = clip] { select (section, lane); pickClip (c); });
    plays.addItem ("Silent", [this, section, lane] { select (section, lane); pickClip (kSilent); });
    for (int i = 0; i < (int) list.size(); ++i)
        if (i != section)
            same.addItem (rigsong::letterFor (i) + dot() + list[(size_t) i].name, [this, i, section, lane]
                          {
                              rigsong::copyBox (timeline(), i, section, lane);
                              edited();
                          });
    menu.addSubMenu ("Plays", plays);
    menu.addSubMenu ("Same as", same, list.size() > 1);
    menu.addItem ("Copy to every section", rigsong::boxBlock (timeline(), section, lane) >= 0, false, [this, section, lane, n = (int) list.size()]
                  {
                      for (int i = 0; i < n; ++i)
                          if (i != section)
                              rigsong::copyBox (timeline(), section, i, lane);
                      edited();
                  });
    menu.addItem ("Copy to the next section", section + 1 < (int) list.size() && rigsong::boxBlock (timeline(), section, lane) >= 0, false,
                  [this, section, lane]
                  {
                      rigsong::copyBox (timeline(), section, section + 1, lane);
                      edited();
                  });
    menu.addItem ("Edit " + rigsong::laneName (lane) + juce::String::fromUTF8 (" \xe2\x86\x97"), [this, lane] { if (onGoTo) onGoTo (pageForLane (lane)); });
    menu.showMenuAsync (juce::PopupMenu::Options());
}

void SongView::renameSection (int section)
{
    const auto list = rigsong::sections (timeline());
    if (! juce::isPositiveAndBelow (section, (int) list.size()))
        return;
    auto* w = new juce::AlertWindow ("Rename section " + rigsong::letterFor (section), "", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", list[(size_t) section].name);
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SongView> (this), w, section] (int result)
                                                                   {
                                                                       if (result == 1 && safe != nullptr)
                                                                       {
                                                                           rigsong::renameSection (safe->timeline(), section, w->getTextEditorContents ("name"));
                                                                           safe->edited();
                                                                       }
                                                                   }),
                        true);
}

//==============================================================================
void SongView::timerCallback()
{
    if (bars != nullptr)
        bars->tick();
    const bool song = timeline().on.load();
    const auto setLit = [] (rigpanels::PanelButton& b, Lit lit)
    {
        if (b.lit != lit)
        {
            b.lit = lit;
            b.repaint();
        }
    };
    setLit (jamButton, song ? Lit::none : Lit::current);
    setLit (songButton, song ? Lit::play : Lit::none);
    setLit (loopButton, timeline().cycle.load() ? Lit::current : Lit::none);
    const bool recording = engine.getRecorder().getState() != RigRecorder::State::idle;
    setLit (recButton, recording ? Lit::arm : Lit::none);

    // Redraw when the song changes (here, in Bars view, or by a section key) or while it plays.
    int hash = (int) timeline().getBlocks().size() * 7919 + (int) timeline().getMarkers().size() * 31;
    for (const auto& b : timeline().getBlocks())
        hash = hash * 31 + juce::roundToInt (b.start * 64 + b.length * 8 + b.clip + b.fadeIn * 5 + b.fadeOut * 3 + b.level * 100);
    for (const auto& m : timeline().getMarkers())
        hash = hash * 31 + juce::roundToInt (m.bar * 16 + m.bars) + m.name.hashCode();
    hash = hash * 31 + (int) shownLanes().size();
    if (hash != lastHash)
    {
        lastHash = hash;
        edited();
    }
    else if (engine.getTransport().isPlaying() || isMouseOverOrDragging (true))
        board->repaint();
}

void SongView::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.setColour (p.ink);
    g.setFont (cond (30.0f, Weight::extra, 0.12f));
    auto t = titleArea;
    g.drawText ("SONG", t.removeFromLeft (100), juce::Justification::centredLeft);
    g.setColour (p.ink2);
    g.setFont (barlow (13.0f));
    g.drawFittedText ("Your song is a row of sections. Each box is what one track plays there.", t, juce::Justification::centredLeft, 2, 0.9f);

    if (! stepsArea.isEmpty())
    {
        const char* steps[] = { "Make sounds on Play, Loops, Sampler", "+ Section for each part", "Click a box: pick what plays",
                                "Song mode: Play runs it over the click", "Rec song: stems for Logic" };
        auto r = stepsArea;
        const int w = r.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
        {
            auto cell = r.removeFromLeft (w).reduced (3, 0);
            g.setColour (p.btn1.withAlpha (0.6f));
            g.fillRoundedRectangle (cell.toFloat(), 5.0f);
            auto num = cell.removeFromLeft (26).toFloat().withSizeKeepingCentre (18.0f, 18.0f);
            g.setColour (p.ink);
            g.fillEllipse (num);
            g.setColour (p.face);
            g.setFont (cond (13.0f, Weight::extra));
            g.drawText (juce::String (i + 1), num, juce::Justification::centred);
            g.setColour (p.ink);
            g.setFont (barlow (12.5f));
            g.drawFittedText (steps[i], cell.reduced (2, 0), juce::Justification::centredLeft, 1, 0.75f);
        }
    }

    if (! emptyArea.isEmpty())
    {
        drawWell (g, emptyArea.toFloat(), 10.0f);
        auto r = emptyArea.reduced (30, 24);
        g.setColour (p.wellInk);
        g.setFont (cond (30.0f, Weight::extra, 0.06f));
        g.drawText ("NO SONG YET", r.removeFromTop (40), juce::Justification::centred);
        g.setColour (p.wellInk2);
        g.setFont (barlow (16.0f));
        g.drawFittedText ("Get a beat, a bass line or some loops going first (Play, Loops, Sampler).\n"
                          "Then make the song's first section from what's playing, or an empty one to fill in box by box.",
                          r.removeFromTop (60), juce::Justification::centred, 3, 1.0f);
    }

    if (! moreLanesArea.isEmpty())
    {
        g.setColour (p.ink2);
        g.setFont (barlow (12.5f));
        int hidden = RigTimeline::kLanes - (int) shownLanes().size();
        g.drawText (showingAllLanes ? juce::String ("Showing every track. Click to show only the ones in use.")
                                    : hidden > 0 ? "+ " + juce::String (hidden) + " more tracks with nothing in them yet (empty loops, Sampler, Line in). Click to show."
                                                 : juce::String(),
                    moreLanesArea, juce::Justification::centredLeft);
    }
}

void SongView::mouseUp (const juce::MouseEvent& e)
{
    if (moreLanesArea.contains (e.getPosition()))
    {
        showingAllLanes = ! showingAllLanes;
        edited();
    }
}

void SongView::resized()
{
    auto area = getLocalBounds().reduced (18, 10);

    auto top = area.removeFromTop (52);
    const int gap = 7;
    moreButton.setBounds (top.removeFromRight (44));
    top.removeFromRight (gap);
    const int w = juce::jlimit (96, 132, (top.getWidth() - 330) / 8 - gap);
    for (auto* b : { &exportButton, &recButton, &barsButton, &loopButton, &playButton })
    {
        b->setBounds (top.removeFromRight (w));
        top.removeFromRight (gap);
    }
    top.removeFromRight (6);
    songButton.setBounds (top.removeFromRight (w - 10));
    jamButton.setBounds (top.removeFromRight (w - 10));
    top.removeFromRight (gap + 6);
    addButton.setBounds (top.removeFromRight (w + 30));
    top.removeFromRight (10);
    titleArea = top;

    area.removeFromTop (8);
    const bool empty = timeline().isEmpty() && timeline().getMarkers().empty();
    stepsArea = area.removeFromTop (28);
    area.removeFromTop (8);

    if (bars != nullptr)
        bars->setBounds (area);
    const bool board_ = ! empty && ! barsShown;
    viewport.setVisible (board_);
    names->setVisible (board_);
    inspector->setVisible (board_);
    bigAdd.setVisible (empty && ! barsShown);
    bigEmpty.setVisible (empty && ! barsShown);
    emptyArea = {};
    moreLanesArea = {};
    if (barsShown)
        return;
    if (empty)
    {
        emptyArea = area.reduced (juce::jmax (0, (area.getWidth() - 900) / 2), juce::jmax (0, (area.getHeight() - 300) / 2));
        auto r = emptyArea.reduced (30, 24);
        r.removeFromTop (110);
        auto buttons = r.removeFromTop (78);
        const int bw = (buttons.getWidth() - 16) / 2;
        bigAdd.setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (16);
        bigEmpty.setBounds (buttons);
        return;
    }

    inspector->setBounds (area.removeFromRight (kInspectorW));
    area.removeFromRight (10);
    moreLanesArea = area.removeFromBottom (22);
    area.removeFromBottom (4);
    boardArea = area;
    names->setBounds (area.removeFromLeft (kNamesW));
    viewport.setBounds (area);
    const int lanes = juce::jmax (1, (int) shownLanes().size());
    const int laneH = juce::jlimit (24, 46, (area.getHeight() - kHead - kRuler - 12) / lanes);
    board->laneH = laneH;
    names->laneH = laneH;
    board->updateSize (area.getWidth(), area.getHeight() - (viewport.getHorizontalScrollBar().isVisible() ? 10 : 0));
    inspector->resized();
}
