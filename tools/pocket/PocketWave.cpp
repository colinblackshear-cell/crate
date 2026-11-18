#include "PocketWave.h"

#include "PocketTheme.h"

using namespace pocket;

static constexpr float kBarPitch = 4.0f;

juce::Rectangle<float> PocketWave::area() const
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 0.0f);
    return r.withTrimmedTop (compact ? 6.0f : 26.0f).withTrimmedBottom (compact ? 8.0f : 22.0f);
}

float PocketWave::xFor (int sample) const
{
    const int n = audio != nullptr ? juce::jmax (1, audio->getNumSamples()) : 1;
    auto a = area();
    return a.getX() + a.getWidth() * (float) sample / (float) n;
}

int PocketWave::sampleFor (float x) const
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    auto a = area();
    return juce::jlimit (0, n, (int) std::round ((x - a.getX()) / a.getWidth() * (float) n));
}

void PocketWave::setAudio (crate::SharedAudio a)
{
    audio = std::move (a);
    selection = {};
    playhead = -1;
    markers.clear();
    peaksFor = -1;
    repaint();
}

void PocketWave::setSelection (juce::Range<int> r)
{
    const int n = audio != nullptr ? audio->getNumSamples() : 0;
    selection = { juce::jlimit (0, n, r.getStart()), juce::jlimit (0, n, r.getEnd()) };
    repaint();
}

void PocketWave::selectAll()
{
    if (audio != nullptr)
        setSelection ({ 0, audio->getNumSamples() });
}

void PocketWave::rebuildPeaks()
{
    peaks.clear();
    peaksFor = juce::jmax (1, (int) (area().getWidth() / kBarPitch));
    if (audio == nullptr || audio->getNumSamples() == 0)
        return;
    const int n = audio->getNumSamples(), channels = audio->getNumChannels();
    peaks.assign ((size_t) peaksFor, 0.0f);
    float top = 0.0001f;
    for (int i = 0; i < peaksFor; ++i)
    {
        const int a = (int) ((juce::int64) n * i / peaksFor), b = juce::jmax (a + 1, (int) ((juce::int64) n * (i + 1) / peaksFor));
        float m = 0;
        for (int c = 0; c < channels; ++c)
        {
            const auto mm = audio->findMinMax (c, a, b - a);
            m = juce::jmax (m, mm.getEnd(), -mm.getStart());
        }
        peaks[(size_t) i] = m;
        top = juce::jmax (top, m);
    }
    for (auto& p : peaks)
        p = std::pow (p / top, 0.8f);
}

void PocketWave::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    drawWell (g, bounds, 16.0f);

    if (title.isNotEmpty() && ! compact)
    {
        g.setColour (col::inkFaint);
        g.setFont (spaced (condensedBold (12.0f), 0.1f));
        g.drawText (title, bounds.withTrimmedLeft (14).withHeight (24).translated (0, 4), juce::Justification::centredLeft);
    }

    if (audio == nullptr || audio->getNumSamples() == 0)
    {
        g.setColour (col::inkFaint);
        g.setFont (small (14.0f));
        g.drawText (emptyText, bounds, juce::Justification::centred);
        return;
    }

    if (peaksFor < 0)
        rebuildPeaks();

    auto a = area();
    const float mid = a.getCentreY(), half = a.getHeight() * 0.5f;
    const bool hasSel = selection.getLength() > 0;
    const float sx0 = hasSel ? xFor (selection.getStart()) : 0.0f, sx1 = hasSel ? xFor (selection.getEnd()) : 0.0f;

    if (hasSel)
    {
        g.setColour (col::amber.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (sx0, a.getY() - 4, sx1 - sx0, a.getHeight() + 8));
    }

    for (int i = 0; i < (int) peaks.size(); ++i)
    {
        const float x = a.getX() + (float) i * (a.getWidth() / (float) peaks.size());
        const float h = juce::jmax (2.0f, peaks[(size_t) i] * half * 0.92f);
        const bool inside = ! hasSel || (x >= sx0 && x <= sx1);
        g.setColour (inside ? col::amber : juce::Colour (0xff9a6c28));
        g.fillRoundedRectangle (x, mid - h, 2.6f, h * 2, 1.2f);
    }

    // chop markers
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    for (int m : markers)
    {
        const float x = xFor (m);
        g.fillRect (x - 0.75f, a.getY() - 2, 1.5f, a.getHeight() + 4);
        juce::Path flag;
        flag.addTriangle (x - 5, a.getY() - 3, x + 5, a.getY() - 3, x, a.getY() + 5);
        g.fillPath (flag);
    }

    if (hasSel)
    {
        g.setColour (col::amber);
        for (float x : { sx0, sx1 })
        {
            g.fillRect (x - 1.0f, a.getY() - 4, 2.0f, a.getHeight() + 8);
            g.fillRoundedRectangle (x - 8.5f, a.getBottom() - 6, 17, 17, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.drawRoundedRectangle (x - 8.5f, a.getBottom() - 6, 17, 17, 4.0f, 1.0f);
            g.setColour (col::amber);
        }
    }

    if (playhead >= 0)
    {
        g.setColour (juce::Colours::white);
        g.fillRect (xFor (playhead) - 1.0f, a.getY() - 4, 2.0f, a.getHeight() + 8);
    }
}

void PocketWave::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    moved = false;
    if (audio == nullptr || audio->getNumSamples() == 0)
        return;

    if (mode == Mode::markers)
        return;

    const float x = e.position.x;
    if (selection.getLength() > 0)
    {
        const float d0 = std::abs (x - xFor (selection.getStart())), d1 = std::abs (x - xFor (selection.getEnd()));
        if (juce::jmin (d0, d1) < 26.0f)
        {
            drag = d0 <= d1 ? Drag::start : Drag::end;
            return;
        }
    }
    drag = Drag::fresh;
    anchor = sampleFor (x);
}

void PocketWave::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || audio == nullptr)
        return;
    moved = moved || e.getDistanceFromDragStart() > 4;
    const int s = sampleFor (e.position.x);
    const int minLen = 200;
    if (drag == Drag::start)
        selection = { juce::jmin (s, selection.getEnd() - minLen), selection.getEnd() };
    else if (drag == Drag::end)
        selection = { selection.getStart(), juce::jmax (s, selection.getStart() + minLen) };
    else if (moved)
        selection = { juce::jmin (anchor, s), juce::jmax (anchor, s) };
    selection = { juce::jmax (0, selection.getStart()), juce::jmin (audio->getNumSamples(), selection.getEnd()) };
    repaint();
    if (onSelectionChange)
        onSelectionChange();
}

void PocketWave::mouseUp (const juce::MouseEvent& e)
{
    if (audio == nullptr || audio->getNumSamples() == 0)
        return;

    if (mode == Mode::markers)
    {
        if (e.getDistanceFromDragStart() > 8)
            return;
        const float x = e.position.x;
        for (size_t i = 0; i < markers.size(); ++i)
        {
            if (std::abs (xFor (markers[i]) - x) < 16.0f)
            {
                markers.erase (markers.begin() + (long) i);
                repaint();
                if (onMarkersChange)
                    onMarkersChange();
                return;
            }
        }
        const int s = sampleFor (x);
        if (s > 0 && s < audio->getNumSamples())
        {
            markers.push_back (s);
            std::sort (markers.begin(), markers.end());
            repaint();
            if (onMarkersChange)
                onMarkersChange();
        }
        return;
    }

    if (drag == Drag::fresh && ! moved)
    {
        selection = {};   // a tap clears the selection
        repaint();
        if (onSelectionChange)
            onSelectionChange();
    }
    drag = Drag::none;
}
