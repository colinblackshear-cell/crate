#include "ControllersPanel.h"

using namespace crate::controllers;

namespace
{
constexpr int kLine = 32, kGap = 8, kLabelW = 74;

juce::String noteName (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")";
}

juce::String knobsText (const Profile& p)
{
    juce::StringArray ccs;
    bool consecutive = true;
    for (int k = 0; k < kNumKnobs; ++k)
    {
        const int cc = p.knobs[(size_t) k];
        if (cc < 0)
        {
            consecutive = false;
            continue;
        }
        ccs.add (juce::String (cc));
        if (k > 0 && cc != p.knobs[(size_t) k - 1] + 1)
            consecutive = false;
    }
    if (ccs.isEmpty())
        return "None yet: press Learn knobs";
    if (consecutive)
        return "Knobs 1-8: CC " + ccs[0] + juce::String::fromUTF8 ("\xe2\x80\x93") + ccs[ccs.size() - 1];
    return "CC " + ccs.joinIntoString (" ");
}
}

//==============================================================================
int ControllersPanel::Row::heightFor (const Profile& p)
{
    int lines = 0;
    if (p.kind != Kind::off)
        lines = (p.hasPads() ? 1 : 0) + (p.hasKeys() ? 1 : 0) + 1;
    return 12 + kLine + 4 + 22 + 8 + lines * (kLine + kGap) + (lines > 0 ? 22 : 0) + 10;
}

ControllersPanel::Row::Row (ControllersPanel& p, const juce::String& n)
    : name (n), panel (p), translator (p.setup.translatorFor (n))
{
    for (int k = 0; k < (int) Kind::numKinds; ++k)
        kind.addItem (kindName ((Kind) k), k + 1);
    kind.setTooltip ("What this controller is. It sets how its keys, pads and knobs play Kaleidobox.");
    kind.onChange = [this]
    {
        const auto k = (Kind) (kind.getSelectedId() - 1);
        if (k == translator.getProfile().kind)
            return;
        // A new kind starts from its defaults (or the guess for this controller, if it's that kind).
        const auto guess = guessFor (name);
        auto np = guess.kind == k ? guess : Profile::defaultsFor (k);
        // Keep what you set that still means the same.
        const auto old = translator.getProfile();
        if (old.hasKeys() && np.hasKeys())
            np.octave = old.octave;
        apply (np);
    };

    layout.addItem (layoutName (Layout::blocks), 1);
    layout.addItem (layoutName (Layout::rows), 2);
    layout.setTooltip ("How the pads count up from pad 1: in 4x4 blocks (bottom left, bottom right, top left, top right: "
                       "the MPC's four banks at once) or in rows of eight from the bottom left.");
    layout.onChange = [this]
    {
        auto np = translator.getProfile();
        np.layout = layout.getSelectedId() == 2 ? Layout::rows : Layout::blocks;
        apply (np);
    };

    for (int o = -3; o <= 3; ++o)
        octave.addItem ("Octave " + juce::String (o > 0 ? "+" : "") + juce::String (o), o + 4);
    octave.setTooltip ("Moves this controller's keys up or down by octaves (a small keyboard reaches the bass this way). The pads stay.");
    octave.onChange = [this]
    {
        auto np = translator.getProfile();
        np.octave = octave.getSelectedId() - 4;
        apply (np);
    };

    learnPad.setTooltip ("Press, then hit the pad you want as pad 1 (the MPC's bottom-left pad). Its note, and its channel, become pad 1.");
    learnPad.onClick = [this]
    {
        if (translator.learning() == Translator::Learn::pad)
            translator.stopLearning();
        else
            translator.learn (Translator::Learn::pad);
        tick();
    };
    learnKnobs.setTooltip ("Press, then turn knobs 1 to 8 in order. They then turn the picked track's eight knobs, like a Launchkey's.");
    learnKnobs.onClick = [this]
    {
        if (translator.learning() == Translator::Learn::knobs)
            translator.stopLearning();
        else
            translator.learn (Translator::Learn::knobs);
        tick();
    };
    reset.setTooltip ("Back to the usual settings for this kind of controller.");
    reset.onClick = [this]
    {
        const auto k = translator.getProfile().kind;
        const auto guess = guessFor (name);
        apply (guess.kind == k ? guess : Profile::defaultsFor (k));
    };

    for (auto* b : { &learnPad, &learnKnobs, &reset })
    {
        vintage::setInWell (*b, true);
        b->getProperties().set ("fontSize", 15.0f);
        addAndMakeVisible (b);
    }
    vintage::setLit (learnPad, vintage::Lit::arm);
    vintage::setLit (learnKnobs, vintage::Lit::arm);
    for (auto* c : { &kind, &layout, &octave })
        addAndMakeVisible (c);
    for (auto* l : { &padsInfo, &knobsInfo, &status })
    {
        l->setColour (juce::Label::textColourId, vintage::pal().wellInk);
        l->setFont (vintage::barlow (14.0f));
        addAndMakeVisible (l);
    }
    status.setColour (juce::Label::textColourId, vintage::colours::next);
    refresh();
}

void ControllersPanel::Row::apply (const Profile& p)
{
    const bool resize = heightFor (p) != heightFor (translator.getProfile());
    const bool wasOn = translator.getProfile().kind != Kind::off;
    panel.setup.setProfile (name, p);
    refresh();
    if (resize)
        panel.layoutList();
    if (wasOn != (p.kind != Kind::off) && panel.onChanged)
        panel.onChanged();
}

void ControllersPanel::Row::refresh()
{
    const auto p = translator.getProfile();
    kind.setSelectedId ((int) p.kind + 1, juce::dontSendNotification);
    layout.setSelectedId (p.layout == Layout::rows ? 2 : 1, juce::dontSendNotification);
    octave.setSelectedId (p.octave + 4, juce::dontSendNotification);
    padsInfo.setText ("Pad 1 = " + noteName (p.firstPadNote)
                          + (p.kind == Kind::pads ? juce::String (", any channel") : ", channel " + juce::String (p.padChannel)),
                      juce::dontSendNotification);
    knobsInfo.setText (knobsText (p), juce::dontSendNotification);
    const bool on = p.kind != Kind::off;
    learnPad.setVisible (on && p.hasPads());
    padsInfo.setVisible (on && p.hasPads());
    layout.setVisible (p.kind == Kind::pads);
    octave.setVisible (on && p.hasKeys());
    for (auto* c : std::initializer_list<juce::Component*> { &learnKnobs, &knobsInfo, &reset, &status })
        c->setVisible (on);
    resized();
    repaint();
}

void ControllersPanel::Row::tick()
{
    const bool nowLit = juce::Time::getMillisecondCounter() - translator.lastHeard() < 250 && translator.lastHeard() != 0;
    if (nowLit != lit)
    {
        lit = nowLit;
        repaint (lampArea);
    }
    juce::String text;
    switch (translator.learning())
    {
        case Translator::Learn::pad:
            text = "Hit pad 1 on " + name + " now (the pad you want as the MPC's bottom-left pad)...";
            break;
        case Translator::Learn::knobs:
            text = "Turn knob " + juce::String (translator.knobsLearned() + 1) + " of 8 now..."
                   + (translator.knobsLearned() > 0 ? " Stop keeps the " + juce::String (translator.knobsLearned()) + " turned so far." : juce::String());
            break;
        case Translator::Learn::none:
        default:
            text = {};
            break;
    }
    if (status.getText() != text)
        status.setText (text, juce::dontSendNotification);
    learnPad.setButtonText (translator.learning() == Translator::Learn::pad ? "Cancel" : "Learn pad 1");
    learnPad.setToggleState (translator.learning() == Translator::Learn::pad, juce::dontSendNotification);
    learnKnobs.setButtonText (translator.learning() == Translator::Learn::knobs ? "Stop" : "Learn knobs");
    learnKnobs.setToggleState (translator.learning() == Translator::Learn::knobs, juce::dontSendNotification);
}

void ControllersPanel::Row::paint (juce::Graphics& g)
{
    const auto& pal = vintage::pal();
    vintage::drawWell (g, getLocalBounds().toFloat(), 10.0f);
    vintage::drawLamp (g, lampArea.getCentre().toFloat(), lit ? vintage::Lit::next : vintage::Lit::none);
    g.setColour (pal.wellInk);
    g.setFont (vintage::cond (19.0f, vintage::Weight::extra, 0.04f));
    g.drawFittedText (name, nameArea, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (pal.wellInk2);
    g.setFont (vintage::barlow (13.5f));
    g.drawFittedText (kindHint (translator.getProfile().kind), hintArea, juce::Justification::centredLeft, 1, 0.85f);
    for (auto [area, text] : { std::pair { padsLabel, "Pads" }, { keysLabel, "Keys" }, { knobsLabel, "Knobs" } })
        if (! area.isEmpty())
            vintage::engraved (g, text, area, juce::Justification::centredLeft, 13.0f, true);
}

void ControllersPanel::Row::resized()
{
    const auto p = translator.getProfile();
    auto r = getLocalBounds().reduced (16, 0).withTrimmedTop (12);
    auto top = r.removeFromTop (kLine);
    lampArea = top.removeFromLeft (20);
    top.removeFromLeft (6);
    kind.setBounds (top.removeFromRight (250).reduced (0, 1));
    top.removeFromRight (10);
    nameArea = top;
    r.removeFromTop (4);
    hintArea = r.removeFromTop (22);
    r.removeFromTop (8);
    padsLabel = keysLabel = knobsLabel = {};
    if (p.kind == Kind::off)
        return;
    auto line = [&r] { auto l = r.removeFromTop (kLine); r.removeFromTop (kGap); return l; };
    if (p.hasPads())
    {
        auto l = line();
        padsLabel = l.removeFromLeft (kLabelW);
        padsInfo.setBounds (l.removeFromLeft (240));
        learnPad.setBounds (l.removeFromLeft (120).reduced (0, 1));
        l.removeFromLeft (10);
        layout.setBounds (l.removeFromLeft (juce::jmin (330, l.getWidth())).reduced (0, 1));
    }
    if (p.hasKeys())
    {
        auto l = line();
        keysLabel = l.removeFromLeft (kLabelW);
        octave.setBounds (l.removeFromLeft (150).reduced (0, 1));
    }
    auto l = line();
    knobsLabel = l.removeFromLeft (kLabelW);
    knobsInfo.setBounds (l.removeFromLeft (240));
    learnKnobs.setBounds (l.removeFromLeft (120).reduced (0, 1));
    l.removeFromLeft (10);
    reset.setBounds (l.removeFromLeft (100).reduced (0, 1));
    status.setBounds (r.removeFromTop (22));
}

//==============================================================================
ControllersPanel::ControllersPanel (Setup& s, std::function<juce::StringArray()> devices, std::function<void()> changed)
    : setup (s), listDevices (std::move (devices)), onChanged (std::move (changed))
{
    setLookAndFeel (&look);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    vintage::setInWell (done, false);
    done.getProperties().set ("fontSize", 16.0f);
    done.setTooltip ("Close. Everything here is saved on this Mac straight away.");
    done.onClick = [this]
    {
        if (onDone)
            onDone();
        else if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };
    addAndMakeVisible (done);
    rebuild();
    setSize (820, 640);
    startTimerHz (15);
}

ControllersPanel::~ControllersPanel()
{
    for (auto* r : rows)
        if (r->learnPad.getToggleState() || r->learnKnobs.getToggleState())
            setup.translatorFor (r->name).stopLearning();
    setup.saveLearned();
    setLookAndFeel (nullptr);
}

void ControllersPanel::setNewDevice (const juce::String& name)
{
    newDevice = name;
    rebuild();
    repaint();
}

ControllersPanel::Row* ControllersPanel::getRow (const juce::String& name) const
{
    for (auto* r : rows)
        if (r->name == name)
            return r;
    return nullptr;
}

void ControllersPanel::rebuild()
{
    auto names = listDevices ? listDevices() : juce::StringArray();
    shownDevices = names;
    if (newDevice.isNotEmpty() && names.contains (newDevice))
    {
        names.removeString (newDevice);
        names.insert (0, newDevice);
    }
    rows.clear();
    for (const auto& n : names)
    {
        setup.markKnown (n);   // seen here: it won't open this again by itself
        list.addAndMakeVisible (rows.add (new Row (*this, n)));
    }
    layoutList();
}

void ControllersPanel::layoutList()
{
    int y = 0;
    const int w = juce::jmax (100, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
    for (auto* r : rows)
    {
        const int h = Row::heightFor (setup.translatorFor (r->name).getProfile());
        r->setBounds (0, y, w, h);
        y += h + 12;
    }
    list.setSize (w, juce::jmax (1, y));
}

void ControllersPanel::timerCallback()
{
    if (setup.saveLearned())
        for (auto* r : rows)
            r->refresh();
    for (auto* r : rows)
        r->tick();

    static constexpr int kCheckEvery = 15;
    if (++checkTicks % kCheckEvery == 0 && listDevices && listDevices() != shownDevices)
        rebuild();
}

void ControllersPanel::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    const auto& p = vintage::pal();
    auto t = titleArea;
    g.setColour (p.ink);
    g.setFont (vintage::cond (28.0f, vintage::Weight::extra, 0.12f));
    g.drawText ("MIDI CONTROLLERS", t.removeFromTop (34), juce::Justification::centredLeft);
    g.setColour (p.ink2);
    g.setFont (vintage::barlow (14.5f));
    g.drawFittedText ("What each controller is, so its keys, pads and knobs play Kaleidobox. Saved on this Mac, for every show.",
                      t.removeFromTop (22), juce::Justification::centredLeft, 1, 0.85f);
    if (newDevice.isNotEmpty())
    {
        g.setColour (vintage::colours::next.darker (0.35f));
        g.setFont (vintage::cond (17.0f, vintage::Weight::bold, 0.02f));
        g.drawFittedText ("New: " + newDevice + ". Kaleidobox guessed " + kindName (setup.profileFor (newDevice).kind)
                              + ". Check it below (press a key or pad: its lamp lights), then Done.",
                          t.removeFromTop (24), juce::Justification::centredLeft, 1, 0.8f);
    }
    if (rows.isEmpty())
    {
        g.setColour (p.ink2);
        g.setFont (vintage::barlow (16.0f));
        g.drawFittedText ("No MIDI controller is plugged in. Plug one in and it shows up here. "
                          "Until then, play with your Mac's keys (A W S E D...).",
                          viewport.getBounds().reduced (10), juce::Justification::centredTop, 3);
    }
    g.setColour (p.ink2);
    g.setFont (vintage::barlow (13.5f));
    g.drawFittedText ("Your Mac's keys (A W S E D...) always play too. Knobs 1-8 turn the picked track's knobs; "
                      "Settings > Knobs puts any knob on any setting, and Learn by Play/Stop key takes any button.",
                      footerArea.withTrimmedRight (done.getWidth() + 16), juce::Justification::centredLeft, 2, 0.85f);
}

void ControllersPanel::resized()
{
    auto r = getLocalBounds().reduced (20, 14);
    titleArea = r.removeFromTop (newDevice.isNotEmpty() ? 84 : 60);
    footerArea = r.removeFromBottom (44);
    done.setBounds (footerArea.withLeft (footerArea.getRight() - 120).reduced (0, 4));
    r.removeFromBottom (8);
    viewport.setBounds (r);
    layoutList();
}
