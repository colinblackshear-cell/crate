#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MpcProcessor.h"

// The sequencer's controls, MPC style. The transport bar (sequence, play, stop, record, where
// you are, tempo, bars, metronome) sits across the window; the sequence view replaces the pads
// with a step grid of the 16 pads you're looking at.

class TransportBar : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit TransportBar (MpcProcessor&);
    ~TransportBar() override;

    void refresh();
    std::function<void (const juce::String&, bool warning)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void sequenceMenu();
    void say (const juce::String& t, bool warning = false) { if (onStatus) onStatus (t, warning); }

    MpcProcessor& processor;
    Project& project;

    juce::ComboBox sequence;
    juce::TextButton playStart { "|<" }, play { "Play" }, stop { "Stop" }, rec { "Rec" };
    juce::Label position;
    juce::Label tempoLabel { {}, "Tempo" }, barsLabel { {}, "Bars" };
    juce::Slider tempo, bars;
    juce::ToggleButton loop { "Loop" }, countIn { "Count in" }, followHost { "Play with Logic" };
    juce::ComboBox click;
    bool wasFollowing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

//==============================================================================
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (MpcProcessor&);

    int padBank = 0;
    int bar = 0;                     // which bar is on screen
    seq::Grid grid = seq::Grid::sixteenth;

    int stepsPerBar() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int kLabelWidth = 112;

private:
    juce::Rectangle<int> cell (int row, int step) const;
    int rowAt (int y) const;
    int stepAt (int x) const;
    int stepStart (int step) const;   // tick
    int padForRow (int row) const { return padBank * kPadsPerBank + (kPadsPerBank - 1 - row); }

    MpcProcessor& processor;
    Project& project;

    // Dragging across cells paints (or erases) them all, like a drum machine.
    int dragRow = -1, heldPad = -1;
    bool dragAdds = true;
    std::vector<int> dragDone;
    void setCell (int row, int step, bool on, int velocity);
};

//==============================================================================
class SequencerView : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit SequencerView (MpcProcessor&);
    ~SequencerView() override;

    void setPadBank (int b) { steps.padBank = b; steps.repaint(); }
    void refresh();
    std::function<void (const juce::String&, bool warning)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void trackMenu();
    void exportMidi();
    void importMidi();
    void say (const juce::String& t, bool warning = false) { if (onStatus) onStatus (t, warning); }

    MpcProcessor& processor;
    Project& project;

    juce::ComboBox track;
    juce::ToggleButton mute { "Mute" };
    juce::Label gridLabel { {}, "Timing" }, swingLabel { {}, "Swing" };
    juce::ComboBox gridChoice;
    juce::Slider swing;
    juce::ToggleButton noteRepeat { "Note repeat" };
    juce::TextButton fix { "Fix timing" }, clearTrack { "Clear" }, undo { "Undo" }, redo { "Redo" };
    juce::TextButton prevBar { "<" }, nextBar { ">" };
    juce::Label barLabel;
    juce::TextButton exportButton { "Save MIDI" }, importButton { "Open MIDI" };
    StepGrid steps;
    std::unique_ptr<juce::FileChooser> chooser;
    int lastFollowBar = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerView)
};
