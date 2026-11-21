#include "IdeasScreen.h"

#include "PocketLoopTools.h"

using namespace pocket;

namespace
{
constexpr int kCardH = 90, kGap = 10, kTop = 312;

}

juce::String IdeasScreen::whenText (juce::Time t)
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

namespace
{
const juce::Colour kNumberColours[3] = { juce::Colour (0xff5fb3c4), juce::Colour (0xfff0674f), juce::Colour (0xff82cf6a) };
}

IdeasScreen::Facts IdeasScreen::factsOf (const juce::File& song)
{
    Facts f;
    const auto loops = juce::JSON::parse (song.getChildFile ("Loops").getChildFile ("loops.json"));
    if (auto* layers = loops.getProperty ("layers", {}).getArray())
        for (auto& l : *layers)
        {
            ++f.layers;
            f.tools.push_back (l.hasProperty ("tool") ? (int) l.getProperty ("tool", 0) : toolForSource ((int) l.getProperty ("source", 0)));
        }
    const auto drums = juce::JSON::parse (song.getChildFile ("Beats").getChildFile ("drums.json"));
    f.bpm = f.layers > 0 ? (double) loops.getProperty ("bpm", 0.0) : 0.0;
    if (f.bpm <= 0.0)
        f.bpm = (double) drums.getProperty ("bpm", 0.0);
    return f;
}

//==============================================================================
struct IdeasScreen::List : public juce::Component
{
    explicit List (IdeasScreen& o) : owner (o) {}

    IdeasScreen& owner;
    int pressed = -1, pressedPlay = -1, pressedMore = -1;
    bool moved = false;
    juce::Point<float> downAt;
    pocket::HoldDetector hold { [this] { holdFired(); } };

    juce::Rectangle<float> card (int row) const { return { 16.0f, (float) (row * (kCardH + kGap)) + 2.0f, (float) getWidth() - 32.0f, (float) kCardH }; }
    juce::Rectangle<float> playButton (int row) const { auto c = card (row); return { c.getRight() - 62, c.getCentreY() - 24, 48, 48 }; }
    juce::Rectangle<float> moreButton (int row) const { auto c = card (row); return { c.getRight() - 112, c.getCentreY() - 22, 44, 44 }; }
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
            const auto it = owner.facts.find (idea.id);
            const Facts f = it != owner.facts.end() ? it->second : Facts();
            auto r = card (row);
            g.setColour (col::creamLight);
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (idea.id == currentId ? col::amber : col::rule);
            g.drawRoundedRectangle (r.reduced (1.0f), 14.0f, 2.0f);
            if (row == pressed && pressedPlay < 0 && pressedMore < 0)
            {
                g.setColour (col::ink.withAlpha (0.06f));
                g.fillRoundedRectangle (r, 14.0f);
            }

            auto in = juce::Rectangle<float> (r.getX() + 14, r.getY() + 10, moreButton (row).getX() - r.getX() - 18, r.getHeight() - 20);
            g.setColour (col::ink);
            g.setFont (small (18.0f).boldened());
            g.drawFittedText (idea.name, in.removeFromTop (24).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (col::inkDim);
            g.setFont (small (13.5f));
            juce::String meta = whenText (idea.modified);
            if (f.bpm > 0.0)
                meta << U8(" \xc2\xb7 ") << juce::String (f.bpm, 0) << " BPM";
            meta << U8(" \xc2\xb7 ") << (f.layers == 0 ? juce::String ("no layers yet") : juce::String (f.layers) + (f.layers == 1 ? " layer" : " layers"));
            g.drawFittedText (meta, in.removeFromTop (20).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            in.removeFromTop (5);
            // the tools on its layers
            float x = in.getX();
            for (int tool : f.tools)
            {
                auto box = juce::Rectangle<float> (x, in.getY(), 24, 24);
                if (box.getRight() > in.getRight())
                    break;
                g.setColour (col::well);
                g.fillRoundedRectangle (box, 6.0f);
                juce::Graphics::ScopedSaveState s (g);
                g.addTransform (juce::AffineTransform::scale (0.62f, 0.62f, box.getCentreX(), box.getCentreY()));
                drawLayerToolIcon (g, tool, box.getCentre(), layerToolColour (tool));
                x += 29;
            }

            // ··· rename / delete
            auto mb = moreButton (row);
            g.setColour (pressedMore == row ? col::creamDeep : col::creamLight);
            g.fillRoundedRectangle (mb.reduced (2), 10.0f);
            g.setColour (col::inkDim);
            for (int k = -1; k <= 1; ++k)
                g.fillEllipse (mb.getCentreX() + (float) k * 8.0f - 2.5f, mb.getCentreY() - 2.5f, 5.0f, 5.0f);

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
            g.drawFittedText ("No songs yet. + NEW SONG starts one.", getLocalBounds().reduced (24, 0).withHeight (80), juce::Justification::centred, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = rowAt (e.position);
        pressedPlay = pressed >= 0 && playButton (pressed).expanded (4).contains (e.position) ? pressed : -1;
        pressedMore = pressed >= 0 && pressedPlay < 0 && moreButton (pressed).contains (e.position) ? pressed : -1;
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
        const auto id = ideaAt (row).id;
        if (playRow == row && playButton (row).expanded (4).contains (e.position))
        {
            if (owner.onPlay)
                owner.onPlay (id);
            return;
        }
        if (moreRow == row && moreButton (row).contains (e.position))
            return showMenu (row, moreButton (row).getCentre());
        if (owner.onOpen)
            owner.onOpen (id);
    }

    void showMenu (int row, juce::Point<float> at)
    {
        if (owner.onHoldSong)
            owner.onHoldSong (ideaAt (row).id, *this, at, card (row));
    }

    void holdFired()
    {
        if (pressed < 0 || pressed >= (int) owner.ideas.list().size())
            return;
        const int row = pressed;
        pressed = -1;
        repaint();
        showMenu (row, downAt);
    }
};

//==============================================================================
IdeasScreen::IdeasScreen (Ideas& i) : ideas (i)
{
    list = std::make_unique<List> (*this);
    view.setViewedComponent (list.get(), false);
    view.setScrollBarsShown (false, false);
    view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (view);
    newButton.setFontHeight (24.0f);
    lessonButton.setFontHeight (15.0f);
    tourButton.setFontHeight (15.0f);
    newButton.onClick = [this] { if (onNew) onNew(); };
    lessonButton.onClick = [this] { if (onNewBeat) onNewBeat(); };
    tourButton.onClick = [this] { if (onTour) onTour(); };
    for (auto* b : { &newButton, &lessonButton, &tourButton })
        addAndMakeVisible (*b);
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
        std::stable_sort (o.begin(), o.end(), [this, &all] (int a, int b)
        {
            const auto fa = facts.find (all[(size_t) a].id), fb = facts.find (all[(size_t) b].id);
            return (fa != facts.end() ? fa->second.layers : 0) > (fb != facts.end() ? fb->second.layers : 0);
        });
    return o;
}

void IdeasScreen::holdRow (int row)
{
    if (juce::isPositiveAndBelow (row, (int) ideas.list().size()))
        list->showMenu (row, list->card (row).getCentre());
}

void IdeasScreen::refresh()
{
    facts.clear();
    for (auto& idea : ideas.list())
        facts[idea.id] = factsOf (ideas.folderFor (idea.id));
    resized();
    list->repaint();
    repaint();
}

void IdeasScreen::resized()
{
    const int w = getWidth();
    newButton.setBounds (16, 144, w - 32, 62);
    lessonButton.setBounds (16, 216, (w - 40) / 2, 46);
    tourButton.setBounds (lessonButton.getRight() + 8, 216, w - 16 - lessonButton.getRight() - 8, 46);
    view.setBounds (0, kTop, w, getHeight() - kTop);
    const int n = (int) ideas.list().size();
    list->setSize (w, juce::jmax (view.getHeight(), n * (kCardH + kGap) + 12));
}

void IdeasScreen::mouseUp (const juce::MouseEvent& e)
{
    if (sortChip().contains (e.position))
    {
        sortBy = (sortBy + 1) % 3;
        view.setViewPosition (0, 0);
        refresh();
    }
}

void IdeasScreen::paint (juce::Graphics& g)
{
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.14f));
    g.drawText ("HOW IT WORKS", 16, 6, 200, 20, juce::Justification::centredLeft);

    static const char* heads[3] = { "PICK A TOOL", "PRESS REC", "IT LOOPS" };
    static const char* bodies[3] = { "Drums, pads, synth, chords or your voice.", "Count-in and click, then play.", "On its own layer, in time. Add the next." };
    const float bw = ((float) getWidth() - 32.0f - 16.0f) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        auto box = juce::Rectangle<float> (16.0f + (float) i * (bw + 8.0f), 30.0f, bw, 104.0f);
        g.setColour (col::well);
        g.fillRoundedRectangle (box, 12.0f);
        auto in = box.reduced (10, 9);
        g.setColour (kNumberColours[i]);
        g.setFont (lcdFont (22.0f));
        g.drawText (juce::String (i + 1), in.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xfff1e7d1));
        g.setFont (spaced (condensedBold (15.0f), 0.04f));
        g.drawFittedText (heads[i], in.removeFromTop (20).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (juce::Colour (0xffcdbfa5));
        g.setFont (small (12.5f));
        g.drawFittedText (bodies[i], in.toNearestInt(), juce::Justification::topLeft, 3, 0.85f);
    }

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.14f));
    g.drawText ("YOUR SONGS", 16, 274, 160, 26, juce::Justification::centredLeft);
    static const char* sorts[3] = { "RECENT", "A-Z", "MOST LAYERS" };
    auto chip = sortChip();
    drawRaised (g, chip, Look::cream, false, 10.0f);
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (13.0f), 0.06f));
    g.drawFittedText (juce::String ("SORT: ") + sorts[sortBy], chip.withTrimmedBottom (3).reduced (6, 0).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}
