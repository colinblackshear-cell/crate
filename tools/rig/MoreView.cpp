#include "MoreView.h"
#include "RigColours.h"

using namespace vintage;

const std::vector<MoreView::ToolInfo>& MoreView::toolList()
{
    static const std::vector<ToolInfo> list {
        { "Song", "Song builder", "Save what's playing as parts, switch tracks on and off in each, and play them in order.", RigColours::song },
        { "MPC", "MPC", "Pads, programs and sequences. Sends kits to your 2000XL.", RigColours::mpc },
        { "Drum machine", "Drum machine", "Analog drums: sixteen steps, accents and swing.", RigColours::drums },
        { "Sampler editor", "Sampler editor", "Record, trim, chop, effects, and play a sample across the keys.", RigColours::sampler },
        { "Bass", "Bass", "The bass synth in full: its sound, and its patterns.", RigColours::bass },
        { "Synth", "Synth", "The synth in full: oscillators, filter, LFO and the arp.", RigColours::synth },
        { "Sounds", "Sounds", "Your samples and Splice, by kind, tempo and key. Send them anywhere.", juce::Colour (0xffa1887f) },
        { "Mixer", "Mixer", "Faders, pan, mute, solo, effects and sends for every tool and loop.", RigColours::master },
        { "Stage", "Big stage", "Giant buttons and the FX pad, for playing live in a dark room.", RigColours::song },
        { "Setup", "Setup", "What the keys and pads play, the split, sampling between tools, knob Learn.", juce::Colour (0xff8fb8ff) },
    };
    return list;
}

const MoreView::ToolInfo& MoreView::guideInfo()
{
    static const ToolInfo info { "Guide", "The Guide", "Every tool, what it's for, and where to start.", juce::Colour (0xff8fb8ff) };
    return info;
}

//==============================================================================
// A tool's module: a picture of what it does, its name and a line about it.
class MoreView::Tile : public juce::Button
{
public:
    Tile (const ToolInfo& i, int n) : juce::Button (i.title), info (i), index (n)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Opens " + info.title + ". " + info.blurb);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (over)
            glow (g, r, 14.0f, info.colour, 12.0f, 0.45f);
        drawWell (g, r.translated (0, down ? 1.0f : 0.0f), 14.0f);
        auto inner = r.reduced (16.0f, 12.0f);
        const auto& p = pal();

        // The colour bar and the name.
        g.setColour (info.colour);
        g.fillRoundedRectangle (inner.removeFromTop (5.0f), 2.5f);
        inner.removeFromTop (8.0f);
        const bool big = getHeight() > 260;
        if (index == guideIndex && getWidth() > getHeight() * 2)
        {
            // The Guide: a wide banner, a night sky on the left, its name beside it.
            auto sky = inner.removeFromLeft (juce::jmin (inner.getWidth() * 0.32f, inner.getHeight() * 1.6f));
            drawIcon (g, sky, over);
            inner.removeFromLeft (14.0f);
            g.setColour (p.wellInk);
            g.setFont (cond (26.0f, Weight::extra, 0.05f));
            g.drawFittedText (info.title.toUpperCase(), inner.removeFromTop (inner.getHeight() * 0.5f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
            g.setColour (p.wellInk2);
            g.setFont (barlow (12.5f));
            g.drawFittedText (info.blurb, inner.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
            return;
        }
        auto bottom = inner.removeFromBottom (18.0f);
        engraved (g, juce::String::fromUTF8 ("Open \xe2\x96\xb8"), bottom.toNearestInt(), juce::Justification::centredRight, 12.0f, true,
                  over ? info.colour : juce::Colour());
        auto blurb = inner.removeFromBottom (big ? 64.0f : 36.0f);
        auto name = inner.removeFromBottom (big ? 38.0f : 28.0f);
        g.setColour (p.wellInk);
        g.setFont (cond (big ? 34.0f : 22.0f, Weight::extra, 0.03f));
        g.drawFittedText (info.title.toUpperCase(), name.toNearestInt(), juce::Justification::bottomLeft, 1, 0.55f);
        g.setColour (p.wellInk2);
        g.setFont (barlow (big ? 15.0f : 12.5f));
        g.drawFittedText (info.blurb, blurb.toNearestInt(), juce::Justification::topLeft, big ? 3 : 2, 0.9f);

        // The picture.
        inner.removeFromBottom (6.0f);
        drawIcon (g, inner, over);
    }

    void drawIcon (juce::Graphics& g, juce::Rectangle<float> area, bool over)
    {
        const auto c = info.colour;
        const auto dim = juce::Colour (0xff4a433d);
        const auto ink = juce::Colour (0xfff1e7d1);
        if (area.getHeight() < 16.0f)
            return;
        // Which picture: by the tool's page, so the order can change.
        static const juce::StringArray pages { "Song", "MPC", "Sampler editor", "Bass", "Synth", "Sounds", "Mixer", "Drum machine" };
        const int art = index == guideIndex ? guideIndex : pages.indexOf (info.page);
        switch (art)
        {
            case 0:   // Song builder: parts in a row, to scale, with a play head
            {
                const float widths[] = { 2, 4, 4, 3, 4, 2 };
                const juce::Colour cols[] = { RigColours::mpc, RigColours::bass, RigColours::song, RigColours::synth, RigColours::song, RigColours::loops };
                auto rows = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 150.0f));
                const float rowH = rows.getHeight() / 4.0f;
                // Four lanes; blocks come in and drop out part by part.
                const bool on[4][6] = { { 1, 1, 1, 1, 1, 0 }, { 0, 1, 1, 0, 1, 1 }, { 0, 0, 1, 1, 1, 0 }, { 1, 0, 1, 0, 1, 1 } };
                for (int lane = 0; lane < 4; ++lane)
                {
                    auto row = rows.removeFromTop (rowH).reduced (0, 4.0f);
                    float x = row.getX();
                    for (int part = 0; part < 6; ++part)
                    {
                        const float w = row.getWidth() * widths[part] / 19.0f;
                        auto block = juce::Rectangle<float> (x + 2.0f, row.getY(), w - 4.0f, row.getHeight());
                        g.setColour (on[lane][part] ? cols[lane % 6].withAlpha (over ? 1.0f : 0.85f) : dim.withAlpha (0.5f));
                        g.fillRoundedRectangle (block, 4.0f);
                        x += w;
                    }
                }
                const float head = area.getX() + area.getWidth() * (over ? 0.62f : 0.47f);
                g.setColour (colours::play);
                g.fillRect (head, area.getY(), 2.5f, area.getHeight());
                break;
            }
            case 1:   // MPC: 4 x 4 pads
            {
                const float s = juce::jmin (area.getWidth(), area.getHeight());
                auto pads = area.withSizeKeepingCentre (s * 1.2f, s);
                const float pw = pads.getWidth() / 4.0f, ph = pads.getHeight() / 4.0f;
                for (int i = 0; i < 16; ++i)
                {
                    auto pad = juce::Rectangle<float> (pads.getX() + (i % 4) * pw, pads.getY() + (i / 4) * ph, pw, ph).reduced (3.0f);
                    const bool lit = i == 5 || i == 10 || (over && i == 12);
                    g.setColour (lit ? c : dim);
                    g.fillRoundedRectangle (pad, 3.0f);
                }
                break;
            }
            case 2:   // Sampler: a waveform between start and end handles
            {
                juce::Random rnd (7);
                const float mid = area.getCentreY();
                for (float x = area.getX() + 4.0f; x < area.getRight() - 4.0f; x += 4.0f)
                {
                    const float t = (x - area.getX()) / area.getWidth();
                    const float env = std::exp (-3.0f * std::fmod (t * 2.0f, 1.0f));
                    const float h = (0.2f + 0.8f * rnd.nextFloat()) * env * area.getHeight() * 0.45f;
                    const bool inside = t > 0.18f && t < 0.78f;
                    g.setColour (inside ? c : dim);
                    g.fillRect (x, mid - h, 2.5f, h * 2.0f);
                }
                g.setColour (ink);
                for (float t : { 0.18f, 0.78f })
                    g.fillRect (area.getX() + area.getWidth() * t, area.getY(), 2.0f, area.getHeight());
                break;
            }
            case 3:   // Bass: a fat saw
            {
                juce::Path saw;
                const int teeth = 3;
                const float w = area.getWidth() / teeth, top = area.getY() + area.getHeight() * 0.18f, bot = area.getBottom() - area.getHeight() * 0.18f;
                saw.startNewSubPath (area.getX(), bot);
                for (int i = 0; i < teeth; ++i)
                {
                    saw.lineTo (area.getX() + w * (i + 1), top);
                    saw.lineTo (area.getX() + w * (i + 1), bot);
                }
                g.setColour (c.withAlpha (0.25f));
                g.strokePath (saw, juce::PathStrokeType (12.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (c);
                g.strokePath (saw, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                break;
            }
            case 4:   // Synth: an octave of keys
            {
                auto keys = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), area.getWidth() * 0.45f));
                const float kw = keys.getWidth() / 7.0f;
                for (int k = 0; k < 7; ++k)
                {
                    g.setColour (k == 2 && over ? c : ink);
                    g.fillRoundedRectangle (keys.getX() + k * kw + 1.5f, keys.getY(), kw - 3.0f, keys.getHeight(), 3.0f);
                }
                for (int k : { 0, 1, 3, 4, 5 })
                {
                    g.setColour (k == 3 ? c : juce::Colour (0xff1a1714));
                    g.fillRoundedRectangle (keys.getX() + (k + 1) * kw - kw * 0.3f, keys.getY(), kw * 0.6f, keys.getHeight() * 0.6f, 2.0f);
                }
                break;
            }
            case 5:   // Sounds: a crate of records
            {
                auto crate = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight() * 1.8f), area.getHeight());
                const int n = 7;
                const float w = crate.getWidth() / n;
                for (int i = 0; i < n; ++i)
                {
                    const float h = crate.getHeight() * (i == 3 ? 1.0f : 0.72f);
                    auto sleeve = juce::Rectangle<float> (crate.getX() + i * w + 2.0f, crate.getBottom() - h, w - 4.0f, h);
                    g.setColour (i == 3 ? c : i % 2 == 0 ? dim.brighter (0.2f) : dim);
                    g.fillRoundedRectangle (sleeve, 2.0f);
                }
                g.setColour (c.darker (0.5f));
                g.fillRoundedRectangle (crate.removeFromBottom (crate.getHeight() * 0.35f), 4.0f);
                break;
            }
            case 7:   // Drum machine: a row of sixteen step keys, in four colour groups
            {
                const juce::Colour groups[] = { juce::Colour (0xffd8452c), juce::Colour (0xfff39b33), juce::Colour (0xffefcb45), ink };
                auto row = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), area.getWidth() / 5.0f));
                auto top = row.removeFromTop (row.getHeight() * 0.45f);
                // Two knobs over the keys.
                for (int k = 0; k < 4; ++k)
                {
                    const float d = top.getHeight() * 0.8f;
                    auto knob = juce::Rectangle<float> (d, d).withCentre ({ top.getX() + top.getWidth() * (k + 0.5f) / 4.0f, top.getCentreY() });
                    g.setColour (dim.brighter (0.3f));
                    g.fillEllipse (knob);
                    g.setColour (ink);
                    g.drawLine (knob.getCentreX(), knob.getCentreY(), knob.getCentreX() + d * 0.3f, knob.getY() + d * 0.15f, 2.0f);
                }
                row.removeFromTop (row.getHeight() * 0.15f);
                const float w = row.getWidth() / 16.0f;
                for (int i = 0; i < 16; ++i)
                {
                    const bool lit = i % 4 == 0 || i == 10 || (over && i == 14);
                    auto key = juce::Rectangle<float> (row.getX() + i * w, row.getY(), w, row.getHeight()).reduced (1.2f, 0.0f);
                    g.setColour (groups[i / 4].withAlpha (lit ? 1.0f : 0.55f));
                    g.fillRoundedRectangle (key, 2.0f);
                    if (lit)
                    {
                        g.setColour (colours::arm);
                        g.fillEllipse (key.getCentreX() - 2.0f, key.getY() + 3.0f, 4.0f, 4.0f);
                    }
                }
                break;
            }
            case guideIndex:   // The Guide: stars and a ringed planet
            {
                juce::Random rnd (42);
                for (int i = 0; i < 26; ++i)
                {
                    const float d = 1.0f + rnd.nextFloat() * 2.2f;
                    g.setColour (ink.withAlpha (0.4f + rnd.nextFloat() * 0.6f));
                    g.fillEllipse (area.getX() + rnd.nextFloat() * area.getWidth(), area.getY() + rnd.nextFloat() * area.getHeight(), d, d);
                }
                const float s = juce::jmin (area.getWidth(), area.getHeight()) * 0.55f;
                auto planet = juce::Rectangle<float> (s, s).withCentre (area.getCentre());
                g.setColour (c);
                g.fillEllipse (planet);
                g.setColour (over ? colours::next : ink);
                g.drawEllipse (planet.withSizeKeepingCentre (s * 1.9f, s * 0.5f), 2.0f);
                break;
            }
            default:  // Mixer: faders
            {
                const int n = 5;
                const float w = area.getWidth() / n;
                const float levels[] = { 0.3f, 0.55f, 0.2f, 0.65f, 0.4f };
                for (int i = 0; i < n; ++i)
                {
                    const float x = area.getX() + w * (i + 0.5f);
                    g.setColour (juce::Colours::black);
                    g.fillRoundedRectangle (x - 2.0f, area.getY(), 4.0f, area.getHeight(), 2.0f);
                    const float y = area.getY() + area.getHeight() * (over ? 1.0f - levels[i] : levels[i]);
                    g.setColour (ink);
                    g.fillRoundedRectangle (x - w * 0.28f, y - 6.0f, w * 0.56f, 12.0f, 3.0f);
                    g.setColour (RigColours::forStrip (i));
                    g.fillRect (x - w * 0.22f, y - 1.0f, w * 0.44f, 2.0f);
                }
                break;
            }
        }
    }

    static constexpr int guideIndex = 100;
    const ToolInfo info;
    const int index;
};

//==============================================================================
MoreView::MoreView (bool isStandalone) : standalone (isStandalone)
{
    const auto& list = toolList();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto* t = tiles.add (new Tile (list[(size_t) i], i));
        t->onClick = [this, page = list[(size_t) i].page] { if (onOpen) onOpen (page); };
        addAndMakeVisible (t);
    }

    guideTile = std::make_unique<Tile> (guideInfo(), Tile::guideIndex);
    guideTile->onClick = [this] { if (onOpen) onOpen ("Guide"); };
    addChildComponent (*guideTile);

    save.sub = "Cmd+S";
    saveAs.sub = "A copy, new name";
    open.sub = "A saved show";
    exportButton.sub = "WAVs + MIDI, bar 1";
    audio.sub = "Interface, keyboard";
    click.sub = "Metronome";
    dark.sub = "For dark rooms";
    popOut.sub = "To a 2nd screen";
    save.setTooltip ("Saves everything: the show (sounds, patterns, mixer, song parts) and your loops, together.");
    saveAs.setTooltip ("Saves a copy of the show (and its loops) under a new name.");
    open.setTooltip ("Opens a show you saved.");
    exportButton.setTooltip ("Every loop as a WAV (with its effects, and dry), each laid out over your song, and a MIDI file with the "
                             "tempo and a marker per part. Every file starts on bar 1.");
    audio.setTooltip ("Pick your audio interface, its inputs and outputs, and your MIDI keyboard.");
    click.setTooltip ("A metronome click on every beat while playing.");
    dark.setTooltip ("Switches the panel to black, for dark rooms and stages.");
    popOut.setTooltip ("Puts a screen in its own window, to drag onto a second screen.");
    save.onClick = [this] { if (onSave) onSave(); };
    saveAs.onClick = [this] { if (onSaveAs) onSaveAs(); };
    open.onClick = [this] { if (onOpenShow) onOpenShow(); };
    exportButton.onClick = [this] { if (onExport) onExport(); };
    audio.onClick = [this] { if (onAudio) onAudio(); };
    click.onClick = [this] { if (onClick) onClick(); tick(); };
    dark.onClick = [this] { if (onDark) onDark(); tick(); };
    popOut.onClick = [this] { if (onPopOut) onPopOut(); };
    for (auto* b : { &save, &saveAs, &open, &exportButton, &audio, &click, &dark, &popOut })
    {
        b->fontSize = 18.0f;
        b->subSize = 11.0f;
        addAndMakeVisible (b);
    }
    audio.setVisible (standalone);
}

MoreView::~MoreView() = default;

void MoreView::tick()
{
    const auto clickLit = isClickOn && isClickOn() ? Lit::current : Lit::none;
    const auto darkLit = isDark && isDark() ? Lit::current : Lit::none;
    if (click.lit != clickLit) { click.lit = clickLit; click.repaint(); }
    if (dark.lit != darkLit) { dark.lit = darkLit; dark.repaint(); }
    repaint (showNameArea);
    const bool guide = hasPage && hasPage ("Guide");
    if (guideTile->isVisible() != guide)
    {
        guideTile->setVisible (guide);
        resized();
    }
}

void MoreView::paint (juce::Graphics& g)
{
    const auto& p = pal();
    drawFace (g, getLocalBounds());
    engraved (g, "The tools in full", toolsLabel, juce::Justification::centredLeft, 13.0f);
    engraved (g, "Your show", showLabel, juce::Justification::centredLeft, 13.0f);
    engraved (g, juce::String::fromUTF8 ("Set-up \xc2\xb7 view"), setupLabel, juce::Justification::centredLeft, 13.0f);

    // The show's name, on an LCD.
    if (! showNameArea.isEmpty())
    {
        const auto lcd = showNameArea.toFloat();
        drawLcd (g, lcd);
        const auto name = showName ? showName() : juce::String();
        lcdText (g, name.isEmpty() ? juce::String ("UNSAVED SHOW") : name.toUpperCase(), lcd.reduced (12.0f, 0.0f), 24.0f,
                 juce::Justification::centredLeft);
    }
    juce::ignoreUnused (p);
}

void MoreView::resized()
{
    auto area = getLocalBounds().reduced (22, 14);

    // The right column: your show, then set-up.
    auto right = area.removeFromRight (juce::jlimit (280, 340, area.getWidth() / 4));
    area.removeFromRight (22);
    const int gap = 10;
    const int bw = (right.getWidth() - gap) / 2;
    const int bh = juce::jlimit (46, 62, (right.getHeight() - 3 * 24 - 40 - 6 * gap ) / 5);
    const auto pair = [&] (juce::Rectangle<int>& col, juce::Component& a, juce::Component* b)
    {
        auto row = col.removeFromTop (bh);
        a.setBounds (row.removeFromLeft (bw));
        if (b != nullptr)
            b->setBounds (row.withTrimmedLeft (gap));
        col.removeFromTop (gap);
    };
    showLabel = right.removeFromTop (24);
    showNameArea = right.removeFromTop (40);
    right.removeFromTop (gap);
    pair (right, save, &saveAs);
    pair (right, open, &exportButton);
    right.removeFromTop (10);
    setupLabel = right.removeFromTop (24);
    if (standalone)
    {
        pair (right, audio, &click);
        pair (right, dark, &popOut);
    }
    else
    {
        pair (right, click, &dark);
        pair (right, popOut, nullptr);
    }

    // The tools: the Song builder big on the left, the rest (and The Guide) in a grid of two rows.
    toolsLabel = area.removeFromTop (24);
    toolsArea = area;
    std::vector<juce::Component*> small;
    for (int i = 1; i < tiles.size(); ++i)
        small.push_back (tiles[i]);
    if (guideTile->isVisible())
        small.push_back (guideTile.get());
    const int cols = juce::jmax (1, ((int) small.size() + 1) / 2);
    const int tgap = 14;
    const int colW = (area.getWidth() - cols * tgap) / (cols + 1);
    const int rowH = (area.getHeight() - tgap) / 2;
    tiles[0]->setBounds (area.removeFromLeft (colW + colW / 4));
    area.removeFromLeft (tgap);
    const int smallW = (area.getWidth() - (cols - 1) * tgap) / cols;
    for (int i = 0; i < (int) small.size(); ++i)
    {
        const int c = i % cols, r = i / cols;
        small[(size_t) i]->setBounds (area.getX() + c * (smallW + tgap), area.getY() + r * (rowH + tgap), smallW, rowH);
    }
}
