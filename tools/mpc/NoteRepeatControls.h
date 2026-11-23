#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SequencerEngine.h"
#include "core/ui/Theme.h"

// The pads' NOTE REPEAT controls, one pair for every screen that plays the MPC's pads (the MPC page, the
// Sampler page, the Sequencer): a red hold button and the rate buttons. They all read and write the one
// SequencerEngine, so every screen shows the same state.
namespace noterepeatui
{
// Hold it and repeat is on; double-tap to latch it on, press once to switch it off. Right-click to Learn it.
class HoldButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit HoldButton (SequencerEngine& e) : engine (e)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip ("NOTE REPEAT: hold this, then hold a pad and it plays again at the rate beside it, in time. "
                    "Let go and it stops. Double-tap to latch it on; press once to switch it off. "
                    "Right-click to learn it onto a keyboard button or pedal.");
        startTimerHz (20);
    }

    std::function<void (juce::Point<int> screenPosition)> onContextMenu;   // right-click: the Learn menu
    std::function<void (const juce::String&)> onSay;

    void paint (juce::Graphics& g) override
    {
        const bool on = engine.isRepeatOn(), latched = engine.isRepeatLatched();
        const bool glow = on && (! latched || blinkPhase);
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto red = crate::Theme::record;
        const bool down = isMouseButtonDown() && ! latched;
        g.setColour (glow ? red.withAlpha (0.35f) : juce::Colours::transparentBlack);
        if (glow)
            g.fillRoundedRectangle (r.expanded (2.0f), 8.0f);
        g.setGradientFill (juce::ColourGradient (glow ? red.brighter (0.25f) : red.darker (0.55f), 0, r.getY(),
                                                 glow ? red.darker (0.1f) : red.darker (0.8f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (red.withAlpha (isMouseOver() || down ? 0.95f : 0.65f));
        g.drawRoundedRectangle (r, 6.0f, 1.2f);

        const auto ink = glow ? juce::Colour (0xff2a0602) : juce::Colour (0xfff1e7d1);
        auto text = getLocalBounds().reduced (4, 2);
        g.setColour (ink);
        g.setFont (juce::FontOptions (juce::jlimit (11.0f, 15.0f, (float) getHeight() * 0.38f), juce::Font::bold));
        g.drawText (latched ? "NOTE REPEAT  LATCHED" : "NOTE REPEAT", text.removeFromTop (juce::roundToInt (getHeight() * 0.55f)),
                    juce::Justification::centredBottom, true);
        g.setColour (ink.withAlpha (0.75f));
        g.setFont (juce::FontOptions (juce::jlimit (9.0f, 11.5f, (float) getHeight() * 0.27f)));
        g.drawText (juce::String::fromUTF8 ("hold \xc2\xb7 double-tap to latch"), text, juce::Justification::centredTop, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu)
                onContextMenu (e.getScreenPosition());
            return;
        }
        engine.repeatPress (juce::Time::getMillisecondCounterHiRes());
        pressed = true;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! pressed)
            return;
        pressed = false;
        engine.repeatRelease();
        repaint();
    }

    // Tests and keyboard shortcuts press it the same way.
    void press (double nowMs) { engine.repeatPress (nowMs); pressed = true; repaint(); }
    void release() { pressed = false; engine.repeatRelease(); repaint(); }

private:
    void timerCallback() override
    {
        if (! isShowing())
            return;
        const bool on = engine.isRepeatOn(), latched = engine.isRepeatLatched();
        const bool phase = ! latched || ((juce::Time::getMillisecondCounter() / 350) & 1) == 0;
        if (on != lastOn || latched != lastLatched || phase != blinkPhase)
        {
            lastOn = on;
            lastLatched = latched;
            blinkPhase = phase;
            repaint();
        }
    }

    SequencerEngine& engine;
    bool pressed = false, lastOn = false, lastLatched = false, blinkPhase = true;
};

// 1/4 1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64: the picked one is lit.
class RateButtons : public juce::Component, private juce::Timer
{
public:
    explicit RateButtons (SequencerEngine& e) : engine (e)
    {
        for (int i = 0; i < noterepeat::kNumRates; ++i)
        {
            auto* b = buttons.add (new juce::TextButton (noterepeat::kRates[i].name));
            b->setTooltip (juce::String ("Note repeat rate: held pads play again every ") + noterepeat::kRates[i].name
                           + (i == 2 || i == 4 || i == 6 ? " (triplets)." : " note."));
            b->onClick = [this, i]
            {
                engine.setRepeatRateIndex (i);
                refresh();
                if (onSay)
                    onSay (juce::String ("Note repeat: held pads repeat every ") + noterepeat::kRates[i].name + ".");
            };
            addAndMakeVisible (b);
        }
        refresh();
        startTimerHz (10);
    }

    std::function<void (const juce::String&)> onSay;
    // For hosts that style their buttons (the Rig lights the picked one with its own look).
    std::function<void (juce::TextButton&, bool picked)> style;

    void refresh()
    {
        const int now = engine.getRepeatRateIndex();
        shown = now;
        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setToggleState (i == now, juce::dontSendNotification);
            if (style)
                style (*buttons[i], i == now);
        }
    }

    juce::TextButton& button (int rate) { return *buttons[rate]; }

    void resized() override
    {
        auto r = getLocalBounds();
        const int n = buttons.size(), gap = 4;
        const int w = (r.getWidth() - gap * (n - 1)) / n;
        for (auto* b : buttons)
        {
            b->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (gap);
        }
    }

private:
    void timerCallback() override
    {
        if (isShowing() && engine.getRepeatRateIndex() != shown)
            refresh();
    }

    SequencerEngine& engine;
    juce::OwnedArray<juce::TextButton> buttons;
    int shown = -2;
};
}
