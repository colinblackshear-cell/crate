#include "MacroStrip.h"
#include "Vintage.h"

MacroStrip::MacroStrip (RigEngine& e) : engine (e)
{
    for (int m = 0; m < RigMacros::kMacros; ++m)
    {
        auto* k = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        k->setRange (0.0, 1.0);
        k->setDoubleClickReturnValue (true, 0.0);
        k->setValue (engine.getMacros().value[(size_t) m].load(), juce::dontSendNotification);
        k->onValueChange = [this, m] { engine.getMacros().value[(size_t) m] = (float) knobs[m]->getValue(); };
        k->setTooltip ("Turns every setting on this macro at once. Double-click: back to the start. Right-click its name for what's on it.");
        addAndMakeVisible (k);

        auto* b = assign.add (new rigpanels::PanelButton ("Assign"));
        b->inWell = true;
        b->fontSize = 13.0f;
        b->setTooltip ("Assign: then move any settings from where they should start to where they should end up. Each joins this macro. Press Done when finished.");
        b->onClick = [this, m] { pressAssign (m); };
        addAndMakeVisible (b);
    }
    update();
    startTimerHz (15);
}

MacroStrip::~MacroStrip() { stopTimer(); }

void MacroStrip::say (const juce::String& text)
{
    if (onSay)
        onSay (text);
}

void MacroStrip::pressAssign (int m)
{
    auto& macros = engine.getMacros();
    if (macros.getAssigning() == m)
    {
        macros.stopAssigning();
        const int n = macros.getNumTargets (m);
        say (n == 0 ? macros.getName (m) + " has nothing on it yet: press Assign, then move some settings."
                    : macros.getName (m) + " turns " + juce::String (n) + (n == 1 ? " setting" : " settings") + ". Turn it to play them all at once.");
    }
    else
    {
        macros.startAssigning (m);
        say ("Assigning " + macros.getName (m) + ": move any settings (on any tool or its effects) from where they start to where they should end up. Press Done when finished.");
    }
    update();
}

void MacroStrip::turn (int m, float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    engine.getMacros().value[(size_t) m] = v;
    knobs[m]->setValue (v, juce::dontSendNotification);
}

void MacroStrip::showMenu (int m)
{
    auto& macros = engine.getMacros();
    juce::PopupMenu menu;
    menu.addSectionHeader (macros.getName (m));
    menu.addItem (macros.getAssigning() == m ? "Done assigning" : "Assign settings...", [this, m] { pressAssign (m); });
    menu.addItem ("Rename...", [this, m]
    {
        auto* box = new juce::AlertWindow ("Rename " + engine.getMacros().getName (m), "A short name for the knob (Build, Space, Dark...).", juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("name", engine.getMacros().getName (m));
        box->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([this, m, box] (int result)
        {
            if (result == 1)
            {
                engine.getMacros().setName (m, box->getTextEditorContents ("name"));
                update();
            }
        }), true);
    });
    const auto targets = macros.getTargets (m);
    if (! targets.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("On it (click one to take it off)");
        for (int i = 0; i < (int) targets.size(); ++i)
        {
            const auto& t = targets[(size_t) i];
            menu.addItem (macros.targetName (t) + "  (" + juce::String (juce::roundToInt (t.from * 100)) + "% to " + juce::String (juce::roundToInt (t.to * 100)) + "%)",
                          [this, m, i, name = macros.targetName (t)]
                          {
                              engine.getMacros().removeTarget (m, i);
                              say (name + " is off " + engine.getMacros().getName (m) + ".");
                              update();
                          });
        }
        menu.addSeparator();
        menu.addItem ("Clear this macro", [this, m]
        {
            engine.getMacros().clear (m);
            say (engine.getMacros().getName (m) + " cleared: the settings stay where they are.");
            update();
        });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (knobs[m]));
}

void MacroStrip::mouseUp (const juce::MouseEvent& e)
{
    for (int m = 0; m < (int) nameAreas.size(); ++m)
        if (nameAreas[(size_t) m].contains (e.getPosition()) || countAreas[(size_t) m].contains (e.getPosition()))
            return showMenu (m);
}

void MacroStrip::timerCallback()
{
    auto& macros = engine.getMacros();
    // A knob on your keyboard, a loaded show or automation can move them too.
    for (int m = 0; m < RigMacros::kMacros; ++m)
        if (! knobs[m]->isMouseButtonDown() && std::abs (knobs[m]->getValue() - macros.value[(size_t) m].load()) > 1.0e-4)
            knobs[m]->setValue (macros.value[(size_t) m].load(), juce::dontSendNotification);
    if (macros.getVersion() != shownVersion)
        update();
}

void MacroStrip::update()
{
    auto& macros = engine.getMacros();
    shownVersion = macros.getVersion();
    for (int m = 0; m < RigMacros::kMacros; ++m)
    {
        const bool on = macros.getAssigning() == m;
        assign[m]->text = on ? "Done" : "Assign";
        assign[m]->lit = on ? vintage::Lit::arm : vintage::Lit::none;
        assign[m]->repaint();
        const bool used = macros.getNumTargets (m) > 0;
        knobs[m]->setEnabled (used);
        knobs[m]->setAlpha (used ? 1.0f : 0.4f);   // (an empty macro has nothing to turn yet)
        if (! knobs[m]->isMouseButtonDown())
            knobs[m]->setValue (macros.value[(size_t) m].load(), juce::dontSendNotification);
    }
    repaint();
}

void MacroStrip::paint (juce::Graphics& g)
{
    const auto& p = vintage::pal();
    vintage::drawWell (g, getLocalBounds().toFloat(), 12.0f);
    g.setColour (p.wellInk);
    g.setFont (vintage::cond (19.0f, vintage::Weight::extra, 0.08f));
    g.drawText ("MACROS", titleArea.withHeight (22), juce::Justification::topLeft);
    g.setColour (p.wellInk2);
    g.setFont (vintage::barlow (12.0f));
    g.drawFittedText ("One knob, many settings. Press Assign, then move settings from start to end. Right-click a name for what's on it.",
                      titleArea.withTrimmedTop (26), juce::Justification::topLeft, 5);
    auto& macros = engine.getMacros();
    for (int m = 0; m < (int) nameAreas.size(); ++m)
    {
        const int n = macros.getNumTargets (m);
        g.setColour (macros.getAssigning() == m ? vintage::colours::arm : p.wellInk);
        g.setFont (vintage::cond (15.0f, vintage::Weight::bold, 0.04f));
        g.drawFittedText (macros.getName (m).toUpperCase(), nameAreas[(size_t) m], juce::Justification::centred, 1);
        g.setColour (p.wellInk2);
        g.setFont (vintage::barlow (11.5f));
        g.drawFittedText (macros.getAssigning() == m ? juce::String ("move settings...") : n == 0 ? juce::String ("empty") : juce::String (n) + (n == 1 ? " setting" : " settings"),
                          countAreas[(size_t) m], juce::Justification::centred, 1);
    }
}

void MacroStrip::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    titleArea = r.removeFromLeft (juce::jmin (150, r.getWidth() / 6));
    r.removeFromLeft (8);
    nameAreas.assign ((size_t) RigMacros::kMacros, {});
    countAreas.assign ((size_t) RigMacros::kMacros, {});
    const int w = r.getWidth() / RigMacros::kMacros;
    for (int m = 0; m < RigMacros::kMacros; ++m)
    {
        auto c = r.removeFromLeft (w).reduced (4, 0);
        nameAreas[(size_t) m] = c.removeFromTop (18);
        countAreas[(size_t) m] = c.removeFromBottom (14);
        assign[m]->setBounds (c.removeFromBottom (26).reduced (2, 0));
        c.removeFromBottom (4);
        const int side = juce::jmin (c.getWidth(), c.getHeight());
        knobs[m]->setBounds (c.withSizeKeepingCentre (side, side));
    }
}
