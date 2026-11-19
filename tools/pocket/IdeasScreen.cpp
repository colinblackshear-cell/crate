#include "IdeasScreen.h"

using namespace pocket;

namespace
{
constexpr int kCardH = 112, kGap = 10;

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
}

//==============================================================================
struct IdeasScreen::List : public juce::Component, private juce::Timer
{
    explicit List (IdeasScreen& o) : owner (o) {}

    IdeasScreen& owner;
    int pressed = -1;
    bool held = false, moved = false;
    juce::Point<float> downAt;

    juce::Rectangle<float> card (int i) const { return { 16.0f, (float) (i * (kCardH + kGap)) + 2.0f, (float) getWidth() - 32.0f, (float) kCardH }; }
    int rowAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) owner.ideas.list().size(); ++i)
            if (card (i).contains (p))
                return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& all = owner.ideas.list();
        const auto currentId = owner.ideas.current().id;
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& idea = all[(size_t) i];
            auto r = card (i);
            g.setColour (col::creamLight);
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (idea.id == currentId ? col::amber : col::rule);
            g.drawRoundedRectangle (r.reduced (1.0f), 14.0f, 2.0f);
            if (i == pressed)
            {
                g.setColour (col::ink.withAlpha (0.06f));
                g.fillRoundedRectangle (r, 14.0f);
            }
            auto in = r.reduced (14.0f, 10.0f);
            g.setColour (col::ink);
            g.setFont (small (18.0f).boldened());
            g.drawFittedText (idea.name, in.removeFromTop (24).toNearestInt(), juce::Justification::centredLeft, 1);
            g.setColour (col::inkDim);
            g.setFont (small (14.0f));
            auto meta = whenText (idea.modified);
            if (idea.summary.isNotEmpty())
                meta << U8(" · ") << idea.summary;
            g.drawFittedText (meta, in.removeFromTop (20).toNearestInt(), juce::Justification::centredLeft, 1);
            in.removeFromTop (8);
            auto dots = in.removeFromTop (6);
            const float w = (dots.getWidth() - 6 * 4.0f) / (float) kSteps;
            for (int s = 0; s < kSteps; ++s)
            {
                g.setColour (s <= idea.reached ? stepColour (s) : col::creamDeep);
                g.fillRoundedRectangle (dots.getX() + (float) s * (w + 4.0f), dots.getY(), w, 6.0f, 3.0f);
            }
            in.removeFromTop (8);
            g.setColour (col::inkDim);
            g.setFont (spaced (condensedBold (14.0f), 0.08f));
            const int next = juce::jmin (kSteps - 1, idea.reached + 1);
            const juce::String nextText = idea.sent ? U8("SENT · open it again any time")
                                                    : U8("NEXT: ") + juce::String (stepName (next)) + U8(" · ") + stepHint (next);
            g.drawFittedText (nextText, in.toNearestInt(), juce::Justification::centredLeft, 1);
        }
        if (all.empty())
        {
            g.setColour (col::inkDim);
            g.setFont (small (16.0f));
            g.drawFittedText ("No ideas yet. Tap NEW IDEA and start recording.", getLocalBounds().reduced (24, 0).withHeight (80), juce::Justification::centred, 2);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = rowAt (e.position);
        held = moved = false;
        downAt = e.position;
        if (pressed >= 0)
            startTimer (550);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! moved && e.getDistanceFromDragStart() > 8)
        {
            moved = true;
            stopTimer();
            pressed = -1;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        const int row = pressed;
        pressed = -1;
        repaint();
        if (row < 0 || moved || held || rowAt (e.position) != row)
            return;
        const auto id = owner.ideas.list()[(size_t) row].id;
        if (owner.onOpen)
            owner.onOpen (id);
    }

    void timerCallback() override
    {
        stopTimer();
        if (pressed < 0 || pressed >= (int) owner.ideas.list().size())
            return;
        held = true;
        const auto id = owner.ideas.list()[(size_t) pressed].id;
        const bool isCurrent = id == owner.ideas.current().id;
        pressed = -1;
        repaint();
        juce::PopupMenu m;
        m.addItem ("Open", [this, id] { if (owner.onOpen) owner.onOpen (id); });
        m.addItem ("Rename", [this, id] { if (owner.onRename) owner.onRename (id); });
        m.addItem (isCurrent ? "Delete (open another idea first)" : "Delete", ! isCurrent, false, [this, id] { if (owner.onDelete) owner.onDelete (id); });
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

void IdeasScreen::refresh()
{
    resized();
    list->repaint();
    repaint();
}

juce::Rectangle<int> IdeasScreen::newBounds() const
{
    return { getWidth() / 2 - 43, getHeight() - 122, 86, 86 };
}

void IdeasScreen::resized()
{
    view.setBounds (0, 58, getWidth(), getHeight() - 58 - 130);
    const int n = (int) ideas.list().size();
    list->setSize (getWidth(), juce::jmax (view.getHeight(), n * (kCardH + kGap) + 4));
    newButton.setBounds (newBounds());
}

void IdeasScreen::paint (juce::Graphics& g)
{
    g.setColour (col::ink);
    g.setFont (condensedBold (32.0f));
    g.drawText ("Ideas", 18, 10, 160, 40, juce::Justification::centredLeft);
    const int n = (int) ideas.list().size();
    g.setColour (col::inkDim);
    g.setFont (small (15.0f));
    g.drawText (juce::String (n) + (n == 1 ? " idea" : " ideas") + U8(" · tap one to carry on"), getWidth() - 236, 14, 218, 34, juce::Justification::centredRight);

    // NEW IDEA: the big red record button, with a white ring like Voice Memos
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
    g.setFont (spaced (condensedBold (16.0f), 0.14f));
    g.drawText ("NEW IDEA", 0, (int) b.getBottom() + 8, getWidth(), 22, juce::Justification::centred);
}
