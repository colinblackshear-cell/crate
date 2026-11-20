#include "IdeasScreen.h"

using namespace pocket;

namespace
{
constexpr int kCardH = 104, kGap = 10, kTop = 136;

juce::String whenText (juce::Time t)
{
    const auto now = juce::Time::getCurrentTime();
    const auto time = t.formatted ("%I:%M %p").trimCharactersAtStart ("0");
    const int days = (int) ((juce::Time (now.getYear(), now.getMonth(), now.getDayOfMonth(), 0, 0).toMilliseconds()
                             - juce::Time (t.getYear(), t.getMonth(), t.getDayOfMonth(), 0, 0).toMilliseconds()) / 86400000);
    if (days <= 0) return "Today " + time;
    if (days == 1) return "Yesterday";
    if (days < 7) return t.getWeekdayName (true);
    return t.formatted ("%b %d");
}

// A little picture for each song: bars in the colour of the step it has reached.
void drawThumb (juce::Graphics& g, juce::Rectangle<float> r, const IdeaInfo& idea)
{
    g.setColour (col::well);
    g.fillRoundedRectangle (r, 10.0f);
    const auto colour = idea.reached >= 0 ? stepColour (idea.reached) : col::grey;
    auto in = r.reduced (8, 10);
    const int n = 9;
    const float w = in.getWidth() / (float) n;
    auto seed = (juce::uint32) idea.id.hashCode();
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float shape = idea.reached >= stepSing ? (float) (i + 2) / (float) (n + 1)                  // building up
                          : idea.reached >= stepBeat ? (i % 2 == 0 ? 0.85f : 0.35f)                      // a beat
                                                     : std::exp (-(float) i * 0.35f);                     // one sound dying away
        const float h = juce::jmax (3.0f, in.getHeight() * juce::jlimit (0.1f, 1.0f, shape * (0.7f + 0.3f * (float) (seed >> 24) / 255.0f)));
        g.setColour (colour.withAlpha (0.55f + 0.45f * (float) i / (float) n));
        g.fillRoundedRectangle (in.getX() + (float) i * w + 1.0f, in.getBottom() - h, w - 2.0f, h, 1.0f);
    }
}
}

//==============================================================================
struct IdeasScreen::List : public juce::Component, private juce::Timer
{
    explicit List (IdeasScreen& o) : owner (o) {}

    IdeasScreen& owner;
    int pressed = -1, pressedPlay = -1;
    bool held = false, moved = false;
    juce::Point<float> downAt;

    juce::Rectangle<float> card (int row) const { return { 16.0f, (float) (row * (kCardH + kGap)) + 2.0f, (float) getWidth() - 32.0f, (float) kCardH }; }
    juce::Rectangle<float> playButton (int row) const { auto c = card (row); return { c.getRight() - 60, c.getCentreY() - 22, 44, 44 }; }
    int rowAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) owner.ideas.list().size(); ++i)
            if (card (i).contains (p))
                return i;
        return -1;
    }
    const IdeaInfo& ideaAt (int row) const { return owner.ideas.list()[(size_t) owner.order()[(size_t) row]]; }

    void paint (juce::Graphics& g) override
    {
        const auto& all = owner.ideas.list();
        const auto currentId = owner.ideas.current().id;
        const auto ord = owner.order();
        for (int row = 0; row < (int) ord.size(); ++row)
        {
            const auto& idea = all[(size_t) ord[(size_t) row]];
            auto r = card (row);
            g.setColour (col::creamLight);
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (idea.id == currentId ? col::amber : col::rule);
            g.drawRoundedRectangle (r.reduced (1.0f), 14.0f, 2.0f);
            if (row == pressed)
            {
                g.setColour (col::ink.withAlpha (0.06f));
                g.fillRoundedRectangle (r, 14.0f);
            }
            drawThumb (g, juce::Rectangle<float> (r.getX() + 12, r.getCentreY() - 28, 56, 56), idea);

            auto in = r.withTrimmedLeft (80).withTrimmedRight (70).reduced (0, 10);
            g.setColour (col::ink);
            g.setFont (small (18.0f).boldened());
            g.drawFittedText (idea.name, in.removeFromTop (24).toNearestInt(), juce::Justification::centredLeft, 1);
            g.setColour (col::inkDim);
            g.setFont (small (13.5f));
            auto meta = whenText (idea.modified);
            if (idea.summary.isNotEmpty())
                meta << U8(" · ") << idea.summary;
            g.drawFittedText (meta, in.removeFromTop (19).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            in.removeFromTop (7);
            auto dots = in.removeFromTop (6);
            const float w = (dots.getWidth() - 6 * 4.0f) / (float) kSteps;
            for (int s = 0; s < kSteps; ++s)
            {
                g.setColour (s <= idea.reached ? stepColour (s) : col::creamDeep);
                g.fillRoundedRectangle (dots.getX() + (float) s * (w + 4.0f), dots.getY(), w, 6.0f, 3.0f);
            }
            in.removeFromTop (6);
            g.setColour (col::inkDim);
            g.setFont (spaced (condensedBold (13.0f), 0.06f));
            const int next = juce::jmin (kSteps - 1, idea.reached + 1);
            const juce::String nextText = idea.sent ? U8("SENT · open it again any time")
                                                    : U8("NEXT: ") + juce::String (stepName (next)) + U8(" · ") + stepHint (next);
            g.drawFittedText (nextText, in.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

            // play what it has so far
            auto pb = playButton (row);
            const bool playing = owner.isPlaying && owner.isPlaying (idea.id);
            g.setColour (playing ? col::red : col::ink);
            g.fillEllipse (pb.translated (0, pressedPlay == row ? 1.0f : 0.0f));
            g.setColour (juce::Colours::white);
            auto c = pb.getCentre();
            if (playing)
                g.fillRoundedRectangle (c.x - 7, c.y - 7, 14, 14, 2.0f);
            else
            {
                juce::Path tri;
                tri.addTriangle (c.x - 5, c.y - 8, c.x - 5, c.y + 8, c.x + 9, c.y);
                g.fillPath (tri);
            }
        }
        if (all.empty())
        {
            g.setColour (col::inkDim);
            g.setFont (small (16.0f));
            g.drawFittedText ("No songs yet. Start one below: record a sound, take a ready beat, or load a kit.",
                              getLocalBounds().reduced (24, 0).withHeight (80), juce::Justification::centred, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = rowAt (e.position);
        pressedPlay = pressed >= 0 && playButton (pressed).expanded (6).contains (e.position) ? pressed : -1;
        held = moved = false;
        downAt = e.position;
        if (pressed >= 0 && pressedPlay < 0)
            startTimer (550);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! moved && e.getDistanceFromDragStart() > 8)
        {
            moved = true;
            stopTimer();
            pressed = pressedPlay = -1;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        const int row = pressed, playRow = pressedPlay;
        pressed = pressedPlay = -1;
        repaint();
        if (row < 0 || moved || held || rowAt (e.position) != row)
            return;
        const auto id = ideaAt (row).id;
        if (playRow == row && playButton (row).expanded (6).contains (e.position))
        {
            if (owner.onPlay)
                owner.onPlay (id);
            return;
        }
        if (owner.onOpen)
            owner.onOpen (id);
    }

    void timerCallback() override
    {
        stopTimer();
        if (pressed < 0 || pressed >= (int) owner.ideas.list().size())
            return;
        held = true;
        const auto id = ideaAt (pressed).id;
        const bool isCurrent = id == owner.ideas.current().id;
        pressed = -1;
        repaint();
        juce::PopupMenu m;
        m.addItem ("Open", [this, id] { if (owner.onOpen) owner.onOpen (id); });
        m.addItem ("Rename", [this, id] { if (owner.onRename) owner.onRename (id); });
        m.addItem (isCurrent ? "Delete (open another song first)" : "Delete", ! isCurrent, false, [this, id] { if (owner.onDelete) owner.onDelete (id); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> ((int) downAt.x, (int) downAt.y, 1, 1))));
    }
};

//==============================================================================
IdeasScreen::IdeasScreen (Ideas& i) : ideas (i), newButton ({}, Look::ghost)
{
    list = std::make_unique<List> (*this);
    view.setViewedComponent (list.get(), false);
    view.setScrollBarsShown (false, false);
    view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (view);
    newButton.onClick = [this] { if (onNew) onNew(); };
    addAndMakeVisible (newButton);
}

IdeasScreen::~IdeasScreen() = default;

std::vector<int> IdeasScreen::order() const
{
    const auto& all = ideas.list();
    std::vector<int> o ((size_t) all.size());
    for (size_t i = 0; i < o.size(); ++i)
        o[i] = (int) i;   // the list is newest first already
    if (sortBy == 1)
        std::stable_sort (o.begin(), o.end(), [&all] (int a, int b) { return all[(size_t) a].name.compareNatural (all[(size_t) b].name) < 0; });
    else if (sortBy == 2)
        std::stable_sort (o.begin(), o.end(), [&all] (int a, int b)
        {
            const int ra = all[(size_t) a].sent ? kSteps : all[(size_t) a].reached, rb = all[(size_t) b].sent ? kSteps : all[(size_t) b].reached;
            return ra > rb;
        });
    return o;
}

void IdeasScreen::refresh()
{
    resized();
    list->repaint();
    repaint();
}

juce::Rectangle<int> IdeasScreen::newBounds() const { return { getWidth() / 2 - 38, getHeight() - 112, 76, 76 }; }
juce::Rectangle<float> IdeasScreen::beatRect() const { return { 16, (float) getHeight() - 104, (float) getWidth() / 2 - 70, 58 }; }
juce::Rectangle<float> IdeasScreen::kitRect() const { return { (float) getWidth() / 2 + 54, (float) getHeight() - 104, (float) getWidth() / 2 - 70, 58 }; }

juce::Rectangle<float> IdeasScreen::chip (int i) const
{
    static const float x[3] = { 16, 104, 192 }, w[3] = { 80, 80, 150 };
    return { x[i], 96, w[i], 32 };
}

void IdeasScreen::resized()
{
    view.setBounds (0, kTop, getWidth(), getHeight() - kTop - 148);
    const int n = (int) ideas.list().size();
    list->setSize (getWidth(), juce::jmax (view.getHeight(), n * (kCardH + kGap) + 4));
    newButton.setBounds (newBounds());
}

void IdeasScreen::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
        if (chip (i).contains (e.position))
        {
            sortBy = i;
            view.setViewPosition (0, 0);
            refresh();
            return;
        }
    if (beatRect().contains (e.position) && onNewBeat)
        onNewBeat();
    else if (kitRect().contains (e.position) && onNewKit)
        onNewKit();
}

void IdeasScreen::paint (juce::Graphics& g)
{
    g.setColour (col::ink);
    g.setFont (condensedBold (32.0f));
    g.drawText ("Your songs", 18, 8, 200, 40, juce::Justification::centredLeft);
    const int n = (int) ideas.list().size();
    g.setColour (col::inkDim);
    g.setFont (small (15.0f));
    g.drawText (juce::String (n) + " in progress", getWidth() - 166, 12, 148, 34, juce::Justification::centredRight);
    g.setFont (small (14.5f));
    g.drawFittedText (U8("Each one is a song in the making: its sounds, pads, beat and layers. \xe2\x96\xb6 plays what it has so far."),
                      juce::Rectangle<int> (18, 48, getWidth() - 36, 42), juce::Justification::topLeft, 2, 0.9f);

    static const char* names[3] = { "RECENT", "NAME", "FURTHEST ALONG" };
    for (int i = 0; i < 3; ++i)
    {
        auto c = chip (i);
        drawRaised (g, c, sortBy == i ? Look::amber : Look::cream, false, 10.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (14.0f), 0.08f));
        g.drawText (names[i], c.withTrimmedBottom (3), juce::Justification::centred);
    }

    // a new song: with a sound (record), a ready beat, or a factory kit
    g.setColour (col::rule);
    g.fillRect (16.0f, (float) getHeight() - 146, (float) getWidth() - 32, 1.0f);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.14f));
    g.drawText ("START A NEW SONG WITH", 0, getHeight() - 140, getWidth(), 22, juce::Justification::centred);

    auto side = [&] (juce::Rectangle<float> r, const juce::String& top, const juce::String& bottom)
    {
        drawRaised (g, r, Look::cream, false, 14.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (17.0f), 0.08f));
        g.drawText (top, r.withHeight (r.getHeight() * 0.55f).translated (0, 3), juce::Justification::centred);
        g.setColour (col::inkDim);
        g.setFont (small (12.5f));
        g.drawText (bottom, r.withTrimmedTop (r.getHeight() * 0.52f).withTrimmedBottom (4), juce::Justification::centred);
    };
    side (beatRect(), "A BEAT", "ready to play over");
    side (kitRect(), "A KIT", "16 pads to jam on");

    auto b = newBounds().toFloat();
    g.setColour (col::creamLight);
    g.fillEllipse (b.expanded (4));
    g.setColour (col::rule);
    g.drawEllipse (b.expanded (5), 2.0f);
    g.setColour (juce::Colour (0xff922a1c));
    g.fillEllipse (b.reduced (4).translated (0, 3));
    g.setColour (juce::Colour (0xffd8452f));
    g.fillEllipse (b.reduced (4));
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (14.0f), 0.12f));
    g.drawText ("A SOUND", 0, (int) b.getBottom() + 6, getWidth(), 20, juce::Justification::centred);
}
