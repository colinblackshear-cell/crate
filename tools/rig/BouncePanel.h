#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RigBounce.h"
#include "RigEngine.h"
#include "Vintage.h"

// The Bounce + stems panel (the Song screen's Bounce button): what to bounce, how to split it, the format,
// as heard or dry, and the tail; then BOUNCE. Its LCD says what it will write. See RigBounce.h.
class BouncePanel : public juce::Component
{
public:
    BouncePanel (RigEngine&, RigBounce::Options start);

    void paint (juce::Graphics&) override;
    void resized() override;

    const RigBounce::Options& getOptions() const { return options; }
    // As the buttons do (the tests use these).
    void pickWhat (RigBounce::What);
    void pickSection (int);
    void pickSplit (RigBounce::Split);
    void setFormats (bool wav, bool mp3);
    void pressBounce();
    // Over a friend's track (its row shows only then): the three ways to send it back.
    enum class SendBack { myPartsEach, myPartsMixed, everythingToListen };
    void pickSendBack (SendBack);
    bool showsTrackRow() const { return trackRow; }

    std::function<void (const RigBounce::Options&)> onBounce;   // the panel closes after it
    std::function<void (const RigBounce::Options&)> onChange;   // remembered for next time

    static constexpr int kWidth = 620, kHeight = 340, kTrackRowHeight = 48;

private:
    void update();

    RigEngine& engine;
    RigBounce::Options options;
    juce::TextButton songButton { "Song" }, loopsButton { "Loops playing now" };
    juce::ComboBox sectionBox;
    juce::TextButton mixButton { "Mix" }, eachButton { "Each loop + mix" }, allButton { "Everything" };
    juce::TextButton wavButton { "WAV" }, mp3Button { "MP3" }, dryButton { "Dry" };
    juce::ComboBox tailBox;
    juce::TextButton bounceButton { "Bounce" };
    juce::TextButton eachPartButton { "My parts, each" }, mixedPartsButton { "My parts mixed" }, listenButton { "All, to listen" };
    bool trackRow = false;
    juce::Rectangle<int> titleArea, whatLabel, splitLabel, formatLabel, trackLabel, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BouncePanel)
};
