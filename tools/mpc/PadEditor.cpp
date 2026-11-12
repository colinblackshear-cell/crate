#include "PadEditor.h"

#include "SampleTools.h"
#include "core/SoundFx.h"
#include "core/ui/Theme.h"

namespace SoundFx = crate::SoundFx;
namespace Theme = crate::Theme;

PadEditor::PadEditor (Project& p) : project (p)
{
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    info.setColour (juce::Label::textColourId, Theme::textDim);

    for (auto* c : std::initializer_list<juce::Component*> { &title, &info, &nameLabel, &name, &volumeLabel, &volume,
                                                             &panLabel, &pan, &tuneLabel, &tune, &modeLabel, &mode, &groupLabel, &group,
                                                             &timingLabel, &timing, &timingInfo, &mono, &reverse, &normalize,
                                                             &load, &clear, &chop, &editSound })
        addAndMakeVisible (c);

    timingInfo.setColour (juce::Label::textColourId, Theme::textDim);
    timingInfo.setJustificationType (juce::Justification::topLeft);
    timingInfo.setFont (juce::FontOptions (12.0f));

    // Sound names become 8.3 file names on the card, so keep them short.
    name.setInputRestrictions (16);
    name.setJustification (juce::Justification::centredLeft);
    name.onTextChange = [this] { pad().name = name.getText(); edited(); };

    for (auto* s : { &volume, &pan, &tune })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    }

    volume.setRange (0, 100, 1);
    volume.onValueChange = [this] { pad().volume = (int) volume.getValue(); edited(); };

    pan.setRange (0, 100, 1);
    pan.textFromValueFunction = [] (double v)
    {
        const int offset = juce::roundToInt (v) - 50;
        return offset == 0 ? juce::String ("C") : (offset < 0 ? "L" : "R") + juce::String (std::abs (offset));
    };
    pan.onValueChange = [this] { pad().pan = (int) pan.getValue(); edited(); };

    // The MPC tunes in tenths of a semitone, +-12 semitones.
    tune.setRange (-12.0, 12.0, 0.1);
    tune.setTextValueSuffix (" st");
    tune.setDoubleClickReturnValue (true, 0.0);
    tune.onValueChange = [this] { pad().tune = juce::roundToInt (tune.getValue() * 10.0); edited(); };

    mode.addItem ("One shot (plays to the end)", 1);
    mode.addItem ("Mono (a new hit cuts the last)", 2);
    mode.addItem ("Note off (stops when released)", 3);
    mode.onChange = [this] { pad().mode = (PadMode) (mode.getSelectedId() - 1); edited(); };

    group.addItem ("Nothing (plays over other pads)", 1);
    for (int g = 1; g <= 8; ++g)
        group.addItem ("Other pads in mute group " + juce::String (g), g + 1);
    group.setTooltip ("Pads in the same mute group cut each other off, like an open and closed hi-hat. Saved to the MPC too.");
    group.onChange = [this] { pad().muteGroup = group.getSelectedId() - 1; edited(); };

    // How long the pad lasts when the kit keeps everything in time.
    timing.addItem ("Auto length", kTimingAuto);
    timing.addItem ("Play normal (never stretch)", kTimingNormal);
    timing.addSeparator();
    for (int i = 0; i < (int) kBeatChoices.size(); ++i)
        timing.addItem (beatsText (kBeatChoices[(size_t) i]), kTimingBeats + i);
    timing.onChange = [this]
    {
        auto& current = pad();
        const int id = timing.getSelectedId();
        current.playNormal = id == kTimingNormal;
        current.beats = id >= kTimingBeats ? kBeatChoices[(size_t) (id - kTimingBeats)] : 0.0;
        edited();
    };

    mono.onClick = [this] { pad().mono = mono.getToggleState(); edited(); };
    reverse.onClick = [this] { pad().reverse = reverse.getToggleState(); edited(); };
    normalize.onClick = [this] { pad().normalize = normalize.getToggleState(); edited(); };

    load.onClick = [this] { if (onLoadFile) onLoadFile(); };
    clear.onClick = [this] { if (onClear) onClear(); };

    editSound.setTooltip ("Trim the sound, fade it, and put effects on it (double-click a pad too)");
    editSound.setColour (juce::TextButton::buttonColourId, Theme::accent);
    editSound.setColour (juce::TextButton::textColourOffId, Theme::background);
    editSound.onClick = [this] { if (onEditSound) onEditSound(); };

    chop.setTooltip ("Cut this sound into pieces, one per pad, starting here");
    chop.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem ("At each hit", [this] { if (onChop) onChop (0); });
        menu.addSeparator();
        for (int slices : { 2, 4, 8, 16 })
            menu.addItem (juce::String (slices) + " equal slices", [this, slices] { if (onChop) onChop (slices); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chop));
    };

    // Sound + FX.
    for (auto* b : { &padPageButton, &soundPageButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (31);
        b->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        b->setColour (juce::TextButton::textColourOnId, Theme::background);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    padPageButton.setToggleState (true, juce::dontSendNotification);
    padPageButton.onClick = [this] { showSoundPage (false); };
    soundPageButton.onClick = [this] { showSoundPage (true); };
    soundPageButton.setTooltip ("The pad's filter, attack and decay, and how much goes to the kit's delay and reverb. "
                                "Turn them while the pad plays.");

    for (auto* c : soundPageControls())
        addChildComponent (c);
    soundInfo.setColour (juce::Label::textColourId, Theme::textDim);
    soundInfo.setFont (juce::FontOptions (12.0f));
    soundInfo.setJustificationType (juce::Justification::topLeft);
    soundInfo.setText ("Shapes the pad as it plays; the sound itself stays as it is. Click the pad (or Play) to hear it.",
                       juce::dontSendNotification);
    kitFxTitle.setFont (juce::FontOptions (14.0f, juce::Font::bold));

    for (auto* sl : { &cutoff, &resonance, &attack, &decay, &delaySend, &reverbSend, &feedback, &room })
    {
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    }
    cutoff.setRange (20.0, 20000.0, 1.0);
    cutoff.setSkewFactorFromMidPoint (1000.0);
    cutoff.setDoubleClickReturnValue (true, 20000.0);
    cutoff.textFromValueFunction = [] (double v)
    {
        return v >= 19500.0 ? juce::String ("Open") : v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    };
    cutoff.valueFromTextFunction = [] (const juce::String& t)
    {
        return t.containsIgnoreCase ("open") ? 20000.0 : t.getDoubleValue() * (t.containsIgnoreCase ("k") ? 1000.0 : 1.0);
    };
    cutoff.onValueChange = [this] { pad().cutoff = (float) cutoff.getValue(); edited(); };

    resonance.setRange (0.0, 100.0, 1.0);
    resonance.setTextValueSuffix (" %");
    resonance.onValueChange = [this] { pad().resonance = (float) resonance.getValue() / 100.0f; edited(); };

    attack.setRange (0.0, 2000.0, 1.0);
    attack.setSkewFactorFromMidPoint (200.0);
    attack.setTextValueSuffix (" ms");
    attack.onValueChange = [this] { pad().attack = (int) attack.getValue(); edited(); };

    decay.setRange (0.0, 5000.0, 1.0);
    decay.setSkewFactorFromMidPoint (600.0);
    decay.setDoubleClickReturnValue (true, 0.0);
    decay.textFromValueFunction = [] (double v) { return v < 1.0 ? juce::String ("Whole") : juce::String (juce::roundToInt (v)) + " ms"; };
    decay.valueFromTextFunction = [] (const juce::String& t) { return t.containsIgnoreCase ("whole") ? 0.0 : t.getDoubleValue(); };
    decay.setTooltip ("How long the pad rings before it dies away (Whole: the whole sound). Short decays make tight hits.");
    decay.onValueChange = [this] { pad().decay = (int) decay.getValue(); edited(); };

    for (auto* sl : { &delaySend, &reverbSend })
    {
        sl->setRange (0.0, 100.0, 1.0);
        sl->setTextValueSuffix (" %");
    }
    delaySend.setTooltip ("How much of the pad goes into the kit's delay");
    reverbSend.setTooltip ("How much of the pad goes into the kit's reverb");
    delaySend.onValueChange = [this] { pad().delaySend = (int) delaySend.getValue(); edited(); };
    reverbSend.onValueChange = [this] { pad().reverbSend = (int) reverbSend.getValue(); edited(); };

    const char* delayNames[] = { "1/16", "1/8 triplet", "1/8", "Dotted 1/8", "1/4", "Dotted 1/4", "1/2" };
    for (int i = 0; i < (int) kDelayChoices.size(); ++i)
        delayTime.addItem (delayNames[i], i + 1);
    delayTime.setTooltip ("The echo's time, in time with the sequence");
    delayTime.onChange = [this]
    {
        if (const int i = delayTime.getSelectedId() - 1; juce::isPositiveAndBelow (i, (int) kDelayChoices.size()))
        {
            project.currentBank().delayBeats = kDelayChoices[(size_t) i];
            edited();
        }
    };
    feedback.setRange (0.0, 90.0, 1.0);
    feedback.setTextValueSuffix (" %");
    feedback.onValueChange = [this] { project.currentBank().delayFeedback = (int) feedback.getValue(); edited(); };
    room.setRange (0.0, 100.0, 1.0);
    room.setTextValueSuffix (" %");
    room.onValueChange = [this] { project.currentBank().reverbSize = (int) room.getValue(); edited(); };

    playButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    playButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    playButton.onClick = [this] { if (onPlay) onPlay(); };
    resetSound.setTooltip ("Back to the pad's plain sound: filter open, no attack or decay, no delay or reverb");
    resetSound.onClick = [this]
    {
        auto& p = pad();
        p.cutoff = 20000.0f;
        p.resonance = 0.0f;
        p.attack = p.decay = p.delaySend = p.reverbSend = 0;
        edited();
        refresh();
    };

    refresh();
    for (auto* sl : { &cutoff, &resonance, &attack, &decay, &delaySend, &reverbSend, &feedback, &room })
        sl->updateText();
}

std::vector<juce::Component*> PadEditor::padPage()
{
    return { &nameLabel, &name, &volumeLabel, &volume, &panLabel, &pan, &tuneLabel, &tune, &modeLabel, &mode, &groupLabel, &group,
             &timingLabel, &timing, &timingInfo, &mono, &reverse, &normalize, &load, &clear, &chop, &editSound };
}

std::vector<juce::Component*> PadEditor::soundPageControls()
{
    return { &cutoffLabel, &cutoff, &resonanceLabel, &resonance, &attackLabel, &attack, &decayLabel, &decay, &delayLabel, &delaySend,
             &reverbLabel, &reverbSend, &soundInfo, &kitFxTitle, &delayTimeLabel, &delayTime, &feedbackLabel, &feedback, &roomLabel, &room,
             &playButton, &resetSound };
}

void PadEditor::showSoundPage (bool sound)
{
    soundPage = sound;
    padPageButton.setToggleState (! sound, juce::dontSendNotification);
    soundPageButton.setToggleState (sound, juce::dontSendNotification);
    for (auto* c : padPage())
        c->setVisible (! sound);
    for (auto* c : soundPageControls())
        c->setVisible (sound);
    resized();
}

juce::String PadEditor::beatsText (double beats)
{
    if (beats < 1)
        return "1/" + juce::String (juce::roundToInt (1.0 / beats)) + " beat";
    const int b = juce::roundToInt (beats);
    if (b >= 4 && b % 4 == 0)
        return juce::String (b / 4) + (b == 4 ? " bar" : " bars") + " (" + juce::String (b) + " beats)";
    return juce::String (b) + (b == 1 ? " beat" : " beats");
}

void PadEditor::setPad (int index)
{
    padIndex = index;
    refresh();
}

void PadEditor::edited()
{
    project.changed();
}

void PadEditor::refresh()
{
    auto& p = pad();
    const bool has = ! p.isEmpty();

    title.setText ("Pad " + padLabel (padIndex), juce::dontSendNotification);

    if (has)
    {
        const double seconds = p.audio->getNumSamples() / kMpcSampleRate;
        info.setText (juce::String (seconds, 2) + " s, " + (p.channelsOnMpc() == 2 ? "stereo" : "mono") + ", "
                          + SampleTools::formatMegabytes (project.currentBank().memoryBytes (padIndex))
                          + " of MPC memory. Saved as " + juce::String (mpc::toMpcName (p.name.toStdString()))
                          + ".SND, MIDI note " + juce::String (noteForPad (padIndex)) + ".",
                      juce::dontSendNotification);
    }
    else
    {
        info.setText ("Empty. Drop a sound on the pad, or load one. MIDI note " + juce::String (noteForPad (padIndex)) + ".",
                      juce::dontSendNotification);
    }

    name.setText (p.name, juce::dontSendNotification);
    volume.setValue (p.volume, juce::dontSendNotification);
    pan.setValue (p.pan, juce::dontSendNotification);
    tune.setValue (p.tune / 10.0, juce::dontSendNotification);
    mode.setSelectedId ((int) p.mode + 1, juce::dontSendNotification);
    group.setSelectedId (juce::jlimit (0, 8, p.muteGroup) + 1, juce::dontSendNotification);
    mono.setToggleState (p.mono || (has && p.audio->getNumChannels() == 1), juce::dontSendNotification);
    mono.setEnabled (has && p.audio->getNumChannels() > 1);
    timing.setSelectedId (p.playNormal ? kTimingNormal : kTimingAuto, juce::dontSendNotification);
    for (int i = 0; i < (int) kBeatChoices.size(); ++i)
        if (! p.playNormal && p.beats > 0 && std::abs (p.beats - kBeatChoices[(size_t) i]) < 0.001)
            timing.setSelectedId (kTimingBeats + i, juce::dontSendNotification);

    const auto& bank = project.currentBank();
    juce::String timingText;
    if (has)
    {
        const auto beats = bank.beatsFor (padIndex);
        const auto guess = SoundFx::guessBeats (p.audio->getNumSamples(), bank.bpm);
        if (! bank.keepInTime)
            timingText = "Plays as recorded. Turn on Keep in time to fit pads to " + juce::String (bank.bpm, 1) + " BPM.";
        else if (beats <= 0)
            timingText = p.playNormal ? "Plays as recorded." : "Short hit, plays as recorded.";
        else
            timingText = "Stretched to " + beatsText (beats).toLowerCase() + " at " + juce::String (bank.bpm, 1)
                         + " BPM (it was " + juce::String (SoundFx::tempoOf (p.audio->getNumSamples(), beats), 1) + " BPM).";
        if (p.beats <= 0 && ! p.playNormal && guess > 0)
            timing.changeItemText (kTimingAuto, "Auto length (" + beatsText (guess).toLowerCase() + ")");
        else
            timing.changeItemText (kTimingAuto, "Auto length");
    }
    timingInfo.setText (timingText, juce::dontSendNotification);

    reverse.setToggleState (p.reverse, juce::dontSendNotification);
    normalize.setToggleState (p.normalize, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &name, &volume, &pan, &tune, &mode, &group, &timing, &reverse, &normalize, &clear, &chop, &editSound })
        c->setEnabled (has);

    cutoff.setValue (p.cutoff, juce::dontSendNotification);
    resonance.setValue (p.resonance * 100.0, juce::dontSendNotification);
    attack.setValue (p.attack, juce::dontSendNotification);
    decay.setValue (p.decay, juce::dontSendNotification);
    delaySend.setValue (p.delaySend, juce::dontSendNotification);
    reverbSend.setValue (p.reverbSend, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &cutoff, &resonance, &attack, &decay, &delaySend, &reverbSend, &playButton, &resetSound })
        c->setEnabled (has);
    delayTime.setSelectedId (0, juce::dontSendNotification);
    for (int i = 0; i < (int) kDelayChoices.size(); ++i)
        if (std::abs (bank.delayBeats - kDelayChoices[(size_t) i]) < 0.001)
            delayTime.setSelectedId (i + 1, juce::dontSendNotification);
    feedback.setValue (bank.delayFeedback, juce::dontSendNotification);
    room.setValue (bank.reverbSize, juce::dontSendNotification);
}

void PadEditor::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void PadEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    {
        auto top = r.removeFromTop (30);
        soundPageButton.setBounds (top.removeFromRight (96));
        top.removeFromRight (4);
        padPageButton.setBounds (top.removeFromRight (56));
        title.setBounds (top);
    }
    info.setBounds (r.removeFromTop (44));
    r.removeFromTop (8);

    if (soundPage)
    {
        auto line = [&r] (juce::Label& label, juce::Component& control)
        {
            auto l = r.removeFromTop (28);
            label.setBounds (l.removeFromLeft (64));
            control.setBounds (l);
            r.removeFromTop (6);
        };
        line (cutoffLabel, cutoff);
        line (resonanceLabel, resonance);
        line (attackLabel, attack);
        line (decayLabel, decay);
        line (delayLabel, delaySend);
        line (reverbLabel, reverbSend);
        auto buttons = r.removeFromTop (30);
        playButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft (8);
        resetSound.setBounds (buttons);
        r.removeFromTop (4);
        soundInfo.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        kitFxTitle.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        line (delayTimeLabel, delayTime);
        line (feedbackLabel, feedback);
        line (roomLabel, room);
        return;
    }

    auto row = [&r] (juce::Label& label, juce::Component& control)
    {
        auto line = r.removeFromTop (28);
        label.setBounds (line.removeFromLeft (64));
        control.setBounds (line);
        r.removeFromTop (8);
    };

    row (nameLabel, name);
    row (volumeLabel, volume);
    row (panLabel, pan);
    row (tuneLabel, tune);
    row (modeLabel, mode);
    row (groupLabel, group);
    row (timingLabel, timing);
    timingInfo.setBounds (r.removeFromTop (34).withTrimmedLeft (64));
    r.removeFromTop (4);

    for (auto* t : { &mono, &reverse, &normalize })
        t->setBounds (r.removeFromTop (26));

    r.removeFromTop (12);
    auto buttons = r.removeFromTop (30);
    load.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
    buttons.removeFromLeft (8);
    clear.setBounds (buttons);
    r.removeFromTop (8);
    editSound.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    chop.setBounds (r.removeFromTop (30));
}
