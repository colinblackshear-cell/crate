#include "SoundsView.h"
#include "RigColours.h"

#include "core/AudioFiles.h"
#include "core/SoundFx.h"

namespace Theme = crate::Theme;

namespace
{
const juce::Colour soundsColour { 0xffa1887f };
}

SoundsView::SoundsView (RigEngine& e, juce::File bankHome)
    : engine (e), bankPtr (std::make_shared<SoundBank> (std::move (bankHome))), bank (*bankPtr)
{
    formats.registerBasicFormats();
    bank.addChangeListener (this);

    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search: \"dusty kick\", \"vocal chop\", \"Fmin\"...", Theme::textDim);
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { list.grabKeyboardFocus(); };

    for (int k = -1; k < SoundBank::numKinds; ++k)
    {
        auto* b = kindButtons.add (new juce::TextButton (k < 0 ? "All" : SoundBank::kindName (k)));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, soundsColour);
        b->setColour (juce::TextButton::textColourOnId, Theme::background);
        b->onClick = [this, k] { setKind (k); };
        addAndMakeVisible (b);
    }
    kindButtons[0]->setToggleState (true, juce::dontSendNotification);

    addAndMakeVisible (length);
    length.addItem ("Loops and one-shots", 1);
    length.addItem ("Loops", 2);
    length.addItem ("One-shots", 3);
    length.setSelectedId (1, juce::dontSendNotification);
    length.onChange = [this] { filter(); };

    for (auto* b : { &nearTempo, &hearButton })
    {
        addAndMakeVisible (b);
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, soundsColour);
        b->setColour (juce::TextButton::textColourOnId, Theme::background);
    }
    nearTempo.setTooltip ("Only loops close to Kaleidobox's tempo (or half or double it), so they stretch without sounding odd.");
    nearTempo.onClick = [this] { filter(); };
    hearButton.setToggleState (true, juce::dontSendNotification);
    hearButton.setTooltip ("Plays each sound when you pick it (also with the arrow keys).");

    addAndMakeVisible (folders);
    folders.setTooltip ("The folders the bank looks in: add your sample folders (Splice is found by itself), or look again.");
    folders.onClick = [this] { showFolders(); };

    addAndMakeVisible (list);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, Theme::background);
    list.setMultipleSelectionEnabled (false);

    for (auto* b : { &play, &stop, &toMpc, &toSampler, &toLoop })
        addAndMakeVisible (b);
    play.onClick = [this] { selectedRowsChanged (list.getSelectedRow()); };
    stop.onClick = [this] { engine.preview (nullptr); };
    toMpc.onClick = [this] { sendToMpc(); };
    toSampler.onClick = [this] { sendToSampler(); };
    toLoop.onClick = [this] { sendToLoop(); };
    toMpc.setTooltip ("Puts the sound on the next empty pad of the MPC (in the bank your keyboard's pads play). Return does it too.");
    toSampler.setTooltip ("Opens the sound in the Sampler, to play from the keys or chop.");
    toLoop.setTooltip ("Puts the sound on the first empty loop layer. A loop with a tempo in its name is stretched to Kaleidobox's tempo.");

    addAndMakeVisible (status);
    status.setColour (juce::Label::textColourId, Theme::textDim);
    status.setJustificationType (juce::Justification::centredRight);

    filter();
    bank.rescan();
    startTimerHz (4);
}

SoundsView::~SoundsView()
{
    bank.removeChangeListener (this);
}

void SoundsView::setKind (int k)
{
    kind = k;
    for (int i = 0; i < kindButtons.size(); ++i)
        kindButtons[i]->setToggleState (i == k + 1, juce::dontSendNotification);
    filter();
}

void SoundsView::filter()
{
    SoundBank::Query q;
    q.text = search.getText();
    q.kind = kind;
    q.length = length.getSelectedId() == 2 ? SoundBank::Length::loops : length.getSelectedId() == 3 ? SoundBank::Length::oneShots : SoundBank::Length::any;
    q.nearBpm = nearTempo.getToggleState() ? engine.getTransport().getBpm() : 0.0;
    shown = bank.search (q);
    list.updateContent();
    list.repaint();
    timerCallback();
}

void SoundsView::timerCallback()
{
    juce::String text = juce::String (bank.size()) + " sounds";
    if (bank.isScanning())
        text = "Looking through your folders... " + juce::String (bank.getFilesFound()) + " found";
    else if (bank.getFolders().isEmpty())
        text = "No folders yet: press Folders... to add yours";
    if ((int) shown.size() < bank.size() && ! bank.isScanning())
        text = juce::String ((int) shown.size()) + (shown.size() >= 2000 ? "+" : "") + " of " + text;
    status.setText (text, juce::dontSendNotification);
}

void SoundsView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    const auto& s = shown[(size_t) row];
    g.fillAll (selected ? soundsColour.withAlpha (0.35f) : (row % 2 == 0 ? Theme::background : Theme::panel.darker (0.2f)));
    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (s.where, r.removeFromRight (juce::jmin (420, width / 3)), juce::Justification::centredRight, true);
    juce::String details = juce::String (s.seconds, s.seconds < 10 ? 1 : 0) + " s";
    if (s.bpm > 0)
        details = juce::String (juce::roundToInt (s.bpm)) + " BPM  " + details;
    if (s.key.isNotEmpty())
        details = s.key + "  " + details;
    g.drawText (details, r.removeFromRight (180), juce::Justification::centredRight);
    g.setColour (soundsColour);
    g.drawText (SoundBank::kindName (s.kind) + (s.loop ? " loop" : ""), r.removeFromLeft (110), juce::Justification::centredLeft);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (s.name, r, juce::Justification::centredLeft, true);
}

std::shared_ptr<const juce::AudioBuffer<float>> SoundsView::loadSelected (const SoundBank::Sound*& sound)
{
    sound = nullptr;
    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return nullptr;
    sound = &shown[(size_t) row];
    juce::String error;
    auto audio = crate::AudioFiles::load (sound->file, formats, error);
    if (audio == nullptr)
        say ("Couldn't open " + sound->file.getFileName() + (error.isNotEmpty() ? ": " + error : juce::String()));
    return audio;
}

void SoundsView::selectedRowsChanged (int)
{
    if (! hearButton.getToggleState() && ! play.isMouseButtonDown())
        return;
    const SoundBank::Sound* sound = nullptr;
    if (auto audio = loadSelected (sound))
        engine.preview (std::move (audio));
}

void SoundsView::selectRow (int row)
{
    list.selectRow (row);
}

void SoundsView::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    sendToMpc();
}

bool SoundsView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        sendToMpc();
        return true;
    }
    return false;
}

void SoundsView::sendToMpc()
{
    const SoundBank::Sound* sound = nullptr;
    auto audio = loadSelected (sound);
    if (audio == nullptr)
        return;
    const double beats = sound->loop && sound->bpm > 0 ? sound->seconds * sound->bpm / 60.0 : 0.0;
    const auto pad = engine.putOnMpcPad (juce::AudioBuffer<float> (*audio), sound->name, beats > 0 ? std::round (beats) : 0.0);
    say (pad.isEmpty() ? "Every MPC pad in this kit has a sound: clear one first." : sound->name + " is on MPC pad " + pad + ".");
}

void SoundsView::sendToSampler()
{
    const SoundBank::Sound* sound = nullptr;
    if (auto audio = loadSelected (sound))
    {
        engine.putInSampler (juce::AudioBuffer<float> (*audio), sound->name);
        say (sound->name + " is in the Sampler: play it from the keys, or chop it in its tab.");
    }
}

void SoundsView::sendToLoop()
{
    const SoundBank::Sound* sound = nullptr;
    auto audio = loadSelected (sound);
    if (audio == nullptr)
        return;
    auto& looper = engine.getLooper();
    int layer = -1;
    for (int i = 0; i < RigLooper::kLayers && layer < 0; ++i)
        if (looper.getState (i) == RigLooper::State::empty)
            layer = i;
    if (layer < 0)
    {
        say ("Every loop layer is full: clear one first.");
        return;
    }
    auto& transport = engine.getTransport();
    const int bpb = transport.getBeatsPerBar();
    // Its length in beats: from the tempo in its name, else a guess at the Rig's tempo.
    double beats = sound->bpm > 0 ? std::round (sound->seconds * sound->bpm / 60.0) : crate::SoundFx::guessBeats (audio->getNumSamples(), transport.getBpm());
    if (beats <= 0)
        beats = bpb;
    auto fitted = crate::SoundFx::fitToTempo (audio, beats, transport.getBpm());
    looper.makeRoom (layer);
    looper.setLayerAudio (layer, juce::AudioBuffer<float> (*fitted), beats);
    say (sound->name + " is on loop " + juce::String (layer + 1) + " (" + juce::String (beats / bpb, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
         + " bars), in time at " + juce::String (transport.getBpm(), 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM.");
}

void SoundsView::showFolders()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Add a folder...");
    menu.addItem (2, "Look through the folders again", ! bank.isScanning());
    const auto list = bank.getFolders();
    if (! list.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Looking in (click one to stop using it)");
        for (int i = 0; i < list.size(); ++i)
            menu.addItem (100 + i, list[i].getFullPathName());
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (folders),
                        [this, list, safe = juce::Component::SafePointer<SoundsView> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1)
                            {
                                auto chooser = std::make_shared<juce::FileChooser> ("Add a sample folder",
                                                                                    juce::File::getSpecialLocation (juce::File::userHomeDirectory));
                                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                                      [this, chooser] (const juce::FileChooser& fc)
                                                      {
                                                          if (fc.getResult().isDirectory())
                                                              bank.addFolder (fc.getResult());
                                                      });
                            }
                            else if (result == 2)
                                bank.rescan();
                            else if (juce::isPositiveAndBelow (result - 100, list.size()))
                                bank.removeFolder (list[result - 100]);
                        });
}

void SoundsView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    if (bank.size() == 0 && ! bank.isScanning())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("No sounds yet. Press Folders... and add your sample folders (your Splice folder is found by itself "
                          "if it's in your home folder, Music, Documents or Desktop).",
                          list.getBounds().reduced (40), juce::Justification::centred, 3);
    }
}

void SoundsView::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (30);
    folders.setBounds (top.removeFromRight (100));
    top.removeFromRight (6);
    hearButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (6);
    nearTempo.setBounds (top.removeFromRight (100));
    top.removeFromRight (6);
    length.setBounds (top.removeFromRight (160));
    top.removeFromRight (10);
    search.setBounds (top);

    area.removeFromTop (8);
    auto kinds = area.removeFromTop (28);
    const int w = (kinds.getWidth() - 3 * (kindButtons.size() - 1)) / kindButtons.size();
    for (auto* b : kindButtons)
    {
        b->setBounds (kinds.removeFromLeft (w));
        kinds.removeFromLeft (3);
    }

    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (30);
    for (auto* b : { &play, &stop })
    {
        b->setBounds (bottom.removeFromLeft (70));
        bottom.removeFromLeft (6);
    }
    bottom.removeFromLeft (10);
    for (auto* b : { &toMpc, &toSampler, &toLoop })
    {
        b->setBounds (bottom.removeFromLeft (130));
        bottom.removeFromLeft (6);
    }
    status.setBounds (bottom);
    area.removeFromBottom (8);
    list.setBounds (area);
}
