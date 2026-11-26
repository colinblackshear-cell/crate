#include "IdeasScreen.h"

#include "PocketFactory.h"
#include "core/ui/LanePeaks.h"
#include "PocketLoopTools.h"

using namespace pocket;

namespace
{
constexpr int kCardH = 90, kGap = 10, kMargin = 16, kLaneH = 16, kLaneGap = 5, kMaxLanes = 5;
const juce::Colour kOnDark { 0xfff3eadb }, kOnDarkDim { 0xffb7a9c2 }, kHeroRing { 0xfff7c04a };
const juce::Colour kNumberColours[3] = { juce::Colour (0xffa877f5), juce::Colour (0xfff59a2a), juce::Colour (0xfff27bb0) };

// The tiles under START A SONG FROM ANY TOOL: the tool, what the tile says, and MY VOICE (LOOPS on the mic).
struct TileInfo { Tool tool; const char* label; bool voice; };
// (the companion order, 2026-11-26: your voice first, then the quick ways in)
const TileInfo kTiles[] = { { Tool::loops, "My voice", true }, { Tool::drums, "A beat", false }, { Tool::pads, "Pads", false },
                            { Tool::synth, "A melody", false }, { Tool::sampler, "A sample", false }, { Tool::vocoder, "Vocoder", false },
                            { Tool::chords, "Chords", false }, { Tool::drone, "Drone", false } };
constexpr int kNumTiles = (int) (sizeof (kTiles) / sizeof (kTiles[0]));
constexpr int kTileW = 86, kTileH = 92, kTileGap = 8;

const char* const kFilterNames[5] = { "Recent", "Starred", "A-Z", "Most layers", "Sent to Mac" };

// A spark step's tool, as the phone draws it (a layer tool's icon and colour).
int layerToolFor (int sparkTool)
{
    using namespace crate::spark;
    switch (sparkTool)
    {
        case drums: return (int) LayerTool::drums;
        case pads: case sampler: return (int) LayerTool::pads;
        case synth: return (int) LayerTool::synth;
        case chords: return (int) LayerTool::chords;
        case vocoder: return (int) LayerTool::vocoder;
        case drone: return (int) LayerTool::drone;
        case voice: default: return (int) LayerTool::mic;
    }
}

std::vector<float> peaksOf (const juce::File& file, int bins) { return crate::lanes::peaksOf (file, bins); }

void drawPlayGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool playing)
{
    const auto centre = r.getCentre();
    g.setColour (c);
    if (playing)
        g.fillRoundedRectangle (centre.x - 7, centre.y - 7, 14, 14, 2.0f);
    else
    {
        juce::Path tri;
        tri.addTriangle (centre.x - 5, centre.y - 8, centre.x - 5, centre.y + 8, centre.x + 9, centre.y);
        g.fillPath (tri);
    }
}

void drawStar (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour)
{
    juce::Path star;
    star.addStar (c, 5, r * 0.45f, r, 0.0f);
    g.setColour (colour);
    g.fillPath (star);
}
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

IdeasScreen::Facts IdeasScreen::factsOf (const juce::File& song)
{
    Facts f;
    const auto loops = juce::JSON::parse (song.getChildFile ("Loops").getChildFile ("loops.json"));
    if (auto* layers = loops.getProperty ("layers", {}).getArray())
        for (auto& l : *layers)
        {
            ++f.layers;
            f.tools.push_back (l.hasProperty ("tool") ? (int) l.getProperty ("tool", 0) : toolForSource ((int) l.getProperty ("source", 0)));
            f.beats.push_back (juce::jmax (0.0, (double) l.getProperty ("beats", 4.0)));
            f.files.push_back (l.getProperty ("file", {}).toString());
        }
    const auto drums = juce::JSON::parse (song.getChildFile ("Beats").getChildFile ("drums.json"));
    f.bpm = f.layers > 0 ? (double) loops.getProperty ("bpm", 0.0) : 0.0;
    if (f.bpm <= 0.0)
        f.bpm = (double) drums.getProperty ("bpm", 0.0);
    const auto songFile = song.getChildFile ("song.json");
    if (songFile.existsAsFile())
    {
        const auto key = juce::JSON::parse (songFile).getProperty ("key", {});
        f.hasKey = key.isObject();
        f.key = SongKey::fromVar (key, {});
    }
    return f;
}

int IdeasScreen::lanePeaksRead() { return crate::lanes::filesRead().load(); }

//==============================================================================
// The songs, as cards. Its rows follow order() (the filter, the search and the sort).
struct IdeasScreen::List : public juce::Component
{
    explicit List (IdeasScreen& o) : owner (o) {}

    IdeasScreen& owner;
    int pressed = -1, pressedPlay = -1, pressedMore = -1;
    bool moved = false;
    juce::Point<float> downAt;
    pocket::HoldDetector hold { [this] { holdFired(); } };

    int count() const { return (int) owner.order().size(); }
    juce::Rectangle<float> card (int row) const { return { (float) kMargin, (float) (row * (kCardH + kGap)), (float) getWidth() - 2.0f * kMargin, (float) kCardH }; }
    juce::Rectangle<float> playButton (int row) const { auto c = card (row); return { c.getRight() - 62, c.getCentreY() - 24, 48, 48 }; }
    juce::Rectangle<float> moreButton (int row) const { auto c = card (row); return { c.getRight() - 112, c.getCentreY() - 22, 44, 44 }; }
    // What a finger on ··· may hit: the card's full height, from a little left of the dots to the play button (a near miss
    // used to open the song, and an open song couldn't be deleted)
    juce::Rectangle<float> moreHit (int row) const
    {
        auto c = card (row);
        const float left = moreButton (row).getX() - 14.0f;
        return { left, c.getY(), playButton (row).getX() - 2.0f - left, c.getHeight() };
    }
    int rowAt (juce::Point<float> p) const
    {
        for (int i = 0; i < count(); ++i)
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

            auto in = juce::Rectangle<float> (r.getX() + 14, r.getY() + 11, moreButton (row).getX() - r.getX() - 18, r.getHeight() - 22);
            auto nameRow = in.removeFromTop (24);
            g.setColour (col::ink);
            g.setFont (small (18.0f).boldened());
            const float nameW = juce::jmin (nameRow.getWidth() - (idea.starred ? 22.0f : 0.0f),
                                            juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), idea.name) + 2.0f);
            g.drawFittedText (idea.name, nameRow.withWidth (nameW).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            if (idea.starred)
                drawStar (g, { nameRow.getX() + nameW + 11.0f, nameRow.getCentreY() }, 7.5f, juce::Colour (0xffc9a54e));
            g.setColour (col::inkDim);
            g.setFont (small (13.5f));
            juce::String meta = whenText (idea.modified);
            if (f.bpm > 0.0)
                meta << U8(" \xc2\xb7 ") << juce::String (f.bpm, 0) << " BPM";
            meta << U8(" \xc2\xb7 ") << (f.layers == 0 ? juce::String ("no layers yet") : juce::String (f.layers) + (f.layers == 1 ? " layer" : " layers"));
            if (idea.sent)
                meta << U8(" \xc2\xb7 sent to Mac");
            g.drawFittedText (meta, in.removeFromTop (20).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            in.removeFromTop (6);
            // the layers, as a strip of their tools' colours, each as long as its layer
            auto strip = in.removeFromTop (6);
            if (f.layers == 0)
            {
                g.setColour (col::creamDeep);
                g.fillRoundedRectangle (strip, 3.0f);
            }
            else
            {
                double total = 0.0;
                for (auto b : f.beats)
                    total += juce::jlimit (1.0, 64.0, b);
                const float gaps = 3.0f * (float) (f.layers - 1);
                float x = strip.getX();
                for (int l = 0; l < f.layers; ++l)
                {
                    const float w = (strip.getWidth() - gaps) * (float) (juce::jlimit (1.0, 64.0, f.beats[(size_t) l]) / total);
                    g.setColour (layerToolColour (f.tools[(size_t) l]));
                    g.fillRoundedRectangle (x, strip.getY(), juce::jmax (3.0f, w), strip.getHeight(), 3.0f);
                    x += w + 3.0f;
                }
            }

            // ··· star / rename / delete
            auto mb = moreButton (row);
            g.setColour (pressedMore == row ? col::creamDeep : col::creamLight);
            g.fillRoundedRectangle (mb.reduced (2), 10.0f);
            g.setColour (col::inkDim);
            for (int k = -1; k <= 1; ++k)
                g.fillEllipse (mb.getCentreX() + (float) k * 8.0f - 2.5f, mb.getCentreY() - 2.5f, 5.0f, 5.0f);

            // play what it has so far
            auto pb = playButton (row);
            const bool playing = owner.isPlaying && owner.isPlaying (idea.id);
            g.setColour (playing ? col::red : col::green);
            g.fillEllipse (pb.translated (0, pressedPlay == row ? 1.0f : 0.0f));
            drawPlayGlyph (g, pb, playing ? juce::Colours::white : col::onBright, playing);
        }
        if (ord.empty())
        {
            g.setColour (col::inkDim);
            g.setFont (small (16.0f));
            const auto text = all.empty() ? juce::String ("No songs yet. Catch an idea, or + NEW SONG.")
                            : owner.search.isNotEmpty() ? "Nothing matches \"" + owner.search + "\"."
                            : owner.filter == 1 ? juce::String ("No starred songs yet. Tap a song's ··· to star it.")
                            : juce::String ("No songs sent to the Mac yet.");
            g.drawFittedText (text, getLocalBounds().reduced (24, 0).withHeight (60), juce::Justification::centred, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = rowAt (e.position);
        pressedPlay = pressed >= 0 && playButton (pressed).expanded (4).contains (e.position) ? pressed : -1;
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
        const auto id = ideaAt (row).id;
        if (playRow == row && playButton (row).expanded (4).contains (e.position))
        {
            if (owner.onPlay)
                owner.onPlay (id);
            return;
        }
        if (moreRow == row && moreHit (row).contains (e.position))
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
        if (pressed < 0 || pressed >= count())
            return;
        const int row = pressed;
        pressed = -1;
        repaint();
        showMenu (row, downAt);
    }
};

//==============================================================================
// START A SONG FROM ANY TOOL: the tiles, in a row that slides.
struct IdeasScreen::Tiles : public juce::Component
{
    explicit Tiles (IdeasScreen& o) : owner (o) { setSize (kNumTiles * (kTileW + kTileGap) - kTileGap + 2 * kMargin, kTileH + 4); }

    IdeasScreen& owner;
    int pressed = -1;
    bool moved = false;

    juce::Rectangle<float> tile (int i) const { return { (float) (kMargin + i * (kTileW + kTileGap)), 0.0f, (float) kTileW, (float) kTileH }; }
    int tileAt (juce::Point<float> p) const
    {
        for (int i = 0; i < kNumTiles; ++i)
            if (tile (i).contains (p))
                return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < kNumTiles; ++i)
        {
            auto r = tile (i).translated (0, pressed == i ? 1.0f : 0.0f);
            const auto colour = kTiles[i].voice ? layerToolColour ((int) LayerTool::mic) : toolColour (kTiles[i].tool);
            g.setColour (col::well);
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (colour);
            g.fillRoundedRectangle (r.withHeight (3.0f).reduced (10.0f, 0), 1.5f);
            const auto iconAt = juce::Point<float> (r.getCentreX(), r.getY() + 36.0f);
            if (kTiles[i].voice)
                drawLayerToolIcon (g, (int) LayerTool::mic, iconAt, colour);
            else
                drawToolIcon (g, kTiles[i].tool, iconAt, colour);
            g.setColour (kOnDark);
            g.setFont (spaced (condensedBold (14.0f), 0.06f));
            g.drawFittedText (juce::String (kTiles[i].label).toUpperCase(), r.removeFromBottom (30).toNearestInt(), juce::Justification::centredTop, 1, 0.8f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { pressed = tileAt (e.position); moved = false; repaint(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! moved && e.getDistanceFromDragStart() > 8)
        {
            moved = true;
            pressed = -1;
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = pressed;
        pressed = -1;
        repaint();
        if (i >= 0 && ! moved && tileAt (e.position) == i)
            owner.pressTile (i);
    }
};

//==============================================================================
// The whole page: what's painted, and where everything goes.
struct IdeasScreen::Content : public juce::Component
{
    explicit Content (IdeasScreen& o) : owner (o) {}

    IdeasScreen& owner;

    struct Layout
    {
        juce::Rectangle<int> title, buttons, caption, hero, lanes, heroButtons, spark, sparkTitle, sparkSteps, sparkButtons,
            toolsLabel, tiles, songsLabel, search, chips, list, how;
        int height = 0;
    };

    int laneCount() const { return juce::jmin (kMaxLanes, (int) owner.heroPeaks.size()); }

    Layout layout() const
    {
        Layout L;
        const int w = getWidth(), iw = w - 2 * kMargin;
        int y = 6;
        L.title = { kMargin, y, iw, 40 };              y += 46;
        L.buttons = { kMargin, y, iw, 86 };            y += 92;
        L.caption = { kMargin, y, iw, 20 };            y += 30;
        // Or start with a tool, right under CATCH AN IDEA (the companion: one tap from an idea to a layer).
        L.toolsLabel = { kMargin, y, iw, 18 };         y += 26;
        L.tiles = { 0, y, w, kTileH + 4 };             y += kTileH + 4 + 22;
        if (owner.hero() != nullptr)
        {
            const int lanes = laneCount();
            const int lanesH = lanes > 0 ? lanes * kLaneH + (lanes - 1) * kLaneGap + (owner.heroPeaks.size() > (size_t) kMaxLanes ? 18 : 0) : 22;
            const int h = 16 + 18 + 6 + 28 + 20 + 12 + lanesH + 14 + 52 + 16;
            L.hero = { kMargin, y, iw, h };
            L.lanes = { kMargin + 16, y + 16 + 18 + 6 + 28 + 20 + 12, iw - 32, lanesH };
            L.heroButtons = { kMargin + 16, L.hero.getBottom() - 16 - 52, iw - 32, 52 };
            y += h + 18;
        }
        L.songsLabel = { kMargin, y, iw, 18 };         y += 26;
        L.search = { kMargin, y, iw, 44 };             y += 54;
        L.chips = { 0, y, w, 36 };                     y += 48;
        const int rows = juce::jmax (1, (int) owner.order().size());
        L.list = { 0, y, w, rows * (kCardH + kGap) + (owner.order().empty() ? 0 : 0) };
        if (owner.order().empty())
            L.list.setHeight (70);
        y = L.list.getBottom() + 14;
        {
            const int h = 16 + 30 + 2 + 58 + 6 + 3 * 34 + 10 + 48 + 8 + 36 + 14;
            L.spark = { kMargin, y, iw, h };
            L.sparkTitle = { kMargin + 16, y + 16 + 30 + 2, iw - 32, 58 };
            L.sparkSteps = { kMargin + 16, L.sparkTitle.getBottom() + 6, iw - 32, 3 * 34 };
            L.sparkButtons = { kMargin + 16, L.sparkSteps.getBottom() + 10, iw - 32, 48 };
            y += h + 20;
        }
        L.how = { kMargin, y, iw, owner.howOpen ? 14 + 24 + 10 + 28 + 12 + 44 + 14 : 14 + 24 + 12 + 44 + 14 };
        y += L.how.getHeight() + 28;
        L.height = y;
        return L;
    }

    void resized() override
    {
        const auto L = layout();
        owner.catchIdea.setBounds (L.buttons.withWidth ((L.buttons.getWidth() - 10) / 2));
        owner.newButton.setBounds (L.buttons.withTrimmedLeft ((L.buttons.getWidth() + 10) / 2));
        const bool hasHero = ! L.hero.isEmpty();
        owner.playCurrent.setVisible (hasHero);
        owner.openCurrent.setVisible (hasHero);
        if (hasHero)
        {
            owner.playCurrent.setBounds (L.heroButtons.withWidth (52));
            owner.openCurrent.setBounds (L.heroButtons.withTrimmedLeft (62));
        }
        owner.rollButton.setBounds (L.spark.getRight() - 16 - 112, L.spark.getY() + 10, 112, 40);
        owner.startSpark.setBounds (L.sparkButtons);
        owner.tileView.setBounds (L.tiles);
        owner.searchBox.setBounds (L.search);
        int x = kMargin;
        for (size_t i = 0; i < owner.chips.size(); ++i)
        {
            const int cw = (int) juce::GlyphArrangement::getStringWidth (spaced (condensed (14.0f)), juce::String (kFilterNames[i]).toUpperCase()) + 28;
            owner.chips[i]->setBounds (x, L.chips.getY(), cw, 36);
            x += cw + 6;
        }
        owner.list->setBounds (L.list);
        auto how = L.how.reduced (16, 14);
        owner.howToggle.setBounds (how.removeFromTop (24).removeFromRight (110).expanded (0, 8));
        auto bottom = how.removeFromBottom (44);
        owner.lessonButton.setBounds (bottom.withWidth ((bottom.getWidth() - 8) / 2));
        owner.tourButton.setBounds (bottom.withTrimmedLeft ((bottom.getWidth() + 8) / 2));
    }

    void paint (juce::Graphics& g) override
    {
        const auto L = layout();

        // MY IDEAS, and this week
        g.setColour (col::ink);
        g.setFont (fraunces (34.0f).italicised());
        g.drawText ("My ideas", L.title, juce::Justification::centredLeft);
        int week = 0, layers = 0;
        const auto weekAgo = juce::Time::getCurrentTime() - juce::RelativeTime::days (7);
        for (auto& i : owner.ideas.list())
        {
            week += i.modified > weekAgo ? 1 : 0;
            if (auto it = owner.facts.find (i.id); it != owner.facts.end())
                layers += it->second.layers;
        }
        g.setColour (col::inkDim);
        g.setFont (lcdFont (19.0f));
        g.drawText (juce::String (week) + U8(" THIS WEEK \xc2\xb7 ") + juce::String (layers) + (layers == 1 ? " LAYER" : " LAYERS"), L.title, juce::Justification::centredRight);

        g.setFont (small (13.5f));
        g.drawFittedText ("Catch an idea: tap, sing or play, and it's layer 1 of a new idea.", L.caption, juce::Justification::centred, 1, 0.85f);

        if (auto* idea = owner.hero(); idea != nullptr && ! L.hero.isEmpty())
            paintHero (g, L, *idea);
        paintSpark (g, L);

        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (13.0f), 0.14f));
        g.drawText ("OR START WITH", L.toolsLabel, juce::Justification::centredLeft);
        g.drawText (U8("YOUR SONGS \xc2\xb7 ") + juce::String ((int) owner.ideas.list().size()), L.songsLabel, juce::Justification::centredLeft);

        // HOW IT WORKS
        g.setColour (col::well);
        g.fillRoundedRectangle (L.how.toFloat(), 16.0f);
        auto how = L.how.reduced (16, 14);
        g.setColour (juce::Colour (0xffe4dbee));
        g.setFont (spaced (condensedBold (13.5f), 0.14f));
        g.drawText ("HOW IT WORKS", how.removeFromTop (24), juce::Justification::centredLeft);
        if (owner.howOpen)
        {
            how.removeFromTop (10);
            auto steps = how.removeFromTop (28);
            static const char* heads[3] = { "PICK A TOOL", "PRESS REC", "IT LOOPS" };
            const int sw = steps.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto s = steps.removeFromLeft (sw);
                g.setColour (kNumberColours[i]);
                g.setFont (lcdFont (22.0f));
                g.drawText (juce::String (i + 1), s.removeFromLeft (16), juce::Justification::centredLeft);
                g.setColour (kOnDark);
                g.setFont (spaced (condensedBold (15.0f), 0.04f));
                g.drawFittedText (heads[i], s, juce::Justification::centredLeft, 1, 0.8f);
            }
        }
    }

    void paintHero (juce::Graphics& g, const Layout& L, const IdeaInfo& idea)
    {
        const auto it = owner.facts.find (idea.id);
        const Facts f = it != owner.facts.end() ? it->second : Facts();
        auto r = L.hero.toFloat();
        g.setColour (kHeroRing);
        g.fillRoundedRectangle (r.expanded (2.0f), 20.0f);
        g.setColour (col::well);
        g.fillRoundedRectangle (r, 18.0f);
        auto in = L.hero.reduced (16, 16);
        auto top = in.removeFromTop (18);
        g.setColour (kOnDarkDim);
        g.setFont (spaced (condensedBold (12.5f), 0.14f));
        g.drawText ("PICK UP WHERE YOU LEFT OFF", top, juce::Justification::centredLeft);
        juce::String lcd;
        if (f.bpm > 0.0)
            lcd << juce::String (f.bpm, 0) << " BPM";
        if (f.hasKey)
            lcd << (lcd.isEmpty() ? "" : U8(" \xc2\xb7 ")) << f.key.label();
        g.setColour (col::lcdAmber);
        g.setFont (lcdFont (17.0f));
        g.drawText (lcd, top, juce::Justification::centredRight);
        in.removeFromTop (6);
        g.setColour (kOnDark);
        g.setFont (small (24.0f).boldened());
        g.drawFittedText (idea.name, in.removeFromTop (28), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (kOnDarkDim);
        g.setFont (small (13.5f));
        juce::String meta = whenText (idea.modified) + U8(" \xc2\xb7 ")
                          + (f.layers == 0 ? juce::String ("no layers yet") : juce::String (f.layers) + (f.layers == 1 ? " layer" : " layers"));
        if (f.layers > 0)
            meta << U8(" \xc2\xb7 last: ") << juce::String (layerToolName (f.tools.back())).toLowerCase() << " on layer " << f.layers;
        g.drawFittedText (meta, in.removeFromTop (20), juce::Justification::centredLeft, 1, 0.85f);

        // the layers, as lanes: their real sound
        auto lanes = L.lanes;
        if (laneCount() == 0)
        {
            g.setColour (kOnDarkDim);
            g.setFont (small (14.0f));
            g.drawText ("No layers yet: pick a tool and press REC.", lanes, juce::Justification::centredLeft);
            return;
        }
        for (int l = 0; l < laneCount(); ++l)
        {
            auto lane = lanes.removeFromTop (kLaneH).toFloat();
            lanes.removeFromTop (kLaneGap);
            const auto colour = layerToolColour (f.tools[(size_t) l]);
            g.setColour (kOnDarkDim);
            g.setFont (lcdFont (15.0f));
            g.drawText (juce::String (l + 1), lane.removeFromLeft (16), juce::Justification::centredLeft);
            lane.removeFromLeft (4);
            g.setColour (juce::Colour (0xff221b2d));
            g.fillRoundedRectangle (lane, 4.0f);
            crate::lanes::drawPeaks (g, owner.heroPeaks[(size_t) l], lane, colour);
        }
        if (owner.heroPeaks.size() > (size_t) kMaxLanes)
        {
            g.setColour (kOnDarkDim);
            g.setFont (small (13.0f));
            g.drawText ("+ " + juce::String ((int) owner.heroPeaks.size() - kMaxLanes) + " more", lanes.withTrimmedLeft (20), juce::Justification::bottomLeft);
        }
    }

    void paintSpark (juce::Graphics& g, const Layout& L)
    {
        const auto& s = owner.sparkNow;
        auto r = L.spark.toFloat();
        g.setColour (col::creamLight);
        g.fillRoundedRectangle (r, 18.0f);
        g.setColour (col::rule);
        g.drawRoundedRectangle (r.reduced (1.0f), 18.0f, 2.0f);
        auto in = L.spark.reduced (16, 16);
        auto top = in.removeFromTop (30);
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (12.5f), 0.14f));
        g.drawText (owner.sparkRolls == 0 ? U8("TONIGHT'S SPARK \xc2\xb7 ") + s.mood : U8("ANOTHER SPARK \xc2\xb7 ") + s.mood, top, juce::Justification::centredLeft);
        g.setColour (col::ink);
        g.setFont (fraunces (25.0f).italicised());
        g.drawFittedText (s.title, L.sparkTitle, juce::Justification::centredLeft, 2, 0.9f);
        auto steps = L.sparkSteps;
        for (auto& step : s.steps)
        {
            auto row = steps.removeFromTop (34);
            auto box = row.removeFromLeft (28).withSizeKeepingCentre (28, 28).toFloat();
            g.setColour (col::well);
            g.fillRoundedRectangle (box, 7.0f);
            {
                juce::Graphics::ScopedSaveState save (g);
                g.addTransform (juce::AffineTransform::scale (0.7f, 0.7f, box.getCentreX(), box.getCentreY()));
                const int lt = layerToolFor (step.tool);
                drawLayerToolIcon (g, lt, box.getCentre(), step.tool == crate::spark::sampler ? toolColour (Tool::sampler) : layerToolColour (lt));
            }
            row.removeFromLeft (10);
            g.setColour (col::ink);
            g.setFont (small (15.0f));
            g.drawFittedText (step.text, row, juce::Justification::centredLeft, 1, 0.8f);
        }
        auto note = L.sparkButtons.withY (L.sparkButtons.getBottom() + 8).withHeight (36);
        g.setColour (col::inkDim);
        g.setFont (small (13.0f));
        static const char* const opens[8] = { "DRUMS", "PADS", "the SAMPLER", "SYNTH", "CHORDS", "the VOCODER", "LOOPS on the mic", "DRONE" };
        const auto& kits = factoryKits();
        const juce::String kit = juce::isPositiveAndBelow (s.kit, (int) kits.size()) ? juce::String (kits[(size_t) s.kit].name) : juce::String ("factory");
        g.drawFittedText ("Sets " + juce::String (s.bpm) + " BPM and " + s.keyName() + ", loads the " + kit + " and opens "
                          + opens[juce::jlimit (0, 7, s.steps[0].tool)] + ". Every step stays yours to change.",
                          note, juce::Justification::topLeft, 2, 0.9f);
    }
};

//==============================================================================
IdeasScreen::IdeasScreen (Ideas& i) : ideas (i)
{
    howOpen = ! ideas.homeFlag ("howItWorksHidden", false);
    sparkNow = crate::spark::make (crate::spark::tonight());

    content = std::make_unique<Content> (*this);
    list = std::make_unique<List> (*this);
    tiles = std::make_unique<Tiles> (*this);
    view.setViewedComponent (content.get(), false);
    view.setScrollBarsShown (false, false);
    view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (view);
    tileView.setViewedComponent (tiles.get(), false);
    tileView.setScrollBarsShown (false, false, false, true);
    tileView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);

    catchIdea.setFontHeight (19.0f);
    newButton.setFontHeight (19.0f);
    catchIdea.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        auto icon = r.removeFromTop (r.getHeight() * 0.55f);
        const auto c = juce::Point<float> (icon.getCentreX(), icon.getBottom() - 16.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (c.x - 16, c.y - 16, 32, 32);
        drawLayerToolIcon (g, (int) LayerTool::mic, c, col::red);
        g.setColour (ink);
        g.setFont (spaced (condensed (19.0f)));
        g.drawText (catchIdea.isToggled() ? "" : "CATCH AN IDEA", r, juce::Justification::centred);
    };
    newButton.glyph = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        auto icon = r.removeFromTop (r.getHeight() * 0.55f);
        g.setColour (ink);
        g.setFont (condensedBold (40.0f));
        g.drawText ("+", icon.withTrimmedBottom (-6), juce::Justification::centredBottom);
        g.setFont (spaced (condensed (19.0f)));
        g.drawText ("NEW SONG", r, juce::Justification::centred);
    };
    playCurrent.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        auto* h = hero();
        drawPlayGlyph (g, r, ink, h != nullptr && isPlaying && isPlaying (h->id));
    };
    openCurrent.setFontHeight (18.0f);
    startSpark.setFontHeight (17.0f);
    rollButton.setFontHeight (14.0f);
    lessonButton.setFontHeight (14.0f);
    tourButton.setFontHeight (14.0f);
    rollButton.glyph = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        auto body = r.reduced (2.0f, 3.0f);
        g.setColour (col::creamDeep);
        g.fillRoundedRectangle (body, 9.0f);
        auto die = body.removeFromLeft (body.getHeight()).withSizeKeepingCentre (16, 16);
        g.setColour (ink);
        g.drawRoundedRectangle (die.reduced (1.0f), 3.0f, 1.6f);
        for (auto p : { juce::Point<float> (-3.5f, -3.5f), juce::Point<float> (0, 0), juce::Point<float> (3.5f, 3.5f) })
            g.fillEllipse (die.getCentreX() + p.x - 1.3f, die.getCentreY() + p.y - 1.3f, 2.6f, 2.6f);
        g.setFont (spaced (condensed (14.0f)));
        g.drawText ("ROLL ANOTHER", body, juce::Justification::centred);
    };
    howToggle.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        g.setColour (kOnDarkDim);
        g.setFont (spaced (condensedBold (12.5f), 0.08f));
        g.drawText (howOpen ? "TAP TO HIDE" : "SHOW", r, juce::Justification::centredRight);
    };
    howToggle.setLook (Look::ghost);

    catchIdea.onClick = [this] { if (onCatch) onCatch(); };
    newButton.onClick = [this] { if (onNew) onNew(); };
    openCurrent.onClick = [this] { if (auto* h = hero(); h != nullptr && onOpen) onOpen (h->id); };
    playCurrent.onClick = [this] { if (auto* h = hero(); h != nullptr && onPlay) onPlay (h->id); };
    startSpark.onClick = [this] { if (onSpark) onSpark (sparkNow); };
    rollButton.onClick = [this] { rollSpark(); };
    lessonButton.onClick = [this] { if (onNewBeat) onNewBeat(); };
    tourButton.onClick = [this] { if (onTour) onTour(); };
    howToggle.onClick = [this] { setHowItWorksOpen (! howOpen); };

    searchBox.setTextToShowWhenEmpty ("Find a song, a tool or a tempo", col::inkFaint);
    searchBox.setFont (small (15.0f));
    searchBox.setIndents (40, 13);
    searchBox.setColour (juce::TextEditor::backgroundColourId, col::creamLight);
    searchBox.setColour (juce::TextEditor::outlineColourId, col::rule);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, col::amber);
    searchBox.setColour (juce::TextEditor::textColourId, col::ink);
    searchBox.setColour (juce::TextEditor::highlightColourId, col::amber.withAlpha (0.4f));
    searchBox.setColour (juce::CaretComponent::caretColourId, col::ink);
    searchBox.setTitle ("Search songs");
    searchBox.onTextChange = [this] { setSearch (searchBox.getText()); };
    searchBox.onReturnKey = [this] { searchBox.unfocusAllComponents(); };

    for (int f = 0; f < 5; ++f)
    {
        auto chip = std::make_unique<PButton> (kFilterNames[f], f == 0 ? Look::dark : Look::cream);
        chip->setFontHeight (14.0f);
        chip->onClick = [this, f] { setFilter (f); };
        content->addAndMakeVisible (*chip);
        chips.push_back (std::move (chip));
    }
    for (juce::Component* c : std::initializer_list<juce::Component*> { &catchIdea, &newButton, &playCurrent, &openCurrent, &rollButton, &startSpark,
                                                                       &tileView, &searchBox, list.get(), &howToggle, &lessonButton, &tourButton })
        content->addAndMakeVisible (*c);
}

IdeasScreen::~IdeasScreen() = default;

const IdeaInfo* IdeasScreen::hero() const
{
    if (auto* open = ideas.find (ideas.current().id))
        return open;
    return ideas.list().empty() ? nullptr : &ideas.list().front();
}

std::vector<int> IdeasScreen::order() const
{
    const auto& all = ideas.list();
    std::vector<int> o;
    const auto needle = search.trim().toLowerCase();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& idea = all[(size_t) i];
        if (filter == 1 && ! idea.starred) continue;
        if (filter == 4 && ! idea.sent) continue;
        if (needle.isNotEmpty())
        {
            bool hit = idea.name.toLowerCase().contains (needle);
            if (auto it = facts.find (idea.id); ! hit && it != facts.end())
            {
                hit = it->second.bpm > 0.0 && juce::String (it->second.bpm, 0) == needle;
                for (int t : it->second.tools)
                    hit = hit || juce::String (layerToolName (t)).toLowerCase().contains (needle);
                hit = hit || (it->second.hasKey && it->second.key.label().toLowerCase().contains (needle));
            }
            if (! hit) continue;
        }
        o.push_back (i);   // the list is newest first already
    }
    if (filter == 2)
        std::stable_sort (o.begin(), o.end(), [&all] (int a, int b) { return all[(size_t) a].name.compareNatural (all[(size_t) b].name) < 0; });
    else if (filter == 3)
        std::stable_sort (o.begin(), o.end(), [this, &all] (int a, int b)
        {
            const auto fa = facts.find (all[(size_t) a].id), fb = facts.find (all[(size_t) b].id);
            return (fa != facts.end() ? fa->second.layers : 0) > (fb != facts.end() ? fb->second.layers : 0);
        });
    return o;
}

void IdeasScreen::holdRow (int row)
{
    if (juce::isPositiveAndBelow (row, rows()))
        list->showMenu (row, list->card (row).getCentre());
}

juce::String IdeasScreen::songIdAtRow (int row) const
{
    const auto o = order();
    return juce::isPositiveAndBelow (row, (int) o.size()) ? ideas.list()[(size_t) o[(size_t) row]].id : juce::String();
}

juce::Rectangle<int> IdeasScreen::tourSpot() const
{
    return getLocalArea (content.get(), newButton.getBounds());
}

void IdeasScreen::rollSpark()
{
    ++sparkRolls;
    sparkNow = crate::spark::make (crate::spark::tonight() + sparkRolls);
    content->repaint();
}

void IdeasScreen::setFilter (int f)
{
    filter = juce::jlimit (0, 4, f);
    for (size_t i = 0; i < chips.size(); ++i)
        chips[i]->setLook ((int) i == filter ? Look::dark : Look::cream);
    resized();
    content->repaint();
    list->repaint();
}

void IdeasScreen::setSearch (const juce::String& s)
{
    if (s == search)
        return;
    search = s;
    if (searchBox.getText() != s)
        searchBox.setText (s, false);
    resized();
    content->repaint();
    list->repaint();
}

void IdeasScreen::setHowItWorksOpen (bool open)
{
    howOpen = open;
    ideas.setHomeFlag ("howItWorksHidden", ! open);
    resized();
    content->repaint();
}

void IdeasScreen::scrollTo (int y)
{
    view.setViewPosition (0, juce::jlimit (0, juce::jmax (0, content->getHeight() - view.getHeight()), y));
}

int IdeasScreen::contentHeight() const { return content->getHeight(); }

void IdeasScreen::pressTile (int tile)
{
    if (juce::isPositiveAndBelow (tile, kNumTiles) && onStartWith)
        onStartWith (kTiles[tile].tool, kTiles[tile].voice);
}

void IdeasScreen::refresh()
{
    facts.clear();
    for (auto& idea : ideas.list())
        facts[idea.id] = factsOf (ideas.folderFor (idea.id));
    heroPeaks.clear();
    if (auto* h = hero())
    {
        const auto& f = facts[h->id];
        const auto loops = ideas.folderFor (h->id).getChildFile ("Loops");
        for (int l = 0; l < f.layers; ++l)
            heroPeaks.push_back (l < kMaxLanes && f.files[(size_t) l].isNotEmpty() ? peaksOf (loops.getChildFile (f.files[(size_t) l]), 72)
                                                                                   : std::vector<float>());
    }
    resized();
    content->repaint();
    list->repaint();
    playCurrent.repaint();
}

void IdeasScreen::resized()
{
    const int w = getWidth();
    view.setBounds (getLocalBounds());
    content->setSize (w, 10);   // (the layout reads the width)
    content->setSize (w, juce::jmax (getHeight(), content->layout().height));
    content->resized();
}

void IdeasScreen::paint (juce::Graphics&) {}
