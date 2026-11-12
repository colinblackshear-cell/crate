#include "DrumView.h"

using namespace crate;

namespace
{
namespace col
{
    const juce::Colour face { 0xff2a2826 }, faceLight { 0xff3a3734 }, band { 0xffcfc8b8 }, bandInk { 0xff2a2826 },
        ink { 0xffefe6cf }, ink2 { 0xffa9a291 }, well { 0xff171614 }, line { 0xff4a4642 },
        red { 0xffd8452c }, orange { 0xffe98b2a }, yellow { 0xffe8c23a }, cream { 0xffefe6cf },
        lcd { 0xff1c1408 }, lcdInk { 0xffffb54a };
}

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

const char* const knobNames[] = { "Level", "Tune", "Decay", "Tone", "Pan" };
const char* const knobTips[] = { "How loud it is.", "Its pitch, up or down an octave.", "How long it rings: short and tight to long and boomy.",
                                 "Its character: the kick's click, the snare's snap, the hats' and cymbal's brightness, the clap's colour.",
                                 "Where it sits, left to right." };

juce::Font panelFont (float size, bool bold = true)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (0.06f);
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
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        const auto c = r.getCentre();
        const float radius = size * 0.5f;

        // The value arc (from the middle for a centred setting such as tune and pan).
        const bool centred = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float from = centred ? (start + end) * 0.5f : start;
        const float to = start + pos * (end - start);
        juce::Path track, value;
        track.addCentredArc (c.x, c.y, radius - 1.5f, radius - 1.5f, 0, start, end, true);
        g.setColour (col::well);
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        value.addCentredArc (c.x, c.y, radius - 1.5f, radius - 1.5f, 0, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (ring);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The knob: a black cap with a lighter rim.
        const float capR = radius - 6.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4744), c.x, c.y - capR, juce::Colour (0xff0d0c0b), c.x, c.y + capR, false));
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
        g.setColour (juce::Colour (0xff050505));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.2f);
        const float inner = capR * 0.72f;
        g.setColour (juce::Colour (0xff1d1c1a));
        g.fillEllipse (c.x - inner, c.y - inner, inner * 2, inner * 2);

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -capR + 1.5f, 3.0f, capR * 0.55f, 1.5f);
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
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0, 2.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), r.getX(), r.getY(), base.darker (0.15f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getToggleState();
        const bool dark = b.getProperties().contains ("dark") && ! on;
        g.setColour (on ? col::well : dark ? col::ink : col::bandInk);
        g.setFont (panelFont (juce::jmin (15.0f, b.getHeight() * 0.5f)));
        g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1, 0.7f);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return panelFont (14.0f, false); }
    juce::Font getPopupMenuFont() override { return panelFont (15.0f, false); }
};

//==============================================================================
DrumView::DrumView (DrumProcessor& owner) : AudioProcessorEditor (owner), processor (owner), look (std::make_unique<Look>()), grid (owner)
{
    setLookAndFeel (look.get());

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
        for (int k = 0; k < 5; ++k)
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
        c.pad = std::make_unique<juce::TextButton> (drums::voiceShort (v));
        c.pad->getProperties().set ("lit", (juce::int64) voiceColour (v).getARGB());
        c.pad->setTooltip ("Play the " + juce::String (drums::voiceName (v)).toLowerCase() + " (or play note "
                           + juce::MidiMessage::getMidiNoteName (drums::noteForVoice (v), true, true, 3) + " on your keys or pads).");
        c.pad->onClick = [this, v] { processor.playVoice (v, 0.9f); };
        addAndMakeVisible (*c.pad);
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
        b->onClick = [this, i] { processor.selectPattern (i); updatePatternButtons(); grid.repaint(); };
        addAndMakeVisible (b);
    }

    for (auto* b : { &shorter, &longer })
    {
        b->getProperties().set ("dark", true);
        addAndMakeVisible (b);
    }
    shorter.setTooltip ("One step shorter (an odd length makes the pattern drift against the bar).");
    longer.setTooltip ("One step longer, up to 32.");
    shorter.onClick = [this] { editPattern ([] (drums::Pattern& p) { p.length = juce::jmax (1, p.length - 1); }); };
    longer.onClick = [this] { editPattern ([] (drums::Pattern& p) { p.length = juce::jmin (drums::kMaxSteps, p.length + 1); }); };

    for (auto* b : { &randomButton, &clearButton, &copyButton, &grooveButton })
        addAndMakeVisible (b);
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
        processor.setPattern (from + 1, processor.getPattern (from));
        processor.selectPattern (from + 1);
        updatePatternButtons();
        grid.repaint();
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
    stepRate.setTooltip ("How long each step is: 1/16 is the usual; 1/8 is half speed, 1/32 double, 1/16 T triplets.");
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
    grid.setTooltip ("Click a step to put a hit there; drag to paint. Right-click (or Option-click) a hit for a roll of 2, 3 or 4. "
                     "The top row accents the whole step.");
    addAndMakeVisible (grid);

    updatePatternButtons();
    setResizable (true, true);
    setResizeLimits (1000, 680, 2400, 1600);
    setSize (1180, 760);
    startTimerHz (30);
}

DrumView::~DrumView()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DrumView::editPattern (const std::function<void (drums::Pattern&)>& change)
{
    const int index = processor.getSelectedPattern();
    auto p = processor.getPattern (index);
    change (p);
    processor.setPattern (index, p);
    grid.repaint();
    repaint (barArea);
}

void DrumView::updatePatternButtons()
{
    const int selected = processor.getSelectedPattern();
    const int playing = processor.getPlayingPattern();
    for (int i = 0; i < patternButtons.size(); ++i)
    {
        auto* b = patternButtons[i];
        b->setToggleState (i == selected, juce::dontSendNotification);
        b->getProperties().set ("lit", (juce::int64) (i == playing || i == selected ? col::orange : col::yellow).getARGB());
        b->getProperties().set ("flash", i != selected && i == playing ? 0.5f : 0.0f);
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
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto& c = columns[(size_t) v];
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
    if (step != shownStep || pattern != shownPattern || fill != shownFill)
    {
        shownStep = step;
        shownFill = fill;
        if (pattern != shownPattern)
        {
            shownPattern = pattern;
            updatePatternButtons();
        }
        fillButton.setToggleState (fill, juce::dontSendNotification);
        grid.repaint();
        repaint (header);
    }
    // The pattern picked somewhere else (a scene, Logic, the Rig's Groove screen).
    for (int i = 0; i < patternButtons.size(); ++i)
        if (patternButtons[i]->getToggleState() != (i == processor.getSelectedPattern()))
        {
            updatePatternButtons();
            grid.repaint();
            repaint (barArea);
            break;
        }
}

//==============================================================================
void DrumView::paint (juce::Graphics& g)
{
    // The face, with a lighter band across the top like the old boxes.
    g.fillAll (col::face);
    {
        auto band = header.toFloat();
        g.setColour (col::band);
        g.fillRect (band);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (band.removeFromBottom (2.0f));
    }
    {
        auto r = header.reduced (20, 8);
        g.setColour (col::bandInk);
        g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold | juce::Font::italic)));
        auto mark = r.removeFromLeft (180);
        g.drawText ("Kaleidobox", mark, juce::Justification::centredLeft);
        g.setFont (panelFont (15.0f));
        g.drawText ("DM-12  ANALOG RHYTHM BOX", r.removeFromLeft (280), juce::Justification::centredLeft);

        // Coloured stripes, like a model's livery.
        auto stripes = r.removeFromLeft (120).withSizeKeepingCentre (110, 10).toFloat();
        for (auto c : { col::red, col::orange, col::yellow, col::cream })
        {
            g.setColour (c);
            g.fillRect (stripes.removeFromLeft (27.5f).reduced (1.5f, 0));
        }

        // The LCD: pattern, step, fill.
        auto lcd = r.removeFromRight (260).toFloat().reduced (0, 2);
        g.setColour (col::lcd);
        g.fillRoundedRectangle (lcd, 6.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (lcd, 6.0f, 1.5f);
        g.setColour (col::lcdInk);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold)));
        const int step = processor.getCurrentStep();
        juce::String text = "PTN " + juce::String (processor.getPlayingPattern() + 1).paddedLeft ('0', 2)
                            + "   STEP " + (step >= 0 ? juce::String (step + 1).paddedLeft ('0', 2) : juce::String ("--"));
        if (processor.isPlayingFill())
            text << "  FILL";
        g.drawText (text, lcd.reduced (10, 0), juce::Justification::centred);

        if (! processor.hasOwnClock())
        {
            g.setColour (col::bandInk.withAlpha (0.7f));
            g.setFont (panelFont (12.0f, false));
            g.drawFittedText ("Plays in time with the Rig\n(or Logic): press Play there", r.removeFromRight (200), juce::Justification::centredRight, 2);
        }
    }

    // The sounds' names over their columns, and the knob names down the side.
    const int labelW = 58;
    auto area = voiceArea;
    auto names = area.removeFromLeft (labelW);
    const int colW = area.getWidth() / drums::kVoices;
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto c = juce::Rectangle<int> (area.getX() + v * colW, area.getY(), colW, 30).reduced (3, 2);
        g.setColour (voiceColour (v));
        g.fillRoundedRectangle (c.toFloat(), 4.0f);
        g.setColour (col::well);
        g.setFont (panelFont (13.0f));
        g.drawFittedText (juce::String (drums::voiceName (v)).toUpperCase(), c, juce::Justification::centred, 1, 0.7f);
        if (v > 0)
        {
            g.setColour (col::line);
            g.drawVerticalLine (area.getX() + v * colW, (float) voiceArea.getY() + 34.0f, (float) voiceArea.getBottom() - 4.0f);
        }
    }
    {
        const auto& knobs = columns[0].knobs;
        g.setFont (panelFont (12.0f));
        g.setColour (col::ink2);
        for (int k = 0; k < 5; ++k)
        {
            const auto b = knobs[(size_t) k]->getBounds();
            g.drawText (juce::String (knobNames[k]).toUpperCase(), names.withY (b.getY()).withHeight (b.getHeight()), juce::Justification::centredLeft);
        }
    }

    // The global knobs' names.
    g.setFont (panelFont (12.0f));
    for (auto* knob : globalKnobs)
    {
        g.setColour (col::ink2);
        const auto label = knob->getBounds().withHeight (14).translated (0, knob->getHeight());
        g.drawFittedText (knob->getName().toUpperCase(), label.withSizeKeepingCentre (globalArea.getWidth() - 8, 14),
                          juce::Justification::centred, 1, 0.75f);
    }
    if (tempo != nullptr)
    {
        g.setColour (col::bandInk);
        g.drawText ("TEMPO " + juce::String (juce::roundToInt (tempo->getValue())),
                    tempo->getBounds().translated (-90, 0).withWidth (86), juce::Justification::centredRight);
    }

    // The middle bar.
    g.setColour (col::faceLight);
    g.fillRect (barArea);
    g.setColour (col::line);
    g.drawHorizontalLine (barArea.getY(), 0, (float) getWidth());
    g.drawHorizontalLine (barArea.getBottom() - 1, 0, (float) getWidth());
    {
        const auto p = processor.getPattern (processor.getSelectedPattern());
        g.setColour (col::ink);
        g.setFont (panelFont (14.0f));
        const auto b = shorter.getBounds();
        g.drawText (juce::String (p.length) + " STEPS", juce::Rectangle<int> (b.getRight(), b.getY(), longer.getX() - b.getRight(), b.getHeight()),
                    juce::Justification::centred);
        g.setColour (col::ink2);
        g.setFont (panelFont (12.0f));
        g.drawText ("PATTERN", patternButtons[0]->getBounds().translated (0, -16).withWidth (100).withHeight (14), juce::Justification::centredLeft);
    }
}

void DrumView::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (64);
    {
        auto h = header.reduced (20, 12);
        h.removeFromRight (270);
        if (processor.hasOwnClock())
        {
            startButton.setBounds (h.removeFromRight (90));
            h.removeFromRight (100);
            tempo->setBounds (h.removeFromRight (44));
        }
    }

    r.reduce (14, 10);
    voiceArea = r.removeFromTop (juce::jlimit (300, 420, r.getHeight() / 2 - 20));
    globalArea = voiceArea.removeFromRight (120);
    {
        auto g = globalArea.reduced (10, 4);
        const int each = g.getHeight() / globalKnobs.size();
        for (auto* knob : globalKnobs)
            knob->setBounds (g.removeFromTop (each).withTrimmedBottom (16).withSizeKeepingCentre (juce::jmin (60, each - 16), juce::jmin (60, each - 16)));
    }
    {
        auto area = voiceArea;
        area.removeFromLeft (58);
        const int colW = area.getWidth() / drums::kVoices;
        for (int v = 0; v < drums::kVoices; ++v)
        {
            auto c = juce::Rectangle<int> (area.getX() + v * colW, area.getY(), colW, area.getHeight());
            c.removeFromTop (34);
            auto& column = columns[(size_t) v];
            column.pad->setBounds (c.removeFromBottom (40).reduced (6, 4));
            const int each = c.getHeight() / 5;
            for (auto& knob : column.knobs)
            {
                const int size = juce::jmin (colW - 14, each - 4);
                knob->setBounds (c.removeFromTop (each).withSizeKeepingCentre (size, size));
            }
        }
    }

    r.removeFromTop (8);
    barArea = r.removeFromTop (96).withX (0).withWidth (getWidth());
    {
        auto bar = barArea.reduced (14, 8);
        auto top = bar.removeFromTop (40).withTrimmedTop (14);
        bar.removeFromTop (6);
        auto right = top.removeFromRight (440);
        for (auto* b : { &grooveButton, &randomButton, &clearButton, &copyButton })
        {
            b->setBounds (right.removeFromLeft (b == &copyButton ? 120 : 100).reduced (3, 0));
        }
        const int each = juce::jmin (46, top.getWidth() / drums::kPatterns);
        for (auto* b : patternButtons)
            b->setBounds (top.removeFromLeft (each).reduced (2, 0));

        auto row = bar;
        shorter.setBounds (row.removeFromLeft (34));
        row.removeFromLeft (90);
        longer.setBounds (row.removeFromLeft (34));
        row.removeFromLeft (14);
        stepRate.setBounds (row.removeFromLeft (100).reduced (0, 2));
        row.removeFromLeft (14);
        sequencerButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (24);
        fillPattern.setBounds (row.removeFromLeft (170).reduced (0, 2));
        row.removeFromLeft (8);
        fillEvery.setBounds (row.removeFromLeft (170).reduced (0, 2));
        row.removeFromLeft (8);
        fillButton.setBounds (row.removeFromLeft (90));
    }

    r.removeFromTop (8);
    gridArea = r;
    grid.setBounds (gridArea);
}

//==============================================================================
namespace
{
constexpr int kGridLabel = 108, kGridHeader = 24;
}

int DrumView::StepGrid::columns() const
{
    const int length = processor.getPattern (processor.getSelectedPattern()).length;
    return length <= 16 ? 16 : drums::kMaxSteps;
}

juce::Rectangle<float> DrumView::StepGrid::cell (int column, int row) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) kGridLabel).withTrimmedTop ((float) kGridHeader);
    const float w = area.getWidth() / (float) columns();
    const float h = area.getHeight() / (float) kRows;
    return { area.getX() + w * (float) column, area.getY() + h * (float) row, w, h };
}

std::pair<int, int> DrumView::StepGrid::cellAt (juce::Point<float> p) const
{
    const auto area = getLocalBounds().toFloat().withTrimmedLeft ((float) kGridLabel).withTrimmedTop ((float) kGridHeader);
    if (! area.contains (p))
        return { -1, -1 };
    const int c = juce::jlimit (0, columns() - 1, (int) ((p.x - area.getX()) / (area.getWidth() / (float) columns())));
    const int r = juce::jlimit (0, kRows - 1, (int) ((p.y - area.getY()) / (area.getHeight() / (float) kRows)));
    return { c, r };
}

void DrumView::StepGrid::click (int column, int row, bool roll)
{
    const int index = processor.getSelectedPattern();
    auto p = processor.getPattern (index);
    if (! juce::isPositiveAndBelow (column, p.length) || ! juce::isPositiveAndBelow (row, kRows))
        return;
    if (row == 0)
    {
        p.accent[(size_t) column] = ! p.accent[(size_t) column];
    }
    else
    {
        auto& h = p.hits[(size_t) (row - 1)][(size_t) column];
        if (roll)
            h = (std::uint8_t) (h == 0 || h == 1 ? 2 : h == 4 ? 1 : h + 1);
        else
            h = (std::uint8_t) (h == 0 ? 1 : 0);
        if (h != 0 && onAudition)
            onAudition (row - 1);
    }
    processor.setPattern (index, p);
    repaint();
}

void DrumView::StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto [c, r] = cellAt (e.position);
    if (c < 0)
    {
        // A sound's name: hear it.
        if (e.x < kGridLabel && e.y > kGridHeader)
        {
            const int row = (int) ((e.position.y - kGridHeader) / ((getHeight() - kGridHeader) / (float) kRows));
            if (row >= 1 && row < kRows && onAudition)
                onAudition (row - 1);
        }
        return;
    }
    const bool roll = e.mods.isPopupMenu() || e.mods.isAltDown();
    click (c, r, roll);
    const auto p = processor.getPattern (processor.getSelectedPattern());
    dragRow = roll ? -1 : r;
    dragValue = r == 0 ? (c < p.length && p.accent[(size_t) c] ? 1 : 0) : (c < p.length ? p.hits[(size_t) (r - 1)][(size_t) c] : 0);
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
    const int index = processor.getSelectedPattern();
    auto p = processor.getPattern (index);
    if (c >= p.length)
        return;
    if (dragRow == 0)
        p.accent[(size_t) c] = dragValue != 0;
    else
        p.hits[(size_t) (dragRow - 1)][(size_t) c] = (std::uint8_t) (dragValue != 0 ? 1 : 0);
    processor.setPattern (index, p);
    repaint();
    juce::ignoreUnused (r);
}

void DrumView::StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragRow = -1;
    lastColumn = -1;
}

void DrumView::StepGrid::paint (juce::Graphics& g)
{
    const auto p = processor.getPattern (processor.getSelectedPattern());
    const bool showingPlaying = processor.getSelectedPattern() == processor.getPlayingPattern() && ! processor.isPlayingFill();
    const int playStep = showingPlaying ? processor.getCurrentStep() : -1;
    const int cols = columns();

    g.setColour (col::well);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // The step numbers, on the coloured step keys.
    for (int c = 0; c < cols; ++c)
    {
        auto key = cell (c, 0).withY (2.0f).withHeight ((float) kGridHeader - 4.0f).reduced (2.0f, 0);
        const bool inside = c < p.length;
        g.setColour (stepColour (c).withAlpha (inside ? 1.0f : 0.25f));
        g.fillRoundedRectangle (key, 3.0f);
        g.setColour (col::well.withAlpha (inside ? 1.0f : 0.5f));
        g.setFont (panelFont (12.0f));
        g.drawText (juce::String (c + 1), key, juce::Justification::centred);
        if (c == playStep)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (key.expanded (1.0f), 3.0f, 2.0f);
        }
    }

    for (int row = 0; row < kRows; ++row)
    {
        const auto first = cell (0, row);
        auto label = juce::Rectangle<float> (6.0f, first.getY(), (float) kGridLabel - 12.0f, first.getHeight());
        const auto colour = row == 0 ? col::ink : voiceColour (row - 1);
        g.setColour (colour.withAlpha (0.9f));
        g.fillRect (label.removeFromLeft (4.0f).reduced (0, 3.0f));
        g.setColour (col::ink);
        g.setFont (panelFont (juce::jmin (14.0f, first.getHeight() * 0.65f)));
        g.drawText (row == 0 ? juce::String ("ACCENT") : juce::String (drums::voiceName (row - 1)).toUpperCase(), label.withTrimmedLeft (6.0f),
                    juce::Justification::centredLeft);

        for (int c = 0; c < cols; ++c)
        {
            auto r = cell (c, row).reduced (2.0f, 2.0f);
            const bool inside = c < p.length;
            const int h = row == 0 ? (p.accent[(size_t) c] ? 1 : 0) : p.hits[(size_t) (row - 1)][(size_t) c];
            const bool beatStart = c % 4 == 0;
            juce::Colour base = beatStart ? juce::Colour (0xff302d2a) : juce::Colour (0xff24221f);
            if (c == playStep)
                base = base.brighter (0.35f);
            g.setColour (base.withAlpha (inside ? 1.0f : 0.35f));
            g.fillRoundedRectangle (r, 3.0f);
            if (h > 0 && inside)
            {
                const auto on = row == 0 ? col::cream : voiceColour (row - 1);
                g.setColour (c == playStep ? on.brighter (0.4f) : on);
                g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
                if (h >= 2)
                {
                    // A roll: its hits as small bars.
                    g.setColour (col::well);
                    const float w = r.getWidth() / (float) (h * 2 + 1);
                    for (int i = 0; i < h; ++i)
                        g.fillRect (r.getX() + w * (float) (i * 2 + 1), r.getCentreY() - 3.0f, w, 6.0f);
                }
            }
        }
    }
}
