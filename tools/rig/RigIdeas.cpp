#include "RigIdeas.h"

#include "RigColours.h"
#include "RigLooper.h"
#include "Vintage.h"
#include "core/ui/LanePeaks.h"
#include "core/ui/ToolIcons.h"
#include "RigShowFiles.h"

#include "FactoryKits.h"

namespace
{
const juce::Colour kPaper { 0xffece3d0 }, kInk { 0xff231d17 }, kDim { 0xff6b5e50 }, kCard { 0xfff6efe1 }, kRule { 0xffd4c7ae },
    kWell { 0xff1c1814 }, kOnDark { 0xfff1e7d1 }, kOnDarkDim { 0xffb8aa92 }, kAmber { 0xfff2b632 }, kRed { 0xffd8452f }, kLane { 0xff2b231d };
const juce::Colour kNumberColours[3] = { juce::Colour (0xff5fb3c4), juce::Colour (0xfff0674f), juce::Colour (0xff82cf6a) };
constexpr int kMargin = 40, kColGap = 28, kTop = 20, kTitleH = 56, kHeroH = 244, kRow2H = 204, kTileH = 84, kHowH = 60,
              kCardH = 80, kCardGap = 10, kMaxLanes = 5;

juce::String u8 (const char* t) { return juce::String (juce::CharPointer_UTF8 (t)); }
const juce::String dot = u8 (" \xc2\xb7 ");

// START A SONG FROM ANY TOOL, on the Mac: what each tile says, its picture and colour (the rail's).
struct TileInfo { const char* label; crate::toolicons::Tool icon; juce::uint32 colour; };
const TileInfo kTileInfo[RigIdeas::kTiles] = {
    { "A beat", crate::toolicons::Tool::drums, 0xffd8452c }, { "Pads", crate::toolicons::Tool::pads, 0xffe85a3a },
    { "A melody", crate::toolicons::Tool::synth, 0xffefcb45 }, { "Chords", crate::toolicons::Tool::chords, 0xffb9a3dc },
    { "My voice", crate::toolicons::Tool::vocoder, 0xff82cf6a }, { "A sample", crate::toolicons::Tool::sampler, 0xffc58fd6 },
    { "A bass line", crate::toolicons::Tool::bass, 0xfff39b33 }, { "Line in", crate::toolicons::Tool::lineIn, 0xffd8c49a } };

const char* const kFilterNames[4] = { "Recent", "Starred", "A-Z", "Most layers" };

crate::toolicons::Tool iconForSparkTool (int t)
{
    using namespace crate::spark;
    using I = crate::toolicons::Tool;
    switch (t)
    {
        case drums: return I::drums;
        case pads: return I::pads;
        case sampler: return I::sampler;
        case synth: return I::synth;
        case chords: return I::chords;
        case vocoder: return I::vocoder;
        case drone: return I::drone;
        case voice: default: return I::vocoder;
    }
}
juce::Colour colourForSparkTool (int t)
{
    using namespace crate::spark;
    switch (t)
    {
        case drums: return RigColours::drums;
        case pads: return RigColours::mpc;
        case sampler: return RigColours::sampler;
        case synth: return RigColours::synth;
        case chords: return juce::Colour (0xffb9a3dc);
        case vocoder: return RigColours::vocoder;
        case drone: return RigColours::drone;
        case voice: default: return juce::Colour (0xff82cf6a);
    }
}

void playGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool playing)
{
    const auto p = r.getCentre();
    g.setColour (c);
    if (playing)
        g.fillRoundedRectangle (p.x - 7, p.y - 7, 14, 14, 2.0f);
    else
    {
        juce::Path tri;
        tri.addTriangle (p.x - 5, p.y - 8, p.x - 5, p.y + 8, p.x + 9, p.y);
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

//==============================================================================
// A button in the page's look: amber, red, dark, cream or a flat chip, with text or a drawn face.
class RigIdeas::Btn : public juce::Button
{
public:
    enum Style { amber, red, dark, cream, chipOn, chipOff, ghost };
    Btn (const juce::String& text, Style s, float fontSize = 15.0f) : juce::Button (text), style (s), size (fontSize)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
    }

    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour ink)> face;
    void setStyle (Style s) { style = s; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        juce::Colour top, bottom, edge, ink;
        float radius = 10.0f;
        switch (style)
        {
            case amber:   top = juce::Colour (0xfffcca64); bottom = juce::Colour (0xffeea02a); edge = juce::Colour (0xffb07f12); ink = kInk; break;
            case red:     top = juce::Colour (0xfff07a64); bottom = kRed; edge = juce::Colour (0xff8d2a1c); ink = juce::Colours::white; break;
            case dark:    top = juce::Colour (0xff3d362e); bottom = juce::Colour (0xff27221c); edge = juce::Colour (0xff0e0b09); ink = kOnDark; break;
            case chipOn:  top = bottom = kInk; edge = juce::Colours::transparentBlack; ink = kOnDark; radius = 8.0f; break;
            case chipOff: top = bottom = kCard; edge = juce::Colour (0xffcdbfa6); ink = kInk; radius = 8.0f; break;
            case ghost:   top = bottom = juce::Colours::transparentBlack; edge = juce::Colours::transparentBlack; ink = kOnDarkDim; break;
            case cream:   top = juce::Colour (0xfff8f2e4); bottom = juce::Colour (0xffe6dcc4); edge = juce::Colour (0xffcdbfa6); ink = kInk; break;
        }
        const float lift = style == ghost || style == chipOn ? 0.0f : (down ? 1.0f : 3.0f);
        auto body = r.withTrimmedBottom (style == ghost || style == chipOn ? 0.0f : 3.0f).translated (0, down && lift > 0.0f ? 2.0f : 0.0f);
        if (lift > 0.0f)
        {
            g.setColour (edge);
            g.fillRoundedRectangle (body.translated (0, lift), radius);
        }
        g.setGradientFill (juce::ColourGradient (top.brighter (over && style != ghost ? 0.05f : 0.0f), 0, body.getY(), bottom, 0, body.getBottom(), false));
        g.fillRoundedRectangle (body, radius);
        if (face)
            return face (g, body, ink);
        g.setColour (ink);
        g.setFont (vintage::cond (size, vintage::Weight::bold, 0.08f));
        g.drawFittedText (getButtonText().toUpperCase(), body.toNearestInt().reduced (8, 0), juce::Justification::centred, 1, 0.8f);
    }

private:
    Style style;
    float size;
};

//==============================================================================
RigIdeas::Song RigIdeas::read (const juce::File& file)
{
    Song s;
    s.file = file;
    s.name = file.getFileNameWithoutExtension();
    s.modified = file.getLastModificationTime();
    s.lastShow = s.name == "Last show";
    if (s.lastShow)
        s.name = "Last show (auto)";
    // Only what the list needs: the top element's tempo and its Loop and Scene children, read without the rest.
    juce::XmlDocument doc (file);
    auto xml = doc.getDocumentElement();
    if (xml == nullptr)
        return s;
    s.bpm = xml->getDoubleAttribute ("bpm", 0.0);
    s.revision = xml->getStringAttribute (rigshow::kRevisionAttribute);
    for (auto* child : xml->getChildIterator())
    {
        if (child->hasTagName ("Loop") && child->hasAttribute ("beats") && child->getDoubleAttribute ("beats") > 0.0)
        {
            s.sources.push_back (child->getIntAttribute ("source", RigLooper::mix));
            s.beats.push_back (child->getDoubleAttribute ("beats"));
            s.indexes.push_back (child->getIntAttribute ("index", (int) s.indexes.size()));
        }
        else if (child->hasTagName ("Scene"))
            ++s.sections;
    }
    return s;
}

juce::File RigIdeas::loopFile (const Song& s, int layer)
{
    if (! juce::isPositiveAndBelow (layer, (int) s.indexes.size()))
        return {};
    const auto name = "Loop " + juce::String (s.indexes[(size_t) layer] + 1) + ".wav";
    auto folder = rigshow::currentFolder (s.file);
    if (s.revision.isNotEmpty() && ! rigshow::holdsRevision (folder, s.revision))
        folder = rigshow::revisionFolder (s.file, s.revision);
    return folder.getChildFile (name);
}

namespace
{
// A show's loop folders: "<show> loops", and its saved revisions and staging folders ("<show> loops.r-...").
juce::Array<juce::File> loopFoldersOf (const juce::File& show)
{
    juce::Array<juce::File> out;
    const auto base = rigshow::currentFolder (show).getFileName();
    for (auto& f : show.getParentDirectory().findChildFiles (juce::File::findDirectories, false))
        if (f.getFileName() == base || f.getFileName().startsWith (base + "."))
            out.add (f);
    return out;
}
}

juce::String RigIdeas::renameSong (const juce::File& show, const juce::String& newName, juce::File& renamed)
{
    const auto name = juce::File::createLegalFileName (newName.trim());
    if (name.isEmpty())
        return "Give it a name.";
    if (name == "Last show")
        return "\"Last show\" is the one Kaleidobox keeps for you. Pick another name.";
    const auto target = show.getSiblingFile (name + ".crateshow");
    if (target == show)
        return {};
    if (target.exists())
        return "There's already a song called " + name + ".";
    const auto oldBase = rigshow::currentFolder (show).getFileName(), newBase = rigshow::currentFolder (target).getFileName();
    std::vector<std::pair<juce::File, juce::File>> moves;
    for (auto& f : loopFoldersOf (show))
    {
        const auto to = f.getSiblingFile (newBase + f.getFileName().substring (oldBase.length()));
        if (to.exists())
            return "Its loops can't move: " + to.getFileName() + " is already there.";
        moves.push_back ({ f, to });
    }
    for (size_t i = 0; i < moves.size(); ++i)
        if (! moves[i].first.moveFileTo (moves[i].second))
        {
            for (size_t j = 0; j < i; ++j)   // put back the ones that moved
                moves[j].second.moveFileTo (moves[j].first);
            return "Its loops folder couldn't be renamed.";
        }
    if (! show.moveFileTo (target))
    {
        for (auto& m : moves)
            m.second.moveFileTo (m.first);
        return "The song couldn't be renamed.";
    }
    renamed = target;
    return {};
}

bool RigIdeas::trashSong (const juce::File& show)
{
    bool ok = true;
    for (auto& f : loopFoldersOf (show))
        ok = f.moveToTrash() && ok;
    return show.moveToTrash() && ok;
}

juce::String RigIdeas::whenText (juce::Time t)
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

juce::Colour RigIdeas::colourForSource (int source)
{
    switch (source)
    {
        case RigLooper::input:   return juce::Colour (0xff82cf6a);
        case RigLooper::vocoder: return RigColours::vocoder;
        case RigLooper::bass:    return RigColours::bass;
        case RigLooper::synth:   return RigColours::synth;
        case RigLooper::mpc:     return RigColours::mpc;
        case RigLooper::sampler: return RigColours::sampler;
        case RigLooper::line:    return RigColours::input;
        case RigLooper::drums:   return RigColours::drums;
        case RigLooper::drone:   return RigColours::drone;
        case RigLooper::mix: default: return RigColours::master;
    }
}

juce::String RigIdeas::nameForSource (int source)
{
    switch (source)
    {
        case RigLooper::input:   return "VOICE";
        case RigLooper::vocoder: return "VOCODER";
        case RigLooper::bass:    return "BASS";
        case RigLooper::synth:   return "SYNTH";
        case RigLooper::mpc:     return "PADS";
        case RigLooper::sampler: return "SAMPLER";
        case RigLooper::line:    return "LINE IN";
        case RigLooper::drums:   return "DRUMS";
        case RigLooper::drone:   return "DRONE";
        case RigLooper::mix: default: return "MIX";
    }
}

//==============================================================================
// YOUR SONGS: the cards, following order().
struct RigIdeas::List : public juce::Component
{
    explicit List (RigIdeas& o) : owner (o) {}
    RigIdeas& owner;
    int pressed = -1, over = -1;

    juce::Rectangle<float> card (int row) const { return { 0.0f, (float) (row * (kCardH + kCardGap)), (float) getWidth(), (float) kCardH }; }
    juce::Rectangle<float> playButton (int row) const { auto c = card (row); return { c.getX() + 14, c.getCentreY() - 23, 46, 46 }; }
    juce::Rectangle<float> moreButton (int row) const { auto c = card (row); return { c.getRight() - 54, c.getCentreY() - 20, 44, 40 }; }
    int rowAt (juce::Point<float> p) const
    {
        const auto o = owner.order();
        for (int i = 0; i < (int) o.size(); ++i)
            if (card (i).contains (p))
                return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto o = owner.order();
        for (int row = 0; row < (int) o.size(); ++row)
        {
            const auto& s = owner.all[(size_t) o[(size_t) row]];
            auto r = card (row);
            const bool open = owner.hero.open && owner.hero.file == s.file;
            g.setColour (kCard);
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (open ? kAmber : kRule);
            g.drawRoundedRectangle (r.reduced (1.0f), 14.0f, open ? 2.5f : 1.5f);
            if (row == over || row == pressed)
            {
                g.setColour (kInk.withAlpha (row == pressed ? 0.07f : 0.03f));
                g.fillRoundedRectangle (r, 14.0f);
            }
            const auto pb = playButton (row);
            const bool playing = owner.isPlaying && owner.isPlaying (s.file);
            g.setColour (playing ? kRed : kInk);
            g.fillEllipse (pb);
            playGlyph (g, pb, juce::Colours::white, playing);

            auto in = juce::Rectangle<float> (pb.getRight() + 14, r.getY() + 12, moreButton (row).getX() - pb.getRight() - 22, r.getHeight() - 24);
            auto nameRow = in.removeFromTop (24);
            g.setColour (kInk);
            g.setFont (vintage::barlow (18.5f).boldened());
            const float nameW = juce::jmin (nameRow.getWidth() - (s.starred ? 24.0f : 0.0f), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s.name) + 2.0f);
            g.drawFittedText (s.name, nameRow.withWidth (nameW).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            if (s.starred)
                drawStar (g, { nameRow.getX() + nameW + 12.0f, nameRow.getCentreY() }, 8.0f, juce::Colour (0xffc98a00));
            g.setColour (kDim);
            g.setFont (vintage::barlow (14.0f));
            juce::String meta = whenText (s.modified);
            if (s.bpm > 0.0)
                meta << dot << juce::String (s.bpm, 0) << " BPM";
            meta << dot << (s.layers() == 0 ? juce::String ("no layers yet") : juce::String (s.layers()) + (s.layers() == 1 ? " layer" : " layers"));
            if (s.sections > 0)
                meta << dot << s.sections << (s.sections == 1 ? " section" : " sections");
            if (open)
                meta << dot << "open now";
            g.drawFittedText (meta, in.removeFromTop (19).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
            in.removeFromTop (6);
            auto strip = in.removeFromTop (6);
            if (s.layers() == 0)
            {
                g.setColour (juce::Colour (0xffe4d9c4));
                g.fillRoundedRectangle (strip, 3.0f);
            }
            else
            {
                double total = 0.0;
                for (auto b : s.beats)
                    total += juce::jlimit (1.0, 64.0, b);
                const float gaps = 3.0f * (float) (s.layers() - 1);
                float x = strip.getX();
                for (int l = 0; l < s.layers(); ++l)
                {
                    const float w = (strip.getWidth() - gaps) * (float) (juce::jlimit (1.0, 64.0, s.beats[(size_t) l]) / total);
                    g.setColour (colourForSource (s.sources[(size_t) l]));
                    g.fillRoundedRectangle (x, strip.getY(), juce::jmax (3.0f, w), strip.getHeight(), 3.0f);
                    x += w + 3.0f;
                }
            }
            auto mb = moreButton (row);
            g.setColour (kDim);
            for (int k = -1; k <= 1; ++k)
                g.fillEllipse (mb.getCentreX() + (float) k * 8.0f - 2.5f, mb.getCentreY() - 2.5f, 5.0f, 5.0f);
        }
        if (o.empty())
        {
            g.setColour (kDim);
            g.setFont (vintage::barlow (16.0f));
            const auto text = owner.all.empty() ? juce::String ("No saved songs yet. Catch an idea, or start a new song: Cmd+S keeps it here.")
                            : owner.search.isNotEmpty() ? "Nothing matches \"" + owner.search + "\"."
                            : juce::String ("No starred songs yet. Click a song's ··· to star it.");
            g.drawFittedText (text, getLocalBounds().withHeight (60).reduced (10, 0), juce::Justification::centred, 2);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.position);
        if (r != over)
        {
            over = r;
            repaint();
        }
        setMouseCursor (r >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { over = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { pressed = rowAt (e.position); repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int row = pressed;
        pressed = -1;
        repaint();
        if (row < 0 || rowAt (e.position) != row)
            return;
        if (e.mods.isPopupMenu() || moreButton (row).contains (e.position))
        {
            owner.menuFor (row).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (moreButton (row).toNearestInt())));
            return;
        }
        const auto file = owner.all[(size_t) owner.order()[(size_t) row]].file;
        if (playButton (row).expanded (4).contains (e.position))
        {
            if (owner.onPlay)
                owner.onPlay (file);
            return;
        }
        owner.clickSong (row);
    }
};

//==============================================================================
// The whole page, painted, and where everything goes.
struct RigIdeas::Content : public juce::Component
{
    explicit Content (RigIdeas& o) : owner (o) {}
    RigIdeas& owner;

    struct Layout
    {
        juce::Rectangle<int> title, hero, heroLeft, lanes, heroButtons, catchNew, spark, sparkButtons, toolsLabel, tiles, how,
            songsLabel, search, chips, list;
        int height = 0;
    };

    Layout layout() const
    {
        Layout L;
        const int w = getWidth();
        const bool wide = w >= 1100;
        const int inner = w - 2 * kMargin;
        const int leftW = wide ? (int) ((inner - kColGap) * 0.62f) : inner;
        L.title = { kMargin, kTop, inner, kTitleH };
        int y = kTop + kTitleH + 12;
        const int x = kMargin;
        L.hero = { x, y, leftW, kHeroH };
        L.heroLeft = L.hero.reduced (22, 20).withWidth (juce::jmin (270, leftW / 2 - 20));
        L.lanes = L.hero.reduced (22, 20).withTrimmedLeft (L.heroLeft.getWidth() + 24);
        L.heroButtons = L.heroLeft.withTrimmedTop (L.heroLeft.getHeight() - 54);
        y += kHeroH + 16;
        L.catchNew = { x, y, juce::jmin (300, leftW * 3 / 8), kRow2H };
        L.spark = { L.catchNew.getRight() + 16, y, leftW - L.catchNew.getWidth() - 16, kRow2H };
        L.sparkButtons = L.spark.reduced (18, 16).removeFromRight (184).withTrimmedTop (kRow2H - 32 - 48 - 8 - 40);
        y += kRow2H + 18;
        L.toolsLabel = { x, y, leftW, 18 };
        y += 26;
        L.tiles = { x, y, leftW, kTileH };
        y += kTileH + 18;
        const int howH = owner.howOpen ? kHowH + 72 : kHowH;
        L.how = { x, y, leftW, howH };
        y += howH;
        const int leftBottom = y;

        int ry = wide ? kTop + kTitleH + 12 : y + 26;
        const int rx = wide ? x + leftW + kColGap : x, rw = wide ? inner - leftW - kColGap : inner;
        L.songsLabel = { rx, ry, rw, 40 };
        L.search = { rx + 170, ry, rw - 170, 40 };
        ry += 50;
        L.chips = { rx, ry, rw, 32 };
        ry += 44;
        const int rows = juce::jmax (1, (int) owner.order().size());
        L.list = { rx, ry, rw, owner.order().empty() ? 70 : rows * (kCardH + kCardGap) };
        ry = L.list.getBottom();
        L.height = juce::jmax (leftBottom, ry) + 32;
        return L;
    }

    int laneCount() const { return juce::jmin (kMaxLanes, (int) owner.hero.sources.size()); }

    void resized() override
    {
        const auto L = layout();
        auto b = [this] (const char* n) -> Btn& { return *owner.buttons[n]; };
        auto right = L.title.withTrimmedTop (6).withTrimmedBottom (6);
        b ("settings").setBounds (right.removeFromRight (124));
        right.removeFromRight (12);
        b ("logic").setBounds (right.removeFromRight (196));
        right.removeFromRight (12);
        b ("file").setBounds (right.removeFromRight (150));
        b ("play").setBounds (L.heroButtons.withWidth (54));
        b ("open").setBounds (L.heroButtons.withTrimmedLeft (64));
        b ("catch").setBounds (L.catchNew.withHeight ((L.catchNew.getHeight() - 10) / 2));
        b ("new").setBounds (L.catchNew.withTrimmedTop ((L.catchNew.getHeight() + 10) / 2));
        auto sb = L.sparkButtons;
        b ("spark").setBounds (sb.removeFromTop (48));
        sb.removeFromTop (8);
        b ("roll").setBounds (sb.removeFromTop (40));
        const int tw = (L.tiles.getWidth() - 7 * 8) / kTiles;
        for (int i = 0; i < (int) owner.tiles.size(); ++i)
            owner.tiles[(size_t) i]->setBounds (L.tiles.getX() + i * (tw + 8), L.tiles.getY(), tw, kTileH);
        auto how = L.how.withTrimmedTop (L.how.getHeight() - kHowH).reduced (10, 9);
        b ("guide").setBounds (how.removeFromRight (128));
        how.removeFromRight (8);
        b ("tour").setBounds (how.removeFromRight (140));
        how.removeFromRight (8);
        b ("lesson").setBounds (how.removeFromRight (200));
        b ("how").setBounds (L.how.withHeight (owner.howOpen ? 36 : kHowH).withWidth (150));
        owner.searchBox.setBounds (L.search);
        int x = L.chips.getX();
        for (size_t i = 0; i < owner.chips.size(); ++i)
        {
            const int cw = (int) juce::GlyphArrangement::getStringWidth (vintage::cond (13.0f, vintage::Weight::bold, 0.08f), juce::String (kFilterNames[i]).toUpperCase()) + 26;
            owner.chips[i]->setBounds (x, L.chips.getY(), cw, 32);
            x += cw + 6;
        }
        owner.list->setBounds (L.list);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kPaper);
        const auto L = layout();

        // MY IDEAS, and this week
        g.setColour (kInk);
        g.setFont (vintage::wordmark (32.0f));
        g.drawText ("My ideas", L.title.withWidth (200), juce::Justification::centredLeft);
        int week = 0, layers = 0;
        const auto weekAgo = juce::Time::getCurrentTime() - juce::RelativeTime::days (7);
        for (auto& s : owner.all)
        {
            if (s.lastShow)
                continue;
            week += s.modified > weekAgo ? 1 : 0;
            layers += s.layers();
        }
        g.setColour (kDim);
        g.setFont (vintage::lcd (21.0f));
        g.drawText (juce::String (week) + " THIS WEEK" + dot + juce::String (layers) + (layers == 1 ? " LAYER" : " LAYERS"),
                    L.title.withTrimmedLeft (186).withWidth (320), juce::Justification::centredLeft);

        paintHero (g, L);
        paintSpark (g, L);

        g.setColour (kDim);
        g.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.16f));
        g.drawText ("START A SONG FROM ANY TOOL", L.toolsLabel, juce::Justification::centredLeft);
        int count = 0;
        for (auto& s : owner.all)
            count += s.lastShow ? 0 : 1;
        g.drawText ("YOUR SONGS" + dot + juce::String (count), L.songsLabel.withWidth (160), juce::Justification::centredLeft);

        // HOW IT WORKS
        g.setColour (kWell);
        g.fillRoundedRectangle (L.how.toFloat(), 14.0f);
        if (owner.howOpen)
        {
            // 1 2 3 along the top, the ways to learn more along the bottom.
            auto steps = L.how.withTrimmedTop (32).withHeight (44).withTrimmedLeft (18).withTrimmedRight (18);
            static const char* heads[3] = { "PICK A TOOL", "PRESS REC", "IT LOOPS" };
            static const char* lines[3] = { "A tile above, or any track on the left", "Play it: it records from the next bar", "Pick another tool and play on top" };
            const int sw = steps.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto s = steps.removeFromLeft (sw).reduced (0, 4);
                g.setColour (kNumberColours[i]);
                g.setFont (vintage::lcd (30.0f));
                g.drawText (juce::String (i + 1), s.removeFromLeft (28), juce::Justification::centredLeft);
                g.setColour (kOnDark);
                g.setFont (vintage::cond (15.0f, vintage::Weight::bold, 0.06f));
                g.drawFittedText (heads[i], s.removeFromTop (s.getHeight() / 2), juce::Justification::bottomLeft, 1, 0.8f);
                g.setColour (kOnDarkDim);
                g.setFont (vintage::barlow (13.0f));
                g.drawFittedText (lines[i], s, juce::Justification::topLeft, 1, 0.8f);
            }
        }
    }

    void paintHero (juce::Graphics& g, const Layout& L)
    {
        const auto& h = owner.hero;
        auto r = L.hero.toFloat();
        g.setColour (kAmber);
        g.fillRoundedRectangle (r.expanded (2.0f), 22.0f);
        g.setColour (kWell);
        g.fillRoundedRectangle (r, 20.0f);
        auto in = L.heroLeft;
        g.setColour (kOnDarkDim);
        g.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.16f));
        g.drawText (h.name.isEmpty() ? "START HERE" : "PICK UP WHERE YOU LEFT OFF", in.removeFromTop (18), juce::Justification::centredLeft);
        in.removeFromTop (6);
        g.setColour (kOnDark);
        g.setFont (vintage::barlow (30.0f).boldened());
        g.drawFittedText (h.name.isEmpty() ? juce::String ("Nothing made yet") : h.name, in.removeFromTop (36), juce::Justification::centredLeft, 1, 0.75f);
        g.setColour (kOnDarkDim);
        g.setFont (vintage::barlow (15.0f));
        g.drawFittedText (h.name.isEmpty() ? juce::String ("Catch an idea, roll a spark, or pick a tool below.") : h.meta, in.removeFromTop (42), juce::Justification::topLeft, 2, 0.85f);
        g.setColour (kAmber);
        g.setFont (vintage::lcd (23.0f));
        g.drawText (h.lcd, in.removeFromTop (26), juce::Justification::centredLeft);

        // the layers, as lanes: their real sound
        auto lanes = L.lanes;
        if (laneCount() == 0)
        {
            g.setColour (kOnDarkDim);
            g.setFont (vintage::barlow (15.0f));
            g.drawFittedText (h.name.isEmpty() ? juce::String() : juce::String ("No layers yet: pick a tool on the left and press Rec."), lanes, juce::Justification::centred, 2);
            return;
        }
        const int laneH = juce::jmin (30, (lanes.getHeight() - (kMaxLanes - 1) * 8) / kMaxLanes);
        lanes = lanes.withSizeKeepingCentre (lanes.getWidth(), laneCount() * laneH + (laneCount() - 1) * 8);
        for (int l = 0; l < laneCount(); ++l)
        {
            auto lane = lanes.removeFromTop (laneH).toFloat();
            lanes.removeFromTop (8);
            const auto colour = colourForSource (h.sources[(size_t) l]);
            g.setColour (colour);
            g.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.06f));
            g.drawFittedText (juce::String (l + 1) + " " + nameForSource (h.sources[(size_t) l]), lane.removeFromLeft (84).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (kLane);
            g.fillRoundedRectangle (lane, 5.0f);
            if (l < (int) h.peaks.size() && ! h.peaks[(size_t) l].empty())
                crate::lanes::drawPeaks (g, h.peaks[(size_t) l], lane.reduced (4.0f, 0.0f), colour);
            else
            {
                g.setColour (colour.withAlpha (0.55f));
                g.fillRoundedRectangle (lane.reduced (4.0f, lane.getHeight() * 0.38f), 2.0f);
            }
        }
        if ((int) h.sources.size() > kMaxLanes)
        {
            g.setColour (kOnDarkDim);
            g.setFont (vintage::barlow (13.0f));
            g.drawText ("+ " + juce::String ((int) h.sources.size() - kMaxLanes) + " more layers", L.lanes.withTrimmedTop (L.lanes.getHeight() - 14), juce::Justification::bottomRight);
        }
    }

    void paintSpark (juce::Graphics& g, const Layout& L)
    {
        const auto& s = owner.sparkNow;
        auto r = L.spark.toFloat();
        g.setColour (kCard);
        g.fillRoundedRectangle (r, 18.0f);
        g.setColour (kRule);
        g.drawRoundedRectangle (r.reduced (1.0f), 18.0f, 2.0f);
        auto in = L.spark.reduced (18, 16);
        in.removeFromRight (196);
        g.setColour (kDim);
        g.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.16f));
        g.drawText ((owner.sparkRolls == 0 ? "TONIGHT'S SPARK" : "ANOTHER SPARK") + dot + s.mood, in.removeFromTop (18), juce::Justification::centredLeft);
        in.removeFromTop (6);
        g.setColour (kInk);
        g.setFont (vintage::wordmark (24.0f));
        g.drawFittedText (s.title, in.removeFromTop (34), juce::Justification::centredLeft, 1, 0.6f);
        in.removeFromTop (8);
        for (auto& step : s.steps)
        {
            auto row = in.removeFromTop (32);
            auto box = row.removeFromLeft (28).withSizeKeepingCentre (28, 28).toFloat();
            g.setColour (kWell);
            g.fillRoundedRectangle (box, 7.0f);
            crate::toolicons::draw (g, iconForSparkTool (step.tool), box.reduced (6.0f), colourForSparkTool (step.tool));
            row.removeFromLeft (10);
            g.setColour (kInk);
            g.setFont (vintage::barlow (15.5f));
            g.drawFittedText (step.text, row, juce::Justification::centredLeft, 1, 0.8f);
        }
        const auto& kits = crate::factory::kits();
        const juce::String kit = juce::isPositiveAndBelow (s.kit, (int) kits.size()) ? juce::String (kits[(size_t) s.kit].name) : juce::String ("factory kit");
        auto note = L.sparkButtons.withTrimmedTop (-62).withHeight (54);
        g.setColour (kDim);
        g.setFont (vintage::barlow (13.0f));
        g.drawFittedText (juce::String (s.bpm) + " BPM, " + s.keyName() + ", the " + kit + " on the pads.", note, juce::Justification::bottomLeft, 3, 0.85f);
    }
};

//==============================================================================
RigIdeas::RigIdeas (juce::File songsFolder) : folder (std::move (songsFolder))
{
    sparkNow = crate::spark::make (crate::spark::tonight());
    content = std::make_unique<Content> (*this);
    list = std::make_unique<List> (*this);
    view.setViewedComponent (content.get(), false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);

    auto add = [this] (const char* name, const juce::String& text, Btn::Style style, float size, std::function<void()> click)
    {
        auto b = std::make_unique<Btn> (text, style, size);
        b->onClick = std::move (click);
        content->addAndMakeVisible (*b);
        buttons[name] = std::move (b);
    };
    add ("file", u8 ("Open a file\xe2\x80\xa6"), Btn::cream, 15.0f, [this] { if (onOpenFile) onOpenFile(); });
    add ("logic", u8 ("Import from Logic\xe2\x80\xa6"), Btn::cream, 15.0f, [this] { if (onImportLogic) onImportLogic(); });
    add ("settings", "Settings", Btn::cream, 15.0f, [this] { if (onSettings) onSettings(); });
    add ("play", {}, Btn::cream, 15.0f, [this] { if (onPlayHero) onPlayHero(); });
    add ("open", "Open and keep going", Btn::amber, 18.0f, [this] { if (onOpenHero) onOpenHero(); });
    add ("catch", "Catch an idea", Btn::red, 20.0f, [this] { if (onCatch) onCatch(); });
    add ("new", "New empty song", Btn::amber, 20.0f, [this] { if (onNew) onNew(); });
    add ("spark", "Start this spark", Btn::dark, 16.0f, [this] { if (onSpark) onSpark (sparkNow); });
    add ("roll", "Roll another", Btn::cream, 14.0f, [this] { rollSpark(); });
    add ("lesson", "Show me: make a beat", Btn::dark, 14.0f, [this] { if (onLesson) onLesson(); });
    add ("tour", "Take the tour", Btn::dark, 14.0f, [this] { if (onTour) onTour(); });
    add ("guide", "Every tool", Btn::dark, 14.0f, [this] { if (onGuide) onGuide(); });
    add ("how", {}, Btn::ghost, 13.0f, [this] { setHowItWorksOpen (! howOpen); });

    buttons["play"]->face = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        playGlyph (g, r, kInk, hero.file != juce::File() && isPlaying && isPlaying (hero.file));
    };
    buttons["catch"]->face = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        auto icon = r.removeFromLeft (r.getHeight()).withSizeKeepingCentre (44, 44).translated (8, 0);
        g.setColour (juce::Colours::white);
        g.fillEllipse (icon);
        crate::toolicons::draw (g, crate::toolicons::Tool::vocoder, icon.reduced (11), kRed);
        auto text = r.reduced (16, 0);
        g.setColour (ink);
        g.setFont (vintage::cond (19.0f, vintage::Weight::bold, 0.06f));
        g.drawText ("CATCH AN IDEA", text.removeFromTop (text.getHeight() * 0.56f), juce::Justification::bottomLeft);
        g.setFont (vintage::barlow (13.5f));
        g.drawText ("Your mic, onto loop 1, now", text, juce::Justification::topLeft);
    };
    buttons["new"]->face = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        auto icon = r.removeFromLeft (r.getHeight()).translated (8, 0);
        g.setColour (ink);
        g.setFont (vintage::cond (44.0f, vintage::Weight::bold));
        g.drawText ("+", icon, juce::Justification::centred);
        auto text = r.reduced (16, 0);
        g.setFont (vintage::cond (19.0f, vintage::Weight::bold, 0.06f));
        g.drawText ("NEW EMPTY SONG", text.removeFromTop (text.getHeight() * 0.56f), juce::Justification::bottomLeft);
        g.setFont (vintage::barlow (13.5f));
        g.drawText ("Cmd+N", text, juce::Justification::topLeft);
    };
    buttons["roll"]->face = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        auto die = juce::Rectangle<float> (r.getX() + 16, r.getCentreY() - 8, 16, 16);
        g.setColour (ink);
        g.drawRoundedRectangle (die.reduced (1.0f), 3.0f, 1.6f);
        for (auto p : { juce::Point<float> (-3.5f, -3.5f), juce::Point<float> (0, 0), juce::Point<float> (3.5f, 3.5f) })
            g.fillEllipse (die.getCentreX() + p.x - 1.3f, die.getCentreY() + p.y - 1.3f, 2.6f, 2.6f);
        g.setFont (vintage::cond (14.0f, vintage::Weight::bold, 0.08f));
        g.drawText ("ROLL ANOTHER", r.withTrimmedLeft (36), juce::Justification::centred);
    };
    buttons["how"]->face = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        g.setColour (juce::Colour (0xffd9ccb4));
        g.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.16f));
        g.drawText ("HOW IT WORKS", r.withTrimmedLeft (18), juce::Justification::centredLeft);
        g.setColour (kOnDarkDim);
        g.setFont (vintage::cond (11.0f, vintage::Weight::bold, 0.1f));
        g.drawText (howOpen ? u8 ("\xe2\x96\xbe") : u8 ("\xe2\x96\xb8 SHOW"), r.withTrimmedRight (6), juce::Justification::centredRight);
    };

    for (int i = 0; i < kTiles; ++i)
    {
        auto t = std::make_unique<Btn> (kTileInfo[i].label, Btn::dark, 14.0f);
        t->face = [i] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
        {
            const auto colour = juce::Colour (kTileInfo[i].colour);
            g.setColour (colour);
            g.fillRoundedRectangle (r.withHeight (3.0f).reduced (10.0f, 0), 1.5f);
            crate::toolicons::draw (g, kTileInfo[i].icon, juce::Rectangle<float> (r.getCentreX() - 14, r.getY() + 16, 28, 26), colour);
            g.setColour (ink);
            g.setFont (vintage::cond (14.0f, vintage::Weight::bold, 0.06f));
            g.drawFittedText (i == 1 ? u8 ("PADS \xc2\xb7 MPC") : juce::String (kTileInfo[i].label).toUpperCase(), r.removeFromBottom (30).toNearestInt().reduced (4, 0), juce::Justification::centredTop, 1, 0.7f);
        };
        t->onClick = [this, i] { pressTile (i); };
        content->addAndMakeVisible (*t);
        tiles.push_back (std::move (t));
    }
    for (int f = 0; f < 4; ++f)
    {
        auto chip = std::make_unique<Btn> (kFilterNames[f], f == 0 ? Btn::chipOn : Btn::chipOff, 13.0f);
        chip->onClick = [this, f] { setFilter (f); };
        content->addAndMakeVisible (*chip);
        chips.push_back (std::move (chip));
    }

    searchBox.setTextToShowWhenEmpty (u8 ("Find a song, tool or tempo"), kDim.withAlpha (0.7f));
    searchBox.setFont (vintage::barlow (15.0f));
    searchBox.setIndents (14, 11);
    searchBox.setColour (juce::TextEditor::backgroundColourId, kCard);
    searchBox.setColour (juce::TextEditor::outlineColourId, kRule);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, kAmber);
    searchBox.setColour (juce::TextEditor::textColourId, kInk);
    searchBox.setColour (juce::TextEditor::highlightColourId, kAmber.withAlpha (0.4f));
    searchBox.setColour (juce::CaretComponent::caretColourId, kInk);
    searchBox.setTitle ("Search songs");
    searchBox.onTextChange = [this] { setSearch (searchBox.getText()); };
    searchBox.onEscapeKey = [this] { setSearch ({}); };
    content->addAndMakeVisible (searchBox);
    content->addAndMakeVisible (*list);

    loadStars();
    howOpen = ! (bool) juce::JSON::parse (folder.getChildFile ("My ideas.json")).getProperty ("howItWorksHidden", false);
}

RigIdeas::~RigIdeas() = default;

juce::Button* RigIdeas::button (const juce::String& name) const
{
    auto it = buttons.find (name);
    return it != buttons.end() ? it->second.get() : nullptr;
}

void RigIdeas::loadStars()
{
    starred.clear();
    const auto saved = juce::JSON::parse (folder.getChildFile ("My ideas.json"));
    const auto list = saved.getProperty ("starred", {});
    if (auto* a = list.getArray())
        for (auto& v : *a)
            starred.add (v.toString());
}

void RigIdeas::saveStars() const
{
    auto file = folder.getChildFile ("My ideas.json");
    auto v = juce::JSON::parse (file);
    if (! v.isObject())
        v = juce::var (new juce::DynamicObject());
    juce::Array<juce::var> list;
    for (auto& s : starred)
        list.add (s);
    v.getDynamicObject()->setProperty ("starred", list);
    v.getDynamicObject()->setProperty ("howItWorksHidden", ! howOpen);
    folder.createDirectory();
    file.replaceWithText (juce::JSON::toString (v));
}

void RigIdeas::setStarred (const juce::File& file, bool on)
{
    const auto key = file.getFileName();
    starred.removeString (key);
    if (on)
        starred.add (key);
    saveStars();
    for (auto& s : all)
        if (s.file == file)
            s.starred = on;
    layoutChildren();
}

void RigIdeas::setHowItWorksOpen (bool open)
{
    howOpen = open;
    saveStars();
    layoutChildren();
}

std::vector<int> RigIdeas::order() const
{
    std::vector<int> o;
    const auto needle = search.trim().toLowerCase();
    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& s = all[(size_t) i];
        if (filter == 1 && ! s.starred) continue;
        if (needle.isNotEmpty())
        {
            bool hit = s.name.toLowerCase().contains (needle) || (s.bpm > 0.0 && juce::String (s.bpm, 0) == needle);
            for (int src : s.sources)
                hit = hit || nameForSource (src).toLowerCase().contains (needle);
            if (! hit) continue;
        }
        o.push_back (i);   // newest first already (the Last show at the end)
    }
    if (filter == 2)
        std::stable_sort (o.begin(), o.end(), [this] (int a, int b) { return all[(size_t) a].name.compareNatural (all[(size_t) b].name) < 0; });
    else if (filter == 3)
        std::stable_sort (o.begin(), o.end(), [this] (int a, int b) { return all[(size_t) a].layers() > all[(size_t) b].layers(); });
    return o;
}

void RigIdeas::rollSpark()
{
    ++sparkRolls;
    sparkNow = crate::spark::make (crate::spark::tonight() + sparkRolls);
    content->repaint();
}

void RigIdeas::setFilter (int f)
{
    filter = juce::jlimit (0, 3, f);
    for (size_t i = 0; i < chips.size(); ++i)
        chips[i]->setStyle ((int) i == filter ? Btn::chipOn : Btn::chipOff);
    layoutChildren();
}

void RigIdeas::setSearch (const juce::String& s)
{
    if (s == search)
        return;
    search = s;
    if (searchBox.getText() != s)
        searchBox.setText (s, false);
    layoutChildren();
}

void RigIdeas::pressTile (int tile)
{
    if (juce::isPositiveAndBelow (tile, kTiles) && onStartWith)
        onStartWith (tile);
}

void RigIdeas::clickSong (int row)
{
    const auto o = order();
    if (juce::isPositiveAndBelow (row, (int) o.size()) && onOpen)
        onOpen (all[(size_t) o[(size_t) row]].file);
}

juce::PopupMenu RigIdeas::menuFor (int row)
{
    juce::PopupMenu menu;
    const auto o = order();
    if (! juce::isPositiveAndBelow (row, (int) o.size()))
        return menu;
    const auto song = all[(size_t) o[(size_t) row]];
    auto safe = juce::Component::SafePointer<RigIdeas> (this);
    menu.addSectionHeader (song.name);
    menu.addItem ("Open", [safe, song] { if (safe != nullptr && safe->onOpen) safe->onOpen (song.file); });
    menu.addItem ("Play", [safe, song] { if (safe != nullptr && safe->onPlay) safe->onPlay (song.file); });
    menu.addItem (song.starred ? "Unstar" : "Star", [safe, song] { if (safe != nullptr) safe->setStarred (song.file, ! song.starred); });
    menu.addSeparator();
    menu.addItem (u8 ("Rename\xe2\x80\xa6"), ! song.lastShow, false, [safe, song] { if (safe != nullptr && safe->onRename) safe->onRename (song.file); });
    menu.addItem ("Show in Finder", [song] { song.file.revealToUser(); });
    menu.addItem (u8 ("Move to Trash\xe2\x80\xa6"), ! song.lastShow, false, [safe, song] { if (safe != nullptr && safe->onTrash) safe->onTrash (song.file); });
    return menu;
}

int RigIdeas::songCardsTop() const { return getLocalPoint (content.get(), list->getPosition()).y; }

void RigIdeas::refresh()
{
    loadStars();
    std::vector<Song> found;
    std::map<juce::String, Song> fresh;
    Song lastShow;
    bool hasLast = false;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.crateshow"))
    {
        Song s;
        if (auto it = cache.find (f.getFullPathName()); it != cache.end() && it->second.modified == f.getLastModificationTime())
            s = it->second;
        else
            s = read (f);
        s.starred = starred.contains (f.getFileName());
        fresh[f.getFullPathName()] = s;
        if (s.lastShow)
        {
            lastShow = s;
            hasLast = true;
        }
        else
            found.push_back (s);
    }
    std::stable_sort (found.begin(), found.end(), [] (const Song& a, const Song& b) { return a.modified > b.modified; });
    if (hasLast)
        found.push_back (lastShow);   // the auto-saved set stays at the end
    all = std::move (found);
    cache = std::move (fresh);
    hero = getHero ? getHero() : Hero();
    layoutChildren();
}

void RigIdeas::layoutChildren()
{
    const int w = juce::jmax (600, view.getWidth() - (view.isVerticalScrollBarShown() ? view.getScrollBarThickness() : 0));
    content->setSize (w, 10);
    content->setSize (w, juce::jmax (getHeight(), content->layout().height));
    content->resized();
    content->repaint();
    list->repaint();
}

void RigIdeas::resized()
{
    view.setBounds (getLocalBounds());
    layoutChildren();
}

void RigIdeas::paint (juce::Graphics& g)
{
    g.fillAll (kPaper);
}
