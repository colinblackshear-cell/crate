#include "StageView.h"
#include "RigColours.h"

namespace Theme = crate::Theme;

namespace
{
constexpr int kHeader = 34, kFooter = 40, kGap = 10;

void setParam (juce::AudioProcessorValueTreeState& params, const char* id, float value)
{
    if (auto* p = params.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

float getParam (juce::AudioProcessorValueTreeState& params, const char* id)
{
    auto* v = params.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

void styleButton (juce::TextButton& b, juce::Colour colour)
{
    b.setColour (juce::TextButton::buttonOnColourId, colour);
    b.setColour (juce::TextButton::textColourOnId, Theme::background);
}

// Lays out `buttons` left to right in rows of `perRow`, each `height` tall.
void layoutGrid (juce::OwnedArray<juce::TextButton>& buttons, juce::Rectangle<int> area, int perRow, int height)
{
    const int w = (area.getWidth() - 4 * (perRow - 1)) / juce::jmax (1, perRow);
    for (int i = 0; i < buttons.size(); ++i)
    {
        const int row = i / perRow, col = i % perRow;
        buttons[i]->setBounds (area.getX() + col * (w + 4), area.getY() + row * (height + 4), w, height);
    }
}
}

//==============================================================================
// A tool's tile: its name and colour, what it's doing (drawn by the tool's own tile), and its
// strip's mute and meter along the bottom. A click that misses the buttons goes into the tool.
class StageView::Tile : public juce::Component
{
public:
    Tile (StageView& o, const juce::String& n, const juce::String& t, juce::Colour c, int s)
        : owner (o), engine (o.engine), name (n), tab (t), colour (c), strip (s)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        if (strip >= 0)
        {
            addAndMakeVisible (mute);
            mute.setClickingTogglesState (true);
            mute.setColour (juce::TextButton::buttonOnColourId, Theme::record);
            mute.onClick = [this] { engine.getStrip (strip).mute = mute.getToggleState(); };
            mute.setTooltip ("Mutes " + name + ", the same as its mixer strip.");
        }
    }

    // 30 times a second, while the Stage shows.
    void tick()
    {
        if (strip >= 0)
        {
            mute.setToggleState (engine.getStrip (strip).mute.load(), juce::dontSendNotification);
            const float m = owner.meterOf ? owner.meterOf (strip) : 0.0f;
            const bool fx = engine.getFx (strip).isActive();
            if (std::abs (m - meter) > 0.002f || fx != fxOn)
            {
                meter = m;
                fxOn = fx;
                repaint (footerArea());
            }
        }
        tickBody();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool over = isMouseOver (true);
        g.setColour (over ? Theme::raised.brighter (0.07f) : Theme::raised);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colour.withAlpha (over ? 0.9f : 0.5f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.fillRect (getLocalBounds().reduced (10, 0).removeFromTop (3));

        auto header = headerArea();
        g.setColour (colour);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (name, header, juce::Justification::centredLeft);
        g.setColour (over ? Theme::text : Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Open " + tab + "  >", header, juce::Justification::centredRight);

        paintBody (g, bodyArea());

        if (strip >= 0)
        {
            auto f = footerArea();
            f.removeFromLeft (mute.getWidth() + 8);
            if (fxOn)
            {
                g.setColour (colour);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText ("FX", f.removeFromRight (22), juce::Justification::centredRight);
                f.removeFromRight (6);
            }
            auto bar = f.withSizeKeepingCentre (f.getWidth(), 8).toFloat();
            g.setColour (Theme::background);
            g.fillRoundedRectangle (bar, 4.0f);
            const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
            const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour (meter > 0.98f ? Theme::record : colour.withAlpha (engine.getStrip (strip).mute.load() ? 0.3f : 0.9f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * fill), 4.0f);
        }
    }

    void resized() override
    {
        if (strip >= 0)
            mute.setBounds (footerArea().removeFromLeft (60));
        layoutBody (bodyArea());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;
        if (! e.mods.isPopupMenu() && bodyClick (e.getPosition()))
            return;
        if (owner.onOpen)
            owner.onOpen (tab);
    }

    // Forget what's been drawn, so the next tick redraws everything (a show was loaded).
    virtual void reset() {}

protected:
    virtual void tickBody() {}
    virtual void paintBody (juce::Graphics&, juce::Rectangle<int>) {}
    virtual void layoutBody (juce::Rectangle<int>) {}
    // A click on the tile itself (not a button): true if the tile used it.
    virtual bool bodyClick (juce::Point<int>) { return false; }

    juce::Rectangle<int> headerArea() const { return getLocalBounds().reduced (12, 0).removeFromTop (kHeader); }
    juce::Rectangle<int> footerArea() const
    {
        return strip >= 0 ? getLocalBounds().reduced (12, 8).removeFromBottom (kFooter - 16) : juce::Rectangle<int>();
    }
    juce::Rectangle<int> bodyArea() const
    {
        auto a = getLocalBounds().reduced (12, 0);
        a.removeFromTop (kHeader);
        a.removeFromBottom (strip >= 0 ? kFooter : 10);
        return a;
    }

    StageView& owner;
    RigEngine& engine;
    const juce::String name, tab;
    const juce::Colour colour;
    const int strip;

private:
    juce::TextButton mute { "Mute" };
    float meter = 0;
    bool fxOn = false;
};

//==============================================================================
// The song's parts: what's playing, what's next, a button for each part, Back / Next and
// song mode.
class StageView::SongTile : public Tile
{
public:
    explicit SongTile (StageView& o) : Tile (o, "Song", "Song", RigColours::song, -1)
    {
        for (auto* b : { &back, &next, &songMode })
        {
            addAndMakeVisible (b);
            styleButton (*b, colour);
        }
        songMode.setClickingTogglesState (true);
        songMode.onClick = [this] { engine.getScenes().songMode = songMode.getToggleState(); };
        songMode.setTooltip ("On, a part with a length moves on to the next part by itself when its bars are up.");
        back.onClick = [this] { engine.getScenes().back(); };
        next.onClick = [this] { engine.getScenes().next(); };
        back.setTooltip ("The part before this one, at the next bar.");
        next.setTooltip ("The next part, at the next bar.");
    }

    void reset() override { signature = {}; }

protected:
    void tickBody() override
    {
        auto& scenes = engine.getScenes();
        const auto& list = scenes.getScenes();
        juce::StringArray names;
        for (const auto& s : list)
            names.add (s.name);
        if (const auto sig = names.joinIntoString ("|") + juce::String ((int) list.size()); sig != signature)
        {
            signature = sig;
            buttons.clear();
            for (int i = 0; i < (int) list.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (list[(size_t) i].name));
                styleButton (*b, colour);
                b->setTooltip ("Plays " + list[(size_t) i].name + " from the next bar.");
                b->onClick = [this, i] { engine.getScenes().launch (i); };
                addAndMakeVisible (b);
            }
            resized();
            repaint();
        }

        const int current = scenes.getCurrent(), queued = scenes.getQueued();
        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setToggleState (i == current, juce::dontSendNotification);
            buttons[i]->setColour (juce::TextButton::buttonColourId, i == queued ? colour.withAlpha (0.35f) : Theme::panel);
        }
        songMode.setToggleState (scenes.songMode, juce::dontSendNotification);
        back.setEnabled (! list.empty());
        next.setEnabled (! list.empty());

        const auto text = describe();
        if (text != shown)
        {
            shown = text;
            repaint (bodyArea());
        }
    }

    juce::String describe() const
    {
        auto& scenes = engine.getScenes();
        const auto& list = scenes.getScenes();
        const int current = scenes.getCurrent(), queued = scenes.getQueued();
        juce::String now = juce::isPositiveAndBelow (current, (int) list.size()) ? list[(size_t) current].name
                         : list.empty() ? juce::String ("No song parts") : juce::String ("Not started");
        juce::String after;
        if (list.empty())
            after = "No parts yet. Set up what plays, then + Add scene in the Song tab.";
        else if (juce::isPositiveAndBelow (queued, (int) list.size()))
        {
            auto& transport = engine.getTransport();
            const int bpb = juce::jmax (1, transport.getBeatsPerBar());
            const double left = scenes.getQueuedBeat() - transport.getPositionInBeats();
            const int bars = (int) std::ceil (left / bpb - 1.0e-6);
            after = "Next: " + list[(size_t) queued].name
                    + (! transport.isPlaying() ? juce::String()
                       : bars <= 1 ? " at the next bar" : " in " + juce::String (bars) + " bars");
        }
        else
            after = current < 0 ? "Press a part to start it." : "Press a part to play it next.";
        return now + "\n" + after;
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int> area) override
    {
        auto text = area.removeFromLeft (textWidth);
        const auto lines = juce::StringArray::fromLines (shown);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawFittedText (lines[0], text.removeFromTop (32), juce::Justification::centredLeft, 1);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (lines[1], text, juce::Justification::topLeft, 2);
    }

    void layoutBody (juce::Rectangle<int> area) override
    {
        textWidth = juce::jmin (300, area.getWidth() / 3);
        area.removeFromLeft (textWidth + 10);
        auto right = area.removeFromRight (240);
        auto row = right.removeFromTop (30);
        back.setBounds (row.removeFromLeft (76));
        row.removeFromLeft (4);
        next.setBounds (row.removeFromLeft (76));
        row.removeFromLeft (4);
        songMode.setBounds (row);
        area.removeFromRight (10);
        const int perRow = juce::jmax (1, juce::jmin (juce::jmax (1, buttons.size()), area.getWidth() / 100));
        layoutGrid (buttons, area, perRow, 30);
    }

private:
    juce::TextButton back { "< Back" }, next { "Next >" }, songMode { "Song mode" };
    juce::OwnedArray<juce::TextButton> buttons;
    juce::String signature, shown;
    int textWidth = 260;
};

//==============================================================================
// The MPC: the sequence playing and where it is, a button for each sequence, and its songs.
class StageView::MpcTile : public Tile
{
public:
    explicit MpcTile (StageView& o) : Tile (o, "MPC", "MPC", RigColours::mpc, RigEngine::mpcStrip)
    {
        addAndMakeVisible (songButton);
        addChildComponent (songBox);
        styleButton (songButton, colour);
        songButton.setTooltip ("Plays the MPC song (the sequences in order, each as many times as it says). "
                               "Playing, it comes in at the next bar; stopped, it starts everything from the top.");
        songButton.onClick = [this]
        {
            auto& mpc = engine.getMpc();
            if (mpc.isSongPlaying())
                engine.playMpcSequence (mpc.getProject().getCurrentSequenceIndex());   // stay on this one
            else
                engine.playMpcSong (mpc.getProject().getCurrentSongIndex());
        };
        songBox.onChange = [this]
        {
            const int i = songBox.getSelectedItemIndex();
            if (i >= 0 && i != engine.getMpc().getProject().getCurrentSongIndex())
                engine.getMpc().getProject().setCurrentSong (i);
        };
    }

    void reset() override { signature = {}; }

protected:
    void tickBody() override
    {
        auto& mpc = engine.getMpc();
        auto& project = mpc.getProject();
        const auto& seqs = project.getSequences();
        const auto& songs = project.getSongs();

        juce::StringArray names, songNames;
        for (int i = 0; i < (int) seqs.size(); ++i)
            names.add (label (i));
        for (const auto& s : songs)
            songNames.add (s.name);
        if (const auto sig = names.joinIntoString ("|") + "/" + songNames.joinIntoString ("|"); sig != signature)
        {
            signature = sig;
            buttons.clear();
            for (int i = 0; i < juce::jmin (kMaxButtons, (int) seqs.size()); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (names[i]));
                styleButton (*b, colour);
                b->setTooltip ("Plays this sequence. Playing, it comes in at the next bar.");
                b->onClick = [this, i] { engine.playMpcSequence (i); };
                addAndMakeVisible (b);
            }
            songBox.clear (juce::dontSendNotification);
            songBox.addItemList (songNames, 1);
            songBox.setVisible (songs.size() > 1);
            resized();
        }

        const bool playing = mpc.getSequencer().isPlaying();
        const int now = playing ? mpc.getSequencer().getPlayingId() : project.getCurrentSequenceIndex();
        const int queued = mpc.getQueuedSequence();
        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setToggleState (i == now, juce::dontSendNotification);
            buttons[i]->setColour (juce::TextButton::buttonColourId, i == queued ? colour.withAlpha (0.35f) : Theme::panel);
        }

        songBox.setSelectedItemIndex (project.getCurrentSongIndex(), juce::dontSendNotification);
        const auto& song = project.currentSong();
        const bool inSong = mpc.isSongPlaying();
        songButton.setEnabled (! song.steps.empty());
        songButton.setToggleState (inSong, juce::dontSendNotification);
        songButton.setButtonText (song.steps.empty() ? "No song yet (make one in the MPC)"
                                  : inSong ? "Song: part " + juce::String (mpc.getSongStep() + 1) + " of " + juce::String ((int) song.steps.size())
                                                 + "  (stop song)"
                                           : "Play song: " + song.name);

        // Where it is in the sequence.
        double fraction = 0;
        juce::String where;
        if (juce::isPositiveAndBelow (now, (int) seqs.size()))
        {
            const auto& s = seqs[(size_t) now];
            const double pos = playing ? mpc.getSequencer().getPosition() : 0.0;
            fraction = pos >= 0 ? juce::jlimit (0.0, 1.0, pos / juce::jmax (1, s.lengthTicks())) : 0.0;
            const int bar = pos >= 0 ? (int) (pos / s.ticksPerBar()) + 1 : 0;
            where = names[now] + "\n" + (bar > 0 ? "bar " + juce::String (bar) + " of " + juce::String (s.bars) : juce::String ("counting in"));
        }
        if (where != shown || std::abs (fraction - shownFraction) > 0.002)
        {
            shown = where;
            shownFraction = fraction;
            repaint (bodyArea().removeFromTop (48));
        }
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int> area) override
    {
        auto top = area.removeFromTop (40);
        const auto lines = juce::StringArray::fromLines (shown);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (lines[1], top, juce::Justification::topRight);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawFittedText (lines[0], top.withTrimmedRight (90), juce::Justification::topLeft, 1);
        auto bar = area.removeFromTop (6).toFloat();
        g.setColour (Theme::background);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) shownFraction), 3.0f);
    }

    void layoutBody (juce::Rectangle<int> area) override
    {
        area.removeFromTop (54);
        auto songRow = area.removeFromBottom (30);
        if (songBox.isVisible())
        {
            songBox.setBounds (songRow.removeFromLeft (140));
            songRow.removeFromLeft (4);
        }
        songButton.setBounds (songRow);
        area.removeFromBottom (8);
        const int perRow = 4;
        const int rows = juce::jmax (1, (buttons.size() + perRow - 1) / perRow);
        layoutGrid (buttons, area, perRow, juce::jlimit (22, 32, (area.getHeight() - 4 * (rows - 1)) / rows));
    }

private:
    static constexpr int kMaxButtons = 8;

    juce::String label (int i) const
    {
        const auto& s = engine.getMpc().getProject().getSequences()[(size_t) i];
        return juce::String (i + 1).paddedLeft ('0', 2) + (s.name.isNotEmpty() ? " " + s.name : juce::String());
    }

    juce::OwnedArray<juce::TextButton> buttons;
    juce::TextButton songButton { "Play song" };
    juce::ComboBox songBox;
    juce::String signature, shown;
    double shownFraction = -1;
};

//==============================================================================
// The bass: the line's 16 steps with the one playing lit, its 8 patterns and the sequencer.
class StageView::BassTile : public Tile
{
public:
    explicit BassTile (StageView& o) : Tile (o, "Bass", "Bass", RigColours::bass, RigEngine::bassStrip)
    {
        for (int i = 0; i < crate::bass::kPatterns; ++i)
        {
            auto* b = patterns.add (new juce::TextButton (juce::String (i + 1)));
            styleButton (*b, colour);
            b->setTooltip ("Bass pattern " + juce::String (i + 1) + ". It changes on its next step, in time.");
            b->onClick = [this, i] { engine.getBass().selectPattern (i); };
            addAndMakeVisible (b);
        }
        addAndMakeVisible (sequencer);
        styleButton (sequencer, colour);
        sequencer.setClickingTogglesState (true);
        sequencer.setTooltip ("On, the bass plays its pattern. Off, it only plays what you play on the keys.");
        sequencer.onClick = [this] { setParam (engine.getBass().params, "sequencer", sequencer.getToggleState() ? 1.0f : 0.0f); };
    }

    void reset() override { shownPattern = -1; }

protected:
    void tickBody() override
    {
        auto& bass = engine.getBass();
        const int selected = bass.getSelectedPattern();
        for (int i = 0; i < patterns.size(); ++i)
            patterns[i]->setToggleState (i == selected, juce::dontSendNotification);
        const bool on = getParam (bass.params, "sequencer") > 0.5f;
        sequencer.setToggleState (on, juce::dontSendNotification);
        sequencer.setButtonText (on ? "Sequencer on" : "Sequencer off");

        const int step = engine.getTransport().isPlaying() && on ? bass.getCurrentStep() : -1;
        const auto pattern = bass.getPattern (selected);
        bool changed = step != shownStep || selected != shownPattern || pattern.length != shown.length;
        for (int i = 0; i < crate::bass::kSteps && ! changed; ++i)
            changed = pattern.steps[(size_t) i].on != shown.steps[(size_t) i].on || pattern.steps[(size_t) i].note != shown.steps[(size_t) i].note
                   || pattern.steps[(size_t) i].accent != shown.steps[(size_t) i].accent;
        if (changed)
        {
            shown = pattern;
            shownStep = step;
            shownPattern = selected;
            repaint (stepArea);
        }
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int>) override
    {
        const int n = crate::bass::kSteps;
        const float w = stepArea.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<float> (stepArea.getX() + i * w, (float) stepArea.getY(), w, (float) stepArea.getHeight()).reduced (1.5f, 0);
            const auto& s = shown.steps[(size_t) i];
            const bool inside = i < shown.length;
            g.setColour (inside ? Theme::background : Theme::background.withAlpha (0.4f));
            g.fillRoundedRectangle (cell, 3.0f);
            if (inside && s.on)
            {
                // Higher notes sit higher in the cell.
                const float height = juce::jmap ((float) juce::jlimit (-12, 24, s.note), -12.0f, 24.0f, 0.25f, 1.0f);
                g.setColour (colour.withAlpha (s.accent ? 1.0f : 0.6f));
                g.fillRoundedRectangle (cell.withTop (cell.getBottom() - cell.getHeight() * height), 3.0f);
            }
            if (i == shownStep)
            {
                g.setColour (Theme::text);
                g.drawRoundedRectangle (cell, 3.0f, 2.0f);
            }
            if (i % 4 == 0)
            {
                g.setColour (Theme::textDim.withAlpha (0.6f));
                g.fillRect (cell.getX(), cell.getBottom() + 2.0f, cell.getWidth(), 2.0f);
            }
        }
    }

    void layoutBody (juce::Rectangle<int> area) override
    {
        auto bottom = area.removeFromBottom (30);
        sequencer.setBounds (bottom);
        area.removeFromBottom (6);
        auto row = area.removeFromBottom (28);
        layoutGrid (patterns, row, patterns.size(), row.getHeight());
        area.removeFromBottom (10);
        stepArea = area.withTrimmedBottom (4);
    }

private:
    juce::OwnedArray<juce::TextButton> patterns;
    juce::TextButton sequencer { "Sequencer" };
    juce::Rectangle<int> stepArea;
    crate::bass::Pattern shown;
    int shownStep = -2, shownPattern = -1;
};

//==============================================================================
// The synth: the arp on or off and its rhythm stepping along.
class StageView::SynthTile : public Tile
{
public:
    explicit SynthTile (StageView& o) : Tile (o, "Synth", "Synth", RigColours::synth, RigEngine::synthStrip)
    {
        addAndMakeVisible (arp);
        styleButton (arp, colour);
        arp.setClickingTogglesState (true);
        arp.setTooltip ("The arpeggiator: hold a chord and it plays the notes in time with everything else.");
        arp.onClick = [this] { setParam (engine.getSynth().params, "arpOn", arp.getToggleState() ? 1.0f : 0.0f); };
    }

protected:
    void tickBody() override
    {
        const bool on = getParam (engine.getSynth().params, "arpOn") > 0.5f;
        arp.setToggleState (on, juce::dontSendNotification);
        arp.setButtonText (on ? "Arp on" : "Arp off");
        const auto rhythm = engine.getSynth().getRhythm();
        const int step = on && engine.getTransport().isPlaying() ? engine.getSynth().getArpStep() : -1;
        if (step != shownStep || rhythm.on != shown.on || rhythm.accent != shown.accent || rhythm.length != shown.length || on != shownOn)
        {
            shown = rhythm;
            shownStep = step;
            shownOn = on;
            repaint (bodyArea());
        }
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int>) override
    {
        const int n = crate::synthtool::kRhythmSteps;
        const float w = stepArea.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<float> (stepArea.getX() + i * w, (float) stepArea.getY(), w, (float) stepArea.getHeight()).reduced (1.5f, 0);
            const bool inside = i < shown.length;
            g.setColour (inside ? Theme::background : Theme::background.withAlpha (0.4f));
            g.fillRoundedRectangle (cell, 3.0f);
            if (inside && shown.on[(size_t) i])
            {
                g.setColour (colour.withAlpha (! shownOn ? 0.25f : shown.accent[(size_t) i] ? 1.0f : 0.6f));
                g.fillRoundedRectangle (cell.withTop (cell.getY() + cell.getHeight() * (shown.accent[(size_t) i] ? 0.0f : 0.35f)), 3.0f);
            }
            if (i == shownStep)
            {
                g.setColour (Theme::text);
                g.drawRoundedRectangle (cell, 3.0f, 2.0f);
            }
        }
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (shownOn ? "Hold a chord on the keys" : "Plays what you play on the keys", hintArea, juce::Justification::centredLeft);
    }

    void layoutBody (juce::Rectangle<int> area) override
    {
        arp.setBounds (area.removeFromBottom (30));
        area.removeFromBottom (6);
        hintArea = area.removeFromBottom (20);
        area.removeFromBottom (6);
        stepArea = area;
        repaint();
    }

private:
    juce::TextButton arp { "Arp" };
    juce::Rectangle<int> stepArea, hintArea;
    crate::synthtool::Rhythm shown;
    int shownStep = -2;
    bool shownOn = false;
};

//==============================================================================
// A tool with an on/off switch: the vocoder, the audio input.
class StageView::SwitchTile : public Tile
{
public:
    SwitchTile (StageView& o, const juce::String& n, juce::Colour c, int s, std::atomic<bool>& f, std::function<juce::String()> h)
        : Tile (o, n, n, c, s), flag (f), hint (std::move (h))
    {
        addAndMakeVisible (on);
        styleButton (on, colour);
        on.setClickingTogglesState (true);
        on.onClick = [this] { flag = on.getToggleState(); };
        on.setTooltip (strip == RigEngine::inputStrip ? "Plays your audio input (a guitar, another mic) through its strip. Off until you turn it on, so a mic doesn't feed back."
                                                      : "Turns the vocoder on or off.");
    }

protected:
    void tickBody() override
    {
        on.setToggleState (flag.load(), juce::dontSendNotification);
        on.setButtonText (name + (flag.load() ? " on" : " off"));
        if (const auto text = hint(); text != shown)
        {
            shown = text;
            repaint (hintArea);
        }
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int>) override
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (shown, hintArea, juce::Justification::topLeft, 4);
    }

    void layoutBody (juce::Rectangle<int> area) override
    {
        on.setBounds (area.removeFromTop (44));
        area.removeFromTop (8);
        hintArea = area;
    }

private:
    std::atomic<bool>& flag;
    std::function<juce::String()> hint;
    juce::TextButton on;
    juce::Rectangle<int> hintArea;
    juce::String shown;
};

//==============================================================================
// The looper's eight layers: click one to record, stop or overdub it.
class StageView::LooperTile : public Tile
{
public:
    explicit LooperTile (StageView& o) : Tile (o, "Looper", "Looper", RigColours::loops, RigEngine::loopsStrip) {}

protected:
    void tickBody() override { repaint (bodyArea()); }

    juce::Rectangle<float> cell (int i, juce::Rectangle<int> area) const
    {
        const int perRow = 4;
        const float w = area.getWidth() / (float) perRow, h = area.getHeight() / 2.0f;
        return { area.getX() + (i % perRow) * w, area.getY() + (i / perRow) * h, w, h };
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int> area) override
    {
        auto& looper = engine.getLooper();
        const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            auto r = cell (i, area).reduced (3.0f);
            const auto& layer = looper.getLayer (i);
            const auto state = looper.getState (i);
            const bool muted = layer.mute.load();
            g.setColour (Theme::background);
            g.fillRoundedRectangle (r, 5.0f);

            juce::String word;
            juce::Colour fill = colour;
            switch (state)
            {
                case RigLooper::State::empty:       word = "Rec"; break;
                case RigLooper::State::armed:       word = "Armed"; fill = Theme::record; break;
                case RigLooper::State::recording:   word = "Recording"; fill = Theme::record; break;
                case RigLooper::State::playing:     word = muted ? "Muted" : "Playing"; break;
                case RigLooper::State::overdubbing: word = "Overdub"; fill = Theme::record; break;
            }
            if (state == RigLooper::State::armed && blink)
            {
                g.setColour (fill.withAlpha (0.35f));
                g.fillRoundedRectangle (r, 5.0f);
            }
            if (state == RigLooper::State::playing || state == RigLooper::State::overdubbing || state == RigLooper::State::recording)
            {
                // How far round the loop it is (while recording: a stripe that grows).
                const float pos = juce::jlimit (0.0f, 1.0f, (float) layer.position.load());
                g.setColour (fill.withAlpha (muted ? 0.15f : 0.3f));
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (fill.withAlpha (muted ? 0.3f : 0.75f));
                g.fillRoundedRectangle (r.withWidth (juce::jmax (4.0f, r.getWidth() * pos)), 5.0f);
            }
            g.setColour (state == RigLooper::State::empty ? Theme::outline : fill);
            g.drawRoundedRectangle (r, 5.0f, 1.0f);

            g.setColour (state == RigLooper::State::empty ? Theme::textDim : Theme::text);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            auto text = r.reduced (8.0f, 4.0f).toNearestInt();
            g.drawText (juce::String (i + 1), text.removeFromTop (18), juce::Justification::topLeft);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (word, text, juce::Justification::bottomLeft);
            g.setColour (Theme::textDim);
            g.drawText (RigLooper::sourceName (layer.source.load()), text, juce::Justification::topRight);
        }
    }

    bool bodyClick (juce::Point<int> p) override
    {
        for (int i = 0; i < RigLooper::kLayers; ++i)
            if (cell (i, bodyArea()).contains (p.toFloat()))
            {
                engine.getLooper().pressRecord (i);
                return true;
            }
        return false;
    }
};

//==============================================================================
// The sampler: what's loaded, and the Rig's Sample button.
class StageView::SamplerTile : public Tile
{
public:
    explicit SamplerTile (StageView& o) : Tile (o, "Sampler", "Sampler", RigColours::sampler, RigEngine::samplerStrip)
    {
        addAndMakeVisible (sample);
        sample.setColour (juce::TextButton::buttonOnColourId, Theme::record);
        sample.setTooltip ("The same as Sample at the top: records what's picked under Sample, into the Sampler or onto an MPC pad.");
        sample.onClick = [this] { if (owner.onSample) owner.onSample(); };
    }

protected:
    void tickBody() override
    {
        const bool recording = owner.isSampling && owner.isSampling();
        sample.setToggleState (recording, juce::dontSendNotification);
        sample.setButtonText (recording ? "Stop sampling" : "Sample");
        const auto text = "Has: " + (engine.getSampler().getDocument().getName().isNotEmpty() ? engine.getSampler().getDocument().getName()
                                                                                              : juce::String ("nothing yet"))
                        + "\nSample records " + RigCapture::sourceName (engine.captureSource) + " "
                        + (engine.captureDest == RigCapture::toMpcPad ? "onto an MPC pad" : "into the Sampler")
                        + (engine.captureBars > 0 ? ", " + juce::String (engine.captureBars) + (engine.captureBars == 1 ? " bar" : " bars") : juce::String());
        if (text != shown)
        {
            shown = text;
            repaint (hintArea);
        }
    }

    void paintBody (juce::Graphics& g, juce::Rectangle<int>) override
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (shown, hintArea, juce::Justification::topLeft, 4);
    }

    void layoutBody (juce::Rectangle<int> area) override
    {
        sample.setBounds (area.removeFromTop (44));
        area.removeFromTop (8);
        hintArea = area;
    }

private:
    juce::TextButton sample { "Sample" };
    juce::Rectangle<int> hintArea;
    juce::String shown;
};

//==============================================================================
StageView::StageView (RigEngine& e)
    : engine (e)
{
    songTile = std::make_unique<SongTile> (*this);
    addAndMakeVisible (*songTile);
    tiles.add (new MpcTile (*this));
    tiles.add (new BassTile (*this));
    tiles.add (new SynthTile (*this));
    tiles.add (new LooperTile (*this));
    tiles.add (new SwitchTile (*this, "Vocoder", RigColours::vocoder, RigEngine::vocoderStrip, engine.vocoderOn,
                               [this] { return "Sing into the mic and play the keys.\nKeys play: "
                                               + RigRouting::destName (engine.getRouting().keysTo.load()); }));
    tiles.add (new SwitchTile (*this, "Input", RigColours::input, RigEngine::inputStrip, engine.inputOn,
                               [this] { return "A guitar or another mic, through its own effects.\nListening to: "
                                               + RigEngine::inputJackName (engine.inputJack.load()); }));
    tiles.add (new SamplerTile (*this));
    for (auto* t : tiles)
        addAndMakeVisible (t);
    startTimerHz (30);
}

StageView::~StageView()
{
    stopTimer();
}

void StageView::refresh()
{
    songTile->reset();
    for (auto* t : tiles)
        t->reset();
    songTile->tick();
    for (auto* t : tiles)
        t->tick();
}

void StageView::timerCallback()
{
    if (! isShowing())
        return;
    songTile->tick();
    for (auto* t : tiles)
        t->tick();
}

void StageView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void StageView::resized()
{
    auto area = getLocalBounds().reduced (kGap);
    songTile->setBounds (area.removeFromTop (104));
    area.removeFromTop (kGap);
    const int rowHeight = (area.getHeight() - kGap) / 2;
    auto top = area.removeFromTop (rowHeight);
    area.removeFromTop (kGap);
    auto bottom = area;

    auto place = [] (juce::Rectangle<int> row, std::initializer_list<std::pair<Tile*, float>> list)
    {
        const int total = row.getWidth() - kGap * ((int) list.size() - 1);
        for (auto [tile, share] : list)
        {
            tile->setBounds (row.removeFromLeft (juce::roundToInt (total * share)));
            row.removeFromLeft (kGap);
        }
    };
    place (top, { { tiles[0], 0.4f }, { tiles[1], 0.3f }, { tiles[2], 0.3f } });
    place (bottom, { { tiles[3], 0.4f }, { tiles[4], 0.2f }, { tiles[5], 0.2f }, { tiles[6], 0.2f } });
}
