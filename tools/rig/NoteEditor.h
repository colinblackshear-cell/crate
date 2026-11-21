#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RigEngine.h"

#include <set>

// Edit notes: the notes of a notes take (a clip of the Bass, the Synth, the Sampler, the Drum
// machine or the MPC in one section of the song), on a grid of bars and beats, with a key for
// every row.
//
// Click a note to pick it (Shift adds to the pick), drag it to move it in time and pitch, drag
// its right end to make it longer or shorter, double-click an empty place to add one, double-
// click a note to delete it, drag across empty space to pick several. Along the bottom, drag a
// note's bar to set how hard it's hit. Arrow keys move the picked notes (Shift: an octave),
// Delete deletes them, Cmd+Z undoes. Every change plays straight away: the song plays the clip.
class NoteEditor : public juce::Component, private juce::Timer
{
public:
    NoteEditor (RigEngine&, int strip, double startBar);
    ~NoteEditor() override;

    bool isValid() const;   // false once the clip is gone (a new song)

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Snap, in beats (0: free).
    static double snapBeats (int choice, int beatsPerBar);

    // For the tests (and the buttons).
    void selectAll();
    void quantizeSelected (double grid, float strength);
    void transposeSelected (int semitones);
    void deleteSelected();
    void addNote (double beat, int note, double length);
    void undo();
    const std::vector<rignotes::Note>& getNotes() const;
    std::vector<int> getSelection() const { return { selected.begin(), selected.end() }; }

private:
    class Grid;
    void timerCallback() override;
    rignotes::Clip* clip();
    const rignotes::Clip* clip() const;
    void edit (std::function<void (std::vector<rignotes::Note>&)>);   // with undo
    void changed();
    void updateText();
    void showQuantizeMenu();

    RigEngine& engine;
    const int strip;
    const double startBar;
    std::vector<std::vector<rignotes::Note>> undos;
    std::set<int> selected;

    std::unique_ptr<Grid> grid;
    juce::Label title, info, hint;
    juce::ComboBox snap;
    juce::TextButton quantize { "Quantize..." }, down12 { "-12" }, down1 { "-1" }, up1 { "+1" }, up12 { "+12" },
                     all { "Pick all" }, remove { "Delete" }, undoButton { "Undo" }, play { "Play" }, done { "Done" };

    friend class Grid;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEditor)
};
