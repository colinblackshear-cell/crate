#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Spark.h"

#include <map>

// My ideas: the Mac's home page, the same as the phone's (Pocket's IdeasScreen), laid out wider.
//  - MY IDEAS, how many songs moved this week and how many layers they hold; OPEN A FILE, IMPORT FROM LOGIC, SETTINGS.
//  - PICK UP WHERE YOU LEFT OFF: the song that's open (or your last one), its tempo and sections, and its layers drawn
//    as lanes (their real sound), with PLAY and OPEN AND KEEP GOING.
//  - CATCH AN IDEA (your mic straight onto layer 1 of a new song, free length) and + NEW EMPTY SONG.
//  - TONIGHT'S SPARK (crate::spark, the same spark as the phone's tonight): START THIS SPARK, ROLL ANOTHER.
//  - START A SONG FROM ANY TOOL: eight tools.
//  - HOW IT WORKS (fold it away), SHOW ME: MAKE A BEAT, TAKE THE TOUR and EVERY TOOL (the Guide).
//  - YOUR SONGS: the shows in ~/Music/Crate/Rig, with search (a name, a tool or a tempo) and RECENT / STARRED / A-Z /
//    MOST LAYERS. Each with its date, tempo, layers and sections, a strip of its layers' colours, a play button, and
//    ··· (or a right-click) for STAR, RENAME, SHOW IN FINDER and MOVE TO TRASH.
class RigIdeas : public juce::Component
{
public:
    // A saved song, as the list shows it (read from its .crateshow).
    struct Song
    {
        juce::File file;
        juce::String name;
        juce::Time modified;
        double bpm = 0.0;
        int sections = 0;
        std::vector<int> sources;     // each layer's RigLooper::Source
        std::vector<double> beats;    // each layer's length
        std::vector<int> indexes;     // each layer's loop number (0-15)
        juce::String revision;        // the show's audioRevision (which folder holds its loops)
        bool starred = false, lastShow = false;
        int layers() const { return (int) sources.size(); }
    };
    static Song read (const juce::File& showFile);
    // A saved loop's WAV ("<show> loops/Loop N.wav", or the revision folder the show names).
    static juce::File loopFile (const Song&, int layer);
    // Renames a show and its loop folders (the show last, so a stop part way leaves it whole).
    // Returns why it couldn't (empty: done), with the new file in `renamed`.
    static juce::String renameSong (const juce::File& show, const juce::String& newName, juce::File& renamed);
    // Moves a show and its loop folders to the Trash. False if any of it couldn't go.
    static bool trashSong (const juce::File& show);
    static juce::String whenText (juce::Time);
    static juce::Colour colourForSource (int source);
    static juce::String nameForSource (int source);   // "DRUMS"

    // PICK UP WHERE YOU LEFT OFF: the window says what's open.
    struct Hero
    {
        juce::String name, meta, lcd;
        std::vector<int> sources;
        std::vector<std::vector<float>> peaks;   // each layer's sound (empty: not saved yet)
        juce::File file;                         // the song to open (none: it's the one open now)
        bool open = false;                       // it's the song open now
    };

    explicit RigIdeas (juce::File songsFolder);
    ~RigIdeas() override;

    std::function<Hero()> getHero;
    std::function<void (const juce::File&)> onOpen, onPlay, onRename, onTrash;
    std::function<void()> onPlayHero, onOpenHero, onCatch, onNew, onLesson, onTour, onGuide, onOpenFile, onImportLogic, onSettings;
    std::function<void (const crate::spark::Spark&)> onSpark;
    std::function<void (int tile)> onStartWith;   // 0 a beat, 1 pads, 2 a melody, 3 chords, 4 my voice, 5 a sample, 6 a bass line, 7 line in
    std::function<bool (const juce::File&)> isPlaying;

    void refresh();   // reads the songs folder again (each show only when it changed) and what's open
    void resized() override;
    void paint (juce::Graphics&) override;

    // The tests and screenshots.
    const std::vector<Song>& songs() const { return all; }
    std::vector<int> order() const;   // the list in the filter, search and sort
    const crate::spark::Spark& spark() const { return sparkNow; }
    void rollSpark();
    void setFilter (int);   // 0 recent, 1 starred, 2 A-Z, 3 most layers
    void setSearch (const juce::String&);
    void setStarred (const juce::File&, bool);
    void setHowItWorksOpen (bool);
    bool howItWorksOpen() const { return howOpen; }
    juce::Button* button (const juce::String& name) const;   // "catch", "new", "spark", "roll", "open", "play", "lesson", "tour", "guide", "file", "logic", "settings"
    void pressTile (int tile);
    void clickSong (int row);     // what a click on that row does
    juce::PopupMenu menuFor (int row);   // its ··· menu
    int songCardsTop() const;     // where the list starts, in this component (the screenshots)
    static constexpr int kTiles = 8;

private:
    class Btn;
    struct Content;
    struct List;
    void loadStars();
    void saveStars() const;
    void layoutChildren();

    juce::File folder;
    std::vector<Song> all;
    std::map<juce::String, Song> cache;   // by path, kept while the file doesn't change
    juce::StringArray starred;
    Hero hero;
    crate::spark::Spark sparkNow;
    int sparkRolls = 0, filter = 0;
    bool howOpen = true;
    juce::String search;

    juce::Viewport view;
    std::unique_ptr<Content> content;
    std::unique_ptr<List> list;
    juce::TextEditor searchBox;
    std::map<juce::String, std::unique_ptr<Btn>> buttons;
    std::vector<std::unique_ptr<Btn>> chips, tiles;
};
