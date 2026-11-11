#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The Guide: the Rig's front page. A night sky with the title, a few friendly words, and an
// entry for every tool: a little picture of it, what it is, what it's for, and a click to go
// in. The five screens come first as big entries; the tools under More follow, smaller.
//
// The entries are a table (entries()). A new tool gets one line there and a picture (or the
// generic one); entries whose page the Rig doesn't have are left out, so the table can run
// ahead of the tools.
class GuideView : public juce::Component,
                  private juce::Timer
{
public:
    // The little pictures. `generic` is for a tool that has none yet.
    enum class Art { groove, vocoder, looper, sampler, stage, pads, bass, synth, song, sounds, mixer, editor, drumMachine, generic };

    struct Entry
    {
        juce::String page;      // the page to open, as the Rig names it ("Groove", "MPC"...)
        juce::String title;     // as the entry shows it
        juce::String tagline;   // one line under the title
        juce::String blurb;     // two or three sentences: what it is and what it's for
        juce::Colour colour;    // the tool's colour (RigColours)
        Art art = Art::generic;
        bool main = false;      // one of the five screens (F1-F5), shown big
        juce::String key;       // its key, if it has one ("F1")
    };
    static const std::vector<Entry>& entries();

    // `hasPage` says which pages the Rig has; entries for others are left out.
    explicit GuideView (std::function<bool (const juce::String& page)> hasPage);
    ~GuideView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // A click on an entry: open its page.
    std::function<void (const juce::String& page)> onOpen;
    // The how-to guides (the ? button's).
    std::function<void()> onHelp;
    // "Open the Rig here": whether the Rig starts on the Guide, and its switch.
    std::function<bool()> startsHere;
    std::function<void (bool)> setStartsHere;

    // The entries shown, in order, and a click on one (the tests use these).
    const std::vector<Entry>& getShown() const { return shown; }
    void open (int shownIndex);
    juce::Component* getCard (int shownIndex) const;
    void refresh();

    // Draws an entry's picture (also used by the tests' snapshot, and free for other screens).
    static void drawArt (juce::Graphics&, Art, juce::Rectangle<float>, juce::Colour, float phase);

private:
    class Card;
    class Content;
    void timerCallback() override;

    std::vector<Entry> shown;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    float phase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuideView)
};
