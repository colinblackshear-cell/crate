#include "WaveView.h"

#include "Theme.h"

#include <algorithm>

namespace crate
{

WaveView::WaveView()
{
    setWantsKeyboardFocus (false);
}

void WaveView::setAudio (SharedAudio a, bool keepView)
{
    audio = std::move (a);
    rebuildPeaks();

    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    if (! keepView || viewLength <= 1)
    {
        viewStart = 0;
        viewLength = juce::jmax (1, n);
    }
    clampView();
    selection = selection.getIntersectionWith ({ 0, n });
    markers.erase (std::remove_if (markers.begin(), markers.end(), [n] (int x) { return x >= n; }), markers.end());
    repaint();
}

void WaveView::setSelection (juce::Range<int> r)
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    selection = r.getIntersectionWith ({ 0, n });
    if (r.isEmpty())
        selection = { juce::jlimit (0, n, r.getStart()), juce::jlimit (0, n, r.getStart()) };
    repaint();
}

void WaveView::selectAll()
{
    if (audio != nullptr)
    {
        selection = { 0, audio->getNumSamples() };
        changed();
    }
}

void WaveView::setMarkers (std::vector<int> m)
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    std::sort (m.begin(), m.end());
    m.erase (std::unique (m.begin(), m.end()), m.end());
    m.erase (std::remove_if (m.begin(), m.end(), [n] (int x) { return x <= 0 || x >= n; }), m.end());
    if (m != markers)
    {
        markers = std::move (m);
        repaint();
    }
}

juce::Range<int> WaveView::sliceAt (int sample) const
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    int start = 0, end = n;
    for (int m : markers)
    {
        if (m <= sample)
            start = m;
        else
        {
            end = m;
            break;
        }
    }
    return { start, end };
}

int WaveView::markerAt (float x) const
{
    int best = -1;
    float bestDistance = 6.0f;
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const float d = std::abs (x - xForSample (markers[i]));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = (int) i;
        }
    }
    return best;
}

void WaveView::setPlayhead (int sample)
{
    if (sample != playhead)
    {
        playhead = sample;
        repaint();
    }
}

void WaveView::changed()
{
    repaint();
    if (onSelectionChange)
        onSelectionChange();
}

void WaveView::rebuildPeaks()
{
    peaksMin.clear();
    peaksMax.clear();
    if (audio == nullptr)
        return;

    const int n = audio->getNumSamples();
    const int blocks = (n + kBlock - 1) / kBlock;

    for (int ch = 0; ch < audio->getNumChannels(); ++ch)
    {
        std::vector<float> lo ((size_t) blocks), hi ((size_t) blocks);
        const auto* d = audio->getReadPointer (ch);
        for (int b = 0; b < blocks; ++b)
        {
            const int from = b * kBlock, to = juce::jmin (n, from + kBlock);
            auto range = juce::FloatVectorOperations::findMinAndMax (d + from, to - from);
            lo[(size_t) b] = range.getStart();
            hi[(size_t) b] = range.getEnd();
        }
        peaksMin.push_back (std::move (lo));
        peaksMax.push_back (std::move (hi));
    }
}

float WaveView::xForSample (double sample) const
{
    return (float) ((sample - viewStart) / viewLength * getWidth());
}

int WaveView::sampleForX (float x) const
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    return juce::jlimit (0, n, (int) std::round (viewStart + x / juce::jmax (1, getWidth()) * viewLength));
}

void WaveView::clampView()
{
    const double n = audio != nullptr ? juce::jmax (1, audio->getNumSamples()) : 1;
    viewLength = juce::jlimit (juce::jmin (n, 32.0), n, viewLength);
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, n - viewLength), viewStart);
}

void WaveView::zoom (float factor, float aroundX)
{
    const double anchor = viewStart + aroundX / juce::jmax (1, getWidth()) * viewLength;
    viewLength /= factor;
    clampView();
    viewStart = anchor - aroundX / juce::jmax (1, getWidth()) * viewLength;
    clampView();
    repaint();
}

void WaveView::zoomToFit()
{
    viewStart = 0;
    viewLength = audio != nullptr ? audio->getNumSamples() : 1;
    clampView();
    repaint();
}

void WaveView::zoomToSelection()
{
    if (! hasSelection())
        return zoomToFit();
    const double pad = selection.getLength() * 0.05;
    viewStart = selection.getStart() - pad;
    viewLength = selection.getLength() + 2 * pad;
    clampView();
    repaint();
}

void WaveView::paintRuler (juce::Graphics& g, juce::Rectangle<int> area)
{
    // pick a tick spacing that leaves ~90 px between labels
    const double secondsPerPixel = viewLength / kSampleRate / juce::jmax (1, getWidth());
    const double steps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
    double step = steps[std::size (steps) - 1];
    for (double s : steps)
        if (s / secondsPerPixel >= 90.0)
        {
            step = s;
            break;
        }

    g.setFont (juce::FontOptions (10.5f));
    const double first = std::ceil (viewStart / kSampleRate / step) * step;
    const double last = (viewStart + viewLength) / kSampleRate;
    const int decimals = step < 0.01 ? 3 : step < 0.1 ? 2 : step < 1 ? 1 : 0;

    for (double t = first; t <= last + 1.0e-9; t += step)
    {
        const float x = xForSample (t * kSampleRate);
        g.setColour (Theme::outline);
        g.drawVerticalLine ((int) x, (float) area.getBottom() - 5, (float) area.getBottom());
        g.setColour (Theme::textDim);
        g.drawText (juce::String (t, decimals) + "s", (int) x + 3, area.getY(), 80, area.getHeight() - 2,
                    juce::Justification::centredLeft);
    }
}

void WaveView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

    auto ruler = bounds.removeFromTop (kRulerHeight);
    auto wave = bounds.reduced (0, 4);

    if (audio == nullptr || audio->getNumSamples() == 0 || peaksMax.empty())
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawFittedText (emptyText, wave.reduced (24), juce::Justification::centred, 3);
        return;
    }

    paintRuler (g, ruler);

    // selection
    if (hasSelection())
    {
        const float x1 = xForSample (selection.getStart()), x2 = xForSample (selection.getEnd());
        g.setColour (Theme::accent.withAlpha (0.2f));
        g.fillRect (juce::Rectangle<float> (x1, (float) wave.getY(), x2 - x1, (float) wave.getHeight()));
        g.setColour (Theme::accent);
        g.drawVerticalLine ((int) x1, (float) wave.getY(), (float) wave.getBottom());
        g.drawVerticalLine ((int) x2, (float) wave.getY(), (float) wave.getBottom());
    }

    // one lane per channel
    const int lanes = (int) peaksMax.size();
    const float laneHeight = (float) wave.getHeight() / (float) lanes;
    const double samplesPerPixel = viewLength / juce::jmax (1, getWidth());

    for (int ch = 0; ch < lanes; ++ch)
    {
        const float mid = (float) wave.getY() + laneHeight * ((float) ch + 0.5f);
        const float half = laneHeight * 0.46f;
        g.setColour (Theme::outline.withAlpha (0.6f));
        g.drawHorizontalLine ((int) mid, 0.0f, (float) getWidth());
        g.setColour (Theme::waveform);

        const auto& lo = peaksMin[(size_t) ch];
        const auto& hi = peaksMax[(size_t) ch];
        const auto* data = audio->getReadPointer (ch);
        const int n = audio->getNumSamples();

        for (int x = 0; x < getWidth(); ++x)
        {
            const double from = viewStart + x * samplesPerPixel;
            const double to = from + samplesPerPixel;
            float a = 0, b = 0;

            if (samplesPerPixel < kBlock)
            {
                // zoomed in: read the samples themselves
                const int s0 = juce::jlimit (0, n - 1, (int) from);
                const int s1 = juce::jlimit (s0 + 1, n, (int) std::ceil (to) + 1);
                auto r = juce::FloatVectorOperations::findMinAndMax (data + s0, s1 - s0);
                a = r.getStart();
                b = r.getEnd();
            }
            else
            {
                const int b0 = (int) (from / kBlock);
                const int b1 = juce::jmax (b0 + 1, (int) (to / kBlock));
                a = 1.0f; b = -1.0f;
                for (int k = b0; k < b1 && k < (int) hi.size(); ++k)
                {
                    a = juce::jmin (a, lo[(size_t) k]);
                    b = juce::jmax (b, hi[(size_t) k]);
                }
                if (a > b)
                    a = b = 0;
            }

            a = juce::jlimit (-1.0f, 1.0f, a);
            b = juce::jlimit (-1.0f, 1.0f, b);
            g.fillRect ((float) x, mid - b * half, 1.0f, juce::jmax (1.0f, (b - a) * half));
        }
    }

    // chop markers, with slice numbers in the ruler
    if (! markers.empty())
    {
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (size_t i = 0; i <= markers.size(); ++i)
        {
            const int start = i == 0 ? 0 : markers[i - 1];
            const float x = xForSample (start);
            const bool removing = drag == Drag::marker && dragMarkerOff && (int) i - 1 == dragMarker;
            if (i > 0)
            {
                g.setColour (removing ? Theme::record.withAlpha (0.5f) : Theme::text.withAlpha (0.8f));
                g.drawLine (x, (float) ruler.getY() + 2, x, (float) wave.getBottom(), 1.2f);
            }
            if (x > -30 && x < (float) getWidth())
            {
                auto flag = juce::Rectangle<float> (x, (float) ruler.getY() + 2, 22.0f, (float) kRulerHeight - 4);
                g.setColour (removing ? Theme::record : Theme::text);
                g.fillRoundedRectangle (flag, 3.0f);
                g.setColour (Theme::panel);
                g.drawText (juce::String ((int) i + 1), flag, juce::Justification::centred);
            }
        }
    }

    // cursor
    if (! hasSelection())
    {
        g.setColour (Theme::accent);
        const float x = xForSample (selection.getStart());
        g.drawLine (x, (float) wave.getY(), x, (float) wave.getBottom(), 1.0f);
    }

    if (playhead >= 0)
    {
        g.setColour (Theme::text);
        const float x = xForSample (playhead);
        g.drawLine (x, (float) ruler.getY(), x, (float) wave.getBottom(), 1.5f);
    }

    // a scroll bar when zoomed in
    const double n = audio->getNumSamples();
    if (viewLength < n)
    {
        auto bar = getLocalBounds().removeFromBottom (4).toFloat();
        g.setColour (Theme::raised);
        g.fillRect (bar);
        g.setColour (Theme::textDim);
        g.fillRect (bar.withX ((float) (viewStart / n) * bar.getWidth()).withWidth (juce::jmax (4.0f, (float) (viewLength / n) * bar.getWidth())));
    }
}

int WaveView::edgeAt (float x) const
{
    if (! hasSelection())
        return -1;
    if (std::abs (x - xForSample (selection.getStart())) < 6)
        return 0;
    if (std::abs (x - xForSample (selection.getEnd())) < 6)
        return 1;
    return -1;
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    const bool inRuler = e.position.y < kRulerHeight;
    if (edgeAt (e.position.x) >= 0 || (markersEditable && markerAt (e.position.x) >= 0))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (markersEditable && inRuler)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::IBeamCursor);
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;

    const int s = sampleForX (e.position.x);
    const int edge = edgeAt (e.position.x);
    const bool inRuler = e.position.y < kRulerHeight;

    if (markersEditable)
    {
        const int marker = markerAt (e.position.x);
        if (marker >= 0 && (e.mods.isAltDown() || e.mods.isPopupMenu()))
        {
            markers.erase (markers.begin() + marker);
            repaint();
            if (onMarkersChange)
                onMarkersChange();
            return;
        }
        if (marker >= 0 && (inRuler || edge < 0))
        {
            drag = Drag::marker;
            dragMarker = marker;
            dragMarkerOff = false;
            return;
        }
        if (inRuler && s > 0 && s < audio->getNumSamples())
        {
            markers.push_back (s);
            std::sort (markers.begin(), markers.end());
            dragMarker = (int) (std::find (markers.begin(), markers.end(), s) - markers.begin());
            dragMarkerOff = false;
            drag = Drag::marker;
            repaint();
            return;
        }
    }

    if (edge == 0)
    {
        drag = Drag::startEdge;
    }
    else if (edge == 1)
    {
        drag = Drag::endEdge;
    }
    else if (e.mods.isShiftDown())
    {
        // extend from whichever end is further away
        const bool fromStart = std::abs (s - selection.getStart()) > std::abs (s - selection.getEnd());
        dragAnchor = fromStart ? selection.getStart() : selection.getEnd();
        selection = { juce::jmin (dragAnchor, s), juce::jmax (dragAnchor, s) };
        drag = Drag::select;
        changed();
    }
    else
    {
        dragAnchor = s;
        selection = { s, s };
        drag = Drag::select;
        changed();
    }
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr || drag == Drag::none)
        return;

    const int s = sampleForX (e.position.x);

    if (drag == Drag::marker)
    {
        if (dragMarker < 0 || dragMarker >= (int) markers.size())
            return;
        // stays between its neighbours
        const int lo = dragMarker > 0 ? markers[(size_t) dragMarker - 1] + 1 : 1;
        const int hi = dragMarker + 1 < (int) markers.size() ? markers[(size_t) dragMarker + 1] - 1 : audio->getNumSamples() - 1;
        markers[(size_t) dragMarker] = juce::jlimit (lo, juce::jmax (lo, hi), s);
        dragMarkerOff = e.position.y < -24 || e.position.y > getHeight() + 24;
        repaint();
        return;
    }

    if (drag == Drag::startEdge)
        selection = { juce::jmin (s, selection.getEnd()), juce::jmax (s, selection.getEnd()) };
    else if (drag == Drag::endEdge)
        selection = { juce::jmin (s, selection.getStart()), juce::jmax (s, selection.getStart()) };
    else
        selection = { juce::jmin (dragAnchor, s), juce::jmax (dragAnchor, s) };

    // scroll when dragging past the edges
    if (e.position.x < 0 || e.position.x > getWidth())
    {
        viewStart += (e.position.x < 0 ? -0.02 : 0.02) * viewLength;
        clampView();
    }

    changed();
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::marker)
    {
        if (dragMarkerOff && dragMarker >= 0 && dragMarker < (int) markers.size())
            markers.erase (markers.begin() + dragMarker);
        drag = Drag::none;
        dragMarker = -1;
        dragMarkerOff = false;
        repaint();
        if (onMarkersChange)
            onMarkersChange();
        return;
    }
    drag = Drag::none;
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // with chop markers, a double-click selects the slice under the mouse
    if (audio != nullptr && ! markers.empty() && e.position.y >= kRulerHeight)
    {
        selection = sliceAt (sampleForX (e.position.x));
        changed();
        return;
    }
    selectAll();
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (audio == nullptr)
        return;

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        viewStart -= delta * viewLength * 0.5;
        clampView();
        repaint();
    }
    else
    {
        zoom (juce::jlimit (0.5f, 2.0f, 1.0f + wheel.deltaY * 1.5f), e.position.x);
    }
}

void WaveView::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    zoom (scale, e.position.x);
}

} // namespace crate
