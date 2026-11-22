#include "PocketDrawer.h"

using namespace pocket;

namespace
{
constexpr int kRowH = 64, kRowGap = 8, kTileH = 48, kTileGap = 8, kMargin = 12;
}

//==============================================================================
// YOUR SONGS: a row each, in a list that scrolls
class SongDrawer::List : public juce::Component
{
public:
    explicit List (SongDrawer& o) : owner (o) {}

    SongDrawer& owner;
    int pressed = -1, pressedPlay = -1, pressedMore = -1;
    bool moved = false;
    juce::Point<float> downAt;
    HoldDetector hold { [this] { holdFired(); } };

    juce::Rectangle<float> card (int row) const { return { (float) kMargin, (float) (row * (kRowH + kRowGap)) + 2.0f, (float) getWidth() - 2.0f * kMargin, (float) kRowH }; }
    juce::Rectangle<float> playButton (int row) const { auto c = card (row); return { c.getRight() - 52.0f, c.getCentreY() - 22.0f, 44.0f, 44.0f }; }
    // ···: RENAME and DELETE without the hold (the card's full height, up to the play button)
    juce::Rectangle<float> moreButton (int row) const { auto c = card (row); return { c.getRight() - 100.0f, c.getCentreY() - 22.0f, 44.0f, 44.0f }; }
    juce::Rectangle<float> moreHit (int row) const { auto c = card (row); return { c.getRight() - 108.0f, c.getY(), 54.0f, c.getHeight() }; }
    int rowAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) owner.rows.size(); ++i)
            if (card (i).contains (p))
                return i;
        return -1;
    }

    void cancelPress()
    {
        hold.cancel();
        pressed = pressedPlay = pressedMore = -1;
        moved = true;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < (int) owner.rows.size(); ++i)
        {
            const auto& row = owner.rows[(size_t) i];
            auto r = card (i);
            g.setColour (col::creamLight);
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (row.current ? col::amber : col::rule);
            g.drawRoundedRectangle (r.reduced (1.0f), 14.0f, row.current ? 2.2f : 1.2f);
            if (i == pressed && pressedPlay < 0 && pressedMore < 0 && ! moved)
            {
                g.setColour (col::ink.withAlpha (0.06f));
                g.fillRoundedRectangle (r, 14.0f);
            }
            auto in = r.reduced (14, 8).withTrimmedRight (100);
            g.setColour (col::ink);
            g.setFont (small (17.0f).boldened());
            g.drawFittedText (row.name, in.removeFromTop (26).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (col::inkDim);
            g.setFont (small (13.0f));
            g.drawFittedText (row.meta, in.removeFromTop (20).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

            auto mb = moreButton (i);
            g.setColour (pressedMore == i ? col::creamDeep : col::creamLight);
            g.fillRoundedRectangle (mb.reduced (2), 10.0f);
            g.setColour (col::inkDim);
            for (int k = -1; k <= 1; ++k)
                g.fillEllipse (mb.getCentreX() + (float) k * 8.0f - 2.5f, mb.getCentreY() - 2.5f, 5.0f, 5.0f);

            auto pb = playButton (i);
            const bool playing = owner.isPlaying && owner.isPlaying (row.id);
            auto disc = pb.withSizeKeepingCentre (38, 38).translated (0, pressedPlay == i ? 1.0f : 0.0f);
            g.setColour (playing ? col::red : col::ink);
            g.fillEllipse (disc);
            g.setColour (juce::Colours::white);
            const auto c = disc.getCentre();
            if (playing)
                g.fillRoundedRectangle (c.x - 6, c.y - 6, 12, 12, 2.0f);
            else
            {
                juce::Path tri;
                tri.addTriangle (c.x - 4, c.y - 8, c.x - 4, c.y + 8, c.x + 8, c.y);
                g.fillPath (tri);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = rowAt (e.position);
        pressedPlay = pressed >= 0 && playButton (pressed).contains (e.position) ? pressed : -1;
        pressedMore = pressed >= 0 && pressedPlay < 0 && moreHit (pressed).contains (e.position) ? pressed : -1;
        moved = false;
        downAt = e.position;
        if (pressed >= 0 && pressedPlay < 0 && pressedMore < 0)
            hold.press (e.position);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        hold.move (e.position);
        if (! moved && e.getDistanceFromDragStart() > 8)
        {
            moved = true;
            hold.cancel();
            pressed = pressedPlay = pressedMore = -1;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        hold.release();
        const int row = pressed, playRow = pressedPlay, moreRow = pressedMore;
        pressed = pressedPlay = pressedMore = -1;
        repaint();
        if (row < 0 || moved || hold.fired() || rowAt (e.position) != row)
            return;
        const auto id = owner.rows[(size_t) row].id;
        if (playRow == row && playButton (row).contains (e.position))
        {
            if (owner.onPlay)
                owner.onPlay (id);
            return;
        }
        if (moreRow == row && moreHit (row).contains (e.position))
            return holdRow (row);
        if (owner.onPick)
            owner.onPick (id);
    }

    void holdFired()
    {
        if (pressed < 0 || pressed >= (int) owner.rows.size())
            return;
        const int row = pressed;
        pressed = pressedPlay = -1;
        repaint();
        holdRow (row);
    }

    void holdRow (int row)
    {
        if (owner.onHoldSong && juce::isPositiveAndBelow (row, (int) owner.rows.size()))
            owner.onHoldSong (owner.rows[(size_t) row].id, *this, card (row).getCentre(), card (row));
    }
};

//==============================================================================
class SongDrawer::Panel : public juce::Component
{
public:
    explicit Panel (SongDrawer& o) : owner (o)
    {
        list = std::make_unique<List> (o);
        view.setViewedComponent (list.get(), false);
        view.setScrollBarsShown (false, false);
        view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
        addAndMakeVisible (view);

        newButton.setFontHeight (22.0f);
        newButton.onClick = [this] { if (owner.onNew) owner.onNew(); };
        addAndMakeVisible (newButton);

        const juce::String names[kTiles] = { "SOUNDS", "RECORD A SOUND", "MAC LINK", "REMOTE", "SETTINGS", U8("TOUR \xc2\xb7 LESSON") };
        for (int i = 0; i < kTiles; ++i)
        {
            tiles[i].setText (names[i]);
            tiles[i].setFontHeight (15.0f);
            tiles[i].onClick = [this, i] { if (owner.onTile) owner.onTile (i); };
            addAndMakeVisible (tiles[i]);
        }
        // MAC LINK carries the link dot: green when linked, amber while linking, grey otherwise
        tiles[macLink].glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
        {
            const auto status = owner.link.getStatus();
            const auto dot = status == LinkClient::Status::linked ? col::green : status == LinkClient::Status::connecting ? col::amber : col::grey;
            g.setFont (spaced (condensed (15.0f)));
            const juce::String text = "MAC LINK";
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
            const float x = r.getCentreX() - (tw + 18.0f) * 0.5f;
            g.setColour (ink);
            g.drawText (text, juce::Rectangle<float> (x, r.getY(), tw + 4.0f, r.getHeight()), juce::Justification::centredLeft);
            auto d = juce::Rectangle<float> (x + tw + 10.0f, r.getCentreY() - 5.0f, 10.0f, 10.0f);
            if (status == LinkClient::Status::linked)
            {
                g.setColour (dot.withAlpha (0.3f));
                g.fillEllipse (d.expanded (3.0f));
            }
            g.setColour (dot);
            g.fillEllipse (d);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawEllipse (d, 1.0f);
        };
    }

    // The three zones, top to bottom: the wordmark and + NEW SONG, the songs, the grid.
    int gridTop() const { return getHeight() - owner.insets.getBottom() - kMargin - (3 * kTileH + 2 * kTileGap); }
    int listTop() const { return owner.insets.getTop() + 154; }
    juce::Rectangle<int> tileRect (int i) const
    {
        const int w = (getWidth() - 2 * kMargin - kTileGap) / 2;
        return { kMargin + (i % 2) * (w + kTileGap), gridTop() + (i / 2) * (kTileH + kTileGap), w, kTileH };
    }

    void resized() override
    {
        const int top = owner.insets.getTop();
        newButton.setBounds (kMargin, top + 66, getWidth() - 2 * kMargin, 58);
        view.setBounds (0, listTop(), getWidth(), juce::jmax (0, gridTop() - 14 - listTop()));
        list->setSize (getWidth(), juce::jmax (view.getHeight(), (int) owner.rows.size() * (kRowH + kRowGap) + 6));
        for (int i = 0; i < kTiles; ++i)
            tiles[i].setBounds (tileRect (i));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::cream);
        const int top = owner.insets.getTop();
        g.setColour (col::ink);
        g.setFont (fraunces (34.0f));
        g.drawText ("Kaleidobox", 18, top + 12, getWidth() - 36, 46, juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (12.0f), 0.14f));
        g.drawFittedText (U8("YOUR SONGS \xc2\xb7 HOLD ONE OR TAP \xc2\xb7\xc2\xb7\xc2\xb7 TO RENAME OR DELETE"), juce::Rectangle<int> (kMargin + 4, top + 128, getWidth() - 2 * kMargin - 4, 22),
                          juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (col::rule);
        g.fillRect (kMargin, gridTop() - 10, getWidth() - 2 * kMargin, 2);
        if (owner.rows.empty())
        {
            g.setColour (col::inkDim);
            g.setFont (small (15.0f));
            g.drawFittedText ("No songs yet. + NEW SONG starts one.", juce::Rectangle<int> (kMargin + 8, listTop() + 6, getWidth() - 2 * kMargin - 16, 60), juce::Justification::topLeft, 3);
        }
    }

    SongDrawer& owner;
    PButton newButton { "+ New song", Look::amber };
    PButton tiles[kTiles];
    juce::Viewport view;
    std::unique_ptr<List> list;
};

//==============================================================================
namespace
{
// A finger on the open drawer: pulling it left closes it, wherever on it (or on the dim) the finger is.
struct Swipe : juce::MouseListener
{
    explicit Swipe (SongDrawer& d) : drawer (d) {}
    void mouseDown (const juce::MouseEvent& e) override
    {
        downX = lastX = prevX = e.getScreenPosition().x;
        downY = e.getScreenPosition().y;
        lastT = prevT = juce::Time::getMillisecondCounter();
        dragging = false;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float dx = e.getScreenPosition().x - downX, dy = e.getScreenPosition().y - downY;
        if (! dragging)
        {
            if (drawer.progress() > 0.99f && dx < -14.0f && std::abs (dx) > std::abs (dy) * 1.4f)
            {
                dragging = true;
                drawer.dragStart();
            }
            else
                return;
        }
        prevX = lastX;
        prevT = lastT;
        lastX = e.getScreenPosition().x;
        lastT = juce::Time::getMillisecondCounter();
        drawer.dragTo (juce::jmin (0.0f, dx));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        dragging = false;
        const auto now = juce::Time::getMillisecondCounter();
        const float v = now - lastT > 90 ? 0.0f : (lastX - prevX) / (float) juce::jmax<juce::uint32> (1, lastT - prevT);
        drawer.dragEnd (juce::jmin (0.0f, e.getScreenPosition().x - downX), v);
    }
    SongDrawer& drawer;
    float downX = 0, downY = 0, lastX = 0, prevX = 0;
    juce::uint32 lastT = 0, prevT = 0;
    bool dragging = false;
};

}

SongDrawer::SongDrawer (Ideas& i, LinkClient& l) : ideas (i), link (l)
{
    setVisible (false);
    panel = std::make_unique<Panel> (*this);
    addAndMakeVisible (*panel);
    swipe = std::make_unique<Swipe> (*this);
    addMouseListener (swipe.get(), true);
}

SongDrawer::~SongDrawer()
{
    stopTimer();
    removeMouseListener (swipe.get());
}

void SongDrawer::refresh()
{
    rows.clear();
    const auto currentId = ideas.current().id;
    for (auto& idea : ideas.list())
    {
        Row r;
        r.id = idea.id;
        r.name = idea.name;
        r.current = idea.id == currentId;
        auto facts = IdeasScreen::factsOf (ideas.folderFor (idea.id));
        if (r.current)
        {
            if (currentLayers)
                facts.layers = currentLayers();
            if (currentBpm && currentBpm() > 0.0)
                facts.bpm = currentBpm();
        }
        auto when = IdeasScreen::whenText (idea.modified);
        if (when.startsWith ("Today"))
            when = "Today";
        r.meta = when;
        if (facts.bpm > 0.0)
            r.meta << U8(" \xc2\xb7 ") << juce::String (facts.bpm, 0) << " BPM";
        r.meta << U8(" \xc2\xb7 ") << (facts.layers == 0 ? juce::String ("no layers yet") : juce::String (facts.layers) + (facts.layers == 1 ? " layer" : " layers"));
        rows.push_back (std::move (r));
    }
    layoutPanel();
    panel->list->repaint();
}

//==============================================================================
void SongDrawer::resized()
{
    layoutPanel();
}

void SongDrawer::layoutPanel()
{
    const int pw = panelWidth();
    panel->setBounds (juce::roundToInt ((float) pw * (shown - 1.0f)), 0, pw, getHeight());
    panel->resized();
}

void SongDrawer::setProgress (float p)
{
    shown = juce::jlimit (0.0f, 1.0f, p);
    const bool wasVisible = isVisible();
    if (shown > 0.0f && ! wasVisible)
    {
        refresh();
        setVisible (true);
        toFront (false);
    }
    layoutPanel();
    repaint();
    if (shown <= 0.0f && wasVisible)
    {
        setVisible (false);
        if (onClosed)
            onClosed();
    }
}

void SongDrawer::animateTo (bool open)
{
    goal = open ? 1.0f : 0.0f;
    if (open && ! isVisible() && shown <= 0.0f)
    {
        refresh();
        setVisible (true);
        toFront (false);
    }
    if (! animated)
    {
        stopTimer();
        return setProgress (goal);
    }
    startTimerHz (60);
}

void SongDrawer::timerCallback()
{
    const float d = goal - shown;
    if (std::abs (d) < 0.015f)
    {
        stopTimer();
        return setProgress (goal);
    }
    setProgress (shown + d * 0.32f + (d > 0 ? 0.012f : -0.012f));
}

void SongDrawer::dragStart()
{
    stopTimer();
    scrimTap = true;   // (the lift after the drag is not a tap on the dim)
    panel->list->cancelPress();
}

void SongDrawer::dragTo (float dx)
{
    setProgress (1.0f + dx / (float) panelWidth());
}

void SongDrawer::dragEnd (float dx, float pxPerMs)
{
    const float open = 1.0f + dx / (float) panelWidth();
    const bool close = open < 0.65f || pxPerMs < -0.6f;
    scrimTap = false;
    animateTo (! close);
}

void SongDrawer::holdRow (int row)
{
    panel->list->holdRow (row);
}

juce::Rectangle<int> SongDrawer::tileBounds (int tile) const
{
    return panel->tileRect (tile).translated (panel->getX(), 0);
}

juce::Rectangle<int> SongDrawer::songRowBounds (int row) const
{
    return panel->list->card ((int) row).toNearestInt().translated (panel->getX(), panel->view.getY() - panel->view.getViewPositionY());
}

void SongDrawer::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.42f * shown));
    // the panel's soft edge
    const auto edge = (float) panel->getRight();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x40000000), edge, 0, juce::Colour (0x00000000), edge + 12.0f, 0, false));
    g.fillRect (edge, 0.0f, 12.0f, (float) getHeight());
}

void SongDrawer::mouseUp (const juce::MouseEvent&)
{
    if (scrimTap)
    {
        scrimTap = false;
        return;
    }
    animateTo (false);   // a tap on the dim
}
