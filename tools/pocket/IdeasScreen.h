#pragma once

#include "PocketFrame.h"
#include "PocketHold.h"
#include "PocketIdeas.h"
#include "PocketKey.h"
#include "PocketTheme.h"

#include "core/Spark.h"

#include <map>

// My ideas, the home page (one page that scrolls), the same as the Mac's:
//  - MY IDEAS, with how many songs moved this week and how many layers they hold.
//  - CATCH AN IDEA (the mic straight onto layer 1 of a new song, free length) and + NEW SONG.
//  - PICK UP WHERE YOU LEFT OFF: the open song, its tempo and key, and its layers drawn as lanes (their real sound),
//    with PLAY and OPEN AND KEEP GOING.
//  - TONIGHT'S SPARK (crate::spark): a mood, tempo, key, kit and three steps. START THIS SPARK, ROLL ANOTHER.
//  - START A SONG FROM ANY TOOL: a row of tools (slide it).
//  - YOUR SONGS: search (a name, a tool or a tempo) and RECENT / STARRED / A-Z / MOST LAYERS / SENT TO MAC. Each song
//    with its date, tempo, layers and a strip of its layers' colours (as long as each layer is), and a play button.
//    Tap one to carry on; ··· (or hold it) for STAR, RENAME and DELETE.
//  - HOW IT WORKS (fold it away once you know it), SHOW ME: MAKE A BEAT and WATCH THE TOUR.
class IdeasScreen : public juce::Component
{
public:
    explicit IdeasScreen (pocket::Ideas&);
    ~IdeasScreen() override;

    std::function<void (const juce::String& id)> onOpen;
    std::function<void()> onNew, onNewBeat, onTour, onCatch;
    std::function<void (const crate::spark::Spark&)> onSpark;      // START THIS SPARK
    std::function<void (pocket::Tool, bool voice)> onStartWith;           // a tool tile (MY VOICE: LOOPS on the mic)
    std::function<void (const juce::String& id)> onPlay;        // play (or stop) what the song has so far
    std::function<bool (const juce::String& id)> isPlaying;
    // Hold a song (or tap its ···): the app opens the hold menu (STAR, RENAME, DELETE) at `at`, keeping `highlight` (the card) bright.
    std::function<void (const juce::String& id, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight)> onHoldSong;

    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

    // What the list shows of a song, read from its folder (Loops/loops.json, Beats/drums.json, song.json).
    struct Facts
    {
        double bpm = 0.0;
        int layers = 0;
        std::vector<int> tools;       // pocket::LayerTool, one per layer
        std::vector<double> beats;    // each layer's length
        std::vector<juce::String> files;   // each layer's sound (Loops/<file>)
        bool hasKey = false;
        pocket::SongKey key;
    };
    static Facts factsOf (const juce::File& songFolder);
    static juce::String whenText (juce::Time);   // "Today 3:20 PM", "Yesterday", "Mon", "Oct 12"
    juce::Rectangle<int> tourSpot() const;       // + NEW SONG, in this screen's coordinates
    void holdRow (int row);   // what a long press on that row does (the self-checks and screenshots)
    juce::String songIdAtRow (int row) const;   // the song on that row, in the picked filter and sort
    int rows() const { return (int) order().size(); }

    // The spark on show (tonight's, plus the rolls) and the filter (0 recent, 1 starred, 2 A-Z, 3 most layers, 4 sent to Mac).
    const crate::spark::Spark& spark() const { return sparkNow; }
    void rollSpark();
    void setFilter (int);
    void setSearch (const juce::String&);
    void setHowItWorksOpen (bool);
    bool howItWorksOpen() const { return howOpen; }
    void scrollTo (int y);
    int contentHeight() const;
    // The self-checks press these.
    pocket::PButton& catchButton() { return catchIdea; }
    pocket::PButton& sparkButton() { return startSpark; }
    pocket::PButton& openButton() { return openCurrent; }
    void pressTile (int tile);

    static int lanePeaksRead();   // how many layer sounds were read for lanes (the cache keeps it down)

private:
    struct Content;
    struct List;
    struct Tiles;
    std::vector<int> order() const;   // the list in the picked filter, search and sort
    const pocket::IdeaInfo* hero() const;

    pocket::Ideas& ideas;
    std::map<juce::String, Facts> facts;
    std::vector<std::vector<float>> heroPeaks;   // the open song's layers, for its lanes
    crate::spark::Spark sparkNow;
    int sparkRolls = 0, filter = 0;
    bool howOpen = true;
    juce::String search;

    juce::Viewport view, tileView;
    std::unique_ptr<Content> content;
    std::unique_ptr<List> list;
    std::unique_ptr<Tiles> tiles;
    juce::TextEditor searchBox;
    pocket::PButton catchIdea { "Catch an idea", pocket::Look::red }, newButton { "New song", pocket::Look::amber },
        openCurrent { "Open and keep going", pocket::Look::amber }, playCurrent { {}, pocket::Look::cream },
        startSpark { "Start this spark", pocket::Look::dark }, rollButton { "Another", pocket::Look::ghost },
        lessonButton { "Show me: make a beat", pocket::Look::dark }, tourButton { "Watch the tour", pocket::Look::dark },
        howToggle { {}, pocket::Look::dark };
    std::vector<std::unique_ptr<pocket::PButton>> chips;
};
