#include "RigTitle.h"

#include "Vintage.h"
#include "core/ui/Kaleidoscope.h"

namespace
{
const juce::Colour kStage { 0xff15110e }, kStageGlow { 0xff2a211a }, kPaper { 0xfff6efe1 }, kSoft { 0xffd9ccb4 }, kDim { 0xffa89a86 };
const juce::Colour kCard { 0xff241e19 }, kCardEdge { 0xff3a322b }, kAmber { 0xfff2b632 }, kInk { 0xff231d17 }, kCardDim { 0xffb8aa92 };
constexpr int kWordmarkH = 104, kTaglineH = 72, kLcdH = 30, kCardH = 92, kButtonH = 60, kChipH = 34;

juce::String u8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }
}

// START MAKING (amber, with its key) and TAKE THE 2-MINUTE TOUR (dark).
class RigTitle::WayIn : public juce::Button
{
public:
    WayIn (const juce::String& text, bool amberLook, juce::String keyHint) : juce::Button (text), amber (amberLook), hint (std::move (keyHint))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        auto body = r.withTrimmedBottom (4.0f).translated (0, down ? 2.0f : 0.0f);
        g.setColour (amber ? juce::Colour (0xffb07f12) : juce::Colour (0xff0e0b09));
        g.fillRoundedRectangle (body.translated (0, down ? 1.0f : 4.0f), 12.0f);
        const auto top = amber ? juce::Colour (0xfffcca64) : juce::Colour (0xff3d362e);
        const auto bottom = amber ? juce::Colour (0xffeea02a) : juce::Colour (0xff2a241e);
        g.setGradientFill (juce::ColourGradient (top.brighter (over ? 0.06f : 0.0f), 0, body.getY(), bottom, 0, body.getBottom(), false));
        g.fillRoundedRectangle (body, 12.0f);
        const auto ink = amber ? kInk : juce::Colour (0xfff1e7d1);
        g.setColour (ink);
        g.setFont (vintage::cond (amber ? 22.0f : 18.0f, vintage::Weight::bold, 0.08f));
        auto text = body.toNearestInt();
        if (hint.isNotEmpty())
        {
            const auto font = vintage::cond (13.0f, vintage::Weight::bold, 0.06f);
            const int hw = (int) juce::GlyphArrangement::getStringWidth (font, hint) + 12;
            const int tw = (int) juce::GlyphArrangement::getStringWidth (vintage::cond (22.0f, vintage::Weight::bold, 0.08f), getButtonText().toUpperCase());
            const int x = text.getCentreX() - (tw + 12 + hw) / 2 - 4;
            g.drawText (getButtonText().toUpperCase(), juce::Rectangle<int> (x, text.getY(), tw + 10, text.getHeight()), juce::Justification::centredLeft);
            auto box = juce::Rectangle<float> ((float) (x + tw + 18), (float) text.getCentreY() - 11.0f, (float) hw, 22.0f);
            g.drawRoundedRectangle (box, 4.0f, 1.5f);
            g.setFont (font);
            g.drawText (hint, box, juce::Justification::centred);
            return;
        }
        g.drawText (getButtonText().toUpperCase(), text, juce::Justification::centred);
    }

private:
    bool amber;
    juce::String hint;
};

RigTitle::RigTitle()
{
    start = std::make_unique<WayIn> ("Start making", true, "RETURN");
    tour = std::make_unique<WayIn> ("Take the 2-minute tour", false, juce::String());
    start->onClick = [this] { if (onStart) onStart(); };
    tour->onClick = [this] { if (onTour) onTour(); };
    addAndMakeVisible (*start);
    addAndMakeVisible (*tour);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

RigTitle::~RigTitle() = default;

juce::Button& RigTitle::startButton() { return *start; }
juce::Button& RigTitle::tourButton() { return *tour; }

void RigTitle::setContinue (const juce::String& name, const juce::String& meta, std::vector<juce::Colour> layers)
{
    continueName = name;
    continueMeta = meta;
    continueLayers = std::move (layers);
    resized();
    repaint();
}

void RigTitle::setRecent (std::vector<std::pair<juce::String, juce::File>> songs)
{
    recent = std::move (songs);
    if (recent.size() > 3)
        recent.resize (3);
    repaint();
}

void RigTitle::visibilityChanged()
{
    if (isVisible())
    {
        if (opened < 9.0f)
            opened = 0.0f;
        startTimerHz (30);
        if (isShowing())
            grabKeyboardFocus();
    }
    else
        stopTimer();
}

void RigTitle::timerCallback()
{
    const float level = micLevel ? micLevel() : 0.0f;
    bloom = juce::jmax (bloom * 0.9f, juce::jlimit (0.0f, 1.0f, std::sqrt (level) * 1.4f));
    angle += 0.003f + bloom * 0.04f;
    const bool opening = opened < 3.6f;
    opened += 1.0f / 30.0f;
    if (opening)
        repaint();
    else
        repaint (mandalaRect().expanded (60).toNearestInt());
}

juce::Rectangle<float> RigTitle::mandalaRect() const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float d = juce::jmax (160.0f, juce::jmin (h * 0.62f, w * 0.36f, 600.0f));
    const float cx = juce::jmax (d * 0.5f + 60.0f, w * 0.28f);
    return { cx - d * 0.5f, h * 0.5f - d * 0.5f, d, d };
}

juce::Rectangle<int> RigTitle::column() const
{
    const auto m = mandalaRect();
    const int x = (int) m.getRight() + juce::jmax (40, getWidth() / 16);
    const int w = juce::jmin (600, getWidth() - x - 60);
    const int total = kWordmarkH + 18 + kTaglineH + 14 + kLcdH + 36 + (continueName.isNotEmpty() ? kCardH + 16 : 0) + kButtonH + 22 + kChipH;
    return { x, (getHeight() - total) / 2, juce::jmax (300, w), total };
}

juce::Rectangle<int> RigTitle::continueCard() const
{
    if (continueName.isEmpty())
        return {};
    auto c = column();
    return { c.getX(), c.getY() + kWordmarkH + 18 + kTaglineH + 14 + kLcdH + 36, c.getWidth(), kCardH };
}

void RigTitle::resized()
{
    auto c = column();
    const int y = c.getY() + kWordmarkH + 18 + kTaglineH + 14 + kLcdH + 36 + (continueName.isNotEmpty() ? kCardH + 16 : 0);
    const int sw = (c.getWidth() - 12) * 13 / 23;
    start->setBounds (c.getX(), y, sw, kButtonH);
    tour->setBounds (c.getX() + sw + 12, y, c.getWidth() - sw - 12, kButtonH);
}

juce::Rectangle<int> RigTitle::recentChip (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) recent.size()))
        return {};
    auto c = column();
    const auto font = vintage::barlow (15.0f);
    int x = c.getX() + (int) juce::GlyphArrangement::getStringWidth (font, "Also recent:") + 12;
    const int y = c.getBottom() - kChipH;
    for (int i = 0; i <= index; ++i)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (font, recent[(size_t) i].first) + 22;
        if (i == index)
            return { x, y, juce::jmin (w, c.getRight() - x), kChipH };
        x += w + 10;
    }
    return {};
}

void RigTitle::mouseUp (const juce::MouseEvent& e)
{
    if (continueCard().contains (e.getPosition()) && onContinue)
        return onContinue();
    for (int i = 0; i < (int) recent.size(); ++i)
        if (recentChip (i).contains (e.getPosition()) && onRecent)
            return onRecent (recent[(size_t) i].second);
    if (onStart)
        onStart();
}

bool RigTitle::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::escapeKey || key == juce::KeyPress::spaceKey)
    {
        if (onStart)
            onStart();
        return true;
    }
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        if (onTour)
            onTour();
        return true;
    }
    return false;
}

void RigTitle::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const auto m = mandalaRect();
    g.setGradientFill (juce::ColourGradient (kStageGlow, m.getCentreX(), m.getCentreY(), kStage, m.getCentreX() + w * 0.55f, m.getCentreY(), true));
    g.fillAll();

    g.setColour (kDim);
    g.setFont (vintage::lcd (20.0f));
    g.drawText (u8 ("KB-1 \xc2\xb7 GROOVE STATION"), juce::Rectangle<float> (40, 26, w * 0.5f, 24), juce::Justification::centredLeft);

    crate::kaleidoscope::draw (g, m, angle, bloom, opened, std::fmod (opened, 1.1f) < 0.55f);

    using crate::kaleidoscope::rise;
    auto c = column();
    int y = c.getY();
    const float a = rise (opened, 1.5f), b = rise (opened, 1.8f), d = rise (opened, 2.1f), e = rise (opened, 2.4f), f = rise (opened, 2.7f);
    g.setColour (kPaper.withAlpha (a));
    g.setFont (vintage::wordmark ((float) kWordmarkH));
    g.drawFittedText ("Kaleidobox", juce::Rectangle<int> (c.getX(), y + (int) (18.0f * (1.0f - a)), c.getWidth(), kWordmarkH), juce::Justification::centredLeft, 1, 0.7f);
    y += kWordmarkH + 18;
    g.setColour (kSoft.withAlpha (b));
    g.setFont (vintage::barlow (23.0f));
    g.drawFittedText ("Every sound is the start of a song. Hum it, loop it, build it out with every tool, then take it to the stage or into Logic.",
                      juce::Rectangle<int> (c.getX(), y + (int) (18.0f * (1.0f - b)), c.getWidth(), kTaglineH), juce::Justification::topLeft, 3, 0.9f);
    y += kTaglineH + 14;
    g.setColour (kAmber.withAlpha (d));
    g.setFont (vintage::lcd (25.0f));
    g.drawText ("MAKE A NOISE. THE PETALS LISTEN.", juce::Rectangle<int> (c.getX(), y + (int) (18.0f * (1.0f - d)), c.getWidth(), kLcdH), juce::Justification::centredLeft);

    if (auto card = continueCard().toFloat(); ! card.isEmpty())
    {
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (e);
        card = card.translated (0, 14.0f * (1.0f - e));
        g.setColour (kCard);
        g.fillRoundedRectangle (card, 16.0f);
        g.setColour (kCardEdge);
        g.drawRoundedRectangle (card.reduced (0.75f), 16.0f, 1.5f);
        auto play = card.removeFromRight (86).withSizeKeepingCentre (58, 58);
        g.setColour (kAmber);
        g.fillEllipse (play);
        juce::Path tri;
        const auto pc = play.getCentre();
        tri.addTriangle (pc.x - 6, pc.y - 10, pc.x - 6, pc.y + 10, pc.x + 11, pc.y);
        g.setColour (kInk);
        g.fillPath (tri);
        auto in = card.reduced (22, 13);
        g.setColour (kCardDim);
        g.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.14f));
        g.drawText ("PICK UP WHERE YOU LEFT OFF", in.removeFromTop (17), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xfff1e7d1));
        g.setFont (vintage::barlow (24.0f).boldened());
        g.drawFittedText (continueName, in.removeFromTop (30).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        auto row = in.removeFromTop (18);
        float x = row.getX();
        for (size_t l = 0; l < juce::jmin<size_t> (continueLayers.size(), 10); ++l)
        {
            g.setColour (continueLayers[l]);
            g.fillRoundedRectangle (x, row.getCentreY() - 3.5f, 28.0f, 7.0f, 3.5f);
            x += 34.0f;
        }
        g.setColour (kCardDim);
        g.setFont (vintage::barlow (15.0f));
        g.drawFittedText (continueMeta, row.withLeft (x + (continueLayers.empty() ? 0.0f : 4.0f)).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    if (! recent.empty())
    {
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (f);
        g.setColour (kCardDim);
        g.setFont (vintage::barlow (15.0f));
        g.drawText ("Also recent:", juce::Rectangle<int> (c.getX(), c.getBottom() - kChipH, 140, kChipH), juce::Justification::centredLeft);
        for (int i = 0; i < (int) recent.size(); ++i)
        {
            auto chip = recentChip (i).toFloat();
            g.setColour (kCard);
            g.fillRoundedRectangle (chip, 8.0f);
            g.setColour (juce::Colour (0xfff1e7d1));
            g.drawFittedText (recent[(size_t) i].first, chip.reduced (11, 0).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }

    g.setColour (kDim);
    g.setFont (vintage::barlow (14.0f));
    g.drawText ("Click anywhere or press Esc to skip the opening", juce::Rectangle<float> (0, h - 46, w, 20), juce::Justification::centred);
}
