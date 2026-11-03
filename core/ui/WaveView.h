#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Audio.h"

namespace crate
{

// A zoomable waveform with a selection, a cursor and a playhead. Stereo sounds show as two lanes.
//  - click to put the cursor somewhere; drag to select; drag a selection's edge to adjust it
//  - shift-click to extend the selection
//  - scroll wheel or pinch to zoom, shift + wheel (or a sideways swipe) to scroll
//
// It can also show chop markers (where slices start). When they're editable: click the ruler to
// add one, drag one to move it, and Option-click, right-click or drag it off the top or bottom
// to remove it. Double-clicking inside a slice selects that slice.
class WaveView : public juce::Component
{
public:
    WaveView();

    void setAudio (SharedAudio, bool keepView = false);
    const SharedAudio& getAudio() const { return audio; }

    // An empty selection is the cursor position.
    juce::Range<int> getSelection() const { return selection; }
    void setSelection (juce::Range<int>);
    bool hasSelection() const { return selection.getLength() > 0; }
    void selectAll();

    void setPlayhead (int sample);   // -1 hides it
    void setEmptyText (const juce::String& t) { emptyText = t; repaint(); }

    void zoomIn() { zoom (2.0f, getWidth() * 0.5f); }
    void zoomOut() { zoom (0.5f, getWidth() * 0.5f); }
    void zoomToFit();
    void zoomToSelection();

    std::function<void()> onSelectionChange;

    // Chop markers: sample positions where slices start (0 is implied), kept sorted.
    void setMarkers (std::vector<int>);
    const std::vector<int>& getMarkers() const { return markers; }
    void setMarkersEditable (bool shouldBeEditable) { markersEditable = shouldBeEditable; }
    // The slice (between markers) around a sample position.
    juce::Range<int> sliceAt (int sample) const;
    std::function<void()> onMarkersChange;   // after the user adds, moves or removes one

    void paint (juce::Graphics&) override;
    void resized() override { repaint(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

private:
    static constexpr int kRulerHeight = 20;
    static constexpr int kBlock = 128;

    float xForSample (double sample) const;
    int sampleForX (float x) const;
    int edgeAt (float x) const;   // 0 = start, 1 = end, -1 = none
    int markerAt (float x) const; // index, or -1
    void zoom (float factor, float aroundX);
    void clampView();
    void rebuildPeaks();
    void changed();
    void paintRuler (juce::Graphics&, juce::Rectangle<int>);

    SharedAudio audio;
    std::vector<std::vector<float>> peaksMin, peaksMax;   // per channel, per kBlock samples

    double viewStart = 0, viewLength = 1;    // in samples
    juce::Range<int> selection;
    int playhead = -1;
    juce::String emptyText { "Record something, open a file, or pick a sound from the library" };

    enum class Drag { none, select, startEdge, endEdge, marker };
    Drag drag = Drag::none;
    int dragAnchor = 0;

    std::vector<int> markers;
    bool markersEditable = false;
    int dragMarker = -1;
    bool dragMarkerOff = false;   // dragged off the top or bottom: removed on release

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveView)
};

} // namespace crate
