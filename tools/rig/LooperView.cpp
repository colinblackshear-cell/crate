#include "LooperView.h"

#include "core/AudioFiles.h"
#include "core/SoundFx.h"

namespace Theme = crate::Theme;

namespace
{
juce::Colour layerColour (int i)
{
    static const juce::Colour colours[] = { juce::Colour (0xffff8a3d), juce::Colour (0xffb48cff), juce::Colour (0xff4fc3f7),
                                            juce::Colour (0xfff06292), juce::Colour (0xffffd54f), juce::Colour (0xff8fd3c8),
                                            juce::Colour (0xff81c784), juce::Colour (0xffe0e0e0) };
    return colours[i % 8];
}

juce::String barsText (double beats, int beatsPerBar)
{
    const double bars = beats / juce::jmax (1, beatsPerBar);
    if (std::abs (bars - std::round (bars)) < 0.01)
        return juce::String ((int) std::round (bars)) + (std::round (bars) == 1.0 ? " bar" : " bars");
    return juce::String (bars, 1) + " bars";
}
}

//==============================================================================
// The loop drawn as a waveform, with the playhead.
class LoopWave : public juce::Component
{
public:
    LoopWave (RigEngine& e, int i) : engine (e), index (i) {}

    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }

    void tick()
    {
        auto& layer = engine.getLooper().getLayer (index);
        const int version = layer.version.load();
        const auto state = engine.getLooper().getState (index);
        if (version != lastVersion && juce::Time::getMillisecondCounter() - lastScan > 100)
        {
            lastVersion = version;
            lastScan = juce::Time::getMillisecondCounter();
            scan();
        }
        if (state != lastState)
        {
            lastState = state;
            scan();
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Theme::background);
        g.fillRoundedRectangle (area, 4.0f);

        const auto& looper = engine.getLooper();
        const auto& layer = looper.getLayer (index);
        const auto state = looper.getState (index);
        const auto colour = layerColour (index);
        const int bpb = engine.getTransport().getBeatsPerBar();

        if (state == RigLooper::State::empty)
        {
            g.setColour (Theme::textDim.withAlpha (0.6f));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("Empty: press Rec to record " + RigLooper::sourceName (layer.source.load()).toLowerCase(), area.reduced (10, 0),
                        juce::Justification::centredLeft);
            return;
        }
        if (state == RigLooper::State::armed)
        {
            g.setColour (Theme::record.withAlpha (0.8f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (engine.getTransport().isPlaying() ? "Starts at the next bar..." : "Starts when the clock starts...",
                        area.reduced (10, 0), juce::Justification::centredLeft);
            return;
        }

        // Peaks.
        const float mid = area.getCentreY();
        const float half = area.getHeight() * 0.45f;
        g.setColour (colour.withAlpha (state == RigLooper::State::recording ? 0.5f : 0.75f));
        const int w = (int) area.getWidth();
        // While it records with a set length, it fills in from the left; with no length yet,
        // the whole width is what's recorded so far.
        const double filled = state == RigLooper::State::recording && layer.position.load() > 0
                                  ? juce::jlimit (0.0, 1.0, layer.position.load()) : 1.0;
        const int drawn = (int) (w * filled);
        for (int x = 0; x < drawn && ! peaks.empty(); ++x)
        {
            const double t = (double) x / juce::jmax (1, drawn - 1);
            const auto p = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (t * (peaks.size() - 1)))];
            g.drawVerticalLine (x, mid - juce::jmax (0.5f, p * half), mid + juce::jmax (0.5f, p * half) + 1);
        }

        // Bar lines.
        const double beats = layer.lengthBeats.load();
        if (state != RigLooper::State::recording && beats > 0)
        {
            g.setColour (Theme::text.withAlpha (0.12f));
            for (double b = bpb; b < beats; b += bpb)
                g.drawVerticalLine ((int) (b / beats * area.getWidth()), area.getY(), area.getBottom());
        }

        // Playhead.
        if (engine.getTransport().isPlaying() && state != RigLooper::State::recording)
        {
            const float x = (float) (layer.position.load() * area.getWidth());
            g.setColour (state == RigLooper::State::overdubbing ? Theme::record : Theme::text);
            g.fillRect (x - 1.0f, area.getY(), 2.0f, area.getHeight());
        }

        g.setColour (Theme::text.withAlpha (0.85f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        const juce::String label = state == RigLooper::State::recording ? "Recording  " + barsText (beats, bpb)
                                 : state == RigLooper::State::overdubbing ? "Overdub  " + barsText (beats, bpb)
                                                                          : barsText (beats, bpb);
        g.drawText (label, area.reduced (8, 3), juce::Justification::topRight);
    }

private:
    void scan() { peaks = engine.getLooper().readPeaks (index, 400); }

    RigEngine& engine;
    const int index;
    std::vector<float> peaks;
    int lastVersion = -1;
    juce::uint32 lastScan = 0;
    RigLooper::State lastState = RigLooper::State::empty;
};

//==============================================================================
class LooperView::Row : public juce::Component
{
public:
    Row (LooperView& o, RigEngine& e, int i) : owner (o), engine (e), index (i), wave (e, i)
    {
        auto& layer = engine.getLooper().getLayer (index);
        for (auto* c : std::initializer_list<juce::Component*> { &record, &source, &bars, &wave, &mute, &level, &fx, &edit, &undo, &clear, &more })
            addAndMakeVisible (c);

        record.setColour (juce::TextButton::buttonOnColourId, Theme::record);
        record.onClick = [this] { engine.getLooper().pressRecord (index); };
        record.setTooltip ("Record a loop: it starts at the next bar, and stops at the end of the bar where you press again "
                           "(or by itself after the bars you picked). On a playing loop, it overdubs. Keys 1-8 do the same.");

        for (int s = 0; s < RigLooper::numSources; ++s)
            source.addItem (RigLooper::sourceName (s), s + 1);
        source.onChange = [this, &layer] { layer.source = source.getSelectedId() - 1; };
        source.setTooltip ("What this layer records: your mic or instrument, one tool, or the whole Rig mix.");

        for (int b : RigLooper::barChoices())
            bars.addItem (b == 0 ? "Free" : juce::String (b) + (b == 1 ? " bar" : " bars"), b + 1);
        bars.onChange = [this, &layer] { layer.bars = bars.getSelectedId() - 1; };
        bars.setTooltip ("Free: record until you press again. Or a length, and it stops by itself.");

        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, Theme::record);
        mute.onClick = [this, &layer] { layer.mute = mute.getToggleState(); };

        level.setRange (0.0, 1.0);
        level.setDoubleClickReturnValue (true, 0.8);
        level.setColour (juce::Slider::thumbColourId, layerColour (index));
        level.setColour (juce::Slider::trackColourId, layerColour (index).withAlpha (0.6f));
        level.onValueChange = [this, &layer] { layer.level = (float) level.getValue(); };

        fx.setTooltip ("This loop's own effects: filter, drive, crush, delay, reverb and beat repeat. Lit when any are on.");
        fx.setColour (juce::TextButton::buttonOnColourId, layerColour (index).withAlpha (0.8f));
        fx.onClick = [this] { owner.showFx (index); };
        edit.setTooltip ("Choose where this loop starts and ends (snapped to bars or beats, or free). Or double-click the loop.");
        edit.onClick = [this] { owner.editLoop (index); };
        wave.onDoubleClick = [this] { owner.editLoop (index); };
        undo.setTooltip ("Takes back the last overdub on this layer.");
        undo.onClick = [this] { engine.getLooper().undo (index); };
        clear.setTooltip ("Empties this layer.");
        clear.onClick = [this]
        {
            engine.getLooper().clear (index);
            juce::Timer::callAfterDelay (100, [safe = juce::Component::SafePointer<Row> (this)]
                                         { if (safe != nullptr) safe->engine.getLooper().makeRoom (safe->index); });
        };
        more.setTooltip ("Import a loop or a song onto this layer, save the loop as a WAV, or put Record and Mute on your keyboard.");
        more.onClick = [this] { showMenu(); };
        refresh();
    }

    void refresh()
    {
        const auto& layer = engine.getLooper().getLayer (index);
        source.setSelectedId (layer.source.load() + 1, juce::dontSendNotification);
        bars.setSelectedId (layer.bars.load() + 1, juce::dontSendNotification);
        mute.setToggleState (layer.mute.load(), juce::dontSendNotification);
        level.setValue (layer.level.load(), juce::dontSendNotification);
    }

    void tick()
    {
        const auto state = engine.getLooper().getState (index);
        switch (state)
        {
            case RigLooper::State::empty:       record.setButtonText ("Rec"); break;
            case RigLooper::State::armed:       record.setButtonText ("Armed"); break;
            case RigLooper::State::recording:   record.setButtonText ("Stop"); break;
            case RigLooper::State::playing:     record.setButtonText ("Dub"); break;
            case RigLooper::State::overdubbing: record.setButtonText ("Stop dub"); break;
        }
        const bool hot = state == RigLooper::State::armed || state == RigLooper::State::recording || state == RigLooper::State::overdubbing;
        record.setToggleState (hot, juce::dontSendNotification);
        const bool hasLoop = state == RigLooper::State::playing || state == RigLooper::State::overdubbing;
        undo.setEnabled (hasLoop);
        edit.setEnabled (state == RigLooper::State::playing);
        fx.setToggleState (engine.getLoopFx (index).isActive(), juce::dontSendNotification);
        clear.setEnabled (state != RigLooper::State::empty);
        source.setEnabled (state != RigLooper::State::recording);
        mute.setToggleState (engine.getLooper().getLayer (index).mute.load(), juce::dontSendNotification);
        wave.tick();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (layerColour (index));
        g.fillRoundedRectangle (getLocalBounds().removeFromLeft (6).toFloat(), 3.0f);
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1), getLocalBounds().withWidth (40).withTrimmedLeft (8), juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 6);
        area.removeFromLeft (36);
        record.setBounds (area.removeFromLeft (84));
        area.removeFromLeft (8);
        auto settings = area.removeFromLeft (130);
        const int h = settings.getHeight() / 2;
        source.setBounds (settings.removeFromTop (h).reduced (0, 1));
        bars.setBounds (settings.reduced (0, 1));
        area.removeFromLeft (8);
        more.setBounds (area.removeFromRight (34).withSizeKeepingCentre (34, 30));
        area.removeFromRight (6);
        clear.setBounds (area.removeFromRight (56).withSizeKeepingCentre (56, 30));
        area.removeFromRight (4);
        undo.setBounds (area.removeFromRight (56).withSizeKeepingCentre (56, 30));
        area.removeFromRight (4);
        edit.setBounds (area.removeFromRight (56).withSizeKeepingCentre (56, 30));
        area.removeFromRight (4);
        fx.setBounds (area.removeFromRight (44).withSizeKeepingCentre (44, 30));
        area.removeFromRight (8);
        level.setBounds (area.removeFromRight (120));
        area.removeFromRight (4);
        mute.setBounds (area.removeFromRight (56).withSizeKeepingCentre (56, 30));
        area.removeFromRight (10);
        wave.setBounds (area);
    }

private:
    void say (const juce::String& s)
    {
        if (owner.onSay)
            owner.onSay (s);
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem ("Import a loop, stretched to the Rig's tempo...", [this] { importFile (false); });
        menu.addItem ("Import a song or loop, and set the Rig's tempo to it...", [this] { importFile (true); });
        menu.addItem ("Edit where it starts and ends...", engine.getLooper().getState (index) == RigLooper::State::playing, false,
                      [this] { owner.editLoop (index); });
        menu.addItem ("Save this loop as a WAV...", engine.getLooper().getState (index) == RigLooper::State::playing, false, [this] { exportFile(); });
        menu.addSeparator();
        const auto learnItem = [this, &menu] (int control, const juce::String& what)
        {
            const auto [channel, cc] = engine.getRouting().getAssignment (control);
            juce::ignoreUnused (channel);
            menu.addItem (cc >= 0 ? what + " is on CC " + juce::String (cc) + ": forget it" : "Put " + what + " on a keyboard button",
                          [this, control, cc, what]
                          {
                              auto& routing = engine.getRouting();
                              if (cc >= 0)
                              {
                                  routing.forget (control);
                                  say (what + " forgotten.");
                              }
                              else
                              {
                                  routing.learn (control);
                                  say ("Press the button on your keyboard for " + what + ".");
                              }
                          });
        };
        learnItem (RigRouting::loopRecord1 + index, "Loop " + juce::String (index + 1) + " record");
        learnItem (RigRouting::loopMute1 + index, "Loop " + juce::String (index + 1) + " mute");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more));
    }

    void importFile (bool setTempo)
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Import onto loop " + juce::String (index + 1),
                                                            juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser, setTempo] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  juce::AudioFormatManager formats;
                                  formats.registerBasicFormats();
                                  juce::String error;
                                  auto audio = crate::AudioFiles::load (file, formats, error);
                                  if (audio == nullptr)
                                  {
                                      say ("Couldn't open " + file.getFileName() + ": " + error);
                                      return;
                                  }
                                  auto& transport = engine.getTransport();
                                  const int bpb = transport.getBeatsPerBar();
                                  double beats = crate::SoundFx::guessBeats (audio->getNumSamples(), transport.getBpm());
                                  if (beats <= 0)
                                      beats = bpb;
                                  if (setTempo)
                                  {
                                      // A song: guess its length in bars at a likely tempo (70-180 BPM).
                                      const double seconds = crate::seconds (audio->getNumSamples());
                                      double bpm = crate::SoundFx::tempoOf (audio->getNumSamples(), beats);
                                      while (bpm < 70.0) { beats *= 2; bpm = crate::SoundFx::tempoOf (audio->getNumSamples(), beats); }
                                      while (bpm > 180.0) { beats /= 2; bpm = crate::SoundFx::tempoOf (audio->getNumSamples(), beats); }
                                      transport.setBpm (std::round (bpm * 10.0) / 10.0);
                                      engine.getLooper().setLayerAudio (index, juce::AudioBuffer<float> (*audio), beats);
                                      say (file.getFileNameWithoutExtension() + " on loop " + juce::String (index + 1) + ": "
                                           + barsText (beats, bpb) + " at " + juce::String (bpm, 1) + " BPM (" + juce::String (seconds, 1)
                                           + " s). The Rig's tempo is now " + juce::String (bpm, 1) + ".");
                                      juce::ignoreUnused (seconds);
                                  }
                                  else
                                  {
                                      auto fitted = crate::SoundFx::fitToTempo (audio, beats, transport.getBpm());
                                      engine.getLooper().setLayerAudio (index, juce::AudioBuffer<float> (*fitted), beats);
                                      say (file.getFileNameWithoutExtension() + " on loop " + juce::String (index + 1) + ": "
                                           + barsText (beats, bpb) + ", stretched to " + juce::String (transport.getBpm(), 1) + " BPM.");
                                  }
                                  owner.repaint();
                              });
    }

    void exportFile()
    {
        auto audio = std::make_shared<juce::AudioBuffer<float>> (engine.getLooper().copyLayerAudio (index));
        if (audio->getNumSamples() == 0)
            return;
        auto chooser = std::make_shared<juce::FileChooser> ("Save loop " + juce::String (index + 1),
                                                            juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                                .getChildFile ("Loop " + juce::String (index + 1) + ".wav"),
                                                            "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, chooser, audio] (const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  file = file.withFileExtension ("wav");
                                  juce::WavAudioFormat wav;
                                  file.deleteFile();
                                  std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
                                  const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
                                  auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
                                  if (stream != nullptr)
                                      if (auto writer = wav.createWriterFor (stream, options))
                                          if (writer->writeFromAudioSampleBuffer (*audio, 0, audio->getNumSamples()))
                                          {
                                              say ("Saved " + file.getFileName() + ".");
                                              return;
                                          }
                                  say ("Couldn't save " + file.getFileName() + ".");
                              });
    }

    LooperView& owner;
    RigEngine& engine;
    const int index;
    juce::TextButton record { "Rec" }, mute { "Mute" }, edit { "Edit" }, fx { "FX" }, undo { "Undo" }, clear { "Clear" }, more { "..." };
    juce::ComboBox source, bars;
    juce::Slider level { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    LoopWave wave;
};

//==============================================================================
LooperView::LooperView (RigEngine& e) : engine (e)
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        addAndMakeVisible (rows.add (new Row (*this, engine, i)));
    help.setText ("Loops stay in time with the Rig. Rec starts at the next bar; press again to finish the bar. "
                  "Rec on a playing loop overdubs. Keys 1-8 record too.",
                  juce::dontSendNotification);
    help.setColour (juce::Label::textColourId, Theme::textDim);
    help.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (help);
    startTimerHz (30);
}

LooperView::~LooperView() { stopTimer(); }

void LooperView::refresh()
{
    for (auto* r : rows)
        r->refresh();
}

void LooperView::timerCallback()
{
    for (auto* r : rows)
        r->tick();
    if (fxPanel != nullptr)
        fxPanel->tick();
    if (editor != nullptr)
    {
        editor->tick();
        if (! editor->isValid())
            closeEditor();   // the loop was cleared (from a key, a scene, or the keyboard)
    }
}

void LooperView::showFx (int layer)
{
    const bool same = fxPanel != nullptr && fxLayer == layer;
    if (fxPanel != nullptr)
        removeChildComponent (fxPanel.get());
    fxPanel.reset();
    fxLayer = -1;
    if (! same && juce::isPositiveAndBelow (layer, RigLooper::kLayers))
    {
        fxLayer = layer;
        fxPanel = std::make_unique<StripFxPanel> (engine.getLoopFx (layer), "Loop " + juce::String (layer + 1) + " effects", layerColour (layer));
        fxPanel->closeButton.onClick = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LooperView> (this)]
                                                                               { if (safe != nullptr && safe->fxPanel != nullptr) safe->showFx (safe->fxLayer); }); };
        addAndMakeVisible (*fxPanel);
    }
    resized();
}

void LooperView::editLoop (int layer)
{
    if (editor != nullptr && editor->getLayer() == layer)
        return;
    closeEditor();
    if (engine.getLooper().getState (layer) != RigLooper::State::playing)
        return;
    auto e = std::make_unique<LoopEditor> (engine, layer);
    if (! e->isValid())
        return;
    e->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LooperView> (this)]
                                                           { if (safe != nullptr) safe->closeEditor(); }); };
    editor = std::move (e);
    addAndMakeVisible (*editor);
    resized();
}

void LooperView::closeEditor()
{
    if (editor == nullptr)
        return;
    removeChildComponent (editor.get());
    editor.reset();
    resized();
}

void LooperView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (juce::Colour (0xff81c784));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("LOOPER", getLocalBounds().reduced (14, 8).removeFromTop (26), juce::Justification::centredLeft);
}

void LooperView::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    auto top = area.removeFromTop (26);
    top.removeFromLeft (100);
    help.setBounds (top);
    area.removeFromTop (6);
    if (fxPanel != nullptr)
    {
        fxPanel->setBounds (area.removeFromBottom (130));
        area.removeFromBottom (6);
    }
    const int h = juce::jmin (72, (area.getHeight() - 6 * (rows.size() - 1)) / rows.size());
    for (auto* r : rows)
    {
        r->setBounds (area.removeFromTop (h));
        area.removeFromTop (6);
    }
    // The editor sits over the layers, leaving its own row (and the ones above) in view.
    if (editor != nullptr)
    {
        auto bounds = getLocalBounds().reduced (10, 8).withTrimmedTop (32);
        if (fxPanel != nullptr)
            bounds = bounds.withTrimmedBottom (136);
        const auto* row = rows[editor->getLayer()];
        const int below = row->getBottom() + 6;
        const int height = juce::jmin (bounds.getHeight(), 320);
        if (bounds.getBottom() - below >= height)
            bounds = bounds.withTop (below).withHeight (height);
        else
            bounds = bounds.withTop (bounds.getBottom() - height);
        editor->setBounds (bounds);
        editor->toFront (false);
    }
}
