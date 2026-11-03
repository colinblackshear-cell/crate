#include "BassView.h"

namespace Theme = crate::Theme;
using crate::bass::Pattern;

namespace
{
const juce::Colour bassColour { 0xff4fc3f7 };

// A step's note as an octave (-1..2) and a semitone (0..11) above the root.
int octaveOf (int note) { return (int) std::floor (note / 12.0); }
int semitoneOf (int note) { return note - 12 * octaveOf (note); }
}

//==============================================================================
juce::Rectangle<float> BassView::StepGrid::cell (int column, int row) const
{
    const float labelWidth = 70.0f;
    const float w = ((float) getWidth() - labelWidth) / crate::bass::kSteps;
    const float h = (float) getHeight() / kRows;
    return { labelWidth + column * w, row * h, w, h };
}

void BassView::StepGrid::paint (juce::Graphics& g)
{
    const auto pattern = processor.getPattern (processor.getSelectedPattern());
    const bool playingThis = processor.getPlayingPattern() == processor.getSelectedPattern();
    const int now = playingThis ? processor.getCurrentStep() : -1;
    const int root = (int) processor.params.getRawParameterValue ("root")->load();

    g.setFont (juce::FontOptions (12.0f));
    for (int row = 0; row < kRows; ++row)
    {
        auto label = cell (0, row).withX (0).withWidth (66);
        juce::String text;
        if (row < kNoteRows)
            text = juce::MidiMessage::getMidiNoteName (root + (kNoteRows - 1 - row), true, true, 3);
        else if (row == kNoteRows)
            text = "Octave";
        else if (row == kNoteRows + 1)
            text = "Accent";
        else
            text = "Slide";
        g.setColour (Theme::textDim);
        g.drawText (text, label, juce::Justification::centredRight);
    }

    for (int col = 0; col < crate::bass::kSteps; ++col)
    {
        const auto& step = pattern.steps[(size_t) col];
        const bool inLength = col < pattern.length;
        const int noteRow = kNoteRows - 1 - semitoneOf (step.note);
        const int octave = octaveOf (step.note);

        for (int row = 0; row < kRows; ++row)
        {
            auto r = cell (col, row).reduced (1.5f);
            const bool black = row < kNoteRows && juce::MidiMessage::isMidiNoteBlack (root + (kNoteRows - 1 - row));
            juce::Colour base = black ? Theme::background : Theme::raised;
            if (col % 4 == 0)
                base = base.brighter (0.06f);
            if (! inLength)
                base = base.darker (0.5f);
            g.setColour (base);
            g.fillRoundedRectangle (r, 3.0f);

            bool lit = false;
            if (row < kNoteRows)
                lit = step.on && row == noteRow;
            else if (row == kNoteRows)
            {
                g.setColour (Theme::textDim);
                g.drawText (octave == 0 ? "0" : (octave > 0 ? "+" : "") + juce::String (octave), r, juce::Justification::centred);
            }
            else if (row == kNoteRows + 1)
                lit = step.accent;
            else
                lit = step.slide;

            if (lit)
            {
                g.setColour (row < kNoteRows ? bassColour : row == kNoteRows + 1 ? Theme::accent : juce::Colour (0xffb48cff));
                g.fillRoundedRectangle (r, 3.0f);
            }
        }

        if (col == now)
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            auto column = cell (col, 0).withBottom ((float) getHeight());
            g.fillRect (column);
        }
    }
}

void BassView::StepGrid::click (juce::Point<float> at, bool drag)
{
    int col = -1, row = -1;
    for (int c = 0; c < crate::bass::kSteps; ++c)
        for (int r = 0; r < kRows; ++r)
            if (cell (c, r).contains (at))
            {
                col = c;
                row = r;
            }
    if (col < 0 || (drag && col == lastColumn))
        return;
    lastColumn = col;

    const int index = processor.getSelectedPattern();
    auto p = processor.getPattern (index);
    auto& step = p.steps[(size_t) col];

    if (row < kNoteRows)
    {
        const int note = (kNoteRows - 1 - row) + 12 * octaveOf (step.note);
        if (! drag)
        {
            // Clicking the note that's already there turns the step off.
            dragValue = ! (step.on && step.note == note);
            dragRow = row;
        }
        step.on = dragValue;
        step.note = note;
    }
    else if (row == kNoteRows)
    {
        // Octave: 0 -> +1 -> -1 -> 0.
        const int octave = octaveOf (step.note);
        const int next = octave == 0 ? 1 : octave > 0 ? -1 : 0;
        step.note = juce::jlimit (-12, 23, semitoneOf (step.note) + 12 * next);
        if (! step.on)
            step.on = true;
    }
    else
    {
        bool& flag = row == kNoteRows + 1 ? step.accent : step.slide;
        if (! drag)
        {
            dragValue = ! flag;
            dragRow = row;
        }
        if (row == dragRow)
            flag = dragValue;
    }

    processor.setPattern (index, p);
    repaint();
}

void BassView::StepGrid::mouseDown (const juce::MouseEvent& e)
{
    lastColumn = -1;
    click (e.position, false);
}

void BassView::StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    click (e.position, true);
}

//==============================================================================
BassView::BassView (BassProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      sequencer (p.params, "sequencer", "Sequencer on", bassColour),
      keysTranspose (p.params, "keysTranspose", "Keys transpose", bassColour),
      stepRate (p.params, "stepRate", "Steps"),
      wave (p.params, "wave", "Wave"),
      lfoRate (p.params, "lfoRate", "LFO rate"),
      lfoShape (p.params, "lfoShape", "LFO shape"),
      grid (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);

    for (int i = 0; i < crate::bass::kPatterns; ++i)
    {
        auto* b = patternButtons.add (new juce::TextButton (juce::String (i + 1)));
        b->setColour (juce::TextButton::buttonOnColourId, bassColour);
        b->setTooltip ("Pattern " + juce::String (i + 1) + ". While playing, it switches at the next bar.");
        b->onClick = [this, i] { processor.selectPattern (i); };
        addAndMakeVisible (b);
    }

    sequencer.setTooltip ("On: the pattern plays while Logic or the Rig plays. Off: the keys play the bass.");
    keysTranspose.setTooltip ("On: while the pattern plays, a key moves the whole pattern to that key.");
    shorter.onClick = [this] { editPattern ([] (Pattern& pt) { pt.length = juce::jmax (1, pt.length - 1); }); };
    longer.onClick = [this] { editPattern ([] (Pattern& pt) { pt.length = juce::jmin (crate::bass::kSteps, pt.length + 1); }); };
    randomButton.setTooltip ("A new random bass line on this pattern.");
    randomButton.onClick = [this] { editPattern ([this] (Pattern& pt) { pt = BassProcessor::randomPattern (random); }); };
    clearButton.onClick = [this] { editPattern ([] (Pattern& pt) { pt = Pattern(); }); };
    copyButton.setTooltip ("Copies this pattern to the next one, to make a variation.");
    copyButton.onClick = [this]
    {
        const int from = processor.getSelectedPattern();
        const int to = (from + 1) % crate::bass::kPatterns;
        processor.setPattern (to, processor.getPattern (from));
        processor.selectPattern (to);
    };
    lengthLabel.setJustificationType (juce::Justification::centred);

    for (auto* c : std::initializer_list<juce::Component*> { &sequencer, &keysTranspose, &shorter, &longer, &lengthLabel,
                                                             &randomButton, &clearButton, &copyButton, &stepRate, &wave,
                                                             &lfoRate, &lfoShape, &grid, &keyboard })
        addAndMakeVisible (c);

    const std::pair<const char*, const char*> knobList[] = { { "cutoff", "Cutoff" }, { "resonance", "Resonance" }, { "envMod", "Env mod" },
                                                             { "decay", "Decay" }, { "accent", "Accent" }, { "glide", "Glide" },
                                                             { "drive", "Drive" }, { "sub", "Sub" }, { "lfoDepth", "LFO depth" },
                                                             { "gate", "Gate" }, { "root", "Key" }, { "volume", "Volume" } };
    for (auto [id, name] : knobList)
        addAndMakeVisible (knobs.add (new crate::ParamKnob (p.params, id, name, bassColour)));
    if (auto* key = knobs[10])
        key->slider.textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3); };

    keyboard.setAvailableRange (24, 84);
    keyboard.setLowestVisibleKey (24);
    keyboard.setOctaveForMiddleC (3);

    setResizable (true, true);
    setResizeLimits (900, 560, 3000, 2000);
    setSize (1200, 760);
    startTimerHz (30);
}

BassView::~BassView()
{
    setLookAndFeel (nullptr);
}

void BassView::editPattern (const std::function<void (Pattern&)>& change)
{
    const int index = processor.getSelectedPattern();
    auto p = processor.getPattern (index);
    change (p);
    processor.setPattern (index, p);
    grid.repaint();
}

void BassView::timerCallback()
{
    const int selected = processor.getSelectedPattern();
    const int playing = processor.getPlayingPattern();
    for (int i = 0; i < patternButtons.size(); ++i)
    {
        patternButtons[i]->setToggleState (i == selected, juce::dontSendNotification);
        patternButtons[i]->setButtonText (juce::String (i + 1) + (i == playing && i != selected ? " >" : ""));
    }
    lengthLabel.setText (juce::String (processor.getPattern (selected).length) + " steps", juce::dontSendNotification);
    grid.repaint();
}

void BassView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (bassColour);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("BASS", getLocalBounds().reduced (14, 10).removeFromTop (34), juce::Justification::centredLeft);

    auto knobArea = getLocalBounds().withTop (knobs.getFirst()->getY() - 8).withBottom (keyboard.getY() - 6).reduced (8, 0);
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (knobArea.toFloat(), 8.0f);
}

void BassView::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (34);
    top.removeFromLeft (70);
    for (auto* b : patternButtons)
    {
        b->setBounds (top.removeFromLeft (40));
        top.removeFromLeft (4);
    }
    top.removeFromLeft (12);
    sequencer.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (6);
    keysTranspose.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (12);
    shorter.setBounds (top.removeFromLeft (30));
    lengthLabel.setBounds (top.removeFromLeft (70));
    longer.setBounds (top.removeFromLeft (30));
    copyButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (6);
    clearButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    randomButton.setBounds (top.removeFromRight (80));

    area.removeFromTop (8);
    keyboard.setBounds (area.removeFromBottom (70));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f);   // the whole range, C0-C5, fills the width
    area.removeFromBottom (8);

    auto knobRow = area.removeFromBottom (100).reduced (10, 6);
    const int count = knobs.size() + 4;
    const int w = knobRow.getWidth() / count;
    for (auto* c : std::initializer_list<juce::Component*> { &wave, &stepRate })
        c->setBounds (knobRow.removeFromLeft (w).reduced (4, 14));
    for (auto* k : knobs)
        k->setBounds (knobRow.removeFromLeft (w));
    for (auto* c : std::initializer_list<juce::Component*> { &lfoRate, &lfoShape })
        c->setBounds (knobRow.removeFromLeft (w).reduced (4, 14));
    area.removeFromBottom (14);

    grid.setBounds (area);
}
