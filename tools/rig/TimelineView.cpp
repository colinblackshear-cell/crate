#include "TimelineView.h"

#include "RigColours.h"
#include "RigExport.h"
#include "SongGrid.h"
#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

namespace
{
constexpr int kNamesWidth = 150, kRuler = 46, kPad = 6;
constexpr double kMinBars = 64;

juce::Colour loopColour (int i)
{
    static const juce::Colour colours[] = { juce::Colour (0xffff8a3d), juce::Colour (0xffb48cff), juce::Colour (0xff4fc3f7),
                                            juce::Colour (0xfff06292), juce::Colour (0xffffd54f), juce::Colour (0xff8fd3c8),
                                            juce::Colour (0xff81c784), juce::Colour (0xffe0e0e0) };
    return colours[i % 8];
}

juce::Colour laneColour (int lane)
{
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    return info.isLoop ? loopColour (info.index) : RigColours::forStrip (info.index);
}

juce::String laneName (int lane)
{
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    return info.isLoop ? "Loop " + juce::String (info.index + 1) : RigEngine::stripName (info.index);
}

juce::String barsText (double bars)
{
    const bool whole = std::abs (bars - std::round (bars)) < 0.01;
    return juce::String (bars, whole ? 0 : 2) + (whole && (int) std::round (bars) == 1 ? " bar" : " bars");
}

const juce::StringArray& sectionNames()
{
    static const juce::StringArray list { "Intro", "Verse", "Pre-chorus", "Chorus", "Bridge", "Drop", "Break", "Solo", "Outro" };
    return list;
}
}

//==============================================================================
// The lane names, fixed on the left while the lanes scroll.
class TimelineView::Names : public juce::Component
{
public:
    explicit Names (TimelineView& o) : owner (o) {}

    void paint (juce::Graphics& g) override;

private:
    TimelineView& owner;
};
//==============================================================================
class TimelineView::Lanes : public juce::Component
{
public:
    explicit Lanes (TimelineView& o) : owner (o) {}

    int laneHeight() const { return juce::jlimit (20, 44, (getHeight() - kRuler) / RigTimeline::kLanes); }
    float xFor (double bar) const { return kPad + (float) bar * owner.pxPerBar; }
    double barFor (float x) const { return juce::jmax (0.0, (double) ((x - kPad) / owner.pxPerBar)); }
    int bpb() const { return juce::jmax (1, owner.engine.getTransport().getBeatsPerBar()); }
    double snap() const { return owner.snapBeats ? 1.0 / bpb() : 1.0; }
    double snapped (double bar) const { return std::round (bar / snap()) * snap(); }
    int laneAt (int y) const { return y < kRuler ? -1 : juce::jlimit (0, RigTimeline::kLanes - 1, (y - kRuler) / laneHeight()); }

    juce::Rectangle<float> blockBounds (const RigTimeline::Block& b) const
    {
        const int h = laneHeight();
        return juce::Rectangle<float> (xFor (b.start), (float) (kRuler + b.lane * h + 2), (float) b.length * owner.pxPerBar, (float) (h - 4));
    }

    void updateSize (int height)
    {
        const double bars = juce::jmax (kMinBars, std::ceil (owner.timeline.lengthBars()) + 16.0,
                                        owner.timeline.cycle.load() ? owner.timeline.cycleEnd.load() + 8.0 : 0.0);
        setSize ((int) std::ceil (xFor (bars)) + kPad, height);
    }

    void paint (juce::Graphics& g) override
    {
        auto& engine = owner.engine;
        auto& timeline = owner.timeline;
        const int h = laneHeight();
        const int lanesBottom = kRuler + h * RigTimeline::kLanes;
        const double visibleBars = barFor ((float) getWidth());

        g.fillAll (Theme::background);
        for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
        {
            g.setColour (lane % 2 == 0 ? Theme::panel.brighter (0.03f) : Theme::panel);
            g.fillRect (0, kRuler + lane * h, getWidth(), h);
            if (lane == RigLooper::kLayers)
            {
                g.setColour (Theme::outline);
                g.drawHorizontalLine (kRuler + lane * h, 0.0f, (float) getWidth());
            }
        }

        // The cycle.
        if (timeline.cycle.load())
        {
            const float x0 = xFor (timeline.cycleStart.load()), x1 = xFor (timeline.cycleEnd.load());
            g.setColour (RigColours::song.withAlpha (0.08f));
            g.fillRect (x0, (float) kRuler, x1 - x0, (float) (lanesBottom - kRuler));
            g.setColour (RigColours::song.withAlpha (0.8f));
            g.fillRoundedRectangle (x0, (float) kRuler - 5, x1 - x0, 5.0f, 2.0f);
        }

        // Bars and beats.
        const int beats = bpb();
        for (int bar = 0; bar <= (int) visibleBars; ++bar)
        {
            const float x = xFor (bar);
            const bool four = bar % 4 == 0;
            g.setColour (Theme::outline.withAlpha (four ? 0.9f : 0.45f));
            g.drawVerticalLine ((int) x, (float) kRuler - (four ? 18 : 8), (float) lanesBottom);
            if (owner.pxPerBar >= 40.0f)
                for (int b = 1; b < beats; ++b)
                {
                    g.setColour (Theme::outline.withAlpha (0.18f));
                    g.drawVerticalLine ((int) (x + b * owner.pxPerBar / beats), (float) kRuler, (float) lanesBottom);
                }
            if (four || owner.pxPerBar >= 24.0f)
            {
                g.setColour (four ? Theme::text : Theme::textDim);
                g.setFont (juce::FontOptions (11.0f, four ? juce::Font::bold : juce::Font::plain));
                g.drawText (juce::String (bar + 1), (int) x + 3, kRuler - 21, 40, 14, juce::Justification::centredLeft);
            }
        }

        // The sections.
        for (const auto& m : timeline.getMarkers())
        {
            const float x = xFor (m.bar);
            g.setColour (RigColours::song);
            g.fillRect (x, 2.0f, 2.0f, (float) kRuler - 22);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (m.name, (int) x + 5, 2, 110, 16, juce::Justification::centredLeft);
        }

        // The blocks.
        const auto& blocks = timeline.getBlocks();
        for (int i = 0; i < (int) blocks.size(); ++i)
            drawBlock (g, blocks[(size_t) i], i == owner.selected);

        // Where Play starts, the jump that's coming, and where it's playing.
        {
            const float x = xFor (owner.startBar);
            juce::Path p;
            p.addTriangle (x - 6, (float) kRuler - 8, x + 6, (float) kRuler - 8, x, (float) kRuler);
            g.setColour (Theme::text);
            g.fillPath (p);
        }
        if (const double jump = timeline.getPendingJump(); jump >= 0 && (juce::Time::getMillisecondCounter() / 250) % 2 == 0)
        {
            g.setColour (RigColours::song);
            g.drawVerticalLine ((int) xFor (jump), (float) kRuler, (float) lanesBottom);
        }
        auto& transport = engine.getTransport();
        if (transport.isPlaying())
        {
            const float x = xFor (transport.getPositionInBeats() / beats);
            g.setColour (timeline.on.load() ? juce::Colours::white : Theme::textDim);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) lanesBottom);
        }

        if (blocks.empty())
        {
            g.setColour (Theme::textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText ("Click a lane to put a block on it: that loop (or tool) plays there. "
                              "Drag its right edge to make it longer, its top corners to fade it.",
                              juce::Rectangle<int> ((int) xFor (1), kRuler + h * 2, 640, h * 2), juce::Justification::centredLeft, 2);
        }
    }

    void drawBlock (juce::Graphics& g, const RigTimeline::Block& b, bool picked)
    {
        auto& engine = owner.engine;
        const auto r = blockBounds (b);
        const auto colour = laneColour (b.lane);
        const auto& info = RigTimeline::lanes()[(size_t) b.lane];
        const bool emptyLoop = info.isLoop && engine.getLooper().getState (info.index) == RigLooper::State::empty;
        g.setColour (colour.withAlpha (emptyLoop ? 0.22f : 0.55f));
        g.fillRoundedRectangle (r, 4.0f);

        // A loop's sound, repeated for as long as the block is.
        if (info.isLoop && ! emptyLoop)
        {
            const double loopBars = engine.getLooper().getLayer (info.index).lengthBeats.load() / bpb();
            if (loopBars > 0)
            {
                const auto& peaks = peaksFor (info.index, juce::jmax (8, (int) (loopBars * owner.pxPerBar)));
                g.setColour (Theme::background.withAlpha (0.55f));
                const float mid = r.getCentreY(), half = r.getHeight() * 0.42f;
                for (int px = 0; px < (int) r.getWidth(); ++px)
                {
                    const double inLoop = std::fmod (px / owner.pxPerBar, loopBars) / loopBars;
                    const float v = peaks.empty() ? 0.0f : peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (inLoop * (double) peaks.size()))];
                    g.drawVerticalLine ((int) r.getX() + px, mid - v * half, mid + v * half);
                }
                // Where it repeats.
                g.setColour (Theme::background.withAlpha (0.5f));
                for (double at = loopBars; at < b.length - 1.0e-6; at += loopBars)
                    g.drawVerticalLine ((int) xFor (b.start + at), r.getY() + 2, r.getBottom() - 2);
            }
        }

        // The fades: the dark corner is quiet, the line is the level.
        const auto fade = [&] (float x0, float x1, bool in)
        {
            juce::Path quiet;
            quiet.startNewSubPath (x0, r.getY());
            quiet.lineTo (x1, r.getY());
            quiet.lineTo (in ? x0 : x1, r.getBottom());
            quiet.closeSubPath();
            g.setColour (Theme::background.withAlpha (0.6f));
            g.fillPath (quiet);
            g.setColour (Theme::text.withAlpha (0.9f));
            g.drawLine (in ? x0 : x1, r.getBottom(), in ? x1 : x0, r.getY(), 1.5f);
        };
        if (b.fadeIn > 0)
            fade (r.getX(), xFor (b.start + b.fadeIn), true);
        if (b.fadeOut > 0)
            fade (xFor (b.end() - b.fadeOut), r.getRight(), false);

        g.setColour (picked ? juce::Colours::white : colour.brighter (0.3f));
        g.drawRoundedRectangle (r, 4.0f, picked ? 2.0f : 1.0f);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        if (r.getWidth() > 50)
            g.drawText (laneName (b.lane), r.reduced (6, 2).withTrimmedTop (r.getHeight() * 0.45f), juce::Justification::bottomLeft, true);
        if (picked)
        {
            g.setColour (juce::Colours::white);
            g.fillRect (fadeHandle (b, true));
            g.fillRect (fadeHandle (b, false));
        }
    }

    juce::Rectangle<float> fadeHandle (const RigTimeline::Block& b, bool in) const
    {
        const auto r = blockBounds (b);
        const float x = in ? xFor (b.start + b.fadeIn) : xFor (b.end() - b.fadeOut);
        return { juce::jlimit (r.getX(), r.getRight() - 7.0f, x - 3.5f), r.getY() + 1, 7.0f, 7.0f };
    }

    //==========================================================================
    enum class Drag { none, move, left, right, fadeIn, fadeOut, draw, ruler, cycle };

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus();
        drag = Drag::none;
        downBar = barFor ((float) e.x);
        auto& timeline = owner.timeline;
        if (e.y < kRuler)
        {
            if (e.y < kRuler - 22)
            {
                markerClick (e);
                return;
            }
            if (e.mods.isPopupMenu())
                return;
            drag = Drag::ruler;
            return;
        }

        const int lane = laneAt (e.y);
        auto& blocks = timeline.getBlocks();
        int hit = -1;
        for (int i = (int) blocks.size(); --i >= 0;)
            if (blocks[(size_t) i].lane == lane && blockBounds (blocks[(size_t) i]).expanded (2, 0).contains (e.position))
            {
                hit = i;
                break;
            }
        if (hit < 0)
        {
            if (e.mods.isPopupMenu())
            {
                laneMenu (lane, downBar);
                return;
            }
            owner.pushUndo();
            const double bar = std::floor (downBar / snap() + 1.0e-9) * snap();
            owner.select (owner.addBlockNoUndo (lane, bar));
            drag = Drag::draw;
            original = blocks[(size_t) owner.selected];
            return;
        }

        owner.select (hit);
        const auto b = blocks[(size_t) hit];
        if (e.mods.isPopupMenu())
        {
            blockMenu (hit);
            return;
        }
        original = b;
        const auto r = blockBounds (b);
        if (fadeHandle (b, true).expanded (4).contains (e.position))
            drag = Drag::fadeIn;
        else if (fadeHandle (b, false).expanded (4).contains (e.position))
            drag = Drag::fadeOut;
        else if (e.position.x > r.getRight() - 7)
            drag = Drag::right;
        else if (e.position.x < r.getX() + 7)
            drag = Drag::left;
        else
            drag = Drag::move;
        owner.pushUndo();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& timeline = owner.timeline;
        const double bar = barFor ((float) e.x);
        if (drag == Drag::ruler || drag == Drag::cycle)
        {
            if (std::abs (e.getDistanceFromDragStartX()) > 6)
            {
                drag = Drag::cycle;
                const double a = std::floor (juce::jmin (downBar, bar) + 1.0e-9), z = std::ceil (juce::jmax (downBar, bar) - 1.0e-9);
                owner.setCycle (a, juce::jmax (a + 1, z));
            }
            return;
        }
        auto& blocks = timeline.getBlocks();
        if (drag == Drag::none || ! juce::isPositiveAndBelow (owner.selected, (int) blocks.size()))
            return;
        auto& b = blocks[(size_t) owner.selected];
        const double s = snap();
        switch (drag)
        {
            case Drag::move:
                b.start = juce::jmax (0.0, snapped (original.start + bar - downBar));
                break;
            case Drag::right:
            case Drag::draw:
                if (drag == Drag::draw && std::abs (e.getDistanceFromDragStartX()) < 4)
                    break;
                b.length = juce::jmax (s, snapped (bar) - b.start);
                break;
            case Drag::left:
            {
                const double start = juce::jlimit (0.0, original.end() - s, snapped (bar));
                b.length = original.end() - start;
                b.start = start;
                break;
            }
            case Drag::fadeIn:
                b.fadeIn = juce::jlimit (0.0, b.length - b.fadeOut, std::round ((bar - b.start) * bpb()) / bpb());
                break;
            case Drag::fadeOut:
                b.fadeOut = juce::jlimit (0.0, b.length - b.fadeIn, std::round ((b.end() - bar) * bpb()) / bpb());
                break;
            case Drag::none:
            case Drag::ruler:
            case Drag::cycle:
                break;
        }
        timeline.changed();   // (heard while it plays)
        reselect (b);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag == Drag::ruler)
        {
            const double bar = std::floor (downBar + 1.0e-9);
            if (owner.engine.getTransport().isPlaying() && owner.timeline.on.load())
            {
                owner.timeline.jumpTo (bar);
                owner.say ("Jumping to bar " + juce::String ((int) bar + 1) + " at the next bar.");
            }
            owner.setStartBar (bar);
        }
        else if (drag != Drag::none && drag != Drag::cycle)
        {
            owner.edited();
        }
        drag = Drag::none;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto pointer = juce::MouseCursor::NormalCursor;
        const auto& blocks = owner.timeline.getBlocks();
        for (const auto& b : blocks)
        {
            const auto r = blockBounds (b);
            if (! r.expanded (2, 0).contains (e.position))
                continue;
            if (e.position.x > r.getRight() - 7 || e.position.x < r.getX() + 7)
                pointer = juce::MouseCursor::LeftRightResizeCursor;
            else
                pointer = juce::MouseCursor::DraggingHandCursor;
        }
        setMouseCursor (pointer);
    }

    // After sorting, the dragged block may have moved in the list.
    void reselect (const RigTimeline::Block& b)
    {
        const auto copy = b;
        const auto& blocks = owner.timeline.getBlocks();
        for (int i = 0; i < (int) blocks.size(); ++i)
        {
            const auto& c = blocks[(size_t) i];
            if (c.lane == copy.lane && std::abs (c.start - copy.start) < 1.0e-9 && std::abs (c.length - copy.length) < 1.0e-9)
            {
                owner.selected = i;
                return;
            }
        }
    }

    void markerClick (const juce::MouseEvent& e)
    {
        auto& markers = owner.timeline.getMarkers();
        int hit = -1;
        for (int i = 0; i < (int) markers.size(); ++i)
            if (e.x >= xFor (markers[(size_t) i].bar) - 3 && e.x < xFor (markers[(size_t) i].bar) + 90)
                hit = i;
        if (hit >= 0 && ! e.mods.isPopupMenu())
        {
            const double bar = markers[(size_t) hit].bar;
            if (owner.engine.getTransport().isPlaying() && owner.timeline.on.load())
            {
                owner.timeline.jumpTo (bar);
                owner.say (markers[(size_t) hit].name + " at the next bar.");
            }
            owner.setStartBar (bar);
            return;
        }
        juce::PopupMenu menu, add;
        const double bar = std::floor (barFor ((float) e.x) + 1.0e-9);
        for (const auto& name : sectionNames())
            add.addItem (name, [this, bar, name] { owner.addMarker (bar, name); });
        menu.addSubMenu ("Name bar " + juce::String ((int) bar + 1) + " as", add);
        if (hit >= 0)
        {
            juce::PopupMenu rename;
            for (const auto& name : sectionNames())
                rename.addItem (name, [this, hit, name]
                                {
                                    auto& m = owner.timeline.getMarkers();
                                    if (hit < (int) m.size())
                                    {
                                        owner.pushUndo();
                                        m[(size_t) hit].name = name;
                                        owner.edited();
                                    }
                                });
            menu.addSubMenu ("Rename " + markers[(size_t) hit].name, rename);
            menu.addItem ("Remove " + markers[(size_t) hit].name, [this, hit]
                          {
                              auto& m = owner.timeline.getMarkers();
                              if (hit < (int) m.size())
                              {
                                  owner.pushUndo();
                                  m.erase (m.begin() + hit);
                                  owner.edited();
                              }
                          });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void laneMenu (int lane, double bar)
    {
        juce::PopupMenu menu;
        const double at = std::floor (bar + 1.0e-9);
        menu.addItem ("Put " + laneName (lane) + " here (bar " + juce::String ((int) at + 1) + ")", [this, lane, at] { owner.select (owner.addBlock (lane, at)); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void blockMenu (int index)
    {
        const auto& b = owner.timeline.getBlocks()[(size_t) index];
        const int beats = bpb();
        juce::PopupMenu menu, fadeIn, fadeOut;
        const std::pair<double, juce::String> lengths[] = { { 0.0, "None" }, { 1.0 / beats, "1 beat" }, { 1.0, "1 bar" }, { 2.0, "2 bars" }, { 4.0, "4 bars" }, { 8.0, "8 bars" } };
        for (const auto& [bars, name] : lengths)
        {
            fadeIn.addItem (name, bars <= b.length - b.fadeOut + 1.0e-9, std::abs (b.fadeIn - bars) < 1.0e-6, [this, index, bars] { owner.setFade (index, true, bars); });
            fadeOut.addItem (name, bars <= b.length - b.fadeIn + 1.0e-9, std::abs (b.fadeOut - bars) < 1.0e-6, [this, index, bars] { owner.setFade (index, false, bars); });
        }
        menu.addSectionHeader (laneName (b.lane) + ", bar " + juce::String (b.start + 1.0, std::abs (b.start - std::round (b.start)) < 0.01 ? 0 : 2)
                               + ", " + barsText (b.length));
        menu.addSubMenu ("Fade in", fadeIn);
        menu.addSubMenu ("Fade out", fadeOut);
        menu.addItem ("Copy after it (Cmd+D)", [this] { owner.duplicateSelected(); });
        menu.addItem ("Delete (Delete key)", [this] { owner.deleteSelected(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    const std::vector<float>& peaksFor (int layer, int count)
    {
        auto& cache = peakCache[(size_t) layer];
        const int version = owner.engine.getLooper().getLayer (layer).version.load();
        if (cache.version != version || cache.count != count)
        {
            cache.peaks = owner.engine.getLooper().readPeaks (layer, count);
            float top = 1.0e-4f;
            for (auto v : cache.peaks)
                top = juce::jmax (top, v);
            for (auto& v : cache.peaks)
                v /= top;
            cache.version = version;
            cache.count = count;
        }
        return cache.peaks;
    }

private:
    TimelineView& owner;
    Drag drag = Drag::none;
    double downBar = 0;
    RigTimeline::Block original;
    struct Peaks
    {
        int version = -1, count = 0;
        std::vector<float> peaks;
    };
    std::array<Peaks, RigLooper::kLayers> peakCache;
};

void TimelineView::Names::paint (juce::Graphics& g)
{
    auto& engine = owner.engine;
    const int h = owner.lanes->laneHeight(), bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        auto area = juce::Rectangle<int> (0, kRuler + lane * h, getWidth() - 6, h).reduced (0, 2);
        g.setColour (laneColour (lane));
        g.fillRoundedRectangle (area.removeFromLeft (5).toFloat(), 2.0f);
        area.removeFromLeft (8);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (laneName (lane), area, juce::Justification::centredLeft);
        juce::String what;
        const auto& info = RigTimeline::lanes()[(size_t) lane];
        if (info.isLoop)
        {
            const double beats = engine.getLooper().getLayer (info.index).lengthBeats.load();
            const auto state = engine.getLooper().getState (info.index);
            what = state == RigLooper::State::empty ? "empty" : beats > 0 ? barsText (beats / bpb) : "recording";
        }
        if (! owner.timeline.laneUsed (lane))
            what = what.isEmpty() ? "live" : what + ", live";
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (what, area, juce::Justification::centredRight);
    }
}

//==============================================================================
TimelineView::TimelineView (RigEngine& e) : engine (e), timeline (e.getTimeline())
{
    setWantsKeyboardFocus (true);
    lanes = std::make_unique<Lanes> (*this);
    names = std::make_unique<Names> (*this);
    viewport.setViewedComponent (lanes.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (*names);
    lastState.fill (-1);
    recordStart.fill (-1.0);

    title.setText ("TIMELINE", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, RigColours::song);
    steps.setText (juce::String::fromUTF8 ("1 Click a lane to add a block   2 Drag it to move, its right edge to make it longer, "
                                           "its top corners to fade   3 Click the ruler to pick where Play starts, drag it to cycle   "
                                           "4 Play, and sing or play over it   5 Record song or Export for Logic"),
                   juce::dontSendNotification);
    steps.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    steps.setColour (juce::Label::textColourId, Theme::text);

    playButton.setTooltip ("Plays the song from the white triangle on the ruler, and turns the timeline on.");
    playButton.onClick = [this] { play(); };
    stopButton.setTooltip ("Stops the clock.");
    stopButton.onClick = [this] { stop(); };
    cycleButton.setClickingTogglesState (true);
    cycleButton.setTooltip ("Plays the bars marked on the ruler over and over. Drag along the ruler to pick them.");
    cycleButton.onClick = [this]
    {
        timeline.cycle = cycleButton.getToggleState();
        if (timeline.cycle.load() && timeline.cycleEnd.load() <= timeline.cycleStart.load())
            setCycle (startBar, startBar + 4);
        lanes->repaint();
    };
    clickButton.setClickingTogglesState (true);
    clickButton.setTooltip ("The click track: a tick on every beat (only you hear it; it isn't recorded).");
    clickButton.onClick = [this] { engine.getTransport().metronome = clickButton.getToggleState(); };
    snapButton.setTooltip ("What blocks snap to as you drag them: whole bars, or beats.");
    snapButton.onClick = [this]
    {
        snapBeats = ! snapBeats;
        snapButton.setButtonText (snapBeats ? "Snap: beat" : "Snap: bar");
    };
    zoomOut.setTooltip ("Zoom out");
    zoomIn.setTooltip ("Zoom in");
    zoomOut.onClick = [this] { pxPerBar = juce::jmax (8.0f, pxPerBar / 1.4f); resized(); };
    zoomIn.onClick = [this] { pxPerBar = juce::jmin (160.0f, pxPerBar * 1.4f); resized(); };
    onButton.setClickingTogglesState (true);
    onButton.setTooltip ("On: while the clock plays, the timeline brings loops and tools in and out. Off: they play as you set them "
                         "(to jam, or to use the Song grid's parts).");
    onButton.onClick = [this] { timeline.on = onButton.getToggleState(); };
    recOnto.setClickingTogglesState (true);
    recOnto.setTooltip ("While the song plays, a loop you record (Looper > Rec next layer) goes on its lane, at the bar you started it.");
    recOnto.onClick = [this] { recordOntoSong = recOnto.getToggleState(); };
    partsButton.setTooltip ("Lays out the Song grid's parts (A, B, C...) on the timeline, with a marker for each, so you can shape them here.");
    partsButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem ("Replace the timeline with the Song grid's parts", ! engine.getScenes().getScenes().empty(), false, [this] { fromParts(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&partsButton));
    };
    recordButton.setTooltip ("Plays the song once from bar 1 and records it: every tool (MPC, Bass, Synth, vocals, Input, Loops) "
                             "as its own WAV, the mix, and what you played as MIDI, in Music > Crate > Rig > Recordings.");
    recordButton.onClick = [this] { recordSong(); };
    exportButton.setTooltip ("Saves the loops for Logic: each loop on its own, and each loop laid out over the timeline, "
                             "with a MIDI file of the tempo and the section markers.");
    exportButton.onClick = [this] { exportForLogic(); };
    close.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &cycleButton, &clickButton, &onButton, &recOnto, &recordButton })
        b->setColour (juce::TextButton::buttonOnColourId, RigColours::song);
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0483e));
    for (auto* c : std::initializer_list<juce::Component*> { &title, &steps, &playButton, &stopButton, &cycleButton, &clickButton, &snapButton, &zoomOut,
                                                             &zoomIn, &onButton, &recOnto, &partsButton, &recordButton, &exportButton, &close })
        addAndMakeVisible (c);
    refresh();
}

TimelineView::~TimelineView() = default;

void TimelineView::refresh()
{
    selected = juce::jmin (selected, (int) timeline.getBlocks().size() - 1);
    updateButtons();
    resized();
    repaint();
}

void TimelineView::updateButtons()
{
    cycleButton.setToggleState (timeline.cycle.load(), juce::dontSendNotification);
    clickButton.setToggleState (engine.getTransport().metronome.load(), juce::dontSendNotification);
    onButton.setToggleState (timeline.on.load(), juce::dontSendNotification);
    recOnto.setToggleState (recordOntoSong, juce::dontSendNotification);
    const bool recording = engine.getRecorder().getState() != RigRecorder::State::idle;
    recordButton.setToggleState (recordingSong && recording, juce::dontSendNotification);
    recordButton.setButtonText (recordingSong && recording ? "Recording..." : "Record song");
}

void TimelineView::tick()
{
    updateButtons();
    watchRecording();
    auto& transport = engine.getTransport();
    const bool playing = transport.isPlaying();
    // Keep where it plays in view.
    if (playing && timeline.on.load())
    {
        const int x = (int) lanes->xFor (transport.getPositionInBeats() / juce::jmax (1, transport.getBeatsPerBar()));
        const auto view = viewport.getViewArea();
        if (x > view.getRight() - 40 || x < view.getX())
            viewport.setViewPosition (juce::jmax (0, x - 60), 0);
    }
    if (playing)
        seenPlaying = true;
    if (recordingSong && seenPlaying && ! playing)
    {
        recordingSong = false;
        if (engine.getRecorder().getState() != RigRecorder::State::idle)
        {
            engine.stopRecording();
            say ("Recorded the song. Every tool is its own WAV in Music > Crate > Rig > Recordings.");
        }
    }
    wasPlaying = playing;
    lanes->repaint();
    names->repaint();
}

void TimelineView::watchRecording()
{
    auto& looper = engine.getLooper();
    auto& transport = engine.getTransport();
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    for (int l = 0; l < RigLooper::kLayers; ++l)
    {
        const int now = (int) looper.getState (l);
        const int before = std::exchange (lastState[(size_t) l], now);
        if (before < 0)
            continue;
        if (now == (int) RigLooper::State::recording && before != now)
            recordStart[(size_t) l] = (recordOntoSong && timeline.on.load() && transport.isPlaying())
                                          ? std::floor (transport.getPositionInBeats() / bpb + 1.0e-6) : -1.0;
        if (before == (int) RigLooper::State::recording && now == (int) RigLooper::State::playing && recordStart[(size_t) l] >= 0)
        {
            const double bars = juce::jmax (1.0, std::round (looper.getLayer (l).lengthBeats.load() / bpb));
            pushUndo();
            RigTimeline::Block b;
            b.lane = RigTimeline::laneForLoop (l);
            b.start = recordStart[(size_t) l];
            b.length = bars;
            timeline.getBlocks().push_back (b);
            edited();
            say ("Loop " + juce::String (l + 1) + " is on the song from bar " + juce::String ((int) b.start + 1) + ". Drag its edge to make it longer.");
            recordStart[(size_t) l] = -1;
        }
    }
}

//==============================================================================
double TimelineView::defaultLength (int lane) const
{
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    if (info.isLoop)
    {
        const double beats = engine.getLooper().getLayer (info.index).lengthBeats.load();
        if (beats > 0)
            return juce::jmax (1.0, std::ceil (beats / juce::jmax (1, engine.getTransport().getBeatsPerBar()) - 1.0e-6));
    }
    return 4.0;
}

int TimelineView::addBlockNoUndo (int lane, double bar)
{
    auto& blocks = timeline.getBlocks();
    if ((int) blocks.size() >= RigTimeline::kMaxBlocks || ! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return -1;
    RigTimeline::Block b;
    b.lane = lane;
    b.start = juce::jmax (0.0, bar);
    b.length = defaultLength (lane);
    blocks.push_back (b);
    timeline.changed();
    for (int i = (int) blocks.size(); --i >= 0;)
        if (blocks[(size_t) i].lane == lane && std::abs (blocks[(size_t) i].start - b.start) < 1.0e-9)
            return i;
    return -1;
}

int TimelineView::addBlock (int lane, double bar)
{
    pushUndo();
    const int index = addBlockNoUndo (lane, bar);
    edited();
    return index;
}

void TimelineView::select (int block)
{
    selected = block;
    lanes->repaint();
}

void TimelineView::pushUndo()
{
    undoStack.emplace_back (timeline.getBlocks(), timeline.getMarkers());
    if (undoStack.size() > 100)
        undoStack.erase (undoStack.begin());
}

void TimelineView::undo()
{
    if (undoStack.empty())
        return;
    timeline.getBlocks() = undoStack.back().first;
    timeline.getMarkers() = undoStack.back().second;
    undoStack.pop_back();
    selected = -1;
    edited();
}

void TimelineView::edited()
{
    timeline.changed();
    selected = juce::jmin (selected, (int) timeline.getBlocks().size() - 1);
    const auto view = viewport.getViewPosition();
    lanes->updateSize (viewport.getHeight());
    viewport.setViewPosition (view);
    lanes->repaint();
    names->repaint();
}

void TimelineView::deleteSelected()
{
    auto& blocks = timeline.getBlocks();
    if (! juce::isPositiveAndBelow (selected, (int) blocks.size()))
        return;
    pushUndo();
    blocks.erase (blocks.begin() + selected);
    selected = -1;
    edited();
}

void TimelineView::duplicateSelected()
{
    auto& blocks = timeline.getBlocks();
    if (! juce::isPositiveAndBelow (selected, (int) blocks.size()) || (int) blocks.size() >= RigTimeline::kMaxBlocks)
        return;
    pushUndo();
    auto copy = blocks[(size_t) selected];
    copy.start = copy.end();
    blocks.push_back (copy);
    timeline.changed();
    for (int i = 0; i < (int) blocks.size(); ++i)
        if (blocks[(size_t) i].lane == copy.lane && std::abs (blocks[(size_t) i].start - copy.start) < 1.0e-9)
            selected = i;
    edited();
}

void TimelineView::setFade (int block, bool in, double bars)
{
    auto& blocks = timeline.getBlocks();
    if (! juce::isPositiveAndBelow (block, (int) blocks.size()))
        return;
    pushUndo();
    (in ? blocks[(size_t) block].fadeIn : blocks[(size_t) block].fadeOut) = juce::jmax (0.0, bars);
    edited();
}

void TimelineView::addMarker (double bar, const juce::String& name)
{
    pushUndo();
    auto& markers = timeline.getMarkers();
    markers.erase (std::remove_if (markers.begin(), markers.end(), [bar] (const RigTimeline::Marker& m) { return std::abs (m.bar - bar) < 1.0e-6; }),
                   markers.end());
    markers.push_back ({ bar, name });
    edited();
}

void TimelineView::setStartBar (double bar)
{
    startBar = juce::jmax (0.0, bar);
    lanes->repaint();
}

void TimelineView::setCycle (double from, double to)
{
    timeline.cycleStart = juce::jmax (0.0, from);
    timeline.cycleEnd = juce::jmax (from + 1.0, to);
    timeline.cycle = true;
    cycleButton.setToggleState (true, juce::dontSendNotification);
    lanes->repaint();
}

//==============================================================================
void TimelineView::play()
{
    if (timeline.isEmpty())
    {
        say ("Put some blocks on the lanes first: click a lane where a loop (or a tool) should play.");
        return;
    }
    const bool wasRunning = engine.getTransport().isPlaying();
    engine.playTimeline (timeline.cycle.load() && startBar >= timeline.cycleEnd.load() ? timeline.cycleStart.load() : startBar);
    onButton.setToggleState (true, juce::dontSendNotification);
    say (wasRunning ? "The song jumps to bar " + juce::String ((int) startBar + 1) + " at the next bar."
                    : "Playing the song from bar " + juce::String ((int) startBar + 1) + ".");
}

void TimelineView::stop()
{
    engine.getTransport().stop();
    engine.allNotesOff();
}

void TimelineView::fromParts()
{
    const auto& parts = engine.getScenes().getScenes();
    if (parts.empty())
    {
        say ("The Song grid has no parts yet.");
        return;
    }
    pushUndo();
    auto& blocks = timeline.getBlocks();
    auto& markers = timeline.getMarkers();
    blocks.clear();
    markers.clear();
    const auto& lanesList = RigTimeline::lanes();
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        const auto& info = lanesList[(size_t) lane];
        const SongGrid::RowInfo row { info.isLoop, info.index };
        // Like song mode: a part leaves it as it was, mutes it, or plays it.
        bool on = info.isLoop ? ! engine.getLooper().getLayer (info.index).mute.load() : ! engine.getStrip (info.index).mute.load();
        double bar = 0, startedAt = on ? 0.0 : -1.0;
        const auto finish = [&] (double end)
        {
            if (startedAt >= 0 && end > startedAt)
            {
                RigTimeline::Block b;
                b.lane = lane;
                b.start = startedAt;
                b.length = end - startedAt;
                blocks.push_back (b);
            }
            startedAt = -1;
        };
        for (const auto& part : parts)
        {
            const int value = SongGrid::cellValue (part, row);
            if (value == 1 && ! on)
                startedAt = bar;
            else if (value == 0 && on)
                finish (bar);
            if (value >= 0)
                on = value == 1;
            // A loop that comes in again starts from its top: a new block.
            bar += part.bars > 0 ? part.bars : 4;
        }
        finish (bar);
    }
    // Lanes the parts never change would play all the way through: leave those live.
    blocks.erase (std::remove_if (blocks.begin(), blocks.end(), [this, total = [&parts]
                                  {
                                      double t = 0;
                                      for (const auto& p : parts)
                                          t += p.bars > 0 ? p.bars : 4;
                                      return t;
                                  }()] (const RigTimeline::Block& b)
                                  {
                                      const auto& info = RigTimeline::lanes()[(size_t) b.lane];
                                      const bool emptyLoop = info.isLoop && engine.getLooper().getState (info.index) == RigLooper::State::empty;
                                      return emptyLoop || (b.start <= 0 && b.length >= total - 1.0e-9 && ! info.isLoop);
                                  }),
                  blocks.end());
    double bar = 0;
    for (const auto& part : parts)
    {
        markers.push_back ({ bar, part.name });
        bar += part.bars > 0 ? part.bars : 4;
    }
    selected = -1;
    startBar = 0;
    edited();
    say ("The Song grid's " + juce::String ((int) parts.size()) + " parts are on the timeline. Drag the blocks to shape them, and add fades.");
}

void TimelineView::recordSong()
{
    if (recordingSong)
    {
        stop();
        return;
    }
    if (timeline.isEmpty())
    {
        say ("Put the song on the timeline first, then Record song plays it once and records every tool.");
        return;
    }
    if (engine.getRecorder().getState() != RigRecorder::State::idle)
    {
        say ("The set is already recording (Rec, top bar). Stop that first.");
        return;
    }
    const auto folder = RigEngine::recordingsFolder().getChildFile ("Song " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"));
    engine.getTransport().stop();
    if (! engine.startRecording (folder))
    {
        say ("Couldn't start recording in " + folder.getFullPathName() + ".");
        return;
    }
    timeline.cycle = false;
    timeline.stopAtEnd = true;
    startBar = 0;
    recordingSong = true;
    seenPlaying = false;
    engine.playTimeline (0);
    onButton.setToggleState (true, juce::dontSendNotification);
    say ("Recording the song from bar 1. It stops by itself at the end (or press Stop). Sing along!");
}

void TimelineView::exportForLogic()
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

//==============================================================================
bool TimelineView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        duplicateSelected();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        undo();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::outline);
    g.drawRect (getLocalBounds(), 1);
}

void TimelineView::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    auto top = area.removeFromTop (30);
    title.setBounds (top.removeFromLeft (100));
    close.setBounds (top.removeFromRight (80));
    top.removeFromRight (10);
    const auto place = [&top] (juce::Component& c, int w)
    {
        c.setBounds (top.removeFromLeft (w));
        top.removeFromLeft (6);
    };
    place (playButton, 70);
    place (stopButton, 60);
    place (cycleButton, 70);
    place (clickButton, 64);
    place (snapButton, 92);
    place (zoomOut, 30);
    place (zoomIn, 30);
    top.removeFromLeft (10);
    place (onButton, 104);
    place (recOnto, 120);
    place (partsButton, 110);
    place (recordButton, 110);
    if (top.getWidth() >= 130)
        exportButton.setBounds (top.removeFromLeft (140));
    exportButton.setVisible (exportButton.getWidth() > 0 && exportButton.getRight() <= close.getX());
    area.removeFromTop (6);
    steps.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);
    names->setBounds (area.removeFromLeft (kNamesWidth));
    viewport.setBounds (area);
    const auto view = viewport.getViewPosition();
    lanes->updateSize (juce::jmax (kRuler + 20 * RigTimeline::kLanes, area.getHeight() - viewport.getScrollBarThickness()));
    viewport.setViewPosition (view);
}
