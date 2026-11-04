#include "SongView.h"
#include "RigColours.h"

namespace Theme = crate::Theme;

namespace
{
const juce::Colour songColour = RigColours::song;
constexpr int kRowHeight = 58;
}

//==============================================================================
class SongView::Row : public juce::Component
{
public:
    Row (SongView& o, int i) : owner (o), index (i)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &launch, &name, &bars, &summary, &update, &remove })
            addAndMakeVisible (c);
        auto& scene = scenes().getScenes()[(size_t) index];

        launch.setColour (juce::TextButton::buttonOnColourId, songColour);
        launch.onClick = [this] { scenes().launch (index); };
        launch.setTooltip ("Starts this part at the next bar (straight away if the Rig is stopped).");

        name.setText (scene.name, juce::dontSendNotification);
        name.setFont (juce::FontOptions (15.0f));
        name.onTextChange = [this] { scenes().getScenes()[(size_t) index].name = name.getText(); };

        bars.addItem ("Stay", 1);
        for (int b : { 1, 2, 4, 8, 16, 32 })
            bars.addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b + 1);
        bars.setSelectedId (scene.bars + 1, juce::dontSendNotification);
        if (bars.getSelectedId() == 0)
            bars.setText (juce::String (scene.bars) + " bars", juce::dontSendNotification);
        bars.onChange = [this] { scenes().getScenes()[(size_t) index].bars = juce::jmax (0, bars.getSelectedId() - 1); };
        bars.setTooltip ("In song mode: how many bars this part plays before the next one starts. Stay: until you move on.");

        summary.setColour (juce::Label::textColourId, Theme::textDim);
        summary.setFont (juce::FontOptions (13.0f));

        update.setTooltip ("Keeps what's playing now in this part instead (sequence, patterns, mutes...).");
        update.onClick = [this]
        {
            scenes().getScenes()[(size_t) index].state = owner.engine.captureScene();
            owner.say ("\"" + scenes().getScenes()[(size_t) index].name + "\" now has what's playing.");
        };
        remove.setTooltip ("Deletes this part.");
        remove.onClick = [this]
        {
            auto& list = scenes().getScenes();
            list.erase (list.begin() + index);
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongView> (&owner)] { if (safe != nullptr) safe->refresh(); });
        };
    }

    void tick()
    {
        if (! juce::isPositiveAndBelow (index, (int) scenes().getScenes().size()))
            return;
        const auto& scene = scenes().getScenes()[(size_t) index];
        const bool isCurrent = scenes().getCurrent() == index;
        const bool isQueued = scenes().getQueued() == index;
        launch.setButtonText (isQueued ? "Next..." : isCurrent ? "Playing" : "Play");
        const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
        launch.setToggleState (isCurrent || (isQueued && blink), juce::dontSendNotification);
        summary.setText (owner.describe (scene.state), juce::dontSendNotification);
        highlighted = isCurrent;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (highlighted ? songColour.withAlpha (0.18f) : Theme::raised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1), getLocalBounds().withWidth (36), juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 8);
        area.removeFromLeft (30);
        launch.setBounds (area.removeFromLeft (90));
        area.removeFromLeft (10);
        name.setBounds (area.removeFromLeft (180).withSizeKeepingCentre (180, 30));
        area.removeFromLeft (8);
        bars.setBounds (area.removeFromLeft (100).withSizeKeepingCentre (100, 30));
        remove.setBounds (area.removeFromRight (70).withSizeKeepingCentre (70, 30));
        area.removeFromRight (6);
        update.setBounds (area.removeFromRight (70).withSizeKeepingCentre (70, 30));
        area.removeFromLeft (12);
        summary.setBounds (area);
    }

private:
    RigScenes& scenes() { return owner.engine.getScenes(); }

    SongView& owner;
    const int index;
    bool highlighted = false;
    juce::TextButton launch { "Play" }, update { "Update" }, remove { "Delete" };
    juce::TextEditor name;
    juce::ComboBox bars;
    juce::Label summary;
};

//==============================================================================
SongView::SongView (RigEngine& e) : engine (e)
{
    for (auto* c : std::initializer_list<juce::Component*> { &addButton, &songMode, &back, &next, &keyboard, &help, &viewport })
        addAndMakeVisible (c);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);

    addButton.setTooltip ("Keeps what's playing now (the MPC's sequence, the bass pattern, the arp, and what's muted, "
                          "loops too) as a new part of the song.");
    addButton.onClick = [this] { addScene(); };
    songMode.setClickingTogglesState (true);
    songMode.setColour (juce::TextButton::buttonOnColourId, songColour);
    songMode.setTooltip ("Parts with a length play that many bars, then the next part starts by itself.");
    songMode.onClick = [this] { engine.getScenes().songMode = songMode.getToggleState(); };
    back.onClick = [this] { engine.getScenes().back(); };
    next.onClick = [this] { engine.getScenes().next(); };
    back.setTooltip ("The part before this one, at the next bar.");
    next.setTooltip ("The next part, at the next bar.");
    keyboard.setTooltip ("Put Next and Back on buttons of your keyboard (the Launchkey's scene arrows, say).");
    keyboard.onClick = [this] { showKeyboardMenu(); };

    help.setText ("Build the song from parts: get something going, then Add scene. Press a part to switch to it on the next bar.",
                  juce::dontSendNotification);
    help.setColour (juce::Label::textColourId, Theme::textDim);
    help.setFont (juce::FontOptions (13.0f));

    refresh();
    startTimerHz (15);
}

SongView::~SongView() { stopTimer(); }

void SongView::refresh()
{
    rows.clear();
    for (int i = 0; i < (int) engine.getScenes().getScenes().size(); ++i)
        list.addAndMakeVisible (rows.add (new Row (*this, i)));
    songMode.setToggleState (engine.getScenes().songMode, juce::dontSendNotification);
    resized();
}

void SongView::addScene()
{
    auto& list = engine.getScenes().getScenes();
    if ((int) list.size() >= RigScenes::kMaxScenes)
    {
        say ("A song can have up to " + juce::String (RigScenes::kMaxScenes) + " parts.");
        return;
    }
    static const char* names[] = { "Intro", "Verse", "Chorus", "Drop", "Break", "Build", "Outro" };
    RigScene scene;
    scene.name = list.size() < 7 ? names[list.size()] : "Part " + juce::String ((int) list.size() + 1);
    scene.state = engine.captureScene();
    list.push_back (scene);
    refresh();
    say ("Added \"" + scene.name + "\" with what's playing now.");
}

void SongView::showKeyboardMenu()
{
    juce::PopupMenu menu;
    for (auto [control, what] : { std::pair<int, const char*> { RigRouting::nextScene, "Next" }, { RigRouting::previousScene, "Back" } })
    {
        const auto [channel, cc] = engine.getRouting().getAssignment (control);
        juce::ignoreUnused (channel);
        const juce::String label (what);
        menu.addItem (cc >= 0 ? label + " is on CC " + juce::String (cc) + ": forget it" : "Put " + label + " on a keyboard button",
                      [this, control, cc, label]
                      {
                          if (cc >= 0)
                          {
                              engine.getRouting().forget (control);
                              say (label + " forgotten.");
                          }
                          else
                          {
                              engine.getRouting().learn (control);
                              say ("Press the button on your keyboard for " + label + ".");
                          }
                      });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyboard));
}

juce::String SongView::describe (const RigScene::State& s) const
{
    juce::StringArray parts;
    auto& sequences = const_cast<RigEngine&> (engine).getMpc().getProject().getSequences();
    if (juce::isPositiveAndBelow (s.mpcSequence, (int) sequences.size()))
        parts.add ("MPC: " + sequences[(size_t) s.mpcSequence].name);
    if (s.bassSequencer == 0)
        parts.add ("Bass seq off");
    else if (s.bassPattern >= 0)
        parts.add ("Bass pattern " + juce::String (s.bassPattern + 1));
    if (s.arp >= 0)
        parts.add (s.arp == 1 ? "Arp on" : "Arp off");
    juce::StringArray muted;
    for (int st = 0; st < RigEngine::numStrips && st < RigScene::kMaxStrips; ++st)
        if (s.stripMutes[(size_t) st] == 1)
            muted.add (RigEngine::stripName (st));
    for (int l = 0; l < RigScene::kLayers; ++l)
        if (s.loopMutes[(size_t) l] == 1)
            muted.add ("loop " + juce::String (l + 1));
    if (s.vocoderOn == 0)
        muted.add ("vocoder off");
    if (! muted.isEmpty())
        parts.add ("Muted: " + muted.joinIntoString (", "));
    return parts.joinIntoString ("  -  ");
}

void SongView::timerCallback()
{
    if ((int) engine.getScenes().getScenes().size() != rows.size())
        refresh();
    for (auto* r : rows)
        r->tick();
}

void SongView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (songColour);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("SONG", getLocalBounds().reduced (14, 8).removeFromTop (34), juce::Justification::centredLeft);
    if (rows.isEmpty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("No parts yet. Get a beat, a bass line or some loops going, then press + Add scene.",
                    getLocalBounds().withTrimmedTop (90).removeFromTop (60), juce::Justification::centred);
    }
}

void SongView::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    auto top = area.removeFromTop (34);
    top.removeFromLeft (80);
    addButton.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (8);
    songMode.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (16);
    back.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (4);
    next.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (4);
    keyboard.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (16);
    help.setBounds (top);
    area.removeFromTop (8);
    viewport.setBounds (area);
    const int w = area.getWidth() - (rows.size() * (kRowHeight + 6) > area.getHeight() ? viewport.getScrollBarThickness() : 0);
    list.setSize (w, rows.size() * (kRowHeight + 6));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * (kRowHeight + 6), w, kRowHeight);
}
