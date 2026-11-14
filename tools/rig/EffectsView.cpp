#include "EffectsView.h"

#include "RigColours.h"

using namespace vintage;

namespace
{
juce::Colour sourceColour (int source)
{
    if (source >= EffectsView::firstLoop && source <= EffectsView::allLoops)
        return colours::loops;
    const auto& s = EffectsView::sources()[(size_t) source];
    return RigColours::forStrip (s.strip);
}

void drawLeds (juce::Graphics& g, juce::Rectangle<float> r, float level, int count, bool vertical)
{
    const float gap = 3.0f;
    const float size = ((vertical ? r.getHeight() : r.getWidth()) - gap * (count - 1)) / count;
    const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, level) * count);
    for (int i = 0; i < count; ++i)
    {
        const auto seg = vertical ? juce::Rectangle<float> (r.getX(), r.getBottom() - (i + 1) * size - i * gap, r.getWidth(), size)
                                  : juce::Rectangle<float> (r.getX() + i * (size + gap), r.getY(), size, r.getHeight());
        const float share = (float) i / count;
        const auto colour = share > 0.85f ? colours::arm : share > 0.65f ? colours::next : colours::play;
        g.setColour (i < lit ? colour : juce::Colour (0xff2e2925));
        g.fillRoundedRectangle (seg, 2.0f);
    }
}
}

const std::vector<EffectsView::Source>& EffectsView::sources()
{
    static const std::vector<Source> list = []
    {
        std::vector<Source> s {
            { "Line in", "guitar, bass, keytar", RigEngine::inputStrip, -1, "", RigLooper::line, RigCapture::input },
            { "Vocoder", "after its own pedals", RigEngine::vocoderStrip, -1, "Vocoder", RigLooper::vocoder, RigCapture::vocoder },
            { "Bass", "the bass synth", RigEngine::bassStrip, -1, "Bass", RigLooper::bass, RigCapture::bass },
            { "Synth", "the poly synth", RigEngine::synthStrip, -1, "Synth", RigLooper::synth, RigCapture::synth },
            { "MPC", "pads and sequences", RigEngine::mpcStrip, -1, "MPC", RigLooper::mpc, RigCapture::mpc },
            { "Drums", "the drum machine", RigEngine::drumsStrip, -1, "Drum machine", RigLooper::drums, RigCapture::drums },
            { "Sampler", "the sampler's keys", RigEngine::samplerStrip, -1, "Sampler", RigLooper::sampler, RigCapture::sampler },
        };
        static const char* loopNames[] = { "Loop 1", "Loop 2", "Loop 3", "Loop 4", "Loop 5", "Loop 6", "Loop 7", "Loop 8" };
        for (int i = 0; i < RigLooper::kLayers; ++i)
            s.push_back ({ loopNames[i], "one loop layer", -1, i, "Looper", -1, -1 });
        s.push_back ({ "All loops", "every loop together", RigEngine::loopsStrip, -1, "Looper", -1, RigCapture::loops });
        s.push_back ({ "Master", "the whole mix", RigEngine::masterStrip, -1, "", RigLooper::mix, RigCapture::mix });
        return s;
    }();
    return list;
}

//==============================================================================
// A stomp box: the name and what it is, a lamp, one knob with its value, a footswitch.
// Clicking the pedal (not the footswitch) opens it in the rack.
class EffectsView::Stomp : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit Stomp (const pedalboard::Pedal& p) : info (p), colour (juce::Colour (p.colour))
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob.setMouseDragSensitivity (200);
        knob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xfff1e7d1));
        knob.onDragStart = [this] { if (onSelect) onSelect(); };
        knob.onValueChange = [this] { repaint(); if (onTurn && ! attaching) onTurn(); };
        addAndMakeVisible (knob);
        setTooltip (juce::String (info.description));
    }

    void attach (juce::AudioProcessorValueTreeState& state)
    {
        attaching = true;
        attachment.reset();
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, info.knob.id, knob);
        auto* param = state.getParameter (info.knob.id);
        if (param != nullptr)
            knob.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        knob.textFromValueFunction = [param] (double v) { return param != nullptr ? param->getText (param->convertTo0to1 ((float) v), 16) : juce::String (v, 2); };
        attaching = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool lit = on != nullptr && on();
        auto body = getLocalBounds().toFloat().reduced (3.0f);
        if (picked)
        {
            glow (g, body, 12.0f, colours::selected, 8.0f, 0.6f);
            g.setColour (colours::selected);
            g.drawRoundedRectangle (body.expanded (1.5f), 13.0f, 3.0f);
        }
        const auto base = lit ? colour : colour.darker (0.55f).withMultipliedSaturation (0.65f);
        glow (g, body, 12.0f, juce::Colours::black, 8.0f, 0.25f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), 0, body.getY(), base.darker (0.2f), 0, body.getBottom(), false));
        g.fillRoundedRectangle (body, 12.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (body.reduced (0.5f), 12.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (body.getX() + 12, body.getY() + 1.5f, body.getRight() - 12, body.getY() + 1.5f, 1.0f);

        g.setColour (juce::Colour (0xe01e1a16));
        g.fillRoundedRectangle (plate.toFloat(), 6.0f);
        auto p = plate.reduced (4, 3);
        g.setColour (juce::Colour (0xfff1e7d1));
        g.setFont (cond (juce::jmin (18.0f, getWidth() * 0.19f), Weight::extra, 0.02f));
        g.drawFittedText (juce::String (info.name).toUpperCase(), p.removeFromTop (21), juce::Justification::centred, 1, 0.7f);
        g.setColour (juce::Colour (0xffbdaf97));
        g.setFont (barlow (11.0f));
        g.drawFittedText (info.kind, p, juce::Justification::centred, 1, 0.75f);

        // The lamp: a gate that's shut, or a comp that's squeezing, glows dimmer.
        drawLamp (g, { body.getX() + 12.0f, (float) plate.getBottom() + 12.0f }, lit ? (dim ? Lit::next : Lit::play) : Lit::none, 12.0f);

        const auto box = valueBox.toFloat();
        drawLcd (g, box, 4.0f);
        lcdText (g, knob.getTextFromValue (knob.getValue()), box, juce::jmin (17.0f, box.getHeight() * 0.85f));

        const auto sw = footswitch.toFloat();
        glow (g, sw, sw.getWidth() / 2, juce::Colours::black, 6.0f, 0.35f);
        g.setColour (juce::Colour (0xff2a2622));
        g.fillEllipse (sw);
        const auto cap = sw.reduced (sw.getWidth() * 0.09f).translated (0, pressed ? 2.0f : 0.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f1ea), cap.getX(), cap.getY(), juce::Colour (0xff9a958c), cap.getRight(),
                                                 cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colour (0xff2a2622));
        g.setFont (cond (juce::jmin (13.0f, cap.getHeight() * 0.3f), Weight::extra, 0.04f));
        g.drawText (lit ? "ON" : "OFF", cap, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        plate = r.removeFromTop (38);
        r.removeFromTop (6);
        const int switchSize = juce::jlimit (30, 48, juce::jmin (r.getWidth() - 20, getHeight() / 5));
        footswitch = r.removeFromBottom (switchSize).withSizeKeepingCentre (switchSize, switchSize);
        r.removeFromBottom (6);
        valueBox = r.removeFromBottom (20);
        r.removeFromBottom (4);
        const int side = juce::jmin (r.getWidth() - 6, r.getHeight(), 76);
        knob.setBounds (r.withSizeKeepingCentre (side, side));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (footswitch.expanded (4).contains (e.getPosition()))
        {
            pressed = true;
            repaint();
        }
        else if (onSelect)
            onSelect();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (pressed)
        {
            pressed = false;
            if (footswitch.expanded (4).contains (e.getPosition()) && onStomp != nullptr)
                onStomp();
            repaint();
        }
    }

    const pedalboard::Pedal& info;
    juce::Colour colour;
    juce::Slider knob;
    std::function<bool()> on;
    std::function<void()> onStomp, onSelect, onTurn;
    bool picked = false, dim = false;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> plate, valueBox, footswitch;
    bool pressed = false, attaching = false;
};

//==============================================================================
EffectsView::EffectsView (RigEngine& e) : engine (e)
{
    setOpaque (true);
    kept.resize ((size_t) numSources);
    for (auto& k : kept)
        k.fill (0.0f);
    tunerBuffer.resize ((size_t) StripFx::kTunerSize);

    // The sources.
    const auto& list = sources();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const bool loop = i >= firstLoop && i < allLoops;
        auto* b = sourceButtons.add (new juce::TextButton (loop ? juce::String (i - firstLoop + 1) : juce::String (list[(size_t) i].name)));
        b->getProperties().set ("fontSize", loop ? 16.0f : 17.0f);
        b->setTooltip (juce::String ("Shows the pedals of the ") + list[(size_t) i].name + " (" + list[(size_t) i].kind + ").");
        b->onClick = [this, i] { showSource (i); };
        addAndMakeVisible (b);
    }

    for (auto* b : { &prevSource, &nextSource, &presetsButton, &saveButton, &copyButton, &pasteButton, &bypassButton, &resetButton, &pluginButton,
                     &pluginOpen })
    {
        b->getProperties().set ("fontSize", 17.0f);
        addAndMakeVisible (*b);
    }
    prevSource.onClick = [this] { showSource ((current + numSources - 1) % numSources); };
    nextSource.onClick = [this] { showSource ((current + 1) % numSources); };
    presetsButton.setTooltip ("Ready-made boards (Clean guitar, Funk wah, Fat bass amp, Dub delay...) and the ones you saved.");
    presetsButton.onClick = [this] { showPresetMenu(); };
    saveButton.setTooltip ("Saves this board's settings with a name, to use on any sound (Presets > My boards).");
    saveButton.onClick = [this] { askToSave(); };
    copyButton.setTooltip ("Copies this board's settings, to paste onto another sound.");
    copyButton.onClick = [this] { pressCopy(); };
    pasteButton.setTooltip ("Puts the board you copied onto this sound.");
    pasteButton.onClick = [this] { pressPaste(); };
    setLit (bypassButton, Lit::mute);
    bypassButton.setTooltip ("Switches the whole board out at once, to hear the sound dry. Press again to put it back.");
    bypassButton.onClick = [this] { pressBypass(); };
    resetButton.setTooltip ("Every pedal on this board off, back to how it starts.");
    resetButton.onClick = [this] { pressResetAll(); };
    pluginButton.setTooltip ("Your own effect plugin after the pedals: Quake, a Valhalla, any Audio Unit on this Mac.");
    pluginButton.onClick = [this]
    {
        PluginMenu::showForEffect (rack().plugin, pluginButton, [safe = juce::Component::SafePointer<EffectsView> (this)] (const juce::String& text)
                                   {
                                       if (safe != nullptr)
                                       {
                                           safe->say (text);
                                           safe->updateButtons();
                                       }
                                   });
    };
    pluginOpen.setTooltip ("Opens the plugin's own window.");
    pluginOpen.onClick = [this] { rack().plugin.showWindow(); };

    // The pedals.
    for (int i = 0; i < pedalboard::numPedals; ++i)
    {
        auto* p = stomps.add (new Stomp (pedalboard::pedals()[(size_t) i]));
        p->on = [this, i] { return isOn (i); };
        p->onStomp = [this, i] { stomp (i); };
        p->onSelect = [this, i] { selectPedal (i); };
        // Turning a pedal's knob is enough to hear it.
        if (pedalboard::pedals()[(size_t) i].onId != nullptr)
            p->onTurn = [this, i] { if (! isOn (i)) pedalboard::setOn (rack(), i, true); };
        addAndMakeVisible (p);
    }

    for (auto* b : { &rackPower, &rackReset })
    {
        b->getProperties().set ("fontSize", 16.0f);
        setInWell (*b, true);
        addAndMakeVisible (*b);
    }
    setLit (rackPower, Lit::play);
    rackPower.onClick = [this] { stomp (selected); };
    rackReset.setTooltip ("This pedal's settings back to how they start.");
    rackReset.onClick = [this]
    {
        pedalboard::resetPedal (rack(), selected);
        rebuildRack();
    };

    // The source's side.
    for (auto* b : { &plugInButton, &toLooperButton, &toSamplerButton, &toPadButton, &openTool })
    {
        b->getProperties().set ("fontSize", b == &plugInButton || b == &openTool ? 15.0f : 13.5f);
        setInWell (*b, true);
        addAndMakeVisible (*b);
    }
    setLit (plugInButton, Lit::play);
    plugInButton.setTooltip ("Switches the Line in on: what you plug into your audio interface (a guitar, bass, keytar) comes through "
                             "these pedals to the speakers. Use headphones with a mic, so it can't feed back.");
    plugInButton.onClick = [this] { pressPlugIn(); };
    for (auto* b : { &toLooperButton, &toSamplerButton, &toPadButton })
        setLit (*b, Lit::arm);
    toLooperButton.setTooltip ("Records this sound, with its pedals, onto the next empty loop layer from the next bar. Press again to loop it.");
    toLooperButton.onClick = [this] { pressRecToLooper(); };
    toSamplerButton.setTooltip ("Samples this sound, with its pedals, into the Sampler from the next bar. Press again to stop.");
    toSamplerButton.onClick = [this] { pressRecTo (RigCapture::toSampler); };
    toPadButton.setTooltip ("Samples this sound, with its pedals, onto the next empty MPC pad from the next bar. Press again to stop.");
    toPadButton.onClick = [this] { pressRecTo (RigCapture::toMpcPad); };
    openTool.onClick = [this]
    {
        const juce::String page (sources()[(size_t) current].page);
        if (page.isNotEmpty() && onOpen)
            onOpen (page);
    };
    for (int j = 0; j < 3; ++j)
        jack.addItem (RigEngine::inputJackName (j), j + 1);
    jack.setTooltip ("Which input of your audio interface the Line in listens to: a guitar is usually on input 1 or 2 alone.");
    jack.onChange = [this] { engine.inputJack = jack.getSelectedId() - 1; };
    addAndMakeVisible (jack);

    showSource (lineIn);
    startTimerHz (20);
}

EffectsView::~EffectsView()
{
    for (int i = 0; i < numSources; ++i)
        rackOf (i).tunerListening = false;
}

StripFx& EffectsView::rackOf (int source)
{
    const auto& s = sources()[(size_t) juce::jlimit (0, numSources - 1, source)];
    return s.loop >= 0 ? engine.getLoopFx (s.loop) : engine.getFx (s.strip);
}

StripFx& EffectsView::rack()
{
    return rackOf (current);
}

void EffectsView::say (const juce::String& text)
{
    if (onSay)
        onSay (text);
}

//==============================================================================
void EffectsView::showSource (int source)
{
    rack().tunerListening = false;
    current = juce::jlimit (0, numSources - 1, source);
    presetShown = {};
    beforeBypass.clear();
    attachPedals();
    rebuildRack();
    if (gainIn != nullptr)
        removeChildComponent (gainIn.get());
    gainIn = std::make_unique<Knob> (rack().params, "inputGain", "Gain in", sourceColour (current));
    gainIn->inWell = true;
    gainIn->nameSize = 13.0f;
    gainIn->slider.setTooltip ("How loud the sound comes in, before the pedals. A guitar or bass straight into an interface often needs "
                        "+6 to +12 dB here.");
    auto* gainParam = rack().params.getParameter ("inputGain");
    gainIn->slider.textFromValueFunction = [gainParam] (double v) { return gainParam->getText (gainParam->convertTo0to1 ((float) v), 16); };
    addAndMakeVisible (*gainIn);
    const juce::String page (sources()[(size_t) current].page);
    openTool.setButtonText (page.isNotEmpty() ? "Open " + page : juce::String());
    looperLayer = -1;
    shownSignature = {};
    resized();
    updateButtons();
    repaint();
}

void EffectsView::showStrip (int strip)
{
    for (int i = 0; i < numSources; ++i)
        if (sources()[(size_t) i].strip == strip && sources()[(size_t) i].loop < 0)
            return showSource (i);
}

void EffectsView::showLoop (int layer)
{
    showSource (firstLoop + juce::jlimit (0, RigLooper::kLayers - 1, layer));
}

void EffectsView::attachPedals()
{
    for (auto* s : stomps)
        s->attach (rack().params);
}

bool EffectsView::isOn (int pedal)
{
    return pedalboard::isOn (rack(), pedal);
}

void EffectsView::stomp (int pedal)
{
    if (pedal < 0 || pedal >= pedalboard::numPedals)
        return;
    auto& fx = rack();
    const bool on = ! isOn (pedal);
    pedalboard::setOn (fx, pedal, on, kept[(size_t) current].data() + pedal * 2);
    if (on && pedalboard::get (fx, "bypass") > 0.5f)
    {
        pedalboard::set (fx, "bypass", 0.0f);
        beforeBypass.clear();
    }
    if (on && current == lineIn && ! (engine.inputOn.load() && engine.micOn.load()))
        say ("The Line in is off: press Plug in to hear your instrument through the pedals.");
    selectPedal (pedal);
    updateButtons();
    for (auto* s : stomps)
        s->repaint();
}

void EffectsView::turnKnob (int pedal, double value)
{
    stomps[juce::jlimit (0, pedalboard::numPedals - 1, pedal)]->knob.setValue (value, juce::sendNotificationSync);
}

void EffectsView::selectPedal (int pedal)
{
    selected = juce::jlimit (0, pedalboard::numPedals - 1, pedal);
    for (int i = 0; i < stomps.size(); ++i)
        if (stomps[i]->picked != (i == selected))
        {
            stomps[i]->picked = i == selected;
            stomps[i]->repaint();
        }
    rebuildRack();
    updateButtons();
    repaint (rackWell);
}

void EffectsView::rebuildRack()
{
    for (auto& k : rackKnobs)
        removeChildComponent (k.get());
    rackKnobs.clear();
    const auto& info = pedalboard::pedals()[(size_t) selected];
    for (const auto& s : info.rack)
    {
        auto knob = std::make_unique<Knob> (rack().params, s.id, s.label, juce::Colour (info.colour));
        knob->inWell = true;
        knob->nameSize = 13.5f;
        auto* param = rack().params.getParameter (s.id);
        knob->slider.textFromValueFunction = [param] (double v) { return param != nullptr ? param->getText (param->convertTo0to1 ((float) v), 16) : juce::String (v, 2); };
        if (info.onId != nullptr)
            knob->slider.onDragStart = [this, pedal = selected] { if (! isOn (pedal)) pedalboard::setOn (rack(), pedal, true); };
        addAndMakeVisible (*knob);
        rackKnobs.push_back (std::move (knob));
    }
    resized();
}

//==============================================================================
void EffectsView::pickPreset (int index)
{
    const auto& list = pedalboard::presets();
    if (index < 0 || index >= (int) list.size())
        return;
    pedalboard::applyPreset (rack(), list[(size_t) index]);
    presetShown = list[(size_t) index].name;
    beforeBypass.clear();
    rebuildRack();
    updateButtons();
    repaint();
    say (presetShown + " on the " + sources()[(size_t) current].name + ". Stomp or turn any pedal to make it yours.");
}

void EffectsView::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto& list = pedalboard::presets();
    juce::StringArray headings;
    for (const auto& p : list)
        headings.addIfNotAlreadyThere (p.forWhat);
    for (const auto& h : headings)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) list.size(); ++i)
            if (h == list[(size_t) i].forWhat)
                sub.addItem (i + 1, list[(size_t) i].name, true, presetShown == list[(size_t) i].name);
        menu.addSubMenu (h, sub);
    }
    const auto files = pedalboard::userPresetsFolder().findChildFiles (juce::File::findFiles, false, "*.xml");
    juce::PopupMenu mine;
    for (int i = 0; i < files.size(); ++i)
        mine.addItem (1000 + i, files[i].getFileNameWithoutExtension(), true, presetShown == files[i].getFileNameWithoutExtension());
    if (files.isEmpty())
        mine.addItem (-1, "(Save a board to see it here)", false);
    menu.addSubMenu ("My boards", mine);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsButton),
                        [safe = juce::Component::SafePointer<EffectsView> (this), files] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result >= 1000)
                            {
                                const auto file = files[result - 1000];
                                if (pedalboard::loadPreset (safe->rack(), file))
                                {
                                    safe->presetShown = file.getFileNameWithoutExtension();
                                    safe->rebuildRack();
                                    safe->updateButtons();
                                    safe->repaint();
                                }
                            }
                            else
                                safe->pickPreset (result - 1);
                        });
}

void EffectsView::askToSave()
{
    auto* window = new juce::AlertWindow ("Save this board", "Give it a name. It goes in Presets, under My boards, for any sound.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", presetShown.isNotEmpty() ? presetShown + " (mine)" : juce::String (sources()[(size_t) current].name) + " board");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<EffectsView> (this), window] (int result)
                                                                        {
                                                                            if (safe == nullptr || result != 1)
                                                                                return;
                                                                            const auto name = juce::File::createLegalFileName (window->getTextEditorContents ("name").trim());
                                                                            if (name.isEmpty())
                                                                                return;
                                                                            if (pedalboard::savePreset (safe->rack(), pedalboard::userPresetsFolder().getChildFile (name + ".xml")))
                                                                            {
                                                                                safe->presetShown = name;
                                                                                safe->say ("Saved \"" + name + "\". It's in Presets, under My boards.");
                                                                                safe->repaint();
                                                                            }
                                                                        }),
                             true);
}

void EffectsView::pressCopy()
{
    clipboard = pedalboard::copySettings (rack());
    clipboardFrom = sources()[(size_t) current].name;
    say ("Copied the " + clipboardFrom + "'s board. Pick another sound and press Paste.");
    updateButtons();
}

void EffectsView::pressPaste()
{
    if (! clipboard.isValid())
        return say ("Copy a board first: pick a sound, press Copy, then come back here and press Paste.");
    pedalboard::pasteSettings (rack(), clipboard);
    presetShown = {};
    rebuildRack();
    updateButtons();
    repaint();
    say ("The " + clipboardFrom + "'s board is on the " + sources()[(size_t) current].name + " now, with its own settings to change.");
}

void EffectsView::pressBypass()
{
    auto& fx = rack();
    const bool bypassed = pedalboard::get (fx, "bypass") > 0.5f;
    pedalboard::set (fx, "bypass", bypassed ? 0.0f : 1.0f);
    updateButtons();
    for (auto* s : stomps)
        s->repaint();
}

void EffectsView::pressResetAll()
{
    auto& fx = rack();
    const float gain = pedalboard::get (fx, "inputGain");
    fx.reset();
    pedalboard::set (fx, "inputGain", gain);
    presetShown = {};
    rebuildRack();
    updateButtons();
    repaint();
}

void EffectsView::pressPlugIn()
{
    const bool live = engine.inputOn.load() && engine.micOn.load();
    if (live)
    {
        engine.inputOn = false;
        say ("The Line in is off.");
    }
    else
    {
        engine.inputOn = true;
        engine.micOn = true;
        say ("Line in on: plug your guitar, bass or keytar into your interface (pick input 1, 2 or both). "
             "If it's quiet, turn up Gain in; use the Tuner before you play.");
    }
    updateButtons();
    repaint (sideWell);
}

void EffectsView::pressRecToLooper()
{
    const auto& source = sources()[(size_t) current];
    if (source.looperSource < 0)
        return;
    auto& looper = engine.getLooper();
    if (looperLayer >= 0)
    {
        const auto state = looper.getState (looperLayer);
        if (state == RigLooper::State::armed || state == RigLooper::State::recording || state == RigLooper::State::empty)
        {
            looper.pressRecord (looperLayer);
            say ("Looping it on layer " + juce::String (looperLayer + 1) + ".");
            looperLayer = -1;
            updateButtons();
            return;
        }
    }
    if (current == lineIn && ! (engine.inputOn.load() && engine.micOn.load()))
    {
        engine.inputOn = true;
        engine.micOn = true;
    }
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (looper.getState (i) == RigLooper::State::empty)
        {
            looper.getLayer (i).source = source.looperSource;
            looper.pressRecord (i);
            looperLayer = i;
            say ("Recording the " + juce::String (source.name) + " (with its pedals) onto loop layer " + juce::String (i + 1)
                 + (engine.getTransport().isPlaying() ? " from the next bar." : ". Press Play to start.") + " Press Rec to looper again to loop it.");
            updateButtons();
            return;
        }
    say ("Every loop layer is in use. Clear one on the Looper screen first.");
}

void EffectsView::pressRecTo (int dest)
{
    const auto& source = sources()[(size_t) current];
    if (source.captureSource < 0)
        return;
    auto& capture = engine.getCapture();
    if (capture.getState() == RigCapture::State::idle)
    {
        engine.captureSource = source.captureSource;
        engine.captureDest = dest;
        if (current == lineIn && ! (engine.inputOn.load() && engine.micOn.load()))
        {
            engine.inputOn = true;
            engine.micOn = true;
        }
    }
    if (onSample)
        onSample();
    else if (capture.getState() == RigCapture::State::idle)
        engine.startCapture (engine.captureSource, engine.captureBars);
    else
        engine.stopCapture();
    updateButtons();
}

//==============================================================================
void EffectsView::updateTuner()
{
    auto& fx = rack();
    const bool want = current == lineIn && isShowing();
    if (fx.tunerListening.load() != want)
        fx.tunerListening = want;
    if (! want)
    {
        tunerText = {};
        tunerHasNote = false;
        return;
    }
    fx.readTuner (tunerBuffer.data(), (int) tunerBuffer.size());
    const float hz = pedalboard::detectPitch (tunerBuffer.data(), (int) tunerBuffer.size(), fx.getSampleRate());
    if (hz > 0)
    {
        const auto note = pedalboard::nearestNote (hz);
        tunerHasNote = true;
        tunerCents += (note.cents - tunerCents) * 0.5f;
        tunerText = note.name + juce::String (note.octave);
    }
    else
    {
        tunerHasNote = false;
        tunerText = "--";
    }
}

void EffectsView::updateButtons()
{
    auto& fx = rack();
    for (int i = 0; i < sourceButtons.size(); ++i)
    {
        sourceButtons[i]->setToggleState (i == current, juce::dontSendNotification);
        setLit (*sourceButtons[i], i == current ? Lit::current : rackOf (i).isActive() ? Lit::play : Lit::none);
    }
    const bool bypassed = pedalboard::get (fx, "bypass") > 0.5f;
    bypassButton.setToggleState (bypassed, juce::dontSendNotification);
    bypassButton.setButtonText (bypassed ? "Bypassed" : "Bypass");
    pasteButton.setEnabled (clipboard.isValid());
    const auto pluginText = fx.plugin.isLoaded() ? fx.plugin.getName() : juce::String ("+ Plugin");
    if (pluginButton.getButtonText() != pluginText)
        pluginButton.setButtonText (pluginText);
    pluginOpen.setVisible (fx.plugin.isLoaded());

    const bool on = isOn (selected);
    rackPower.setToggleState (on, juce::dontSendNotification);
    rackPower.setButtonText (on ? "On" : "Off");

    const auto& source = sources()[(size_t) current];
    const bool line = current == lineIn;
    const bool live = engine.inputOn.load() && engine.micOn.load();
    plugInButton.setVisible (line);
    jack.setVisible (line);
    plugInButton.setToggleState (live, juce::dontSendNotification);
    plugInButton.setButtonText (live ? "Plugged in" : "Plug in");
    jack.setSelectedId (engine.inputJack.load() + 1, juce::dontSendNotification);

    toLooperButton.setVisible (source.looperSource >= 0);
    bool looping = false;
    if (looperLayer >= 0)
    {
        const auto state = engine.getLooper().getState (looperLayer);
        looping = state == RigLooper::State::armed || state == RigLooper::State::recording || state == RigLooper::State::empty;
        if (state == RigLooper::State::playing || state == RigLooper::State::overdubbing)
            looperLayer = -1;
    }
    toLooperButton.setToggleState (looping, juce::dontSendNotification);
    toLooperButton.setButtonText (looping ? "Loop it" : "To looper");

    const auto& capture = engine.getCapture();
    const bool capturing = capture.getState() != RigCapture::State::idle && capture.getSource() == source.captureSource;
    for (auto [button, dest, text] : { std::tuple<juce::TextButton*, int, const char*> { &toSamplerButton, RigCapture::toSampler, "To sampler" },
                                       { &toPadButton, RigCapture::toMpcPad, "To MPC pad" } })
    {
        button->setVisible (source.captureSource >= 0);
        const bool mine = capturing && engine.captureDest == dest;
        button->setToggleState (mine, juce::dontSendNotification);
        const juce::String t = mine ? (capture.getState() == RigCapture::State::armed ? "Next bar" : "Stop") : juce::String (text);
        if (button->getButtonText() != t)
            button->setButtonText (t);
    }
    openTool.setVisible (juce::String (source.page).isNotEmpty());

    for (int i = 0; i < stomps.size(); ++i)
    {
        const bool dim = (i == pedalboard::gate && fx.getGateOpen() < 0.5f) || (i == pedalboard::comp && fx.getCompReduction() > 0.25f);
        if (stomps[i]->dim != dim)
        {
            stomps[i]->dim = dim;
            stomps[i]->repaint();
        }
    }
}

void EffectsView::timerCallback()
{
    if (! isShowing())
    {
        if (rack().tunerListening.load())
            rack().tunerListening = false;
        return;
    }
    const auto& source = sources()[(size_t) current];
    if (source.loop >= 0)
        outLevel = engine.getLooper().getLayer (source.loop).peak.load();
    else
        outLevel = meterOf != nullptr ? meterOf (source.strip) : 0.0f;
    inLevel = engine.micPeak.load();
    updateTuner();

    juce::String sig;
    auto& fx = rack();
    for (int i = 0; i < pedalboard::numPedals; ++i)
        sig << (int) isOn (i);
    for (auto* s : stomps)
        sig << "/" << s->knob.getValue();
    sig << "/" << pedalboard::get (fx, "bypass") << "/" << (int) fx.plugin.isLoaded();
    if (sig != shownSignature)
    {
        shownSignature = sig;
        for (auto* s : stomps)
            s->repaint();
        repaint (topRow);
    }
    updateButtons();
    repaint (sideWell);
}

//==============================================================================
void EffectsView::paint (juce::Graphics& g)
{
    drawFace (g, getLocalBounds());
    const auto& p = pal();
    const auto& source = sources()[(size_t) current];
    auto& fx = rack();

    // The sound's name and what's on its board.
    {
        const auto box = sourceLcd.toFloat();
        drawLcd (g, box, 7.0f);
        auto text = box.reduced (12, 2);
        const float nameWidth = juce::jmin (text.getWidth() * 0.5f, 220.0f);
        lcdText (g, juce::String (source.name).toUpperCase(), text.removeFromLeft (nameWidth), 24.0f, juce::Justification::centredLeft);
        const int on = pedalboard::countOn (fx);
        juce::String what = presetShown.isNotEmpty() ? presetShown : on == 0 ? juce::String ("no pedals on") : juce::String (on) + (on == 1 ? " pedal on" : " pedals on");
        if (pedalboard::get (fx, "bypass") > 0.5f)
            what = "bypassed";
        lcdText (g, what, text, 20.0f, juce::Justification::centredLeft, colours::lcdGreen);
    }

    // The source column.
    drawWell (g, column.toFloat(), 12.0f);
    engraved (g, "Through the pedals", toolsLabel, juce::Justification::centredLeft, 12.5f, true);
    engraved (g, "Loops, one by one", loopsLabel, juce::Justification::centredLeft, 12.5f, true);
    engraved (g, "Everything", masterLabel, juce::Justification::centredLeft, 12.5f, true);

    // The patch cable along the board.
    if (! stomps.isEmpty())
    {
        g.setColour (juce::Colour (0xff1e1a16));
        for (int row = 0, lastY = -1; row < stomps.size(); ++row)
        {
            const int y = stomps[row]->getY();
            if (y == lastY)
                continue;
            lastY = y;
            g.fillRoundedRectangle ((float) boardArea.getX() - 10.0f, (float) y + 23.0f, (float) boardArea.getWidth() + 20.0f, 6.0f, 3.0f);
        }
        const int captionY = boardArea.getY() - 20;
        engraved (g, "In", { boardArea.getX(), captionY, 200, 16 }, juce::Justification::centredLeft, 12.0f);
        engraved (g, juce::String::fromUTF8 ("Out \xe2\x86\x92"), { boardArea.getRight() - 120, captionY, 120, 16 }, juce::Justification::centredRight, 12.0f);
        engraved (g, juce::String::fromUTF8 ("Stomp to switch \xc2\xb7 click a pedal to open all its settings"),
                  { boardArea.getX() + 200, captionY, boardArea.getWidth() - 320, 16 }, juce::Justification::centred, 12.0f);
    }

    // The source.
    drawWell (g, sideWell.toFloat(), 12.0f);
    g.setColour (p.wellInk);
    g.setFont (cond (21.0f, Weight::extra, 0.08f));
    g.drawText (juce::String (source.name).toUpperCase(), sideTitle, juce::Justification::centredLeft);
    const bool line = current == lineIn;
    const bool live = engine.inputOn.load() && engine.micOn.load();
    juce::String sub = source.kind;
    if (line)
        sub = live ? juce::String::fromUTF8 ("Plugged in \xc2\xb7 ") + RigEngine::inputJackName (engine.inputJack.load())
                   : juce::String::fromUTF8 ("Off \xc2\xb7 press Plug in");
    else if (current == vocoderSource)
        sub = juce::String::fromUTF8 ("Mic \xe2\x86\x92 vocoder's 13 pedals \xe2\x86\x92 these");
    engraved (g, sub, sideSub, juce::Justification::centredLeft, 12.0f, true);
    if (line)
    {
        drawLamp (g, plugLamp.getCentre().toFloat(), live ? Lit::play : Lit::none, 14.0f);
        // The tuner: the note, and a needle that's green when it's in tune.
        const auto t = tunerArea.toFloat();
        drawLcd (g, t, 6.0f);
        auto inner = t.reduced (10, 6);
        const auto noteBox = inner.removeFromLeft (64);
        lcdText (g, tunerText.isEmpty() ? juce::String ("--") : tunerText, noteBox, 28.0f, juce::Justification::centredLeft,
                 tunerHasNote && std::abs (tunerCents) < 5 ? colours::lcdGreen : colours::lcdAmber);
        auto scale = inner.reduced (4, inner.getHeight() * 0.3f);
        g.setColour (colours::lcdAmber.withAlpha (0.3f));
        g.fillRect (scale.withSizeKeepingCentre (scale.getWidth(), 2.0f));
        g.fillRect (scale.withSizeKeepingCentre (2.0f, scale.getHeight() + 6));
        if (tunerHasNote)
        {
            const float x = scale.getCentreX() + juce::jlimit (-50.0f, 50.0f, tunerCents) / 50.0f * scale.getWidth() / 2;
            g.setColour (std::abs (tunerCents) < 5 ? colours::lcdGreen : colours::lcdAmber);
            g.fillRoundedRectangle (x - 3, scale.getY() - 3, 6, scale.getHeight() + 6, 2.0f);
        }
        engraved (g, "Tuner", tunerArea.withY (tunerArea.getY() - 15).withHeight (13), juce::Justification::centredLeft, 11.5f, true);
        drawLeds (g, inLeds.toFloat(), std::sqrt (inLevel), 18, false);
        engraved (g, "In", inLeds.withX (inLeds.getX() - 34).withWidth (30).expanded (0, 3), juce::Justification::centredLeft, 11.5f, true);
    }
    drawLeds (g, outLeds.toFloat(), std::sqrt (outLevel), 18, false);
    engraved (g, "Out", outLeds.withX (outLeds.getX() - 34).withWidth (30).expanded (0, 3), juce::Justification::centredLeft, 11.5f, true);

    // The rack: the picked pedal, every setting.
    drawWell (g, rackWell.toFloat(), 12.0f);
    {
        const auto& info = pedalboard::pedals()[(size_t) selected];
        auto t = rackTitle;
        g.setColour (juce::Colour (info.colour));
        g.fillRoundedRectangle (t.removeFromLeft (8).toFloat().reduced (0, 4), 3.0f);
        t.removeFromLeft (10);
        g.setColour (p.wellInk);
        g.setFont (cond (24.0f, Weight::extra, 0.08f));
        const auto title = juce::String (info.name).toUpperCase();
        const int titleWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 14;
        g.drawText (title, t.removeFromLeft (titleWidth), juce::Justification::centredLeft);
        g.setColour (p.wellInk2);
        g.setFont (cond (15.0f, Weight::semi, 0.06f));
        g.drawText (juce::String (info.kind).toUpperCase() + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (source.name).toUpperCase(), t,
                    juce::Justification::centredLeft);
        g.setFont (barlow (13.5f));
        g.drawFittedText (info.description, rackText, juce::Justification::topLeft, 2, 0.9f);
    }
}

void EffectsView::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (22).withTrimmedRight (22).withTrimmedTop (6).withTrimmedBottom (12);
    const bool wide = r.getWidth() >= 1300;
    topRow = r.removeFromTop (44);
    {
        auto m = topRow;
        if (rack().plugin.isLoaded())
        {
            pluginOpen.setBounds (m.removeFromRight (wide ? 70 : 60));
            m.removeFromRight (6);
        }
        else
            pluginOpen.setBounds ({});
        pluginButton.setBounds (m.removeFromRight (wide ? 150 : 120));
        m.removeFromRight (wide ? 14 : 8);
        resetButton.setBounds (m.removeFromRight (wide ? 100 : 92));
        m.removeFromRight (6);
        bypassButton.setBounds (m.removeFromRight (wide ? 104 : 96));
        m.removeFromRight (wide ? 14 : 8);
        pasteButton.setBounds (m.removeFromRight (wide ? 74 : 66));
        m.removeFromRight (6);
        copyButton.setBounds (m.removeFromRight (wide ? 70 : 62));
        m.removeFromRight (6);
        saveButton.setBounds (m.removeFromRight (wide ? 66 : 60));
        m.removeFromRight (6);
        presetsButton.setBounds (m.removeFromRight (wide ? 96 : 90));
        m.removeFromRight (wide ? 14 : 8);
        prevSource.setBounds (m.removeFromLeft (40));
        m.removeFromLeft (6);
        nextSource.setBounds (m.removeFromRight (40));
        m.removeFromRight (6);
        sourceLcd = m;
    }
    r.removeFromTop (10);

    // The sources, down the left.
    column = r.removeFromLeft (juce::jlimit (140, 180, r.getWidth() / 8));
    r.removeFromLeft (12);
    {
        auto c = column.reduced (8, 8);
        const int labelH = 16;
        const int tools = firstLoop;
        const int loopRowH = juce::jlimit (22, 30, c.getHeight() / 16);
        const int bh = juce::jlimit (22, 36, (c.getHeight() - labelH * 3 - loopRowH * 2 - 6 * 2) / (tools + 2));
        toolsLabel = c.removeFromTop (labelH);
        for (int i = 0; i < tools; ++i)
            sourceButtons[i]->setBounds (c.removeFromTop (bh).reduced (0, 2));
        c.removeFromTop (6);
        loopsLabel = c.removeFromTop (labelH);
        for (int row = 0; row < 2; ++row)
        {
            auto line = c.removeFromTop (loopRowH);
            const int w = line.getWidth() / 4;
            for (int col = 0; col < 4; ++col)
                sourceButtons[firstLoop + row * 4 + col]->setBounds (line.removeFromLeft (w).reduced (2, 2));
        }
        sourceButtons[allLoops]->setBounds (c.removeFromTop (bh).reduced (0, 2));
        c.removeFromTop (6);
        masterLabel = c.removeFromTop (labelH);
        sourceButtons[masterSource]->setBounds (c.removeFromTop (bh).reduced (0, 2));
    }

    // The pedalboard: one row when they fit, else two.
    r.removeFromTop (20);
    const int n = stomps.size();
    const bool oneRow = r.getWidth() / n >= 70;
    const int bottomMin = 180;
    const int boardHeight = oneRow ? juce::jlimit (150, 230, juce::jmin (r.getHeight() * 2 / 5, r.getHeight() - bottomMin))
                                   : juce::jlimit (240, 400, juce::jmin (r.getHeight() * 3 / 5, r.getHeight() - bottomMin));
    boardArea = r.removeFromTop (boardHeight);
    {
        const int gap = oneRow ? 5 : 7;
        const int perRow = oneRow ? n : (n + 1) / 2;
        const int rows = oneRow ? 1 : 2;
        const int w = (boardArea.getWidth() - gap * (perRow - 1)) / perRow;
        const int h = (boardArea.getHeight() - gap * (rows - 1)) / rows;
        for (int i = 0; i < n; ++i)
        {
            const int row = i / perRow, col = i % perRow;
            stomps[i]->setBounds (boardArea.getX() + col * (w + gap), boardArea.getY() + row * (h + gap), w, h);
        }
    }
    r.removeFromTop (10);

    // The source | the rack.
    sideWell = r.removeFromLeft (juce::jlimit (250, 300, r.getWidth() / 4));
    r.removeFromLeft (12);
    rackWell = r;
    {
        auto w = sideWell.reduced (12, 10);
        auto title = w.removeFromTop (24);
        const bool line = current == lineIn;
        if (line)
            plugLamp = title.removeFromRight (18);
        else
        {
            plugLamp = {};
            openTool.setBounds (title.removeFromRight (juce::jmin (130, title.getWidth() / 2)).reduced (0, 1));
        }
        sideTitle = title;
        sideSub = w.removeFromTop (15);
        w.removeFromTop (6);

        // Along the bottom: record it (with its pedals) to the looper, the sampler or a pad.
        auto recRow = w.removeFromBottom (28);
        const int third = (recRow.getWidth() - 8) / 3;
        toLooperButton.setBounds (recRow.removeFromLeft (third));
        recRow.removeFromLeft (4);
        toSamplerButton.setBounds (recRow.removeFromLeft (third));
        recRow.removeFromLeft (4);
        toPadButton.setBounds (recRow);
        w.removeFromBottom (8);
        outLeds = w.removeFromBottom (9).withTrimmedLeft (34);
        w.removeFromBottom (6);
        if (line)
        {
            inLeds = w.removeFromBottom (9).withTrimmedLeft (34);
            w.removeFromBottom (6);
            if (openTool.isVisible())
                openTool.setBounds ({});
            auto row = w.removeFromTop (28);
            plugInButton.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
            row.removeFromLeft (6);
            jack.setBounds (row);
            w.removeFromTop (4);
            auto rest = w;
            if (gainIn != nullptr)
                gainIn->setBounds (rest.removeFromLeft (juce::jmin (84, rest.getWidth() / 3)));
            rest.removeFromLeft (8);
            rest.removeFromTop (14);   // its label
            tunerArea = rest.withSizeKeepingCentre (rest.getWidth(), juce::jlimit (0, 44, rest.getHeight() - 4));
        }
        else
        {
            inLeds = tunerArea = {};
            if (gainIn != nullptr)
                gainIn->setBounds (w.withSizeKeepingCentre (juce::jmin (90, w.getWidth()), juce::jmin (130, w.getHeight())));
        }
    }

    // The rack.
    {
        auto w = rackWell.reduced (16, 10);
        auto head = w.removeFromTop (32);
        rackReset.setBounds (head.removeFromRight (84).reduced (0, 2));
        head.removeFromRight (8);
        rackPower.setBounds (head.removeFromRight (100).reduced (0, 2));
        head.removeFromRight (12);
        rackTitle = head;
        rackText = w.removeFromTop (w.getWidth() > 900 ? 22 : 36);
        w.removeFromTop (4);
        rackKnobArea = w;
        const int count = (int) rackKnobs.size();
        if (count > 0)
        {
            const int kw = juce::jmin (130, w.getWidth() / count);
            const int kh = juce::jmin (150, w.getHeight());
            const int x0 = w.getX() + (w.getWidth() - count * kw) / 2;
            for (int i = 0; i < count; ++i)
                rackKnobs[(size_t) i]->setBounds (juce::Rectangle<int> (x0 + i * kw, w.getY(), kw, kh).reduced (4, 0));
        }
    }
}

void EffectsView::mouseUp (const juce::MouseEvent& e)
{
    if (current == lineIn && (plugLamp.expanded (8).contains (e.getPosition()) || sideSub.contains (e.getPosition())))
        pressPlugIn();
}
