#include "DrumView.h"
#include "StepMarks.h"

#include "core/AudioFiles.h"

using namespace crate;
namespace st = crate::drums::steptools;

namespace
{
namespace col
{
    const juce::Colour face { 0xff2a2826 }, faceLight { 0xff3a3734 }, band { 0xffcfc8b8 }, bandInk { 0xff2a2826 },
        ink { 0xffefe6cf }, ink2 { 0xffa9a291 }, well { 0xff171614 }, line { 0xff4a4642 },
        red { 0xffd8452c }, orange { 0xffe98b2a }, yellow { 0xffe8c23a }, cream { 0xffefe6cf },
        lcd { 0xff1c1408 }, lcdInk { 0xffffb54a }, lamp { 0xffff3b22 }, lampOff { 0xff3a1612 };
}

constexpr int kCheek = 22;        // the walnut on each side
constexpr int kSide = kCheek + 8; // where the panel's parts start
constexpr int kHeaderH = 58, kToolH = 36, kBarH = 34, kRowH = 30, kChainH = 32;
constexpr int kVoiceFull = 232, kVoiceCompact = 118, kKeysMin = 104, kKeysMax = 150, kGridMin = 219;
constexpr int kGlobalW = 110;

// The step keys' colours in groups of four, as on the classic boxes.
juce::Colour stepColour (int step)
{
    const juce::Colour colours[] = { col::red, col::orange, col::yellow, col::cream };
    return colours[(step / 4) % 4];
}

// Each sound's colour: drums red, toms orange, percussion yellow, metal cream.
juce::Colour voiceColour (int voice)
{
    if (voice <= drums::rim)
        return col::red;
    if (voice <= drums::highTom)
        return col::orange;
    if (voice <= drums::cowbell)
        return col::yellow;
    return col::cream;
}

constexpr int kKnobs = 6;
const char* const knobNames[kKnobs] = { "Level", "Tune", "Decay", "Tone", "Snappy", "Pan" };
const char* const knobShort[kKnobs] = { "LEVEL", "TUNE", "DECAY", "TONE", "SNAPPY", "PAN" };
const char* const knobTips[kKnobs] = { "How loud it is.", "Its pitch, up or down an octave.", "How long it rings: short and tight to long and boomy.",
                                       "Its colour: the kick's pitch sweep, the balance of the snare's two drums, the hats' and cymbal's "
                                       "brightness, the clap's colour.",
                                       "The snare's snares (more rattle), and the attack on the others: the kick's beater click, the toms' "
                                       "skin, the hats' tick, the cowbell's clank.",
                                       "Where it sits, left to right." };

juce::Font panelFont (float size, bool bold = true)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (0.06f);
}

// A step's level as how bright its lamp and cap are (soft hits are dimmer).
float levelBrightness (int velocity)
{
    return velocity > 80 ? 1.0f : velocity > 45 ? 0.62f : 0.4f;
}

//==============================================================================
// Seven-segment digits for the display (a b c d e f g, as bits 0-6).
int segmentsFor (juce::juce_wchar c)
{
    switch (c)
    {
        case '0': return 63;
        case '1': return 6;
        case '2': return 91;
        case '3': return 79;
        case '4': return 102;
        case '5': return 109;
        case '6': return 125;
        case '7': return 7;
        case '8': return 127;
        case '9': return 111;
        case 'A': return 119;
        case 'B': return 124;
        case '-': return 64;
        default: return 0;
    }
}

void drawDigit (juce::Graphics& g, juce::Rectangle<float> r, int mask, juce::Colour on)
{
    const float t = r.getHeight() * 0.13f, w = r.getWidth(), h = r.getHeight();
    auto bar = [&] (float cx, float cy, float length, bool horizontal, bool lit)
    {
        juce::Path p;
        const float a = length * 0.5f, k = t * 0.5f;
        if (horizontal)
        {
            p.startNewSubPath (cx - a, cy);
            p.lineTo (cx - a + k, cy - k);
            p.lineTo (cx + a - k, cy - k);
            p.lineTo (cx + a, cy);
            p.lineTo (cx + a - k, cy + k);
            p.lineTo (cx - a + k, cy + k);
        }
        else
        {
            p.startNewSubPath (cx, cy - a);
            p.lineTo (cx + k, cy - a + k);
            p.lineTo (cx + k, cy + a - k);
            p.lineTo (cx, cy + a);
            p.lineTo (cx - k, cy + a - k);
            p.lineTo (cx - k, cy - a + k);
        }
        p.closeSubPath();
        g.setColour (lit ? on : on.withAlpha (0.09f));
        g.fillPath (p);
    };
    const float lh = w - t * 1.15f, lv = h * 0.5f - t * 0.95f;
    const float x0 = r.getX() + t * 0.5f, x1 = r.getRight() - t * 0.5f, cx = r.getCentreX();
    bar (cx, r.getY() + t * 0.5f, lh, true, (mask & 1) != 0);
    bar (x1, r.getY() + h * 0.25f, lv, false, (mask & 2) != 0);
    bar (x1, r.getY() + h * 0.75f, lv, false, (mask & 4) != 0);
    bar (cx, r.getBottom() - t * 0.5f, lh, true, (mask & 8) != 0);
    bar (x0, r.getY() + h * 0.75f, lv, false, (mask & 16) != 0);
    bar (x0, r.getY() + h * 0.25f, lv, false, (mask & 32) != 0);
    bar (cx, r.getCentreY(), lh, true, (mask & 64) != 0);
}

// Draws `text` in seven segments (digits, A, B, - and /) from the left of `r`; returns the width used.
float drawSegments (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour on)
{
    const float cellW = r.getHeight() * 0.56f, gap = r.getHeight() * 0.22f;
    float x = r.getX();
    for (auto c : text)
    {
        if (c == '/')
        {
            g.setColour (on);
            g.drawLine (x + cellW * 0.15f, r.getBottom() - 1.0f, x + cellW * 0.7f, r.getY() + 1.0f, r.getHeight() * 0.1f);
            x += cellW * 0.8f + gap;
            continue;
        }
        drawDigit (g, { x, r.getY(), cellW, r.getHeight() }, 0, on);   // the unlit ghost
        drawDigit (g, { x, r.getY(), cellW, r.getHeight() }, segmentsFor (c), on);
        x += cellW + gap;
    }
    return x - r.getX() - gap;
}

// How a hit looks (ticks for a roll, rising ticks for builds, a grace tick for a flam).
void drawHitMarks (juce::Graphics& g, juce::Rectangle<float> r, int hits, int shape, juce::Colour ink, bool label)
{
    st::drawHitMarks (g, r, hits, shape, ink, label, panelFont (11.0f));
}

void drawLamp (juce::Graphics& g, juce::Point<float> c, float diameter, float lit, juce::Colour onColour)
{
    const auto r = juce::Rectangle<float> (diameter, diameter).withCentre (c);
    if (lit > 0.0f)
    {
        g.setColour (onColour.withAlpha (0.22f * lit));
        g.fillEllipse (r.expanded (diameter * 0.45f));
    }
    g.setColour (col::lampOff.interpolatedWith (onColour, lit));
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 1.0f);
    if (lit > 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.35f * lit));
        g.fillEllipse (r.reduced (diameter * 0.32f).translated (-diameter * 0.1f, -diameter * 0.1f));
    }
}

//==============================================================================
// Lays components out along a row: each has a wanted width; if they don't all fit they shrink a little together.
struct RowItem { juce::Component* c; int w; };
void layoutRow (juce::Rectangle<int> r, std::initializer_list<RowItem> items, int gap, bool fromRight = false)
{
    int total = (int) (items.size() > 0 ? (items.size() - 1) * (size_t) gap : 0);
    for (auto& i : items)
        total += i.w;
    const float scale = total > r.getWidth() ? (float) (r.getWidth() - (int) (items.size() - 1) * gap) / (float) (total - (int) (items.size() - 1) * gap) : 1.0f;
    std::vector<int> widths;
    for (auto& i : items)
        widths.push_back (i.w == 0 ? 0 : juce::jmax (8, (int) ((float) i.w * scale)));
    int used = (int) (items.size() - 1) * gap;
    for (auto w : widths)
        used += w;
    int x = fromRight ? r.getRight() - used : r.getX();
    size_t k = 0;
    for (auto& i : items)
    {
        if (i.c != nullptr)
            i.c->setBounds (x, r.getY(), widths[k], r.getHeight());
        x += widths[k++] + gap;
    }
}
}

//==============================================================================
// Black knobs with a cream pointer and a coloured ring, chunky lit buttons.
class DrumView::Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ComboBox::backgroundColourId, col::well);
        setColour (juce::ComboBox::textColourId, col::ink);
        setColour (juce::ComboBox::outlineColourId, col::line);
        setColour (juce::ComboBox::arrowColourId, col::ink2);
        setColour (juce::PopupMenu::backgroundColourId, col::face);
        setColour (juce::PopupMenu::textColourId, col::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::orange);
        setColour (juce::PopupMenu::highlightedTextColourId, col::well);
        setColour (juce::TextButton::textColourOffId, col::ink);
        setColour (juce::TextButton::textColourOnId, col::well);
        setColour (juce::Slider::textBoxTextColourId, col::ink);
        setColour (juce::Slider::textBoxBackgroundColourId, col::well);
        setColour (juce::Slider::textBoxOutlineColourId, col::line);
        setColour (juce::BubbleComponent::backgroundColourId, col::lcd);
        setColour (juce::BubbleComponent::outlineColourId, col::orange);
        setColour (juce::TooltipWindow::backgroundColourId, col::well);
        setColour (juce::TooltipWindow::textColourId, col::ink);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        const auto ring = s.getProperties().contains ("ring") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["ring"]) : col::orange;
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        const auto c = r.getCentre();
        const float radius = size * 0.5f;
        const float ringW = juce::jmax (2.0f, size * 0.09f);

        // The value arc (from the middle for a centred setting such as tune and pan).
        const bool centred = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float from = centred ? (start + end) * 0.5f : start;
        const float to = start + pos * (end - start);
        juce::Path track, value;
        track.addCentredArc (c.x, c.y, radius - ringW * 0.5f, radius - ringW * 0.5f, 0, start, end, true);
        g.setColour (col::well);
        g.strokePath (track, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        value.addCentredArc (c.x, c.y, radius - ringW * 0.5f, radius - ringW * 0.5f, 0, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (ring);
        g.strokePath (value, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The knob: a black skirted cap with a lighter rim.
        const float capR = radius - ringW - 1.5f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4744), c.x, c.y - capR, juce::Colour (0xff0d0c0b), c.x, c.y + capR, false));
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
        g.setColour (juce::Colour (0xff050505));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.2f);
        const float inner = capR * 0.72f;
        g.setColour (juce::Colour (0xff1d1c1a));
        g.fillEllipse (c.x - inner, c.y - inner, inner * 2, inner * 2);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.3f, -capR + 1.2f, 2.6f, capR * 0.55f, 1.3f);
        g.setColour (col::cream);
        g.fillPath (pointer, juce::AffineTransform::rotation (to).translated (c));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const bool on = b.getToggleState();
        const auto lit = b.getProperties().contains ("lit") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["lit"]) : col::orange;
        const float flash = (float) b.getProperties().getWithDefault ("flash", 0.0f);
        juce::Colour base = on ? lit : col::band;
        if (b.getProperties().contains ("dark") && ! on)
            base = col::faceLight;
        if (flash > 0.0f)
            base = base.interpolatedWith (lit.brighter (0.6f), flash);
        if (down)
            base = base.darker (0.25f);
        else if (over)
            base = base.brighter (0.08f);
        const float alpha = ! b.isEnabled() || b.getProperties().contains ("dim") ? 0.45f : 1.0f;
        g.setOpacity (alpha);
        g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
        g.fillRoundedRectangle (r.translated (0, 2.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.15f).withAlpha (alpha), r.getX(), r.getY(), base.darker (0.15f).withAlpha (alpha), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        if (b.getProperties().contains ("selected"))
        {
            g.setColour (col::lcdInk);
            g.drawRoundedRectangle (r.expanded (0.5f), 5.0f, 2.2f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getToggleState();
        const bool dark = b.getProperties().contains ("dark") && ! on;
        const float alpha = ! b.isEnabled() || b.getProperties().contains ("dim") ? 0.5f : 1.0f;
        auto area = b.getLocalBounds().reduced (4, 2);
        if (b.getProperties().contains ("dot"))
        {
            const float d = juce::jmin (10.0f, (float) b.getHeight() * 0.3f);
            const auto c = juce::Point<float> ((float) area.getX() + d * 0.5f + 2.0f, (float) b.getHeight() * 0.5f);
            drawLamp (g, c, d, on ? 1.0f : 0.25f, col::lamp);
            area.removeFromLeft ((int) (d + 6.0f));
        }
        g.setColour ((on ? col::well : dark ? col::ink : col::bandInk).withAlpha (alpha));
        g.setFont (panelFont (juce::jmin (15.0f, b.getHeight() * 0.5f)));
        g.drawFittedText (b.getButtonText().toUpperCase(), area, juce::Justification::centred, 1, 0.7f);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override { return panelFont (box.getWidth() < 110 ? 12.0f : 14.0f, false); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        // Narrow boxes (the sounds) keep more room for the name.
        if (box.getWidth() < 110)
        {
            label.setBounds (1, 1, box.getWidth() - 20, box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
            label.setMinimumHorizontalScale (0.7f);
            return;
        }
        LookAndFeel_V4::positionComboBoxText (box, label);
    }
    juce::Font getPopupMenuFont() override { return panelFont (15.0f, false); }
};

//==============================================================================
DrumView::DrumView (DrumProcessor& owner)
    : AudioProcessorEditor (owner), processor (owner), look (std::make_unique<Look>()), grid (*this), keys (*this)
{
    setLookAndFeel (look.get());
    processor.clearRecordedHits();

    auto makeKnob = [this] (juce::Colour ring)
    {
        auto s = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        s->getProperties().set ("ring", (juce::int64) ring.getARGB());
        s->setPopupDisplayEnabled (true, true, this);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        return s;
    };

    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto& c = columns[(size_t) v];
        for (int k = 0; k < kKnobs; ++k)
        {
            c.knobs[(size_t) k] = makeKnob (voiceColour (v));
            auto& knob = *c.knobs[(size_t) k];
            knob.setTooltip (juce::String (drums::voiceName (v)) + " " + juce::String (knobNames[k]).toLowerCase() + ": " + knobTips[k]
                             + " Double-click to reset.");
            c.attachments[(size_t) k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.params, DrumProcessor::voiceParam (v, knobNames[k]), knob);
            if (auto* param = processor.params.getParameter (DrumProcessor::voiceParam (v, knobNames[k])))
                knob.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            addAndMakeVisible (knob);
        }
        // The SELECT key: picks this sound for the step keys, and plays it.
        c.pad = std::make_unique<juce::TextButton> (drums::voiceShort (v));
        c.pad->getProperties().set ("lit", (juce::int64) voiceColour (v).getARGB());
        c.pad->setTooltip ("SELECT the " + juce::String (drums::voiceName (v)).toLowerCase() + ": the step keys below now write this sound, and it plays "
                           "(note " + juce::MidiMessage::getMidiNoteName (drums::noteForVoice (v), true, true, 3) + " on your keys or pads plays it too).");
        c.pad->onClick = [this, v] { selectVoice (v); };
        addAndMakeVisible (*c.pad);

        // The name lights while the sound is on; click it to mute.
        c.name = std::make_unique<juce::TextButton> (juce::String (drums::voiceName (v)));
        c.name->getProperties().set ("lit", (juce::int64) voiceColour (v).getARGB());
        c.name->getProperties().set ("dark", true);
        c.name->setTooltip ("Click to mute or unmute the " + juce::String (drums::voiceName (v)).toLowerCase() + ".");
        c.name->setToggleState (processor.params.getRawParameterValue (DrumProcessor::voiceParam (v, "Mute"))->load() < 0.5f,
                                juce::dontSendNotification);
        c.name->onClick = [this, v]
        {
            if (auto* p = processor.params.getParameter (DrumProcessor::voiceParam (v, "Mute")))
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        };
        addAndMakeVisible (*c.name);

        c.pad->addMouseListener (this, false);   // right-click: the sound's menu

        // Where its sound comes from: analog, or a sample (your own kit).
        c.source = std::make_unique<SourceChip> (*this, v);
        addAndMakeVisible (*c.source);

        // Its choice of sounds.
        c.sound = std::make_unique<juce::ComboBox>();
        c.sound->addItemList (drums::soundNames (v), 1);
        c.sound->setTooltip ("Which " + juce::String (drums::voiceName (v)).toLowerCase() + " sound: " + drums::soundNames (v).joinIntoString (", ")
                             + ". (It doesn't apply while a sample of your own is on this sound.)");
        c.soundAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.params, DrumProcessor::voiceParam (v, "Sound"), *c.sound);
        addAndMakeVisible (*c.sound);
    }

    const std::pair<const char*, const char*> globals[] = {
        { "accent", "Accent: how much harder the accented steps (the top row) hit." },
        { "swing", "Swing: pushes every second step late, from straight to a heavy shuffle." },
        { "drive", "Drive: warm analog saturation on the whole kit." },
        { "humanize", "Humanize: every hit a little softer or harder, like a drummer." },
        { "volume", "Volume: the whole machine." } };
    for (auto [id, tip] : globals)
    {
        auto* knob = globalKnobs.add (makeKnob (col::orange).release());
        knob->setName (id);
        knob->setTooltip (tip);
        globalAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (processor.params, id, *knob));
        addAndMakeVisible (knob);
    }

    for (int i = 0; i < drums::kPatterns; ++i)
    {
        auto* b = patternButtons.add (new juce::TextButton (juce::String (i + 1)));
        b->setClickingTogglesState (false);
        b->getProperties().set ("dark", true);
        b->setTooltip ("Pattern " + juce::String (i + 1) + ". Picked while it plays, it comes in at the next bar.");
        b->onClick = [this, i] { processor.selectPattern (i); updatePatternButtons(); grid.repaint(); keys.repaint(); };
        addAndMakeVisible (b);
    }

    //----------------------------------------------------------------------- the STEP TOOL strip
    static const char* const toolText[] = { "Hit", "Roll", "Builds", "Flam", "Accent", "Soft/Loud", "Erase" };
    static const char* const toolTips[] = {
        "Hit: press a step key (or a step in the grid) to put a hit there. Press it again to take it away.",
        "Roll: press a step for a stutter roll of N quick hits in that step (pick N with - and +, 2 to 8). Press the same step again to go back to one hit.",
        "Builds: a roll that gets louder with every hit, like a drum fill running up. It uses the roll size N.",
        "Flam: a hit with a soft grace hit just before it. Press the step again to make it a plain hit.",
        "Accent: makes the whole step hit harder (the top row of the grid does the same).",
        "Soft/Loud: press a hit to step its level: full, then soft, then very soft, then full again.",
        "Erase: clears the step (its hit, its roll or flam, its level)." };
    for (size_t i = 0; i < toolButtons.size(); ++i)
    {
        auto& b = toolButtons[i];
        b.setButtonText (toolText[i]);
        b.getProperties().set ("dark", true);
        b.getProperties().set ("lit", (juce::int64) col::lcdInk.getARGB());
        b.setTooltip (toolTips[i]);
        b.onClick = [this, i] { setTool ((Tool) i); };
        addAndMakeVisible (b);
    }
    for (auto* b : { &rollLess, &rollMore })
    {
        b->getProperties().set ("dark", true);
        addAndMakeVisible (b);
    }
    rollLess.setTooltip ("A smaller roll (down to 2 hits).");
    rollMore.setTooltip ("A bigger roll (up to 8 hits).");
    rollLess.onClick = [this] { setRollCount (rollCount - 1); setTool (Tool::roll); };
    rollMore.onClick = [this] { setRollCount (rollCount + 1); setTool (Tool::roll); };

    for (auto* b : { &keysMode, &gridMode })
    {
        b->getProperties().set ("dark", true);
        b->getProperties().set ("lit", (juce::int64) col::lcdInk.getARGB());
        addChildComponent (b);
    }
    keysMode.setTooltip ("Show the 16 big step keys (this window is too short for the keys and the grid together).");
    gridMode.setTooltip ("Show the grid of every sound (this window is too short for the keys and the grid together; the sound knobs fold away to make room).");
    keysMode.onClick = [this] { setShowKeys (true); };
    gridMode.onClick = [this] { setShowKeys (false); };

    recButton.getProperties().set ("dark", true);
    recButton.getProperties().set ("lit", (juce::int64) juce::Colour (0xffe9503a).getARGB());
    recButton.getProperties().set ("dot", true);
    recButton.setClickingTogglesState (true);
    recButton.setTooltip ("Rec live: while the pattern plays, the notes you play (the keys, the pads, a controller) are written into the pattern at the nearest step.");
    recButton.onClick = [this]
    {
        processor.clearRecordedHits();
        processor.recLive = recButton.getToggleState();
    };
    addAndMakeVisible (recButton);

    undoButton.getProperties().set ("dark", true);
    redoButton.getProperties().set ("dark", true);
    undoButton.setTooltip ("Undo the last change to a pattern from this panel (50 steps back).");
    redoButton.setTooltip ("Put back what Undo took away.");
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    keys.setTooltip ("The step keys: pick a sound with its SELECT key, pick a tool, then press the keys. A lamp over a key means a hit there. "
                     "Use Roll x N for a stutter roll, Flam, Builds, Soft/Loud and Accent the same way.");
    addAndMakeVisible (keys);

    //----------------------------------------------------------------------- the BAR row
    for (size_t i = 0; i < barButtons.size(); ++i)
    {
        auto& b = barButtons[i];
        b.getProperties().set ("dark", true);
        b.setTooltip ("Show bar " + juce::String ((int) i + 1) + " (steps " + juce::String ((int) i * 16 + 1) + " to " + juce::String ((int) i * 16 + 16)
                      + "). A bar past the end of the pattern makes the pattern longer.");
        b.onClick = [this, i] { setBar ((int) i); };
        addAndMakeVisible (b);
    }
    followButton.setClickingTogglesState (false);
    followButton.getProperties().set ("dark", true);
    followButton.setTooltip ("Follow: the keys and the grid turn to whichever bar is playing.");
    followButton.onClick = [this] { setFollow (! follow); };
    addAndMakeVisible (followButton);
    copyBarButton.getProperties().set ("dark", true);
    copyBarButton.setTooltip ("Copy this bar into the next one (the pattern gets longer to hold it).");
    copyBarButton.onClick = [this] { copyBarToNext(); };
    addAndMakeVisible (copyBarButton);
    doubleButton.getProperties().set ("dark", true);
    doubleButton.setTooltip ("Double: play the whole pattern twice over (steps 1 to the length are copied after themselves, up to 64 steps).");
    doubleButton.onClick = [this] { doubleLength(); };
    addAndMakeVisible (doubleButton);

    for (auto* b : { &shorter, &longer })
    {
        b->getProperties().set ("dark", true);
        addAndMakeVisible (b);
    }
    shorter.setTooltip ("One step shorter (an odd length makes the pattern drift against the bar). Shift-click: a whole bar shorter.");
    longer.setTooltip ("One step longer, up to 64 (four bars). Shift-click: a whole bar longer.");
    shorter.onClick = [this]
    {
        const int by = juce::ModifierKeys::currentModifiers.isShiftDown() ? 16 : 1;
        editPattern ([by] (drums::Pattern& p) { p.length = juce::jmax (1, p.length - by); });
    };
    longer.onClick = [this]
    {
        const int by = juce::ModifierKeys::currentModifiers.isShiftDown() ? 16 : 1;
        editPattern ([by] (drums::Pattern& p) { p.length = juce::jmin (drums::kMaxSteps, p.length + by); });
    };

    for (auto* b : { &randomButton, &clearButton, &copyButton, &grooveButton, &kitButton })
        addAndMakeVisible (b);
    kitButton.setTooltip ("Set the whole machine up for a style: Classic, Deep sub (the long boom kick), Punchy electro, Latin (congas and "
                          "maracas), Dirty, Tight and dry, Big room. Your patterns stay as they are.");
    kitButton.onClick = [this] { showKitMenu(); };

    // Your own kits: the KIT chip and Load kit... in the nameplate.
    for (auto* b : { &kitChip, &loadKitButton })
        addAndMakeVisible (b);
    kitChip.setTooltip ("The kit on the machine: the sounds you loaded (or Analog). Click for your kits, the analog kits, Save kit as and All analog again.");
    kitChip.onClick = [this] { showKitMenu2 (kitChip); };
    loadKitButton.setTooltip ("Load a kit: one of your saved kits, or a folder of drum hits (Splice, Cymatics, your MPC folders, an external drive). "
                              "You can also drag a folder onto the machine, or one file onto a sound.");
    loadKitButton.onClick = [this] { showKitMenu2 (loadKitButton); };

    // A and B: which one plays (A, B, or A then B for a two-bar beat), and which one you edit.
    const char* variationTips[] = { "Play (and edit) the A variation.", "Play (and edit) the B variation.",
                                    "Play A then B, back and forth: a two-bar beat. Click again to switch which one the grid edits." };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = variationButtons[(size_t) i];
        b.getProperties().set ("lit", (juce::int64) col::orange.getARGB());
        b.getProperties().set ("dark", true);
        b.setTooltip (variationTips[i]);
        b.onClick = [this, i]
        {
            if (i == 2 && processor.getVariation() == 2)
                processor.editVariation = 1 - processor.editVariation;   // A+B again: flip which one you edit
            processor.setVariation (i);
            updateVariationButtons();
            grid.repaint();
            keys.repaint();
            repaint();
        };
        addAndMakeVisible (b);
    }
    copyAB.getProperties().set ("dark", true);
    copyAB.setTooltip ("Copy this pattern's A into its B (and edit B), to make a variation.");
    copyAB.onClick = [this]
    {
        const int index = processor.getSelectedPattern();
        remember (index, 1);
        processor.setPattern (index, processor.getPattern (index, 0), 1);
        processor.editVariation = 1;
        if (processor.getVariation() == 0)
            processor.setVariation (2);
        updateVariationButtons();
        updateUndoButtons();
        grid.repaint();
        keys.repaint();
        repaint();
    };
    addAndMakeVisible (copyAB);

    introPattern.addItem ("No intro", 1);
    for (int i = 1; i <= drums::kPatterns; ++i)
        introPattern.addItem ("Intro: pattern " + juce::String (i), i + 1);
    introPattern.setTooltip ("A pattern that plays once when you press Play from the top, before the beat comes in.");
    introAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "intro", introPattern);
    addAndMakeVisible (introPattern);

    chainButton.setClickingTogglesState (true);
    chainButton.getProperties().set ("lit", (juce::int64) juce::Colour (0xff57e389).getARGB());
    chainButton.getProperties().set ("dark", true);
    chainButton.setTooltip ("Play the chain: the patterns along this row, one after another, round and round (each plays its whole length).");
    chainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.params, "chain", chainButton);
    addAndMakeVisible (chainButton);
    addChain.getProperties().set ("dark", true);
    addChain.setTooltip ("Put the pattern you're editing (its A or B) on the end of the chain.");
    addChain.onClick = [this]
    {
        auto c = processor.getChain();
        if ((int) c.size() < drums::kChain)
        {
            c.push_back (processor.getSelectedPattern() * 2 + processor.editVariation);
            processor.setChain (c);
        }
        repaint (chainArea);
    };
    clearChain.getProperties().set ("dark", true);
    clearChain.setTooltip ("Empty the chain.");
    clearChain.onClick = [this] { processor.setChain ({}); repaint (chainArea); };
    addAndMakeVisible (addChain);
    addAndMakeVisible (clearChain);
    randomButton.setTooltip ("A fresh beat to start from.");
    randomButton.onClick = [this] { editPattern ([this] (drums::Pattern& p) { const int len = p.length; p = DrumProcessor::randomPattern (random); p.length = len <= 16 ? 16 : len; }); };
    clearButton.setTooltip ("Empty this pattern.");
    clearButton.onClick = [this] { editPattern ([] (drums::Pattern& p) { const int len = p.length; p = {}; p.length = len; }); };
    copyButton.setTooltip ("Copy this pattern into the next one and go there, to make a variation or a fill.");
    copyButton.onClick = [this]
    {
        const int from = processor.getSelectedPattern();
        if (from + 1 >= drums::kPatterns)
            return;
        remember (from + 1, 0);
        processor.setPattern (from + 1, processor.getPattern (from));
        processor.selectPattern (from + 1);
        updatePatternButtons();
        updateUndoButtons();
        grid.repaint();
        keys.repaint();
    };
    grooveButton.setTooltip ("Load a ready-made beat into this pattern: electro, boom bap, trap, house, funk, latin, fills.");
    grooveButton.onClick = [this] { showGrooveMenu(); };

    fillButton.getProperties().set ("lit", (juce::int64) col::yellow.getARGB());
    fillButton.setTooltip ("Play the fill pattern once, the next time the beat comes round.");
    fillButton.onClick = [this] { processor.fillNow(); };
    addAndMakeVisible (fillButton);

    sequencerButton.setClickingTogglesState (true);
    sequencerButton.getProperties().set ("lit", (juce::int64) juce::Colour (0xff57e389).getARGB());
    sequencerButton.setTooltip ("The pattern plays with the clock. Off: the pads and keys still play the sounds.");
    sequencerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.params, "sequencer", sequencerButton);
    addAndMakeVisible (sequencerButton);

    stepRate.addItemList (drums::stepRateNames(), 1);
    stepRate.setTooltip ("How long each step is (the scale): 1/16 is the usual; 1/8 is half speed, 1/32 double, 1/16 T and 1/8 T triplets "
                         "(12 steps make a bar of 1/16 T, 6 of 1/8 T).");
    stepRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "stepRate", stepRate);
    fillPattern.addItem ("No fill", 1);
    for (int i = 1; i <= drums::kPatterns; ++i)
        fillPattern.addItem ("Fill: pattern " + juce::String (i), i + 1);
    fillPattern.setTooltip ("The pattern that plays as the fill.");
    fillAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "fill", fillPattern);
    fillEvery.addItemList ({ "When I press Fill", "Every 2nd time", "Every 4th time", "Every 8th time", "Every 16th time" }, 1);
    fillEvery.setTooltip ("When the fill plays by itself: every few times round the pattern, or only when you press Fill.");
    fillEveryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.params, "fillEvery", fillEvery);
    for (auto* c : { &stepRate, &fillPattern, &fillEvery })
        addAndMakeVisible (c);

    if (processor.hasOwnClock())
    {
        startButton.setClickingTogglesState (false);
        startButton.getProperties().set ("lit", (juce::int64) juce::Colour (0xff57e389).getARGB());
        startButton.onClick = [this]
        {
            processor.ownClockRunning = ! processor.ownClockRunning.load();
            startButton.setToggleState (processor.ownClockRunning.load(), juce::dontSendNotification);
            startButton.setButtonText (processor.ownClockRunning.load() ? "Stop" : "Start");
        };
        addAndMakeVisible (startButton);
        tempo = makeKnob (col::orange);
        tempo->setTooltip ("Tempo, in beats per minute.");
        tempoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.params, "tempo", *tempo);
        addAndMakeVisible (*tempo);
    }

    grid.onAudition = [this] (int v) { processor.playVoice (v, 0.7f); };
    grid.setTooltip ("Every sound's steps for the shown bar. Click a step with the picked tool (Hit by default); drag to paint hits. "
                     "Right-click (or Option-click) a step to cycle a roll: 2, 3, 4, 6, 8 hits, then back to one. The top row accents the whole step. "
                     "At the right of each row: shift it a step left or right, reverse it, or the ... menu (copy, paste, clear, spread evenly, its own length).");
    addAndMakeVisible (grid);

    updatePatternButtons();
    updateVariationButtons();
    updateToolButtons();
    updateBarButtons();
    updateUndoButtons();
    updateSelection();
    setResizable (true, true);
    setResizeLimits (1000, 560, 2400, 1600);
    setSize (1180, 840);
    refreshSources();
    startTimerHz (30);
}

DrumView::~DrumView()
{
    stopTimer();
    processor.recLive = false;
    setLookAndFeel (nullptr);
}

//==============================================================================
// Editing: every change goes through editPattern, so Undo covers all of it.
void DrumView::remember (int pattern, int variation)
{
    undoStack.push_back ({ pattern, variation, processor.getPattern (pattern, variation) });
    if (undoStack.size() > 50)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    updateUndoButtons();
}

void DrumView::editPattern (const std::function<void (drums::Pattern&)>& change, bool rememberIt)
{
    if (rememberIt)
        remember (processor.getSelectedPattern(), processor.editVariation);
    auto p = processor.getEditedPattern();
    change (p);
    processor.setEditedPattern (p);
    updateBarButtons();
    grid.repaint();
    keys.repaint();
    repaint (patternRow.getUnion (optionsRow).getUnion (barRow));
}

void DrumView::undo()
{
    if (undoStack.empty())
        return;
    auto s = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back ({ s.pattern, s.variation, processor.getPattern (s.pattern, s.variation) });
    processor.setPattern (s.pattern, s.was, s.variation);
    processor.selectPattern (s.pattern);
    processor.editVariation = s.variation;
    updatePatternButtons();
    updateVariationButtons();
    updateBarButtons();
    updateUndoButtons();
    grid.repaint();
    keys.repaint();
    repaint();
}

void DrumView::redo()
{
    if (redoStack.empty())
        return;
    auto s = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back ({ s.pattern, s.variation, processor.getPattern (s.pattern, s.variation) });
    processor.setPattern (s.pattern, s.was, s.variation);
    processor.selectPattern (s.pattern);
    processor.editVariation = s.variation;
    updatePatternButtons();
    updateVariationButtons();
    updateBarButtons();
    updateUndoButtons();
    grid.repaint();
    keys.repaint();
    repaint();
}

void DrumView::updateUndoButtons()
{
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
    undoButton.repaint();
    redoButton.repaint();
}

void DrumView::selectVoice (int voice, bool audition)
{
    selected = juce::jlimit (0, drums::kVoices - 1, voice);
    updateSelection();
    if (audition)
        processor.playVoice (selected, 0.9f);
}

void DrumView::updateSelection()
{
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto& pad = *columns[(size_t) v].pad;
        const bool on = v == selected;
        if (on != pad.getProperties().contains ("selected"))
        {
            if (on)
                pad.getProperties().set ("selected", true);
            else
                pad.getProperties().remove ("selected");
            pad.repaint();
        }
    }
    repaint (voiceArea);
    keys.repaint();
}

void DrumView::setTool (Tool t)
{
    tool = t;
    updateToolButtons();
}

void DrumView::setRollCount (int n)
{
    rollCount = juce::jlimit (2, drums::kMaxRoll, n);
    updateToolButtons();
}

void DrumView::updateToolButtons()
{
    for (size_t i = 0; i < toolButtons.size(); ++i)
        toolButtons[i].setToggleState ((int) i == (int) tool, juce::dontSendNotification);
    toolButtons[(size_t) Tool::roll].setButtonText (juce::String ("Roll ") + juce::String::fromUTF8 ("\xc3\x97") + juce::String (rollCount));
    rollLess.setEnabled (rollCount > 2);
    rollMore.setEnabled (rollCount < drums::kMaxRoll);
    for (auto* b : { &rollLess, &rollMore, &toolButtons[(size_t) Tool::roll] })
        b->repaint();
    for (auto& b : toolButtons)
        b.repaint();
}

void DrumView::setShowKeys (bool keysNotGrid)
{
    showKeysWhenShort = keysNotGrid;
    resized();
    repaint();
}

int DrumView::getShownBar() const
{
    if (follow && playingHere())
    {
        const int step = processor.getCurrentStep();
        if (step >= 0)
            return juce::jlimit (0, 3, step / 16);
    }
    return bar;
}

bool DrumView::playingHere() const
{
    return processor.getPlayingCode() == processor.getSelectedPattern() * 2 + processor.editVariation
           && ! processor.isPlayingFill() && ! processor.isPlayingIntro();
}

void DrumView::setBar (int b)
{
    bar = juce::jlimit (0, 3, b);
    follow = false;
    if (bar * 16 >= processor.getEditedPattern().length)
        editPattern ([this] (drums::Pattern& p) { st::extendTo (p, bar * 16); });
    updateBarButtons();
    grid.repaint();
    keys.repaint();
    repaint (header);
}

void DrumView::setFollow (bool on)
{
    if (on == follow)
        return;
    if (! on)
        bar = getShownBar();   // stay where you were looking
    follow = on;
    updateBarButtons();
    grid.repaint();
    keys.repaint();
}

void DrumView::updateBarButtons()
{
    const auto length = processor.getEditedPattern().length;
    const int shown = getShownBar();
    shownBarNow = shown;
    for (int i = 0; i < 4; ++i)
    {
        auto& b = barButtons[(size_t) i];
        b.setToggleState (i == shown, juce::dontSendNotification);
        if (i * 16 >= length)
            b.getProperties().set ("dim", true);
        else
            b.getProperties().remove ("dim");
        b.repaint();
    }
    followButton.setToggleState (follow, juce::dontSendNotification);
    followButton.repaint();
    const bool full = length >= drums::kMaxSteps;
    doubleButton.setEnabled (! full);
    copyBarButton.setEnabled (shown < 3);
    doubleButton.repaint();
    copyBarButton.repaint();
}

void DrumView::pressKey (int key)
{
    const int step = getShownBar() * 16 + key;
    if (! juce::isPositiveAndBelow (key, 16) || step >= processor.getEditedPattern().length)
        return;
    bool audible = false;
    const int voice = selected;
    editPattern ([&] (drums::Pattern& p) { audible = st::apply (p, tool, voice, step, rollCount); });
    if (audible)
        processor.playVoice (voice, 0.8f);
}

void DrumView::gridClick (int column, int row, bool roll)
{
    const int step = getShownBar() * 16 + column;
    if (! juce::isPositiveAndBelow (column, 16) || step >= processor.getEditedPattern().length || ! juce::isPositiveAndBelow (row, StepGrid::kRows))
        return;
    bool audible = false;
    const int voice = row - 1;
    if (row > 0)
        selectVoice (voice, false);
    editPattern ([&] (drums::Pattern& p)
    {
        if (row == 0)
            p.accent[(size_t) step] = ! p.accent[(size_t) step];
        else if (roll)
            audible = st::cycleRoll (p, voice, step) > 0;
        else
            audible = st::apply (p, tool, voice, step, rollCount);
    });
    if (audible && grid.onAudition)
        grid.onAudition (voice);
}

void DrumView::copyBarToNext()
{
    const int b = getShownBar();
    if (b >= 3)
        return;
    editPattern ([b] (drums::Pattern& p) { st::copyBar (p, b); });
}

void DrumView::doubleLength()
{
    if (processor.getEditedPattern().length >= drums::kMaxSteps)
        return;
    editPattern ([] (drums::Pattern& p) { st::doubleLength (p); });
}

void DrumView::shiftRow (int voice, int by)
{
    editPattern ([=] (drums::Pattern& p) { st::shiftRow (p, voice, by); });
}

void DrumView::reverseRow (int voice)
{
    editPattern ([=] (drums::Pattern& p) { st::reverseRow (p, voice); });
}

void DrumView::spreadRow (int voice, int hits)
{
    editPattern ([=] (drums::Pattern& p) { st::spreadRow (p, voice, hits); });
}

void DrumView::clearRow (int voice)
{
    editPattern ([=] (drums::Pattern& p) { st::clearRow (p, voice); });
}

void DrumView::copyRow (int voice)
{
    const auto p = processor.getEditedPattern();
    const auto v = (size_t) voice;
    rowClip.hits = p.hits[v];
    rowClip.shape = p.shape[v];
    rowClip.chance = p.chance[v];
    rowClip.cond = p.cond[v];
    rowClip.velocity = p.velocity[v];
    rowClip.nudge = p.nudge[v];
    rowClip.pitch = p.pitch[v];
    rowClip.decayLock = p.decayLock[v];
    rowClip.rowLength = p.rowLength[v];
    rowClip.valid = true;
}

void DrumView::pasteRow (int voice)
{
    if (! rowClip.valid)
        return;
    const auto clip = rowClip;
    editPattern ([=] (drums::Pattern& p)
    {
        const auto v = (size_t) voice;
        p.hits[v] = clip.hits;
        p.shape[v] = clip.shape;
        p.chance[v] = clip.chance;
        p.cond[v] = clip.cond;
        p.velocity[v] = clip.velocity;
        p.nudge[v] = clip.nudge;
        p.pitch[v] = clip.pitch;
        p.decayLock[v] = clip.decayLock;
        p.rowLength[v] = clip.rowLength;
    });
}

void DrumView::flushRecorded()
{
    std::vector<DrumProcessor::RecordedHit> hits;
    DrumProcessor::RecordedHit h;
    while (processor.popRecordedHit (h))
        hits.push_back (h);
    if (hits.empty())
        return;
    const int editedCode = processor.getSelectedPattern() * 2 + processor.editVariation;
    // One Undo for what came in together.
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool edited = pass == 0;
        std::vector<DrumProcessor::RecordedHit> mine;
        for (auto& x : hits)
            if ((x.code == editedCode) == edited)
                mine.push_back (x);
        if (mine.empty())
            continue;
        auto write = [&mine] (drums::Pattern& p)
        {
            for (auto& x : mine)
                if (x.step < p.length && p.hits[(size_t) x.voice][(size_t) x.step] == 0)
                {
                    p.hits[(size_t) x.voice][(size_t) x.step] = 1;
                    p.shape[(size_t) x.voice][(size_t) x.step] = drums::plain;
                }
        };
        if (edited)
        {
            editPattern (write);
        }
        else
        {
            const int index = mine[0].code >> 1, variation = mine[0].code & 1;
            remember (index, variation);
            auto p = processor.getPattern (index, variation);
            write (p);
            processor.setPattern (index, p, variation);
        }
    }
}

//==============================================================================
void DrumView::showKitMenu()
{
    juce::PopupMenu menu;
    const auto names = DrumProcessor::kitNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (kitButton),
                        [safe = juce::Component::SafePointer<DrumView> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->processor.loadKit (result - 1);
                        });
}

//==============================================================================
// Your own kits
juce::String DrumView::getSourceLabel (int voice) const
{
    if (auto s = processor.getVoiceSample (voice))
        return s->name.isNotEmpty() ? s->name : juce::String ("Sample");
    return "Analog";
}

bool DrumView::isSoundChoiceDimmed (int voice) const
{
    return juce::isPositiveAndBelow (voice, drums::kVoices) && columns[(size_t) voice].sound->getAlpha() < 1.0f;
}

juce::String DrumView::getKitLabel() const
{
    const auto name = processor.getKitName();
    return "Kit: " + (name.isNotEmpty() ? name : processor.hasSamples() ? juce::String ("Custom") : juce::String ("Analog"));
}

juce::Rectangle<int> DrumView::columnBounds (int voice) const
{
    const int colW = columnsArea.getWidth() / drums::kVoices;
    return { columnsArea.getX() + voice * colW, voiceArea.getY(), colW, voiceArea.getHeight() };
}

int DrumView::voiceAtPoint (juce::Point<int> p) const
{
    for (int v = 0; v < drums::kVoices; ++v)
        if (columnBounds (v).contains (p))
            return v;
    return -1;
}

void DrumView::say (const juce::String& text)
{
    status = text;
    statusUntil = juce::Time::getMillisecondCounter() + 7000;
    repaint (header);
}

void DrumView::refreshSources()
{
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto& c = columns[(size_t) v];
        auto s = processor.getVoiceSample (v);
        if (s == c.shownSample && c.source->text.isNotEmpty() && (s != nullptr) == c.source->sample)
            continue;
        c.shownSample = s;
        c.source->sample = s != nullptr;
        c.source->text = getSourceLabel (v);
        c.source->setTooltip (s != nullptr
                                  ? "Your own sound on the " + juce::String (drums::voiceName (v)).toLowerCase() + ": " + s->name
                                        + (s->file != juce::File() ? " (" + s->file.getFullPathName() + ")" : juce::String())
                                        + ". Click for Load sample, Hear and Back to analog. Drop another file here to change it."
                                  : "The analog " + juce::String (drums::voiceName (v)).toLowerCase() + ". Click to load a sample of your own, or drop a sound file on this column.");
        c.sound->setAlpha (s != nullptr ? 0.4f : 1.0f);   // the choice doesn't apply while a sample is on it
        c.source->repaint();
    }
    const auto label = getKitLabel();
    if (label != shownKit)
    {
        shownKit = label;
        kitChip.setButtonText (label + " " + juce::String::fromUTF8 ("\xe2\x96\xbe"));
    }
}

bool DrumView::putSampleOnVoice (int voice, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (voice, drums::kVoices))
        return false;
    juce::String error;
    if (! processor.setVoiceSample (voice, file, error))
    {
        say ("Couldn't read " + file.getFileName());
        return false;
    }
    refreshSources();
    selectVoice (voice);   // picks it and plays it
    say (file.getFileName() + " is on the " + juce::String (drums::voiceName (voice)).toLowerCase());
    return true;
}

void DrumView::backToAnalog (int voice)
{
    if (! juce::isPositiveAndBelow (voice, drums::kVoices))
        return;
    processor.setVoiceSample (voice, drums::SharedSample());
    refreshSources();
    say ("The " + juce::String (drums::voiceName (voice)).toLowerCase() + " is analog again");
}

int DrumView::loadKitFiles (const juce::Array<juce::File>& files, const juce::String& name)
{
    const int n = processor.loadKitFiles (files, name);
    refreshSources();
    say (n > 0 ? "Loaded " + juce::String (n) + (n == 1 ? " sound from " : " sounds from ") + name : "No drum sounds found in " + name);
    return n;
}

int DrumView::loadKitFolder (const juce::File& folder)
{
    const int n = processor.loadKitFolder (folder);
    refreshSources();
    say (n > 0 ? "Loaded " + juce::String (n) + (n == 1 ? " sound from " : " sounds from ") + folder.getFileName()
               : "No drum sounds found in " + folder.getFileName());
    return n;
}

namespace
{
juce::File spliceFolder() { return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Splice"); }
}

void DrumView::loadSampleFor (int voice)
{
    auto start = juce::File();
    if (auto s = processor.getVoiceSample (voice); s != nullptr && s->file.getParentDirectory().isDirectory())
        start = s->file.getParentDirectory();
    else if (spliceFolder().isDirectory())
        start = spliceFolder();
    else
        start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Pick a sound for the " + juce::String (drums::voiceName (voice)).toLowerCase(), start,
                                                   AudioFiles::wildcard (processor.formats));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<DrumView> (this), voice] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safe != nullptr && f != juce::File())
                                  safe->putSampleOnVoice (voice, f);
                          });
}

void DrumView::loadKitFolderChooser (const juce::File& startIn)
{
    auto start = startIn;
    if (! start.isDirectory())
        start = spliceFolder().isDirectory() ? spliceFolder() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Pick a folder of drum hits", start, "");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<DrumView> (this)] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safe != nullptr && f.isDirectory())
                                  safe->loadKitFolder (f);
                          });
}

void DrumView::askKitName()
{
    auto* w = new juce::AlertWindow ("Save kit as", "Name this kit. It is kept in Music/Crate/Drum Kits.", juce::MessageBoxIconType::NoIcon, this);
    const auto current = processor.getKitName();
    w->addTextEditor ("name", current.isNotEmpty() ? current : juce::String ("My kit"));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true,
                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<DrumView> (this), w] (int result)
                                                             {
                                                                 if (result != 1 || safe == nullptr)
                                                                     return;
                                                                 const auto name = w->getTextEditorContents ("name").trim();
                                                                 if (name.isEmpty())
                                                                     return;
                                                                 juce::File saved;
                                                                 if (safe->processor.saveUserKit (name, &saved))
                                                                     safe->say ("Saved kit " + name);
                                                                 else
                                                                     safe->say ("Couldn't save the kit " + name);
                                                                 safe->refreshSources();
                                                             }),
                        true);
}

void DrumView::showKitMenu2 (juce::Component& target)
{
    juce::Component::SafePointer<DrumView> safe (this);
    juce::PopupMenu menu, yours, analog;
    const auto saved = drums::kits::userKits();
    if (saved.isEmpty())
        yours.addItem ("(none saved yet: use Save kit as...)", false, false, [] {});
    for (auto& f : saved)
        yours.addItem (f.getFileNameWithoutExtension(), [safe, f]
                       {
                           if (safe == nullptr)
                               return;
                           if (safe->processor.loadUserKit (f))
                               safe->say ("Loaded kit " + f.getFileNameWithoutExtension());
                           else
                               safe->say ("Couldn't load the kit " + f.getFileNameWithoutExtension());
                           safe->refreshSources();
                       });
    menu.addSubMenu ("Your kits", yours);
    menu.addItem ("Load a folder as a kit...", [safe]
                  {
                      if (safe != nullptr)
                          safe->loadKitFolderChooser ({});
                  });
    if (spliceFolder().isDirectory())
        menu.addItem ("Splice", [safe]
                      {
                          if (safe != nullptr)
                              safe->loadKitFolderChooser (spliceFolder());
                      });
    menu.addSeparator();
    menu.addItem ("Save kit as...", [safe]
                  {
                      if (safe != nullptr)
                          safe->askKitName();
                  });
    menu.addItem ("All analog again", processor.hasSamples(), false, [safe]
                  {
                      if (safe == nullptr)
                          return;
                      safe->processor.clearSamples();
                      safe->refreshSources();
                      safe->say ("All analog again");
                  });
    menu.addSeparator();
    const auto names = DrumProcessor::kitNames();
    for (int i = 0; i < names.size(); ++i)
        analog.addItem (names[i], [safe, i, name = names[i]]
                        {
                            if (safe == nullptr)
                                return;
                            safe->processor.loadKit (i);
                            safe->say ("Analog kit " + name);
                        });
    menu.addSubMenu ("Analog kits", analog);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void DrumView::showVoiceMenu (int voice, juce::Component& target)
{
    juce::Component::SafePointer<DrumView> safe (this);
    const auto s = processor.getVoiceSample (voice);
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (drums::voiceName (voice)).toUpperCase());
    menu.addItem ("Load sample...", [safe, voice]
                  {
                      if (safe != nullptr)
                          safe->loadSampleFor (voice);
                  });
    menu.addItem ("Hear", [safe, voice]
                  {
                      if (safe != nullptr)
                          safe->processor.playVoice (voice);
                  });
    menu.addItem ("Back to analog", s != nullptr, false, [safe, voice]
                  {
                      if (safe != nullptr)
                          safe->backToAnalog (voice);
                  });
    if (s != nullptr && s->file != juce::File() && s->file.exists())
        menu.addItem ("Show file in Finder", [file = s->file] { file.revealToUser(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void DrumView::SourceChip::mouseDown (const juce::MouseEvent&)
{
    owner.showVoiceMenu (index, *this);
}

void DrumView::SourceChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.0f, 0.5f);
    if (sample)
    {
        g.setColour (juce::Colour (0xff2f5d94));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colour (0xffe3eefb));
        g.setFont (panelFont (9.5f));
    }
    else
    {
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (col::ink2);
        g.setFont (panelFont (9.5f, false));
    }
    g.drawText (text, r.reduced (4.0f, 0.0f), juce::Justification::centred, true);
    if (owner.dropVoice == index)
    {
        g.setColour (col::orange);
        g.drawRoundedRectangle (r, 3.0f, 1.5f);
    }
}

bool DrumView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory() || file.hasFileExtension ("drumkit") || AudioFiles::isAudioFile (file, processor.formats))
            return true;
    }
    return false;
}

void DrumView::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void DrumView::fileDragMove (const juce::StringArray& files, int x, int y)
{
    int now = -2;
    if (files.size() == 1 && juce::File (files[0]).existsAsFile() && AudioFiles::isAudioFile (juce::File (files[0]), processor.formats))
        if (const int v = voiceAtPoint ({ x, y }); v >= 0)
            now = v;
    if (now != dropVoice)
    {
        dropVoice = now;
        repaint();
    }
}

void DrumView::fileDragExit (const juce::StringArray&)
{
    dropVoice = -1;
    repaint();
}

void DrumView::filesDropped (const juce::StringArray& paths, int x, int y)
{
    dropVoice = -1;
    repaint();
    if (paths.isEmpty())
        return;
    if (paths.size() == 1)
    {
        const juce::File f (paths[0]);
        if (f.isDirectory())
        {
            loadKitFolder (f);
            return;
        }
        if (f.hasFileExtension ("drumkit"))
        {
            if (processor.loadUserKit (f))
                say ("Loaded kit " + f.getFileNameWithoutExtension());
            else
                say ("Couldn't load the kit " + f.getFileNameWithoutExtension());
            refreshSources();
            return;
        }
        // One sound: onto the column it was dropped on, else the selected sound.
        const int v = voiceAtPoint ({ x, y });
        putSampleOnVoice (v >= 0 ? v : selected, f);
        return;
    }
    // Several files (or folders): a kit.
    const auto files = AudioFiles::expandDropped (paths, processor.formats);
    juce::String name = "your files";
    if (files.size() > 0)
    {
        name = files[0].getParentDirectory().getFileName();
        for (auto& f : files)
            if (f.getParentDirectory() != files[0].getParentDirectory())
                name = "dropped files";
    }
    loadKitFiles (files, name);
}

void DrumView::showRowMenu (int voice, juce::Rectangle<int> screenArea)
{
    const auto p = processor.getEditedPattern();
    const int length = p.rowLengthOf (voice);
    juce::PopupMenu menu, spread, rowLength;
    menu.addSectionHeader (juce::String (drums::voiceName (voice)).toUpperCase());
    menu.addItem ("Copy row", [this, voice] { copyRow (voice); });
    menu.addItem ("Paste row", rowClip.valid, false, [this, voice] { pasteRow (voice); });
    menu.addItem ("Clear row", [this, voice] { clearRow (voice); });
    for (int n : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16 })
        if (n <= length)
            spread.addItem (juce::String (n) + (n == 1 ? " hit" : " hits") + " over " + juce::String (length) + " steps", [this, voice, n] { spreadRow (voice, n); });
    menu.addSubMenu ("Spread hits evenly", spread);
    const int own = p.rowLength[(size_t) voice];
    rowLength.addItem ("With the pattern (" + juce::String (p.length) + " steps)", true, own == 0,
                       [this, voice] { editPattern ([voice] (drums::Pattern& q) { q.rowLength[(size_t) voice] = 0; }); });
    for (int n : { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24, 32 })
        if (n != p.length)
            rowLength.addItem (juce::String (n) + " steps", true, own == n,
                               [this, voice, n] { editPattern ([voice, n] (drums::Pattern& q) { q.rowLength[(size_t) voice] = (std::uint8_t) n; }); });
    menu.addSubMenu ("Row length (polymeter)", rowLength);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea));
}

juce::Rectangle<int> DrumView::chainSlot (int i) const
{
    // 16 to a row: one row, or two once the chain is longer than 16.
    const int perRow = drums::kChain / 2;
    const int w = chainSlots.getWidth() / perRow;
    const int h = processor.getChain().size() > (size_t) perRow ? chainSlots.getHeight() / 2 : chainSlots.getHeight();
    return { chainSlots.getX() + (i % perRow) * w, chainSlots.getY() + (i / perRow) * h, w, h };
}

void DrumView::showChainSlotMenu (int slot)
{
    auto c = processor.getChain();
    if (! juce::isPositiveAndBelow (slot, (int) c.size()))
        return;
    juce::PopupMenu menu;
    const auto here = processor.getSelectedPattern() * 2 + processor.editVariation;
    menu.addItem (1, "Make it " + DrumProcessor::codeName (here) + " (the one you're editing)");
    menu.addItem (2, "Edit " + DrumProcessor::codeName (c[(size_t) slot]));
    menu.addItem (3, "Remove");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chainSlot (slot))),
                        [safe = juce::Component::SafePointer<DrumView> (this), slot, here] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& p = safe->processor;
                            auto entries = p.getChain();
                            if (! juce::isPositiveAndBelow (slot, (int) entries.size()))
                                return;
                            if (result == 1)
                                entries[(size_t) slot] = here;
                            else if (result == 3)
                                entries.erase (entries.begin() + slot);
                            else if (result == 2)
                            {
                                p.selectPattern (entries[(size_t) slot] >> 1);
                                p.editVariation = entries[(size_t) slot] & 1;
                                safe->updatePatternButtons();
                                safe->updateVariationButtons();
                                safe->updateBarButtons();
                                safe->grid.repaint();
                                safe->keys.repaint();
                            }
                            p.setChain (entries);
                            safe->repaint();
                        });
}

void DrumView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        for (int v = 0; v < drums::kVoices; ++v)
            if (e.eventComponent == columns[(size_t) v].pad.get())
            {
                showVoiceMenu (v, *columns[(size_t) v].pad);
                return;
            }
    const int count = (int) processor.getChain().size();
    for (int i = 0; i < count; ++i)
        if (chainSlot (i).contains (e.getPosition()))
        {
            showChainSlotMenu (i);
            return;
        }
}

void DrumView::updateVariationButtons()
{
    const int playing = processor.getVariation();
    shownVariation = playing * 2 + processor.editVariation;
    for (int i = 0; i < 3; ++i)
    {
        auto& b = variationButtons[(size_t) i];
        b.setToggleState (i == playing, juce::dontSendNotification);
        b.repaint();
    }
    repaint (optionsRow);
}

void DrumView::updatePatternButtons()
{
    const int selectedPattern = processor.getSelectedPattern();
    const int playing = processor.getPlayingPattern();
    for (int i = 0; i < patternButtons.size(); ++i)
    {
        auto* b = patternButtons[i];
        b->setToggleState (i == selectedPattern, juce::dontSendNotification);
        b->getProperties().set ("lit", (juce::int64) (i == playing || i == selectedPattern ? col::orange : col::yellow).getARGB());
        b->getProperties().set ("flash", i != selectedPattern && i == playing ? 0.5f : 0.0f);
        b->repaint();
    }
}

void DrumView::showGrooveMenu()
{
    juce::PopupMenu menu;
    const auto names = DrumProcessor::grooveNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (grooveButton),
                        [safe = juce::Component::SafePointer<DrumView> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->editPattern ([result] (drums::Pattern& p) { p = DrumProcessor::groove (result - 1); });
                        });
}

void DrumView::timerCallback()
{
    flushRecorded();
    refreshSources();
    if (status.isNotEmpty() && juce::Time::getMillisecondCounter() > statusUntil)
    {
        status = {};
        repaint (header);
    }
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto& c = columns[(size_t) v];
        const bool on = processor.params.getRawParameterValue (DrumProcessor::voiceParam (v, "Mute"))->load() < 0.5f;
        if (c.name->getToggleState() != on)
            c.name->setToggleState (on, juce::dontSendNotification);
        const int hits = processor.getHitCount (v);
        if (hits != c.shownHits)
        {
            c.shownHits = hits;
            c.flash = 1.0f;
        }
        else if (c.flash > 0.0f)
        {
            c.flash = juce::jmax (0.0f, c.flash - 0.12f);
        }
        else
        {
            continue;
        }
        c.pad->getProperties().set ("flash", c.flash);
        c.pad->repaint();
    }

    const int step = processor.getCurrentStep();
    const int pattern = processor.getPlayingPattern();
    const bool fill = processor.isPlayingFill();
    const int code = processor.getPlayingCode(), chainPos = processor.getChainPosition();
    const bool intro = processor.isPlayingIntro();
    if (processor.getVariation() * 2 + processor.editVariation != shownVariation)
    {
        updateVariationButtons();
        grid.repaint();
        keys.repaint();
    }
    if (getShownBar() != shownBarNow)
    {
        updateBarButtons();
        grid.repaint();
        keys.repaint();
    }
    if (step != shownStep || pattern != shownPattern || fill != shownFill || code != shownCode || chainPos != shownChainPos || intro != shownIntro)
    {
        shownStep = step;
        shownFill = fill;
        shownIntro = intro;
        if (chainPos != shownChainPos)
            repaint (chainArea);
        shownCode = code;
        shownChainPos = chainPos;
        if (pattern != shownPattern)
        {
            shownPattern = pattern;
            updatePatternButtons();
        }
        fillButton.setToggleState (fill, juce::dontSendNotification);
        grid.repaint();
        keys.repaint();
        repaint (header);
    }
    // The pattern picked somewhere else (a scene, Logic, the Rig's Groove screen).
    for (int i = 0; i < patternButtons.size(); ++i)
        if (patternButtons[i]->getToggleState() != (i == processor.getSelectedPattern()))
        {
            updatePatternButtons();
            updateBarButtons();
            grid.repaint();
            keys.repaint();
            repaint (patternRow);
            break;
        }
}

//==============================================================================
// The walnut cheeks and the brushed charcoal face, drawn once per size.
void DrumView::rebuildFace()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;
    faceImage = juce::Image (juce::Image::ARGB, w, h, true);
    juce::Graphics g (faceImage);
    juce::Random r (0x0d12);

    // The face: charcoal, with fine vertical brush lines.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2c2a), 0, 0, juce::Colour (0xff232120), 0, (float) h, false));
    g.fillAll();
    for (int x = kCheek; x < w - kCheek; ++x)
    {
        const float a = r.nextFloat();
        if (a < 0.55f)
        {
            g.setColour ((a < 0.28f ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + 0.03f * r.nextFloat()));
            g.fillRect (x, 0, 1, h);
        }
    }

    // The walnut cheeks, with a grain that runs top to bottom.
    for (int side = 0; side < 2; ++side)
    {
        const int x0 = side == 0 ? 0 : w - kCheek;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff7a4b2a), (float) x0, 0, juce::Colour (0xff55321c), (float) (x0 + kCheek), 0, false));
        g.fillRect (x0, 0, kCheek, h);
        for (int i = 0; i < 26; ++i)
        {
            const float x = (float) x0 + r.nextFloat() * (float) kCheek;
            const float drift = (r.nextFloat() - 0.5f) * 6.0f;
            juce::Path grain;
            grain.startNewSubPath (x, 0);
            for (int y = 0; y <= h; y += 40)
                grain.lineTo (x + drift * std::sin ((float) y * 0.011f + (float) i), (float) y);
            g.setColour ((r.nextBool() ? juce::Colour (0xff2c180b) : juce::Colour (0xff9b6a40)).withAlpha (0.10f + 0.18f * r.nextFloat()));
            g.strokePath (grain, juce::PathStrokeType (0.6f + r.nextFloat() * 1.1f));
        }
        // Where the wood meets the face: a lit edge and a shadow.
        const int edge = side == 0 ? kCheek : w - kCheek;
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRect (side == 0 ? edge : edge - 2, 0, 2, h);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), (float) edge, 0,
                                                 juce::Colours::transparentBlack, (float) edge + (side == 0 ? 10.0f : -10.0f), 0, false));
        g.fillRect (side == 0 ? edge : edge - 10, 0, 10, h);
        g.setColour (juce::Colour (0xffb98a5c).withAlpha (0.35f));
        g.fillRect (side == 0 ? 0 : w - 1, 0, 1, h);
    }
}

void DrumView::paint (juce::Graphics& g)
{
    if (faceImage.isNull() || faceImage.getWidth() != getWidth() || faceImage.getHeight() != getHeight())
        rebuildFace();
    g.drawImageAt (faceImage, 0, 0);

    // The nameplate: a lighter brushed band across the top, between the cheeks.
    {
        auto band = header.toFloat().withTrimmedLeft ((float) kCheek).withTrimmedRight ((float) kCheek);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d2c1), 0, band.getY(), juce::Colour (0xffbdb6a5), 0, band.getBottom(), false));
        g.fillRect (band);
        juce::Random r (0x51ab);
        for (int x = (int) band.getX(); x < (int) band.getRight(); x += 2)
        {
            g.setColour (juce::Colours::black.withAlpha (0.015f + 0.025f * r.nextFloat()));
            g.fillRect ((float) x, band.getY(), 1.0f, band.getHeight());
        }
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (band.removeFromBottom (2.0f));
    }
    {
        auto r = header.withTrimmedLeft (kSide).withTrimmedRight (kSide).reduced (0, 6);
        g.setColour (col::bandInk);
        g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold | juce::Font::italic)));
        g.drawText ("Kaleidobox", r.removeFromLeft (160), juce::Justification::centredLeft);
        auto text = r.removeFromLeft (260);
        g.setFont (panelFont (14.0f));
        g.drawText ("DM-12  ANALOG RHYTHM COMPOSER", text.removeFromTop (24), juce::Justification::bottomLeft);
        g.setColour (col::bandInk.withAlpha (0.7f));
        g.setFont (panelFont (11.0f, false));
        if (status.isNotEmpty())
        {
            g.setColour (juce::Colour (0xff1f4f86));
            g.setFont (panelFont (11.0f));
            g.drawText (status, text, juce::Justification::centredLeft);
        }
        else
        {
            g.drawText (processor.hasOwnClock() ? "Press Start to play the pattern" : "Plays in time with the Rig or Logic: press Play there", text,
                        juce::Justification::centredLeft);
        }

        // Coloured stripes, like a model's livery.
        if (r.getWidth() > displayArea.getWidth() + kitArea.getWidth() + 120)
        {
            auto stripes = r.removeFromLeft (110).withSizeKeepingCentre (100, 10).toFloat();
            for (auto c : { col::red, col::orange, col::yellow, col::cream })
            {
                g.setColour (c);
                g.fillRect (stripes.removeFromLeft (25.0f).reduced (1.5f, 0));
            }
        }
    }

    // The display: pattern, bar and step in amber seven segments.
    {
        auto lcd = displayArea.toFloat();
        g.setColour (col::lcd);
        g.fillRoundedRectangle (lcd, 6.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (lcd, 6.0f, 1.5f);
        const auto edited = processor.getEditedPattern();
        const int step = processor.getCurrentStep();
        const int code = processor.getPlayingCode();
        const int number = code >= 0 ? (code >> 1) + 1 : processor.getSelectedPattern() + 1;
        const bool b = code >= 0 ? (code & 1) != 0 : processor.editVariation == 1;
        const juce::String ptn = juce::String (number).paddedLeft ('0', 2) + (b ? "B" : "A");
        const int barNow = step >= 0 ? juce::jlimit (0, 3, step / 16) : getShownBar();
        const juce::String barText = juce::String (barNow + 1) + "/" + juce::String (st::barsOf (edited));
        const juce::String stepText = step >= 0 ? juce::String (step % 16 + 1).paddedLeft ('0', 2) : juce::String ("--");

        auto inner = lcd.reduced (14, 5);
        const float digitH = inner.getHeight() - 12.0f;
        const juce::Colour amber = col::lcdInk;
        struct Field { const char* caption; juce::String text; float width; };
        const float cell = digitH * 0.78f;
        Field fields[] = { { "PTN", ptn, cell * 3 }, { "BAR", barText, cell * 3 - 4 }, { "STEP", stepText, juce::jmax (cell * 2, 32.0f) } };
        const float gap = (inner.getWidth() - (fields[0].width + fields[1].width + fields[2].width)) / 2.0f;
        float x = inner.getX();
        for (auto& f : fields)
        {
            g.setColour (amber.withAlpha (0.6f));
            g.setFont (panelFont (9.5f));
            g.drawText (f.caption, juce::Rectangle<float> (x, inner.getY(), f.width, 10.0f), juce::Justification::centredLeft);
            drawSegments (g, { x, inner.getY() + 12.0f, f.width, digitH }, f.text, amber);
            x += f.width + gap;
        }
        juce::String tag;
        if (processor.isPlayingFill())
            tag = "FILL";
        else if (processor.isPlayingIntro())
            tag = "INTRO";
        else if (processor.getChainPosition() >= 0)
            tag = "CH" + juce::String (processor.getChainPosition() + 1);
        if (tag.isNotEmpty())
        {
            g.setColour (amber);
            g.setFont (panelFont (10.0f));
            g.drawText (tag, lcd.removeFromRight (46).withTrimmedTop (4).withHeight (12), juce::Justification::centred);
        }
    }

    // The sounds: dividers, the knobs' names, a lamp over each SELECT key.
    {
        const int colW = columnsArea.getWidth() / drums::kVoices;
        for (int v = 1; v < drums::kVoices; ++v)
        {
            g.setColour (col::line.withAlpha (0.7f));
            g.drawVerticalLine (columnsArea.getX() + v * colW, (float) voiceArea.getY() + 4.0f, (float) voiceArea.getBottom() - 2.0f);
        }
        g.setFont (panelFont (8.5f));
        for (int v = 0; v < drums::kVoices; ++v)
        {
            auto& c = columns[(size_t) v];
            if (fullVoices)
            {
                g.setColour (col::ink2);
                for (int k = 0; k < kKnobs; ++k)
                {
                    const auto b = c.knobs[(size_t) k]->getBounds();
                    g.drawText (knobShort[k], juce::Rectangle<int> (b.getCentreX() - 22, b.getBottom() - 1, 44, 10), juce::Justification::centred);
                }
            }
            const auto pad = c.pad->getBounds();
            drawLamp (g, { (float) pad.getCentreX(), (float) pad.getY() - 6.0f }, 8.0f, v == selected ? 1.0f : 0.0f, col::lcdInk);
        }
    }
    if (fullVoices)
    {
        g.setFont (panelFont (11.0f));
        for (auto* knob : globalKnobs)
        {
            g.setColour (col::ink2);
            const auto label = knob->getBounds().withHeight (12).translated (0, knob->getHeight());
            g.drawFittedText (knob->getName().toUpperCase(), label.withSizeKeepingCentre (globalArea.getWidth() - 8, 12), juce::Justification::centred, 1, 0.75f);
        }
    }
    if (tempo != nullptr)
    {
        g.setColour (col::bandInk);
        g.setFont (panelFont (12.0f));
        g.drawText ("TEMPO " + juce::String (juce::roundToInt (tempo->getValue())), tempo->getBounds().translated (-84, 0).withWidth (80),
                    juce::Justification::centredRight);
    }

    // The STEP TOOL strip and the keys' well.
    {
        g.setColour (col::faceLight.withAlpha (0.55f));
        g.fillRect (toolArea.withX (kCheek).withWidth (getWidth() - kCheek * 2));
        g.setColour (col::line);
        g.drawHorizontalLine (toolArea.getY(), (float) kCheek, (float) (getWidth() - kCheek));
        g.setColour (col::ink2);
        g.setFont (panelFont (10.0f));
        g.drawText ("STEP TOOL", juce::Rectangle<int> (toolArea.getX(), toolArea.getY(), 56, toolArea.getHeight()), juce::Justification::centredLeft);
    }
    if (keys.isVisible())
    {
        g.setColour (col::well);
        g.fillRoundedRectangle (keysArea.toFloat(), 8.0f);
        g.setColour (col::ink2);
        g.setFont (panelFont (10.5f, false));
        const auto hint = juce::String (drums::voiceName (selected)).toUpperCase() + ": pick a tool, then press the keys.  Roll x N + a key = a stutter roll.";
        g.drawText (hint, keysArea.withTrimmedLeft (14).withHeight (18), juce::Justification::centredLeft);
    }

    // The bar row.
    g.setColour (col::faceLight.withAlpha (0.55f));
    g.fillRect (barRow.withX (kCheek).withWidth (getWidth() - kCheek * 2));
    g.setColour (col::line);
    g.drawHorizontalLine (barRow.getY(), (float) kCheek, (float) (getWidth() - kCheek));
    {
        const auto p = processor.getEditedPattern();
        g.setColour (col::ink);
        g.setFont (panelFont (14.0f));
        const auto b = shorter.getBounds();
        g.drawText (juce::String (p.length) + " STEPS", juce::Rectangle<int> (b.getRight(), b.getY(), longer.getX() - b.getRight(), b.getHeight()),
                    juce::Justification::centred);
        g.setColour (col::ink2);
        g.setFont (panelFont (10.0f));
        g.drawText ("BAR", juce::Rectangle<int> (barButtons[0].getX() - 34, barRow.getY(), 32, barRow.getHeight()), juce::Justification::centredRight);
    }

    // The pattern bar.
    g.setColour (col::face.darker (0.15f));
    g.fillRect (patternRow.getUnion (optionsRow).withX (kCheek).withWidth (getWidth() - kCheek * 2));
    g.setColour (col::line);
    g.drawHorizontalLine (patternRow.getY(), (float) kCheek, (float) (getWidth() - kCheek));
    {
        g.setColour (col::ink2);
        g.setFont (panelFont (10.0f));
        g.drawText ("PATTERN", juce::Rectangle<int> (patternRow.getX(), patternRow.getY(), 56, patternRow.getHeight()), juce::Justification::centredLeft);
        g.setColour (col::ink);
        g.setFont (panelFont (12.0f));
        g.drawText (juce::String ("EDITING ") + (processor.editVariation == 1 ? "B" : "A"),
                    copyAB.getBounds().translated (copyAB.getWidth() + 8, 0).withWidth (86), juce::Justification::centredLeft);
    }

    // The chain: its patterns in order, the one playing lit.
    g.setColour (col::face.darker (0.3f));
    g.fillRect (chainArea.withX (kCheek).withWidth (getWidth() - kCheek * 2));
    g.setColour (col::line);
    g.drawHorizontalLine (chainArea.getY(), (float) kCheek, (float) (getWidth() - kCheek));
    {
        const auto chain = processor.getChain();
        const int playingAt = processor.getChainPosition();
        g.setFont (panelFont (10.0f));
        g.setColour (col::ink2);
        g.drawText ("CHAIN", chainSlots.withWidth (50).translated (-54, 0), juce::Justification::centredRight);
        for (int i = 0; i < drums::kChain; ++i)
        {
            const auto slot = chainSlot (i).toFloat().reduced (2.0f);
            if (i >= drums::kChain / 2 && chain.size() <= (size_t) drums::kChain / 2)
                break;
            if (i < (int) chain.size())
            {
                const auto c = stepColour (i);
                g.setColour (i == playingAt ? c.brighter (0.5f) : c.withAlpha (0.85f));
                g.fillRoundedRectangle (slot, 3.0f);
                g.setColour (col::well);
                g.setFont (panelFont (juce::jmin (13.0f, slot.getHeight() * 0.7f)));
                g.drawFittedText (DrumProcessor::codeName (chain[(size_t) i]), slot.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                if (i == playingAt)
                {
                    g.setColour (juce::Colours::white);
                    g.drawRoundedRectangle (slot.expanded (1.0f), 3.0f, 2.0f);
                }
            }
            else if (! chain.empty())
            {
                g.setColour (col::well.withAlpha (0.6f));
                g.fillRoundedRectangle (slot, 3.0f);
            }
        }
        if (chain.empty())
        {
            g.setColour (col::ink2);
            g.setFont (panelFont (12.0f, false));
            g.drawText ("Pick a pattern and press Add to chain. Play chain plays them one after another.", chainSlots.reduced (4, 0),
                        juce::Justification::centredLeft);
        }
    }

    // A file being dragged over: the column it would land on, or the whole row of sounds for a kit.
    if (dropVoice != -1)
    {
        auto area = dropVoice >= 0 ? columnBounds (dropVoice) : voiceArea;
        g.setColour (col::orange.withAlpha (0.16f));
        g.fillRect (area);
        g.setColour (col::orange);
        g.drawRect (area, 2);
    }
}

void DrumView::resized()
{
    const int W = getWidth(), H = getHeight();
    rebuildFace();
    const auto inner = juce::Rectangle<int> (kSide, 0, juce::jmax (100, W - kSide * 2), H);

    // The heights: what's fixed, then what the sounds, the keys and the grid share.
    const int fixed = kHeaderH + 6 + 4 + kToolH + 4 + kBarH + 4 + kRowH + 4 + kRowH + 4 + kChainH + 6;
    const int avail = juce::jmax (0, H - fixed);
    const bool both = avail >= kVoiceFull + kKeysMin + kGridMin;
    const bool keysOn = both || showKeysWhenShort, gridOn = both || ! showKeysWhenShort;
    int voiceH, keysH = 0, gridH = 0;
    if (both)
    {
        voiceH = juce::jmin (260, kVoiceFull + (avail - kVoiceFull - kKeysMin - kGridMin) / 4);
        keysH = juce::jmin (kKeysMax, kKeysMin + (avail - voiceH - kKeysMin - kGridMin) / 3);
        gridH = avail - voiceH - keysH;
    }
    else if (keysOn)
    {
        keysH = juce::jlimit (kKeysMin, kKeysMax, avail - kVoiceFull);
        voiceH = avail - keysH;
    }
    else
    {
        voiceH = avail >= kVoiceFull + kGridMin ? juce::jmin (kVoiceFull + 20, avail - kGridMin) : kVoiceCompact;
        gridH = avail - voiceH;
    }
    fullVoices = voiceH >= 176;

    // The nameplate: brand, model and stripes on the left, the display at the right.
    header = { 0, 0, W, kHeaderH };
    {
        auto h = header.withTrimmedLeft (kSide).withTrimmedRight (kSide).reduced (0, 7);
        displayArea = h.removeFromRight (juce::jlimit (250, 300, h.getWidth() - 560));
        if (processor.hasOwnClock())
        {
            h.removeFromRight (10);
            startButton.setBounds (h.removeFromRight (78).reduced (0, 3));
            h.removeFromRight (96);
            tempo->setBounds (h.removeFromRight (38).withSizeKeepingCentre (38, 38));
        }
        // The KIT chip and Load kit..., just left of the display (or the standalone's own controls).
        const int kitW = juce::jlimit (150, 262, h.getWidth() - 430);
        h.removeFromRight (8);
        kitArea = h.removeFromRight (kitW);
        auto k = kitArea.withSizeKeepingCentre (kitW, 30);
        loadKitButton.setBounds (k.removeFromRight (juce::jmin (96, kitW * 2 / 5)));
        k.removeFromRight (4);
        kitChip.setBounds (k);
    }

    int y = kHeaderH + 6;
    voiceArea = { kSide, y, inner.getWidth(), voiceH };
    columnsArea = voiceArea;
    globalArea = {};
    if (fullVoices)
        globalArea = columnsArea.removeFromRight (kGlobalW);
    const int colW = columnsArea.getWidth() / drums::kVoices;
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto c = juce::Rectangle<int> (columnsArea.getX() + v * colW, voiceArea.getY(), colW, voiceH);
        auto& column = columns[(size_t) v];
        column.name->setBounds (c.removeFromTop (26).reduced (3, 1));
        column.source->setBounds (c.removeFromTop (16).reduced (4, 1));
        column.sound->setBounds (c.removeFromTop (24).reduced (3, 1));
        column.pad->setBounds (c.removeFromBottom (36).reduced (6, 2));
        c.removeFromBottom (12);   // the lamp
        for (auto& knob : column.knobs)
            knob->setVisible (fullVoices);
        if (fullVoices)
        {
            const int each = c.getHeight() / 3;
            const int half = colW / 2;
            const int size = juce::jlimit (20, 44, juce::jmin (half - 4, each - 11));
            for (int k = 0; k < kKnobs; ++k)
            {
                auto cellArea = juce::Rectangle<int> (c.getX() + (k % 2) * half, c.getY() + (k / 2) * each, half, each - 10);
                column.knobs[(size_t) k]->setBounds (cellArea.withSizeKeepingCentre (size, size));
            }
        }
    }
    for (auto* knob : globalKnobs)
        knob->setVisible (fullVoices);
    if (fullVoices)
    {
        auto g = globalArea.reduced (14, 4);
        const int each = g.getHeight() / globalKnobs.size();
        for (auto* knob : globalKnobs)
        {
            const int size = juce::jmin (40, each - 14);
            knob->setBounds (g.removeFromTop (each).withHeight (size).withSizeKeepingCentre (size, size));
        }
    }
    y = voiceArea.getBottom() + 4;

    // The STEP TOOL strip.
    toolArea = { kSide, y, inner.getWidth(), kToolH };
    {
        auto r = toolArea.withTrimmedLeft (60).reduced (0, 3);
        auto right = r;
        layoutRow (right, { { &recButton, 104 }, { &undoButton, 62 }, { &redoButton, 62 } }, 6, true);
        r.removeFromRight (104 + 62 + 62 + 12 + 14);
        auto& t = toolButtons;
        const bool needModes = ! both;
        keysMode.setVisible (needModes);
        gridMode.setVisible (needModes);
        if (needModes)
            r.removeFromRight (0);
        layoutRow (r, { { &t[(size_t) Tool::hit], 54 }, { &t[(size_t) Tool::roll], 90 }, { &rollLess, 26 }, { &rollMore, 26 },
                        { &t[(size_t) Tool::builds], 68 }, { &t[(size_t) Tool::flam], 54 }, { &t[(size_t) Tool::accent], 68 },
                        { &t[(size_t) Tool::level], 92 }, { &t[(size_t) Tool::erase], 58 },
                        { nullptr, 10 }, { needModes ? &keysMode : nullptr, needModes ? 54 : 0 }, { needModes ? &gridMode : nullptr, needModes ? 54 : 0 } }, 5);
        if (! needModes)
        {
            keysMode.setBounds ({});
            gridMode.setBounds ({});
        }
        keysMode.setToggleState (keysOn, juce::dontSendNotification);
        gridMode.setToggleState (gridOn, juce::dontSendNotification);
    }
    y = toolArea.getBottom() + 4;

    keysArea = {};
    keys.setVisible (keysOn);
    if (keysOn)
    {
        keysArea = { kSide, y, inner.getWidth(), keysH };
        keys.setBounds (keysArea);
        y += keysH;
    }

    barRow = { kSide, y, inner.getWidth(), kBarH };
    {
        auto r = barRow.withTrimmedLeft (36).reduced (0, 3);
        layoutRow (r, { { &barButtons[0], 62 }, { &barButtons[1], 62 }, { &barButtons[2], 62 }, { &barButtons[3], 62 }, { &followButton, 72 },
                        { nullptr, 14 }, { &shorter, 32 }, { nullptr, 92 }, { &longer, 32 }, { nullptr, 12 }, { &stepRate, 96 },
                        { nullptr, 14 }, { &copyBarButton, 92 }, { &doubleButton, 82 } }, 4);
    }
    y = barRow.getBottom() + 4;

    grid.setVisible (gridOn);
    gridArea = {};
    if (gridOn)
    {
        gridArea = { kSide, y, inner.getWidth(), gridH };
        grid.setBounds (gridArea);
        y += gridH;
    }

    patternRow = { kSide, y + 4, inner.getWidth(), kRowH };
    {
        auto r = patternRow.withTrimmedLeft (58);
        auto right = r.removeFromRight (500);
        layoutRow (right.reduced (0, 1), { { &kitButton, 96 }, { &grooveButton, 96 }, { &randomButton, 96 }, { &clearButton, 96 }, { &copyButton, 116 } }, 0, true);
        const int each = juce::jmin (46, r.getWidth() / drums::kPatterns);
        for (auto* b : patternButtons)
            b->setBounds (r.removeFromLeft (each).reduced (2, 1));
    }
    optionsRow = { kSide, patternRow.getBottom() + 4, inner.getWidth(), kRowH };
    {
        auto r = optionsRow.withTrimmedLeft (58).reduced (0, 1);
        layoutRow (r, { { &sequencerButton, 112 }, { &fillPattern, 160 }, { &fillEvery, 160 }, { &fillButton, 64 }, { nullptr, 12 },
                        { &variationButtons[0], 38 }, { &variationButtons[1], 38 }, { &variationButtons[2], 50 }, { &copyAB, 72 } }, 4);
    }
    chainArea = { 0, optionsRow.getBottom() + 4, W, kChainH };
    {
        auto row = juce::Rectangle<int> (kSide, chainArea.getY(), inner.getWidth(), kChainH).reduced (0, 3);
        introPattern.setBounds (row.removeFromLeft (160).reduced (0, 1));
        row.removeFromLeft (8);
        chainButton.setBounds (row.removeFromLeft (104));
        row.removeFromLeft (4);
        addChain.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (4);
        clearChain.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (64);
        chainSlots = row;
    }
    updateBarButtons();
}

//==============================================================================
// The step keys
juce::Rectangle<float> DrumView::StepKeys::keyRect (int key) const
{
    const float small = 5.0f, big = 14.0f, pad = 12.0f;
    const float total = (float) getWidth() - pad * 2.0f - small * 12.0f - big * 3.0f;
    const float w = total / 16.0f;
    const float x = pad + (float) key * w + (float) (key - key / 4) * small + (float) (key / 4) * big;
    const float top = 22.0f, bottom = (float) getHeight() - 20.0f;
    return { x, top + 8.0f, w, juce::jmax (20.0f, bottom - top - 8.0f) };
}

int DrumView::StepKeys::keyAt (juce::Point<float> p) const
{
    for (int i = 0; i < 16; ++i)
        if (keyRect (i).expanded (2.0f, 6.0f).contains (p))
            return i;
    return -1;
}

void DrumView::StepKeys::mouseDown (const juce::MouseEvent& e)
{
    if (const int k = keyAt (e.position); k >= 0)
        owner.pressKey (k);
}

void DrumView::StepKeys::paint (juce::Graphics& g)
{
    const auto p = owner.processor.getEditedPattern();
    const int voice = owner.selected, shownBar = owner.getShownBar();
    const int playStep = owner.playingHere() ? owner.processor.getCurrentStep() : -1;
    for (int i = 0; i < 16; ++i)
    {
        const int step = shownBar * 16 + i;
        const bool inside = step < p.length;
        const auto cap = keyRect (i);
        const int hits = p.hits[(size_t) voice][(size_t) step];
        const int shape = p.shape[(size_t) voice][(size_t) step];
        const int velocity = p.velocity[(size_t) voice][(size_t) step];
        const bool accent = p.accent[(size_t) step];
        const float fade = inside ? 1.0f : 0.3f;

        // The lamp over the key: lit when the step has a hit, dimmer for a soft one.
        const juce::Point<float> lamp { cap.getCentreX(), cap.getY() - 8.0f };
        drawLamp (g, lamp, 9.0f, hits > 0 && inside ? levelBrightness (velocity) : 0.0f, col::lamp);

        // The cap: a chunky key with a darker edge below.
        const auto base = stepColour (step).withMultipliedBrightness (hits > 0 ? 1.0f : 0.86f);
        g.setColour (juce::Colours::black.withAlpha (0.55f * fade));
        g.fillRoundedRectangle (cap.translated (0, 3.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.2f).withAlpha (fade), cap.getX(), cap.getY(), base.darker (0.25f).withAlpha (fade), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f * fade));
        g.drawRoundedRectangle (cap, 5.0f, 1.0f);
        if (hits > 0 && inside)
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.fillRoundedRectangle (cap.reduced (3.0f).withHeight (cap.getHeight() * 0.3f), 3.0f);
        }
        if (inside)
            drawHitMarks (g, cap.reduced (cap.getWidth() * 0.14f, cap.getHeight() * 0.12f), hits, shape, col::well, true);
        if (accent && inside)
        {
            // An accent: a small wedge in the corner of the cap.
            juce::Path wedge;
            wedge.addTriangle (cap.getX() + 5, cap.getY() + 5, cap.getX() + 5, cap.getY() + 15, cap.getX() + 14, cap.getY() + 10);
            g.setColour (col::well.withAlpha (0.85f));
            g.fillPath (wedge);
        }
        if (step == playStep)
        {
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.drawRoundedRectangle (cap.expanded (2.5f), 6.0f, 2.2f);
        }

        // The step number below.
        g.setColour (col::ink2.withAlpha (inside ? 1.0f : 0.4f));
        g.setFont (panelFont (11.0f));
        g.drawText (juce::String (step + 1), juce::Rectangle<float> (cap.getX() - 4.0f, cap.getBottom() + 4.0f, cap.getWidth() + 8.0f, 13.0f), juce::Justification::centred);
    }
}

//==============================================================================
// The step grid
namespace
{
constexpr int kGridLabel = 104, kGridHeader = 22, kToolBox = 24;
}

juce::Rectangle<float> DrumView::StepGrid::cell (int column, int row) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) kGridLabel).withTrimmedTop ((float) kGridHeader).withTrimmedRight ((float) kToolBox * 4.0f + 6.0f);
    const float w = area.getWidth() / (float) kColumns;
    const float h = area.getHeight() / (float) kRows;
    return { area.getX() + w * (float) column, area.getY() + h * (float) row, w, h };
}

std::pair<int, int> DrumView::StepGrid::cellAt (juce::Point<float> p) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) kGridLabel).withTrimmedTop ((float) kGridHeader).withTrimmedRight ((float) kToolBox * 4.0f + 6.0f);
    if (! area.contains (p))
        return { -1, -1 };
    const int c = juce::jlimit (0, kColumns - 1, (int) ((p.x - area.getX()) / (area.getWidth() / (float) kColumns)));
    const int r = juce::jlimit (0, kRows - 1, (int) ((p.y - area.getY()) / (area.getHeight() / (float) kRows)));
    return { c, r };
}

juce::Rectangle<float> DrumView::StepGrid::rowTool (int row, int tool) const
{
    const auto first = cell (0, row);
    const float x = (float) getWidth() - (float) kToolBox * 4.0f - 2.0f + (float) tool * (float) kToolBox;
    return { x, first.getY(), (float) kToolBox, first.getHeight() };
}

std::pair<int, int> DrumView::StepGrid::rowToolAt (juce::Point<float> p) const
{
    for (int row = 1; row < kRows; ++row)
        for (int t = 0; t < 4; ++t)
            if (rowTool (row, t).contains (p))
                return { row, t };
    return { -1, -1 };
}

void DrumView::StepGrid::click (int column, int row, bool roll)
{
    owner.gridClick (column, row, roll);
}

void DrumView::StepGrid::mouseDown (const juce::MouseEvent& e)
{
    if (const auto [row, t] = rowToolAt (e.position); row > 0)
    {
        const int voice = row - 1;
        if (t == 0)
            owner.shiftRow (voice, -1);
        else if (t == 1)
            owner.shiftRow (voice, 1);
        else if (t == 2)
            owner.reverseRow (voice);
        else
            owner.showRowMenu (voice, localAreaToGlobal (rowTool (row, t).toNearestInt()));
        return;
    }
    const auto [c, r] = cellAt (e.position);
    if (c < 0)
    {
        // A sound's name: hear it, and pick it for the step keys.
        if (e.x < kGridLabel && e.y > kGridHeader)
        {
            const int row = (int) ((e.position.y - kGridHeader) / ((getHeight() - kGridHeader) / (float) kRows));
            if (row >= 1 && row < kRows)
            {
                owner.selectVoice (row - 1, false);
                if (onAudition)
                    onAudition (row - 1);
            }
        }
        return;
    }
    const bool roll = e.mods.isPopupMenu() || e.mods.isAltDown();
    click (c, r, roll);
    const auto p = owner.processor.getEditedPattern();
    const int step = owner.getShownBar() * 16 + c;
    // Dragging paints hits (or clears them, or erases) along the row.
    const bool paints = ! roll && (r == 0 || owner.tool == Tool::hit || owner.tool == Tool::erase);
    dragRow = paints ? r : -1;
    dragValue = r == 0 ? (step < p.length && p.accent[(size_t) step] ? 1 : 0)
                       : (step < p.length ? (p.hits[(size_t) (r - 1)][(size_t) step] != 0 ? 1 : 0) : 0);
    lastColumn = c;
}

void DrumView::StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0)
        return;
    const auto [c, r] = cellAt (e.position);
    if (c < 0 || c == lastColumn)
        return;
    lastColumn = c;
    const int step = owner.getShownBar() * 16 + c;
    if (step >= owner.processor.getEditedPattern().length)
        return;
    const int row = dragRow;
    const int value = dragValue;
    owner.editPattern ([=] (drums::Pattern& p)
    {
        if (row == 0)
            p.accent[(size_t) step] = value != 0;
        else if (value != 0)
        {
            if (p.hits[(size_t) (row - 1)][(size_t) step] == 0)
                p.hits[(size_t) (row - 1)][(size_t) step] = 1;
        }
        else
        {
            st::clearStep (p, row - 1, step);
        }
    }, false);
    juce::ignoreUnused (r);
}

void DrumView::StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragRow = -1;
    lastColumn = -1;
}

void DrumView::StepGrid::paint (juce::Graphics& g)
{
    const auto p = owner.processor.getEditedPattern();
    const int shownBar = owner.getShownBar();
    const int playStep = owner.playingHere() ? owner.processor.getCurrentStep() : -1;

    g.setColour (col::well);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // The step numbers, on the coloured step keys.
    for (int c = 0; c < kColumns; ++c)
    {
        const int step = shownBar * 16 + c;
        auto key = cell (c, 0).withY (2.0f).withHeight ((float) kGridHeader - 4.0f).reduced (2.0f, 0);
        const bool inside = step < p.length;
        g.setColour (stepColour (step).withAlpha (inside ? 1.0f : 0.25f));
        g.fillRoundedRectangle (key, 3.0f);
        g.setColour (col::well.withAlpha (inside ? 1.0f : 0.5f));
        g.setFont (panelFont (11.0f));
        g.drawText (juce::String (step + 1), key, juce::Justification::centred);
        if (step == playStep)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (key.expanded (1.0f), 3.0f, 2.0f);
        }
    }

    for (int row = 0; row < kRows; ++row)
    {
        const auto first = cell (0, row);
        auto label = juce::Rectangle<float> (6.0f, first.getY(), (float) kGridLabel - 12.0f, first.getHeight());
        const bool isSelected = row > 0 && row - 1 == owner.selected;
        const auto colour = row == 0 ? col::ink : voiceColour (row - 1);
        if (isSelected)
        {
            g.setColour (col::lcdInk.withAlpha (0.18f));
            g.fillRoundedRectangle (label.expanded (2.0f, 0.0f), 3.0f);
        }
        g.setColour (colour.withAlpha (0.9f));
        g.fillRect (label.removeFromLeft (4.0f).reduced (0, 3.0f));
        g.setColour (isSelected ? col::lcdInk : col::ink);
        g.setFont (panelFont (juce::jmin (13.0f, first.getHeight() * 0.65f)));
        g.drawText (row == 0 ? juce::String ("ACCENT") : juce::String (drums::voiceName (row - 1)).toUpperCase(), label.withTrimmedLeft (6.0f),
                    juce::Justification::centredLeft);

        const int voice = row - 1;
        const int length = row == 0 ? p.length : p.rowLengthOf (voice);
        for (int c = 0; c < kColumns; ++c)
        {
            const int step = shownBar * 16 + c;
            auto r = cell (c, row).reduced (1.5f, 1.5f);
            const bool inside = step < p.length;
            const bool inRow = inside && step < length;
            const int h = row == 0 ? (p.accent[(size_t) step] ? 1 : 0) : p.hits[(size_t) voice][(size_t) step];
            const bool beatStart = c % 4 == 0;
            juce::Colour base = beatStart ? juce::Colour (0xff302d2a) : juce::Colour (0xff24221f);
            if (step == playStep)
                base = base.brighter (0.35f);
            g.setColour (base.withAlpha (inside ? 1.0f : 0.35f));
            g.fillRoundedRectangle (r, 3.0f);
            if (h > 0 && inside)
            {
                const auto on = row == 0 ? col::cream : voiceColour (voice);
                const float level = row == 0 ? 1.0f : levelBrightness (p.velocity[(size_t) voice][(size_t) step]);
                auto fill = (step == playStep ? on.brighter (0.4f) : on).interpolatedWith (col::well, (1.0f - level) * 0.7f);
                g.setColour (inRow ? fill : fill.withAlpha (0.4f));
                g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
                if (row > 0)
                    drawHitMarks (g, r.reduced (3.0f, 2.0f), h, p.shape[(size_t) voice][(size_t) step], col::well, true);
            }
        }

        // The row's tools: shift left and right, reverse, and the ... menu.
        if (row > 0)
        {
            for (int t = 0; t < 4; ++t)
            {
                auto b = rowTool (row, t).reduced (1.5f, 1.5f);
                g.setColour (col::faceLight);
                g.fillRoundedRectangle (b, 3.0f);
                g.setColour (col::ink);
                const auto c = b.getCentre();
                const float s = juce::jmin (4.0f, b.getHeight() * 0.28f);
                juce::Path glyph;
                if (t == 0)
                    glyph.addTriangle (c.x + s, c.y - s, c.x + s, c.y + s, c.x - s, c.y);
                else if (t == 1)
                    glyph.addTriangle (c.x - s, c.y - s, c.x - s, c.y + s, c.x + s, c.y);
                else if (t == 2)
                {
                    glyph.addTriangle (c.x - s * 1.5f, c.y - s * 0.55f, c.x - s * 0.2f, c.y - s * 1.35f, c.x - s * 0.2f, c.y + s * 0.25f);
                    glyph.addTriangle (c.x + s * 1.5f, c.y + s * 0.55f, c.x + s * 0.2f, c.y + s * 1.35f, c.x + s * 0.2f, c.y - s * 0.25f);
                }
                else
                {
                    for (int d = -1; d <= 1; ++d)
                        glyph.addEllipse (c.x + (float) d * s * 1.15f - 1.2f, c.y - 1.2f, 2.4f, 2.4f);
                }
                g.fillPath (glyph);
            }
        }
    }
}
