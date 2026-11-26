#include "DrumKitSheet.h"

#include "PocketKit.h"

using namespace pocket;

constexpr int kRowH = 56;

class DrumKitSheet::List : public juce::Component
{
public:
    explicit List (DrumKitSheet& o) : owner (o) {}

    void load (const std::vector<SoundItem>& all)
    {
        items = all;
        std::stable_sort (items.begin(), items.end(), [] (const SoundItem& a, const SoundItem& b) { return a.date > b.date; });
        setSize (juce::jmax (1, getWidth()), juce::jmax (kRowH, (int) items.size() * kRowH));
        repaint();
    }

    int count() const { return (int) items.size(); }

    void paint (juce::Graphics& g) override
    {
        if (items.empty())
        {
            g.setColour (col::inkDim);
            g.setFont (small (15.0f));
            g.drawFittedText ("No sounds yet. Record one, or bring some in (menu > Sounds), and they show up here.",
                              getLocalBounds().reduced (12, 8), juce::Justification::topLeft, 3);
            return;
        }
        for (int i = 0; i < (int) items.size(); ++i)
        {
            auto r = juce::Rectangle<float> (0.0f, (float) (i * kRowH), (float) getWidth(), (float) kRowH).reduced (0.0f, 3.0f);
            if (! r.intersects (g.getClipBounds().toFloat()))
                continue;
            const auto& item = items[(size_t) i];
            const bool on = item.id == owner.sourceId;
            g.setColour (on ? col::creamLight : col::cream);
            g.fillRoundedRectangle (r, 12.0f);
            if (! on)
            {
                g.setColour (col::rule);
                g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
            }
            if (on)
            {
                g.setColour (col::blue);
                g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
            }
            g.setColour (col::ink);
            g.setFont (small (16.5f).boldened());
            g.drawText (item.name.toUpperCase(), r.withTrimmedLeft (14).withTrimmedRight (74), juce::Justification::centredLeft, true);
            g.setColour (col::inkDim);
            g.setFont (small (14.0f));
            g.drawText (Kit::lengthText (item.seconds), r.withTrimmedRight (14), juce::Justification::centredRight);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        const int row = e.y / kRowH;
        if (juce::isPositiveAndBelow (row, count()))
            owner.pickRow (row);
    }

    const juce::String& idAt (int row) const { return items[(size_t) row].id; }

private:
    DrumKitSheet& owner;
    std::vector<SoundItem> items;
};

DrumKitSheet::DrumKitSheet (Library& lib) : library (lib), list (std::make_unique<List> (*this))
{
    for (auto* b : { &done, &analog, &usePads })
    {
        b->setFontHeight (16.0f);
        addAndMakeVisible (*b);
    }
    done.onClick = [this] { if (onDone) onDone(); };
    analog.onClick = [this] { if (onBackToAnalog) onBackToAnalog(); };
    usePads.onClick = [this] { if (onUsePads) onUsePads(); };
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (false, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (viewport);
}

DrumKitSheet::~DrumKitSheet() = default;

void DrumKitSheet::open (const juce::String& voiceName, const juce::String& currentId, const juce::String& currentName)
{
    voice = voiceName;
    sourceId = currentId;
    sourceName = currentName;
    list->load (library.items());
    analog.setLook (currentId.isEmpty() && currentName.isEmpty() ? Look::ghost : Look::cream);
    viewport.setViewPosition (0, 0);
    resized();
    repaint();
}

int DrumKitSheet::rowCount() const { return list->count(); }

void DrumKitSheet::pickRow (int row)
{
    if (juce::isPositiveAndBelow (row, list->count()) && onPick)
        onPick (list->idAt (row));
}

juce::Rectangle<int> DrumKitSheet::rowBounds (int row) const
{
    if (! juce::isPositiveAndBelow (row, list->count()))
        return {};
    auto r = juce::Rectangle<int> (0, row * kRowH, list->getWidth(), kRowH) - viewport.getViewPosition() + viewport.getPosition();
    return r.getIntersection (viewport.getBounds());
}

void DrumKitSheet::resized()
{
    done.setBounds (getWidth() - 100, 12, 84, 44);
    headingBox = { 18.0f, 12.0f, (float) getWidth() - 130.0f, 44.0f };
    sourceBox = { 14.0f, 62.0f, (float) getWidth() - 28.0f, 40.0f };
    const int bw = getWidth() - 28;
    analog.setBounds (14, 110, bw, 48);
    usePads.setBounds (14, 164, bw, 48);
    const int top = 252, bottom = getHeight() - bottomInset - 8;
    viewport.setBounds (14, top, bw, juce::jmax (kRowH, bottom - top));
    list->setSize (bw, juce::jmax (kRowH, list->count() * kRowH));
}

void DrumKitSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (24.0f));
    g.drawText ("Sound for the " + voice.toLowerCase(), headingBox.toNearestInt(), juce::Justification::centredLeft, true);

    // What is on it now.
    g.setColour (col::well);
    g.fillRoundedRectangle (sourceBox, 12.0f);
    const bool sample = sourceName.isNotEmpty();
    g.setColour (sample ? col::blue : col::amber);
    g.setFont (spaced (condensedBold (14.0f), 0.08f));
    g.drawText (sample ? "YOUR SOUND" : "ANALOG", sourceBox.reduced (14, 0).withWidth (96), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xfff3eadb));
    g.setFont (small (15.0f));
    g.drawText (sample ? sourceName.toUpperCase() : juce::String ("The machine's own sound"), sourceBox.reduced (14, 0).withTrimmedLeft (104),
                juce::Justification::centredLeft, true);

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (14.0f), 0.1f));
    g.drawText ("YOUR SOUNDS, NEWEST FIRST. TAP ONE TO PUT IT ON THE " + voice.toUpperCase(),
                juce::Rectangle<int> (16, 218, getWidth() - 32, 30), juce::Justification::centredLeft, true);
}
