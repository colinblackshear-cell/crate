#include "VisualsView.h"

namespace crate::visuals
{
namespace
{
const juce::Colour kCream { 0xfff1e7d1 }, kInk { 0xff2a1606 }, kAmber { 0xffffb23e }, kDim { 0xffbdaf97 };
}

//==============================================================================
// A chip on the picture: dark glass, cream when it's the one picked (amber for Drift). A key chip
// is filled with its note's colour and glows while that note sounds.
class VisualsView::Chip : public juce::Component
{
public:
    Chip (VisualsView& v, juce::String t) : view (v), text (std::move (t)) { setName (text); }

    juce::String text;
    bool lit = false, amber = false, cross = false;
    juce::Colour fill;          // a key's colour (transparent: not a key)
    float glow = 0.0f;
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float radius = juce::jmin (10.0f, r.getHeight() * 0.3f);
        if (! fill.isTransparent())
        {
            if (glow > 0.02f)
            {
                g.setColour (fill.withAlpha (0.5f * glow));
                g.fillRoundedRectangle (r.expanded (2.0f), radius + 2.0f);
            }
            g.setColour (fill.withMultipliedAlpha (0.45f + 0.55f * glow));
            g.fillRoundedRectangle (r.withTrimmedTop (-4.0f * glow), radius);
            g.setColour (juce::Colours::black.withAlpha (down ? 0.4f : 0.25f));
            g.drawRoundedRectangle (r, radius, 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (view.font (juce::jmin (18.0f, r.getWidth() * 0.4f)));
            g.drawText (text, r.removeFromBottom (r.getHeight() * 0.4f), juce::Justification::centred, false);
            return;
        }
        const auto face = lit ? (amber ? kAmber : kCream) : juce::Colours::black.withAlpha (0.55f);
        g.setColour (down ? face.brighter (0.2f) : face);
        g.fillRoundedRectangle (r, radius);
        g.setColour (lit ? face : juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (r, radius, 1.0f);
        g.setColour (lit ? kInk : kCream);
        if (cross)
        {
            const auto c = r.getCentre();
            const float s = r.getHeight() * 0.18f;
            g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, 2.0f);
            g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 2.0f);
            return;
        }
        g.setFont (view.font (juce::jmin (r.getHeight() * 0.42f, 17.0f)));
        g.drawFittedText (text, r.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        down = false;
        repaint();
        if (getLocalBounds().expanded (6).contains (e.getPosition()) && onClick)
            onClick();
    }

private:
    VisualsView& view;
    bool down = false;
};

//==============================================================================
// COLOURS: the sets, and the 12 keys for the note colours.
class VisualsView::ColoursPanel : public juce::Component
{
public:
    explicit ColoursPanel (VisualsView& v) : view (v)
    {
        for (int c = 0; c < (int) Colours::count; ++c)
        {
            static const char* shortNames[] { "SCRIABIN", "CIRCLE", "YOURS", "TOOLS", "ACID", "EMBER", "ICE" };
            auto chip = std::make_unique<Chip> (view, shortNames[c]);
            chip->setName (juce::String (coloursName ((Colours) c)).toUpperCase());
            chip->onClick = [this, c] { view.state.colours = c; view.changed(); refresh(); };
            addAndMakeVisible (*chip);
            sets.push_back (std::move (chip));
        }
        for (int pc = 0; pc < 12; ++pc)
        {
            auto key = std::make_unique<Chip> (view, noteName (pc));
            key->setName (juce::String ("Key ") + noteName (pc));
            key->onClick = [this, pc] { pickColour (pc); };
            addAndMakeVisible (*key);
            keys[(size_t) pc] = std::move (key);
        }
        done = std::make_unique<Chip> (view, "DONE");
        done->lit = true;
        done->amber = true;
        done->onClick = [this] { setVisible (false); view.refresh(); };
        addAndMakeVisible (*done);
        refresh();
    }

    void refresh()
    {
        const auto now = view.state.getColours();
        for (int c = 0; c < (int) sets.size(); ++c)
        {
            sets[(size_t) c]->lit = (int) now == c;
            sets[(size_t) c]->repaint();
        }
        const bool notes = followsNotes (now);
        const auto map = view.state.noteColours();
        for (int pc = 0; pc < 12; ++pc)
        {
            keys[(size_t) pc]->fill = map[(size_t) pc];
            keys[(size_t) pc]->setVisible (notes);
            keys[(size_t) pc]->repaint();
        }
        repaint();
    }

    void glow (const std::array<float, 12>& notes)
    {
        for (int pc = 0; pc < 12; ++pc)
            if (std::abs (keys[(size_t) pc]->glow - notes[(size_t) pc]) > 0.03f)
            {
                keys[(size_t) pc]->glow = notes[(size_t) pc];
                keys[(size_t) pc]->repaint();
            }
    }

    juce::String line() const
    {
        switch (view.state.getColours())
        {
            case Colours::scriabin: return "Every note owns a colour, from the composer Scriabin's colour keyboard (1910). Tap a key to make it yours.";
            case Colours::circle: return "A rainbow round the circle of fifths: notes that sound good together sit side by side. Tap a key to make it yours.";
            case Colours::yours: return "Your own colour for every note. Tap a key to change it.";
            case Colours::tools: return "Each tool in its rail colour: whatever plays takes over the picture.";
            default: return "A fixed set of six colours, whatever plays.";
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xe61b1612));
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (r.reduced (0.5f), 16.0f, 1.0f);
        g.setColour (kAmber);
        g.setFont (view.font (15.0f));
        g.drawText ("COLOURS", titleArea, juce::Justification::centredLeft, false);
        g.setColour (kDim);
        g.setFont (view.font (15.0f));
        g.drawFittedText (line(), lineArea, juce::Justification::centredLeft, 3, 0.9f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 14);
        const int chipH = view.options.phone ? 42 : 32;
        auto top = r.removeFromTop (chipH);
        done->setBounds (top.removeFromRight (90));
        titleArea = top;
        r.removeFromTop (10);
        // The sets, in rows that fit.
        const int perRow = r.getWidth() >= 620 ? 7 : 4;
        const int rows = ((int) sets.size() + perRow - 1) / perRow;
        for (int row = 0; row < rows; ++row)
        {
            auto line = r.removeFromTop (chipH);
            r.removeFromTop (6);
            const int w = (line.getWidth() - 6 * (perRow - 1)) / perRow;
            for (int i = 0; i < perRow; ++i)
                if (const int idx = row * perRow + i; idx < (int) sets.size())
                    sets[(size_t) idx]->setBounds (line.getX() + i * (w + 6), line.getY(), w, chipH);
        }
        r.removeFromTop (4);
        lineArea = r.removeFromTop (view.options.phone ? 58 : 40);
        r.removeFromTop (6);
        // The 12 keys: one row on a wide screen, two rows of six on the phone (thumb-sized).
        const int perKeyRow = keysPerRow (view.options.phone), gapK = 6;
        const int kw = (r.getWidth() - gapK * (perKeyRow - 1)) / perKeyRow, kh = keyHeight (view.options.phone, kw);
        for (int pc = 0; pc < 12; ++pc)
            keys[(size_t) pc]->setBounds (r.getX() + (pc % perKeyRow) * (kw + gapK), r.getY() + (pc / perKeyRow) * (kh + gapK), kw, kh);
    }

    static int keysPerRow (bool phone) { return phone ? 6 : 12; }
    static int keyHeight (bool phone, int keyWidth) { return phone ? juce::jmin (58, keyWidth) : juce::jmin (64, (int) ((float) keyWidth * 1.4f)); }
    static int preferredHeight (bool phone, int width)
    {
        const int chipH = phone ? 42 : 32, perRow = width - 32 >= 620 ? 7 : 4;
        const int rows = ((int) Colours::count + perRow - 1) / perRow;
        const int perKeyRow = keysPerRow (phone), kw = (width - 32 - 6 * (perKeyRow - 1)) / perKeyRow, kh = keyHeight (phone, kw);
        const int keyRows = 12 / perKeyRow;
        return 14 + chipH + 10 + rows * (chipH + 6) + 4 + (phone ? 58 : 40) + 6 + keyRows * kh + (keyRows - 1) * 6 + 16;
    }

    juce::Component* find (const juce::String& n)
    {
        for (auto* c : getChildren())
            if (c->getName() == n)
                return c;
        return nullptr;
    }

private:
    class Picker : public juce::ColourSelector, private juce::ChangeListener
    {
    public:
        Picker (VisualsView& v, int note) : juce::ColourSelector (juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace),
                                           view (v), pc (note)
        {
            setCurrentColour (view.state.noteColours()[(size_t) pc]);
            addChangeListener (this);
            setSize (300, 380);
        }
        ~Picker() override { removeChangeListener (this); }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            view.state.setYours (pc, getCurrentColour().withAlpha (1.0f));
            view.changed();
            if (view.panel != nullptr)
                view.panel->refresh();
        }
        VisualsView& view;
        int pc;
    };

    void pickColour (int pc)
    {
        // A key you change makes the colours yours, starting from the ones showing.
        if (view.state.getColours() != Colours::yours)
        {
            const auto shown = view.state.noteColours();
            for (int i = 0; i < 12; ++i)
                view.state.setYours (i, shown[(size_t) i]);
            view.state.colours = (int) Colours::yours;
            view.changed();
            refresh();
        }
        auto& key = *keys[(size_t) pc];
        juce::CallOutBox::launchAsynchronously (std::make_unique<Picker> (view, pc), key.getScreenBounds(), nullptr);
    }

    VisualsView& view;
    std::vector<std::unique_ptr<Chip>> sets;
    std::array<std::unique_ptr<Chip>, 12> keys;
    std::unique_ptr<Chip> done;
    juce::Rectangle<int> titleArea, lineArea;
};

//==============================================================================
VisualsView::VisualsView (VisualTap& t, VisualState& s, Options o)
    : tap (t), state (s), options (std::move (o)), renderer (t, s)
{
    setOpaque (false);   // the picture shows through the controls (JUCE paints them over it)
    setWantsKeyboardFocus (false);
    renderer.onFrame = [this] (const Frame& f)
    {
        drawnLook.store (f.lookB);
        for (size_t i = 0; i < 12; ++i)
            drawnNotes[i].store (f.notes[i]);
        drawnAny = true;
    };
    context.setRenderer (&renderer);
    context.setComponentPaintingEnabled (true);
    context.setContinuousRepainting (true);
    context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);

    close = std::make_unique<Chip> (*this, "Close");
    close->cross = true;
    close->onClick = [this] { if (onClose) onClose(); };
    name = std::make_unique<Chip> (*this, "Look");
    name->onClick = [this] { nextLook (1); };
    drift = std::make_unique<Chip> (*this, "DRIFT");
    drift->amber = true;
    drift->onClick = [this] { toggleDrift(); };
    wild = std::make_unique<Chip> (*this, "WILD");
    wild->onClick = [this] { state.wild = state.wild.load() % 4 + 1; changed(); };
    trails = std::make_unique<Chip> (*this, "TRAILS");
    trails->onClick = [this] { state.trails = state.trails.load() % 4 + 1; changed(); };
    colours = std::make_unique<Chip> (*this, "COLOURS");
    colours->onClick = [this]
    {
        panel->setVisible (! panel->isVisible());
        panel->refresh();
        resized();
        refresh();
    };
    for (int i = 0; i < kLooks; ++i)
    {
        looks[(size_t) i] = std::make_unique<Chip> (*this, juce::String (lookName (i)).toUpperCase());
        looks[(size_t) i]->onClick = [this, i] { pickLook (i); };
    }
    panel = std::make_unique<ColoursPanel> (*this);
    panel->setVisible (false);
    for (auto* c : { close.get(), name.get(), drift.get(), wild.get(), trails.get(), colours.get() })
        addChildComponent (*c);
    for (auto& l : looks)
        addChildComponent (*l);
    addChildComponent (*panel);
    showControls (options.controls);
    refresh();
}

VisualsView::~VisualsView()
{
    stopTimer();
    context.detach();
    if (listening)
        tap.removeListener();
}

juce::Font VisualsView::font (float h) const
{
    if (options.font)
        return options.font (h);
    return juce::Font (juce::FontOptions (h, juce::Font::bold));
}

juce::Component* VisualsView::control (const juce::String& n)
{
    for (auto* c : getChildren())
        if (c->getName() == n)
            return c;
    return panel->find (n);
}

void VisualsView::attachIfShowing()
{
    const bool want = isShowing() && getWidth() > 0 && getHeight() > 0;
    if (want && ! context.isAttached())
    {
        context.attachTo (*this);
        startTimerHz (15);
    }
    else if (! want && context.isAttached())
    {
        context.detach();
        stopTimer();
    }
    // The sound side copies the mix for the picture only while a picture draws.
    if (want != listening)
    {
        listening = want;
        want ? tap.addListener() : tap.removeListener();
    }
}

void VisualsView::showControls (bool on)
{
    controlsOn = on && options.controls;
    for (auto* c : { close.get(), name.get(), drift.get(), wild.get(), trails.get(), colours.get() })
        c->setVisible (controlsOn);
    for (auto& l : looks)
        l->setVisible (controlsOn);
    if (! controlsOn)
        panel->setVisible (false);
    resized();
}

bool VisualsView::coloursOpen() const { return panel->isVisible(); }

void VisualsView::pickLook (int i)
{
    state.drift = false;
    state.look = juce::jlimit (0, kLooks - 1, i);
    changed();
}

void VisualsView::nextLook (int direction)
{
    state.look = ((state.look.load() + direction) % kLooks + kLooks) % kLooks;
    changed();
}

void VisualsView::toggleDrift()
{
    state.drift = ! state.drift.load();
    changed();
}

void VisualsView::changed()
{
    refresh();
    if (onChanged)
        onChanged();
}

void VisualsView::refresh()
{
    const int look = state.look.load();
    for (int i = 0; i < kLooks; ++i)
    {
        looks[(size_t) i]->lit = i == look;
        looks[(size_t) i]->repaint();
    }
    name->text = juce::String (lookName (look)).toUpperCase();
    drift->lit = state.drift.load();
    wild->text = "WILD " + juce::String (state.wild.load()) + "/4";
    trails->text = "TRAILS " + juce::String (state.trails.load()) + "/4";
    colours->text = juce::String (coloursName (state.getColours())).toUpperCase();
    colours->lit = panel->isVisible();
    for (auto* c : { name.get(), drift.get(), wild.get(), trails.get(), colours.get() })
        c->repaint();
    if (panel->isVisible())
        panel->refresh();
    resized();
}

void VisualsView::timerCallback()
{
    if (clock)
        renderer.setClock (clock());
    // Drift (or the Mac's menu and keys) can change the picks: the chips follow.
    const int stamp = state.look.load() | (state.drift.load() ? 16 : 0) | (state.colours.load() << 5) | (state.wild.load() << 9) | (state.trails.load() << 12);
    if (stamp != shownStamp)
    {
        shownStamp = stamp;
        refresh();
    }
    // VISUALS_SNAPSHOT (see VisualsRenderer): the controls too, every 2 seconds, to lay over the picture.
    static const auto snapshotFolder = juce::SystemStats::getEnvironmentVariable ("VISUALS_SNAPSHOT", {});
    if (snapshotFolder.isNotEmpty() && ++snapshotTicks % 30 == 0)
    {
        juce::Image img (juce::Image::ARGB, getWidth(), getHeight(), true);
        juce::Graphics g (img);
        paintEntireComponent (g, true);
        juce::PNGImageFormat png;
        juce::FileOutputStream out (juce::File (snapshotFolder).getChildFile ("controls.png"));
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            png.writeImageToStream (img, out);
        }
    }
    if (panel->isVisible() && drawnAny.load())
    {
        std::array<float, 12> n;
        for (size_t i = 0; i < 12; ++i)
            n[i] = drawnNotes[i].load();
        panel->glow (n);
    }
}

void VisualsView::paint (juce::Graphics& g)
{
    // Painted under the controls when there's no picture (no graphics chip, or before it starts).
    if (! context.isAttached() || ! renderer.working())
    {
        g.fillAll (juce::Colours::black);
        if (! renderer.working())
        {
            g.setColour (kDim);
            g.setFont (font (18.0f));
            g.drawFittedText ("This computer's graphics couldn't draw Visuals.", getLocalBounds().reduced (30), juce::Justification::centred, 3);
        }
    }
}

void VisualsView::resized()
{
    attachIfShowing();
    auto r = insets.subtractedFrom (getLocalBounds());
    const int pad = options.phone ? 14 : 16, chipH = options.phone ? 46 : 34, gap = options.phone ? 6 : 8;
    r.reduce (pad, pad);

    auto top = r.removeFromTop (chipH);
    close->setBounds (top.removeFromLeft (chipH));
    drift->setBounds (top.removeFromRight (options.phone ? 92 : 100));
    const int nameW = juce::jmin (top.getWidth() - 20, juce::roundToInt (font (17.0f).getHeight() * 0.6f * (float) name->text.length()) + 44);
    name->setBounds (top.withSizeKeepingCentre (juce::jmax (90, nameW), chipH));

    // The looks and the settings along the bottom: one row on a wide screen, two on the phone.
    const bool oneRow = r.getWidth() >= 940;
    auto bottom = r.removeFromBottom (oneRow ? chipH : chipH * 2 + gap);
    auto lookRow = oneRow ? bottom.removeFromLeft (bottom.getWidth() * 58 / 100) : bottom.removeFromTop (chipH);
    if (! oneRow)
        bottom.removeFromTop (gap);
    else
        bottom.removeFromLeft (gap * 3);
    {
        const int w = (lookRow.getWidth() - gap * (kLooks - 1)) / kLooks;
        for (int i = 0; i < kLooks; ++i)
            looks[(size_t) i]->setBounds (lookRow.getX() + i * (w + gap), lookRow.getY(), w, chipH);
    }
    {
        auto row = bottom.withHeight (chipH);
        const int w = (row.getWidth() - gap * 2) / 3;
        wild->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (gap);
        trails->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (gap);
        colours->setBounds (row);
    }
    // COLOURS: a card above the bottom rows.
    const int pw = juce::jmin (r.getWidth(), 680);
    const int ph = juce::jmin (r.getHeight() - 12, ColoursPanel::preferredHeight (options.phone, pw));
    panel->setBounds (r.getCentreX() - pw / 2, r.getBottom() - 12 - ph, pw, ph);
}

void VisualsView::mouseDown (const juce::MouseEvent& e)
{
    downAt = e.position;
    downTime = juce::Time::getMillisecondCounter();
}

void VisualsView::mouseUp (const juce::MouseEvent& e)
{
    const auto d = e.position - downAt;
    // A swipe across the picture: the next look (left) or the one before (right).
    if (std::abs (d.x) > 60.0f && std::abs (d.x) > std::abs (d.y) * 1.5f)
    {
        nextLook (d.x < 0 ? 1 : -1);
        return;
    }
    if (d.getDistanceFromOrigin() < 12.0f && juce::Time::getMillisecondCounter() - downTime < 500)
    {
        if (panel->isVisible())
        {
            panel->setVisible (false);
            refresh();
        }
        else if (options.controls)
            showControls (! controlsOn);
    }
}
} // namespace crate::visuals
