#include "InstrumentShell.h"

using namespace analog;

InstrumentShell::InstrumentShell (PluginSlot& s, const juce::String& name, juce::Colour a) : slot (s), trackName (name), accent (a)
{
    for (auto* b : { &load, &window, &remove, &prev, &next, &presets, &autoMap, &screenButton })
        addAndMakeVisible (b);
    screenView.setViewedComponent (&holder, false);
    screenView.setScrollBarsShown (true, true);
    addChildComponent (screenView);
    screenButton.setTooltip ("The plugin's own interface, right here on the panel (Arturia's whole synth). Press again for the 8 knobs.");
    screenButton.onClick = [this] { showScreen (! isShowingScreen()); };
    load.setTooltip ("Pick one of your instrument plugins (Arturia and the rest) to play the " + name + " track.");
    window.setTooltip ("Moves the plugin's own interface to a window of its own (for a second screen). Its screen brings it back here.");
    remove.setTooltip ("Takes the plugin out. The " + name + "'s own sound plays again.");
    prev.setTooltip ("The plugin's previous preset.");
    next.setTooltip ("The plugin's next preset.");
    presets.setTooltip ("All of the plugin's presets.");
    autoMap.setTooltip ("Puts the plugin's most useful settings on the 8 knobs again.");
    load.onClick = [this] { showLoadMenu(); };
    window.onClick = [this] { slot.showWindow(); };
    remove.onClick = [this]
    {
        const auto was = slot.getName();
        slot.clear();
        say (was + " is out. The " + trackName + " plays its own sound again.");
        refresh();
        if (onChanged)
            onChanged();
    };
    prev.onClick = [this] { stepPreset (-1); };
    next.onClick = [this] { stepPreset (1); };
    presets.onClick = [this] { showPresetMenu(); };
    autoMap.onClick = [this]
    {
        slot.autoMapMacros();
        refresh();
        say ("The knobs are on " + slot.getName() + "'s main settings again.");
    };

    for (int i = 0; i < PluginSlot::kMacros; ++i)
    {
        auto knob = std::make_unique<Knob> ("Macro " + juce::String (i + 1), accent);
        knob->slider.setRange (0.0, 1.0);
        knob->slider.setDoubleClickReturnValue (false, 0.0);
        knob->slider.textFromValueFunction = [this, i] (double v)
        {
            if (auto* p = slot.macroParameter (i))
                return p->getText ((float) v, 12).trim().substring (0, 10);
            return juce::String ("--");
        };
        knob->slider.onDragStart = [this, i]
        {
            dragging[(size_t) i] = true;
            if (auto* p = slot.macroParameter (i))
                p->beginChangeGesture();
        };
        knob->slider.onDragEnd = [this, i]
        {
            dragging[(size_t) i] = false;
            if (auto* p = slot.macroParameter (i))
                p->endChangeGesture();
        };
        knob->slider.onValueChange = [this, i, k = knob.get()]
        {
            if (auto* p = slot.macroParameter (i); p != nullptr && std::abs (p->getValue() - (float) k->slider.getValue()) > 1.0e-5f)
                p->setValueNotifyingHost ((float) k->slider.getValue());
            k->repaint();
        };
        knob->onNameClicked = [this, i] { showMacroMenu (i); };
        knob->setTooltip ("Turns one of the plugin's settings. Click its name to pick which.");
        addAndMakeVisible (*knob);
        macros.push_back (std::move (knob));
    }

    level.slider.setRange (0.0, 2.0);
    level.slider.setSkewFactorFromMidPoint (1.0);
    level.slider.setDoubleClickReturnValue (true, 1.0);
    level.slider.setValue (slot.level.load(), juce::dontSendNotification);
    level.slider.textFromValueFunction = [] (double v)
    {
        return v < 0.001 ? juce::String ("off") : juce::String (juce::Decibels::gainToDecibels (v), 1) + " dB";
    };
    level.slider.onValueChange = [this] { slot.level = (float) level.slider.getValue(); level.repaint(); };
    level.setTooltip ("The plugin's level in Kaleidobox. Double-click for 0 dB.");
    addAndMakeVisible (level);

    refresh();
    startTimerHz (15);
}

InstrumentShell::~InstrumentShell() { stopTimer(); }

void InstrumentShell::say (const juce::String& text)
{
    if (onSay)
        onSay (text);
}

juce::String InstrumentShell::signature() const
{
    juce::String s;
    s << (slot.isLoaded() ? slot.getName() : juce::String ("-")) << "|" << slot.currentProgram() << "|" << (int) slot.bypassed.load()
      << "|" << (int) slot.isWindowOpen() << "|" << (int) isShowingScreen();
    for (int i = 0; i < PluginSlot::kMacros; ++i)
        s << "|" << slot.getMacro (i);
    return s;
}

void InstrumentShell::refresh()
{
    shown = signature();
    const bool loaded = slot.isLoaded();
    for (auto* b : { &window, &remove, &prev, &next, &presets, &autoMap })
        b->setEnabled (loaded);
    for (int i = 0; i < (int) macros.size(); ++i)
    {
        auto* p = slot.macroParameter (i);
        auto& k = *macros[(size_t) i];
        k.setName (p != nullptr ? p->getName (14) : juce::String ("Macro ") + juce::String (i + 1));
        k.setEnabled (p != nullptr);
        k.setAlpha (p != nullptr ? 1.0f : 0.45f);
        k.slider.setValue (p != nullptr ? p->getValue() : 0.0, juce::dontSendNotification);
        k.repaint();
    }
    level.slider.setValue (slot.level.load(), juce::dontSendNotification);
    syncScreen();
    repaint();
}

void InstrumentShell::showScreen (bool show)
{
    wantScreen = show;
    if (show && slot.isLoaded() && slot.isWindowOpen())
        slot.closeWindow();   // its interface comes back from its window onto the panel
    refresh();
}

void InstrumentShell::syncScreen()
{
    const bool show = isShowingScreen();
    if (show)
    {
        auto* editor = slot.getEmbeddedEditor();
        if (editor != nullptr && editor != editorShown.getComponent())
        {
            holder.addAndMakeVisible (editor);
            editorShown = editor;
        }
    }
    else
    {
        // Not on the panel: let it go, so its window (or the next plugin) can have it.
        if (editorShown != nullptr && slot.isEmbedded())
            slot.releaseEmbeddedEditor();
        editorShown = nullptr;
    }
    screenView.setVisible (show && editorShown != nullptr);
    for (auto& m : macros)
        m->setVisible (! show);
    level.setVisible (! show);
    autoMap.setVisible (! show);
    screenButton.setEnabled (slot.isLoaded());
    screenButton.lit = show;
    screenButton.setButtonText (show ? "8 knobs" : "Its screen");
    screenButton.setTooltip (show ? "The 8 knobs on the plugin's main settings, and its level, instead of its own interface."
                                  : "The plugin's own interface, right here on the panel (Arturia's whole synth). Press again for the 8 knobs.");
    screenButton.repaint();
    if (show != screenWasShown)
    {
        screenWasShown = show;
        resized();
        if (onLayout)
            onLayout();
    }
    else
    {
        layoutScreen();
    }
}

void InstrumentShell::layoutScreen()
{
    auto* editor = editorShown.getComponent();
    if (editor == nullptr)
        return;
    editorSize = editor->getBounds().withZeroOrigin();
    const auto visible = screenView.getLocalBounds();
    const int w = juce::jmax (visible.getWidth() - (editor->getHeight() > visible.getHeight() ? screenView.getScrollBarThickness() : 0), editor->getWidth());
    const int h = juce::jmax (visible.getHeight() - (editor->getWidth() > visible.getWidth() ? screenView.getScrollBarThickness() : 0), editor->getHeight());
    holder.setSize (w, h);
    // In the middle when it's smaller than the panel.
    editor->setTopLeftPosition ((w - editor->getWidth()) / 2, (h - editor->getHeight()) / 2);
}

void InstrumentShell::timerCallback()
{
    // A plugin can resize its own interface (a bigger zoom, a panel opening).
    if (auto* editor = editorShown.getComponent(); editor != nullptr && editor->getBounds().withZeroOrigin() != editorSize)
        layoutScreen();
    if (signature() != shown)
    {
        refresh();
        if (onChanged)
            onChanged();
        return;
    }
    // The plugin's own window (or a preset) moves the knobs too.
    for (int i = 0; i < (int) macros.size(); ++i)
        if (auto* p = slot.macroParameter (i); p != nullptr && ! dragging[(size_t) i])
            if (std::abs (macros[(size_t) i]->slider.getValue() - p->getValue()) > 1.0e-4)
            {
                macros[(size_t) i]->slider.setValue (p->getValue(), juce::dontSendNotification);
                macros[(size_t) i]->repaint();
            }
}

void InstrumentShell::stepPreset (int delta)
{
    const int n = slot.numPrograms();
    if (n <= 1)
    {
        say (slot.getName() + " keeps its presets in its own window: Open window.");
        return;
    }
    slot.setProgram ((slot.currentProgram() + delta + n) % n);
    refresh();
}

void InstrumentShell::showLoadMenu()
{
    juce::SharedResourcePointer<PluginHost> host;
    auto plugins = host->instruments();
    // Arturia first: that's what this shell is mostly for.
    std::stable_partition (plugins.begin(), plugins.end(), [] (const juce::PluginDescription& d)
                           { return d.manufacturerName.containsIgnoreCase ("arturia"); });
    juce::PopupMenu menu;
    menu.addSectionHeader ("Your instrument plugins");
    if (plugins.empty())
        menu.addItem (-1, host->hasScanned() ? "No instrument plugins found" : "Arturia and the rest show here after a scan", false, false);
    else
        PluginMenu::addPlugins (menu, plugins, 100);
    menu.addSeparator();
    menu.addItem (1, host->hasScanned() ? "Scan for plugins again" : "Scan for plugins (Arturia, Quake...)");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (load),
                        [safe = juce::Component::SafePointer<InstrumentShell> (this), plugins] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1)
                            {
                                PluginMenu::scan ([safe] (const juce::String& t) { if (safe != nullptr) safe->say (t); },
                                                  [safe] { if (safe != nullptr) safe->showLoadMenu(); });
                                return;
                            }
                            const auto d = plugins[(size_t) (result - 100)];
                            safe->say ("Loading " + d.name + "...");
                            auto& s = safe->slot;
                            s.load (d, [safe, name = d.name] (const juce::String& error)
                                    {
                                        if (safe == nullptr)
                                            return;
                                        if (error.isEmpty())
                                        {
                                            safe->slot.bypassed = false;
                                            safe->say (name + " plays the " + safe->trackName + " now: its keys, steps and arp. "
                                                       "The 8 knobs are on its main settings.");
                                        }
                                        else
                                        {
                                            safe->say (name + " didn't load: " + error);
                                        }
                                        safe->refresh();
                                        if (safe->onChanged)
                                            safe->onChanged();
                                    });
                        });
}

void InstrumentShell::showPresetMenu()
{
    const int n = slot.numPrograms();
    if (n <= 1)
    {
        say (slot.getName() + " keeps its presets in its own window: Open window.");
        return;
    }
    juce::PopupMenu menu;
    // In pages of 64, so a big library still fits on the screen.
    for (int start = 0; start < juce::jmin (n, 1024); start += 64)
    {
        juce::PopupMenu page;
        for (int i = start; i < juce::jmin (n, start + 64); ++i)
            page.addItem (1 + i, slot.programName (i).isNotEmpty() ? slot.programName (i) : "Preset " + juce::String (i + 1), true,
                          i == slot.currentProgram());
        if (n <= 64)
            menu = page;
        else
            menu.addSubMenu (juce::String (start + 1) + " - " + juce::String (juce::jmin (n, start + 64)), page, true, nullptr,
                             juce::isPositiveAndBelow (slot.currentProgram() - start, 64));
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presets),
                        [safe = juce::Component::SafePointer<InstrumentShell> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                            {
                                safe->slot.setProgram (result - 1);
                                safe->refresh();
                            }
                        });
}

void InstrumentShell::showMacroMenu (int knob)
{
    auto* instance = slot.getInstance();
    if (instance == nullptr)
        return;
    const auto& list = instance->getParameters();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Knob " + juce::String (knob + 1) + " turns...");
    menu.addItem (1, "Nothing", true, slot.getMacro (knob) < 0);
    juce::PopupMenu page;
    int pageStart = 0;
    auto flush = [&] (int end)
    {
        if (page.getNumItems() > 0)
            menu.addSubMenu (juce::String (pageStart + 1) + " - " + juce::String (end), page);
        page = {};
    };
    for (int i = 0; i < juce::jmin (list.size(), 2048); ++i)
    {
        if (i > 0 && i % 40 == 0)
        {
            flush (i);
            pageStart = i;
        }
        const auto name = list[i]->getName (40);
        page.addItem (100 + i, name.isNotEmpty() ? name : "Setting " + juce::String (i + 1), true, slot.getMacro (knob) == i);
    }
    flush (juce::jmin (list.size(), 2048));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (macros[(size_t) knob].get()),
                        [safe = juce::Component::SafePointer<InstrumentShell> (this), knob] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            safe->slot.setMacro (knob, result >= 100 ? result - 100 : -1);
                            safe->refresh();
                        });
}

void InstrumentShell::paint (juce::Graphics& g)
{
    if (isShowingScreen())
        drawPanel (g, getLocalBounds());   // it covers the arp / step section: the panel under it
    drawSection (g, getLocalBounds(), "Instrument shell", accent);

    if (isShowingScreen())
    {
        // Its own interface, in a dark well, and what's loaded on the button row.
        g.setColour (juce::Colour (0xff0e0c0a));
        g.fillRoundedRectangle (screenView.getBounds().toFloat().expanded (3.0f), 5.0f);
        auto label = topRow.withTrimmedLeft (juce::jmax (0, screenButton.getRight() - topRow.getX() + 14));
        g.setColour (colours::legend);
        g.setFont (vintage::barlow (15.0f));
        juce::String text = slot.getName();
        if (const int n = slot.numPrograms(); n > 1)
            text << "  " << juce::String::fromUTF8 ("\xc2\xb7") << "  " << slot.programName (slot.currentProgram()).trim();
        text << "  " << juce::String::fromUTF8 ("\xc2\xb7") << "  " << (slot.bypassed.load() ? "switched off" : "plays the " + trackName);
        g.drawFittedText (text, label, juce::Justification::centredLeft, 1);
        return;
    }

    // The LCD: what's loaded, by whom, and its preset.
    auto lcd = lcdArea.toFloat();
    vintage::drawLcd (g, lcd, 6.0f);
    auto text = lcd.reduced (12.0f, 6.0f);
    if (slot.isLoaded())
    {
        juce::String maker;
        if (auto* i = slot.getInstance())
            maker = i->getPluginDescription().manufacturerName;
        vintage::lcdText (g, slot.getName().toUpperCase(), text.removeFromTop (text.getHeight() * 0.42f), 30.0f, juce::Justification::centredLeft);
        const int n = slot.numPrograms();
        const auto preset = n > 1 ? juce::String (slot.currentProgram() + 1).paddedLeft ('0', 3) + " " + slot.programName (slot.currentProgram()).trim()
                                  : juce::String ("PRESETS IN ITS WINDOW");
        vintage::lcdText (g, preset.toUpperCase(), text.removeFromTop (text.getHeight() * 0.55f), 22.0f, juce::Justification::centredLeft);
        vintage::lcdText (g, (maker.isNotEmpty() ? maker.toUpperCase() + "  ·  " : juce::String()) + (slot.bypassed.load() ? "SWITCHED OFF" : "PLAYING THE " + trackName.toUpperCase()),
                          text, 17.0f, juce::Justification::centredLeft, slot.bypassed.load() ? vintage::colours::lcdAmber.withAlpha (0.6f) : vintage::colours::lcdGreen);
    }
    else
    {
        vintage::lcdText (g, "EMPTY SLOT", text.removeFromTop (text.getHeight() * 0.42f), 30.0f, juce::Justification::centredLeft);
        vintage::lcdText (g, "LOAD AN ARTURIA SYNTH", text.removeFromTop (text.getHeight() * 0.55f), 22.0f, juce::Justification::centredLeft);
        vintage::lcdText (g, "OR ANY AU / VST3 INSTRUMENT", text, 17.0f, juce::Justification::centredLeft, vintage::colours::lcdAmber.withAlpha (0.6f));
    }

    g.setColour (colours::legendDim);
    g.setFont (vintage::barlow (13.0f));
    g.drawFittedText (slot.isLoaded() ? "It plays this track's keys, steps and arp, and goes through its strip and effects. "
                                        "Click a knob's name to pick what it turns."
                                      : "Loaded, it plays this track's keys, steps and arp, through its strip, effects and stems. "
                                        "The " + trackName + "'s own sound switches back with the Engine buttons.",
                      hintArea, juce::Justification::topLeft, 3);

    g.setColour (colours::line);
    g.drawVerticalLine (macroArea.getX() - 10, (float) macroArea.getY() + 4.0f, (float) macroArea.getBottom() - 4.0f);
}

void InstrumentShell::resized()
{
    auto area = getLocalBounds().reduced (14, 0).withTrimmedTop (26).withTrimmedBottom (10);
    if (isShowingScreen())
    {
        // One row of buttons, then the plugin's own interface as big as the panel allows.
        topRow = area.removeFromTop (30);
        auto row = topRow;
        auto place = [&row] (juce::Component& c, int w) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (6); };
        place (load, 112);
        place (window, 120);
        place (remove, 84);
        place (prev, 34);
        place (presets, 90);
        place (next, 34);
        place (screenButton, 96);
        area.removeFromTop (10);
        screenView.setBounds (area.reduced (3));
        layoutScreen();
        return;
    }
    auto left = area.removeFromLeft (juce::jlimit (300, 440, area.getWidth() * 34 / 100));
    area.removeFromLeft (20);
    macroArea = area;

    lcdArea = left.removeFromTop (juce::jmin (100, left.getHeight() / 2));
    left.removeFromTop (8);
    auto row = left.removeFromTop (30);
    const int w = row.getWidth() / 3;
    load.setBounds (row.removeFromLeft (w).reduced (3, 0));
    window.setBounds (row.removeFromLeft (w).reduced (3, 0));
    remove.setBounds (row.reduced (3, 0));
    left.removeFromTop (6);
    row = left.removeFromTop (30);
    prev.setBounds (row.removeFromLeft (40).reduced (3, 0));
    next.setBounds (row.removeFromLeft (40).reduced (3, 0));
    presets.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (3, 0));
    autoMap.setBounds (row.reduced (3, 0));
    left.removeFromTop (6);
    screenButton.setBounds (left.removeFromTop (30).reduced (3, 0));
    left.removeFromTop (6);
    hintArea = left;

    // The macros in one or two rows, then the level.
    auto knobs = macroArea;
    const int perRow = knobs.getWidth() >= 9 * 78 ? 9 : 5;
    const int rows = perRow == 9 ? 1 : 2;
    const int kw = knobs.getWidth() / perRow, kh = juce::jmin (juce::jmin (170, kw + 40), knobs.getHeight() / rows);
    knobs = knobs.withSizeKeepingCentre (knobs.getWidth(), kh * rows);
    std::vector<juce::Component*> all;
    for (auto& m : macros)
        all.push_back (m.get());
    all.push_back (&level);
    for (int i = 0; i < (int) all.size(); ++i)
        all[(size_t) i]->setBounds (knobs.getX() + (i % perRow) * kw, knobs.getY() + (i / perRow) * kh, kw, kh);
}
