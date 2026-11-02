#include "EffectsPanel.h"

#include "Theme.h"

namespace crate
{

namespace
{
    using S = SoundFx::Settings;

    struct Preset
    {
        const char* name;
        std::vector<std::tuple<const char*, int, double>> values;   // section, param, value
    };

    const std::vector<Preset>& allPresets()
    {
        static const std::vector<Preset> presets {
            { "Lo-fi sampler", { { "Lo-fi", 0, 12 }, { "Lo-fi", 1, 22050 }, { "Filter", 1, 9000 }, { "Drive", 0, 0.15 } } },
            { "Old radio", { { "Filter", 0, 400 }, { "Filter", 1, 3200 }, { "Filter", 2, 0.3 }, { "Drive", 0, 0.3 }, { "Compressor", 0, 0.6 } } },
            { "Crushed", { { "Lo-fi", 0, 4 }, { "Lo-fi", 1, 6000 }, { "Drive", 0, 0.5 } } },
            { "Underwater", { { "Filter", 1, 700 }, { "Filter", 2, 0.5 }, { "Chorus", 0, 0.3 }, { "Chorus", 1, 0.8 }, { "Chorus", 2, 0.6 } } },
            { "Big hall", { { "Reverb", 0, 0.9 }, { "Reverb", 1, 0.45 } } },
            { "Dub echo", { { "Delay", 0, 0.375 }, { "Delay", 1, 0.6 }, { "Delay", 2, 0.45 }, { "Filter", 1, 5000 }, { "Reverb", 0, 0.4 }, { "Reverb", 1, 0.15 } } },
            { "Spacey", { { "Phaser", 0, 0.2 }, { "Phaser", 1, 0.8 }, { "Phaser", 2, 0.5 }, { "Delay", 0, 0.5 }, { "Delay", 1, 0.5 }, { "Delay", 2, 0.3 }, { "Reverb", 0, 0.8 }, { "Reverb", 1, 0.35 } } },
            { "Chipmunk", { { "Pitch and speed", 0, 12 } } },
            { "Slowed down", { { "Pitch and speed", 0, -4 }, { "Pitch and speed", 1, 0.8 }, { "Reverb", 0, 0.6 }, { "Reverb", 1, 0.2 } } },
            { "Punchy", { { "Compressor", 0, 0.7 }, { "Drive", 0, 0.1 } } },
        };
        return presets;
    }
}

EffectsPanel::EffectsPanel()
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (title);

    presets.setTextWhenNothingSelected ("Presets");
    presets.addItemList (presetNames(), 1);
    presets.setTooltip ("Start from a ready-made sound, then adjust it");
    presets.onChange = [this]
    {
        if (presets.getSelectedItemIndex() >= 0)
            loadPreset (presets.getText());
    };
    addAndMakeVisible (presets);

    addSection ("Pitch and speed", {
        { "Pitch", " st", -24, 24, 0, 0.1, 0, [] (S& s, float v) { s.pitch = v; } },
        { "Speed", "x", 0.25, 4, 1, 0.01, 1, [] (S& s, float v) { s.speed = v; } } });
    addSection ("Filter", {
        { "Low cut", " Hz", 20, 2000, 20, 1, 200, [] (S& s, float v) { s.lowCut = v; } },
        { "High cut", " Hz", 200, 20000, 20000, 1, 3000, [] (S& s, float v) { s.highCut = v; } },
        { "Resonance", "", 0, 1, 0, 0.01, 0, [] (S& s, float v) { s.resonance = v; } } });
    addSection ("Compressor", {
        { "Amount", "", 0, 1, 0.5, 0.01, 0, [] (S& s, float v) { s.compress = v; } } });
    addSection ("Drive", {
        { "Amount", "", 0, 1, 0.3, 0.01, 0, [] (S& s, float v) { s.drive = v; } } });
    addSection ("Lo-fi", {
        { "Bits", "", 2, 16, 8, 1, 0, [] (S& s, float v) { s.bits = (int) v; } },
        { "Rate", " Hz", 1000, 44100, 12000, 1, 8000, [] (S& s, float v) { s.sampleRate = v; } } });
    addSection ("Chorus", {
        { "Rate", " Hz", 0.1, 5, 0.8, 0.01, 1, [] (S& s, float v) { s.chorusRate = v; } },
        { "Depth", "", 0, 1, 0.4, 0.01, 0, [] (S& s, float v) { s.chorusDepth = v; } },
        { "Mix", "", 0, 1, 0.5, 0.01, 0, [] (S& s, float v) { s.chorusMix = v; } } });
    addSection ("Phaser", {
        { "Rate", " Hz", 0.05, 5, 0.4, 0.01, 0.7, [] (S& s, float v) { s.phaserRate = v; } },
        { "Depth", "", 0, 1, 0.7, 0.01, 0, [] (S& s, float v) { s.phaserDepth = v; } },
        { "Mix", "", 0, 1, 0.5, 0.01, 0, [] (S& s, float v) { s.phaserMix = v; } } });
    addSection ("Delay", {
        { "Time", " s", 0.02, 1.5, 0.25, 0.005, 0.3, [] (S& s, float v) { s.delayTime = v; } },
        { "Feedback", "", 0, 0.95, 0.4, 0.01, 0, [] (S& s, float v) { s.delayFeedback = v; } },
        { "Mix", "", 0, 1, 0.35, 0.01, 0, [] (S& s, float v) { s.delayMix = v; } } });
    addSection ("Reverb", {
        { "Size", "", 0, 1, 0.6, 0.01, 0, [] (S& s, float v) { s.reverbSize = v; } },
        { "Mix", "", 0, 1, 0.3, 0.01, 0, [] (S& s, float v) { s.reverbMix = v; } } });

    changed();   // dim the switched-off sections
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    previewButton.setClickingTogglesState (true);
    previewButton.setTooltip ("Play with the effects on, before you apply them. Change the controls while it plays.");
    previewButton.onClick = [this] { if (onPreviewChanged) onPreviewChanged (previewButton.getToggleState()); };
    applyButton.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.35f));
    applyButton.setTooltip ("Make the effects part of the sound. You can undo it.");
    applyButton.onClick = [this] { if (onApply) onApply(); };
    resetButton.setTooltip ("Switch every effect off");
    resetButton.onClick = [this] { reset(); };
    for (auto* b : { &previewButton, &applyButton, &resetButton })
        addAndMakeVisible (b);
}

EffectsPanel::~EffectsPanel() = default;

juce::StringArray EffectsPanel::presetNames()
{
    juce::StringArray names;
    for (const auto& p : allPresets())
        names.add (p.name);
    return names;
}

EffectsPanel::Section& EffectsPanel::addSection (const juce::String& name, std::vector<Param> params)
{
    auto* section = sections.add (new Section());
    section->params = std::move (params);
    section->toggle.setButtonText (name);
    section->toggle.setComponentID (name);
    section->toggle.onClick = [this] { changed(); };
    section->addAndMakeVisible (section->toggle);

    for (const auto& p : section->params)
    {
        auto* label = section->labels.add (new juce::Label ({}, p.name));
        label->setColour (juce::Label::textColourId, Theme::textDim);
        label->setFont (juce::FontOptions (13.0f));
        section->addAndMakeVisible (label);

        auto* slider = section->sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
        slider->setRange (p.min, p.max, p.step);
        if (p.skewCentre > p.min && p.skewCentre < p.max)
            slider->setSkewFactorFromMidPoint (p.skewCentre);
        slider->setValue (p.initial, juce::dontSendNotification);
        slider->setDoubleClickReturnValue (true, p.initial);
        slider->setTextValueSuffix (p.suffix);
        slider->setNumDecimalPlacesToDisplay (p.step >= 1 ? 0 : p.step >= 0.1 ? 1 : 2);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
        slider->onValueChange = [this, section]
        {
            if (! loading)
                section->toggle.setToggleState (true, juce::dontSendNotification);
            changed();
        };
        section->addAndMakeVisible (slider);
    }

    content.addAndMakeVisible (section);
    return *section;
}

void EffectsPanel::Section::resized()
{
    auto r = getLocalBounds();
    toggle.setBounds (r.removeFromTop (28));
    r.removeFromTop (2);
    for (int i = 0; i < sliders.size(); ++i)
    {
        auto row = r.removeFromTop (28).withTrimmedLeft (22);
        labels[i]->setBounds (row.removeFromLeft (76));
        sliders[i]->setBounds (row.reduced (0, 3));
    }
}

SoundFx::Settings EffectsPanel::getSettings() const
{
    SoundFx::Settings s;
    for (auto* section : sections)
        if (section->toggle.getToggleState())
            for (int i = 0; i < section->sliders.size(); ++i)
                section->params[(size_t) i].write (s, (float) section->sliders[i]->getValue());
    return s;
}

void EffectsPanel::changed()
{
    // controls of switched-off sections are dimmed
    for (auto* section : sections)
    {
        const float alpha = section->toggle.getToggleState() ? 1.0f : 0.45f;
        for (auto* c : section->sliders)
            c->setAlpha (alpha);
        for (auto* c : section->labels)
            c->setAlpha (alpha);
    }

    if (! loading && onChange)
        onChange();
}

void EffectsPanel::reset()
{
    loading = true;
    for (auto* section : sections)
    {
        section->toggle.setToggleState (false, juce::dontSendNotification);
        for (int i = 0; i < section->sliders.size(); ++i)
            section->sliders[i]->setValue (section->params[(size_t) i].initial, juce::dontSendNotification);
    }
    presets.setSelectedId (0, juce::dontSendNotification);
    loading = false;
    changed();
}

void EffectsPanel::set (const juce::String& sectionName, int param, double value)
{
    for (auto* section : sections)
        if (section->toggle.getComponentID() == sectionName && param < section->sliders.size())
        {
            section->toggle.setToggleState (true, juce::dontSendNotification);
            section->sliders[param]->setValue (value, juce::dontSendNotification);
        }
}

bool EffectsPanel::loadPreset (const juce::String& name)
{
    for (const auto& preset : allPresets())
    {
        if (name != preset.name)
            continue;

        reset();
        loading = true;
        for (const auto& [section, param, value] : preset.values)
            set (section, param, value);
        presets.setText (name, juce::dontSendNotification);
        loading = false;
        changed();
        return true;
    }
    return false;
}

void EffectsPanel::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void EffectsPanel::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (28);
    title.setBounds (top.removeFromLeft (70));
    presets.setBounds (top);
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (64);
    auto row1 = bottom.removeFromTop (30);
    resetButton.setBounds (row1.removeFromRight (64));
    row1.removeFromRight (6);
    previewButton.setBounds (row1);
    bottom.removeFromTop (4);
    applyButton.setBounds (bottom);
    r.removeFromBottom (8);

    viewport.setBounds (r);
    const int width = r.getWidth() - viewport.getScrollBarThickness() - 2;
    int y = 0;
    for (auto* section : sections)
    {
        section->setBounds (0, y, width, section->preferredHeight());
        y += section->preferredHeight() + 6;
    }
    content.setSize (width, y);
}

} // namespace crate
