#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Audio.h"

// The waveform well: amber bars on a dark panel, a selection with two draggable handles, a
// playhead and chop markers. In marker mode a tap adds a marker (or removes the one you tap).
class PocketWave : public juce::Component
{
public:
    enum class Mode { select, markers };

    void setAudio (crate::SharedAudio);
    const crate::SharedAudio& getAudio() const { return audio; }
    void setTitle (juce::String t) { title = std::move (t); repaint(); }
    void setMode (Mode m) { mode = m; }
    void setCompact (bool c) { compact = c; repaint(); }
    void setEmptyText (juce::String t) { emptyText = std::move (t); repaint(); }

    juce::Range<int> getSelection() const { return selection; }
    void setSelection (juce::Range<int>);
    void selectAll();
    void setPlayhead (int sample) { if (sample != playhead) { playhead = sample; repaint(); } }
    void setMarkers (std::vector<int> m) { markers = std::move (m); repaint(); }
    const std::vector<int>& getMarkers() const { return markers; }

    std::function<void()> onSelectionChange, onMarkersChange;

    void paint (juce::Graphics&) override;
    void resized() override { peaksFor = -1; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> area() const;
    float xFor (int sample) const;
    int sampleFor (float x) const;
    void rebuildPeaks();

    crate::SharedAudio audio;
    juce::String title, emptyText { "Tap REC to record a sound" };
    Mode mode = Mode::select;
    bool compact = false;
    juce::Range<int> selection;
    int playhead = -1;
    std::vector<int> markers;
    std::vector<float> peaks;
    int peaksFor = -1;

    enum class Drag { none, start, end, fresh };
    Drag drag = Drag::none;
    int anchor = 0;
    bool moved = false;
};
