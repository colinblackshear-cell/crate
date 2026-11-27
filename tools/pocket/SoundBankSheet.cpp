#include "SoundBankSheet.h"

using namespace pocket;

namespace
{
constexpr int kRowH = 58;
}

// The factory kits, one row each with its LOAD button. The row marked is the kit on the bank now.
class SoundBankSheet::KitList : public juce::Component
{
public:
    explicit KitList (SoundBankSheet& o) : owner (o)
    {
        const int n = (int) factoryKits().size();
        loads.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            auto& b = loads[(size_t) i];
            b = std::make_unique<PButton> ("Load");
            b->setFontHeight (15.0f);
            b->onClick = [this, i] { if (owner.onLoad) owner.onLoad (i); };
            addAndMakeVisible (*b);
        }
    }

    int rowCount() const { return (int) loads.size(); }
    juce::Rectangle<float> row (int i) const { return { 6.0f, (float) i * kRowH + 3.0f, (float) getWidth() - 12.0f, (float) kRowH - 6.0f }; }

    void resized() override
    {
        for (int i = 0; i < rowCount(); ++i)
        {
            const auto r = row (i);
            loads[(size_t) i]->setBounds ((int) r.getRight() - 78, (int) r.getY() + 1, 72, (int) r.getHeight() - 2);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto loaded = owner.loadedKitName != nullptr ? owner.loadedKitName() : juce::String();
        auto& kits = factoryKits();
        for (int i = 0; i < rowCount(); ++i)
        {
            auto r = row (i);
            const bool on = Kit::mpcName (juce::String (kits[(size_t) i].name).toUpperCase()) == loaded;
            g.setColour (on ? col::creamLight : col::cream);
            g.fillRoundedRectangle (r, 12.0f);
            if (on)
            {
                g.setColour (col::amber);
                g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
            }
            g.setColour (juce::Colour (kits[(size_t) i].colour));
            g.fillRoundedRectangle (r.getX() + 8, r.getY() + 7, 36, 36, 9.0f);
            g.setColour (col::ink);
            g.setFont (small (16.0f).boldened());
            g.drawText (kits[(size_t) i].name, juce::Rectangle<float> (r.getX() + 54, r.getY() + 5, r.getWidth() - 140, 22), juce::Justification::centredLeft);
            g.setColour (col::inkDim);
            g.setFont (small (13.0f));
            g.drawFittedText (kits[(size_t) i].what, juce::Rectangle<int> ((int) r.getX() + 54, (int) r.getY() + 26, (int) r.getWidth() - 140, 20), juce::Justification::centredLeft, 1);
        }
    }

private:
    SoundBankSheet& owner;
    std::vector<std::unique_ptr<PButton>> loads;
};

SoundBankSheet::SoundBankSheet() : list (std::make_unique<KitList> (*this))
{
    view.setViewedComponent (list.get(), false);
    view.setScrollBarsShown (false, false);
    view.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (view);
    for (auto* b : { &import, &openSounds, &done, &sets, &rename })
    {
        b->setFontHeight (16.0f);
        addAndMakeVisible (*b);
    }
    import.onClick = [this] { if (onImport) onImport(); };
    openSounds.onClick = [this] { if (onOpenSounds) onOpenSounds(); };
    done.onClick = [this] { if (onDone) onDone(); };
    sets.onClick = [this] { if (onSets) onSets(); };
    rename.onClick = [this] { if (onRename) onRename(); };
}

SoundBankSheet::~SoundBankSheet() = default;

void SoundBankSheet::resized()
{
    done.setBounds (getWidth() - 100, 10, 84, 44);
    sets.setBounds (done.getX() - 8 - 84, 10, 84, 44);
    rename.setBounds (sets.getX() - 8 - 104, 10, 104, 44);
    const auto lb = listBounds();
    view.setBounds (lb);
    list->setSize (lb.getWidth(), list->rowCount() * kRowH + 6);
    const int y = lb.getBottom() + 112;
    import.setBounds (16, y, (getWidth() - 40) * 3 / 5, 44);
    openSounds.setBounds (import.getRight() + 8, y, getWidth() - 16 - import.getRight() - 8, 44);
}

void SoundBankSheet::scrollKitsTo (int kit)
{
    view.setViewPosition (0, juce::jlimit (0, juce::jmax (0, list->getHeight() - view.getHeight()), kit * kRowH));
}

void SoundBankSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (24.0f));
    g.drawText ("Kits", 18, 14, rename.getX() - 24, 36, juce::Justification::centredLeft);

    // a hint that the list goes on
    const auto lb = listBounds();
    g.setColour (col::inkDim);
    g.setFont (small (12.0f));
    g.drawText (juce::String (kitCount()) + " kits, scroll for more. LOAD puts one on the bank you're on (16 pads).", 16, lb.getBottom() + 2, getWidth() - 32, 18, juce::Justification::centredLeft);

    auto box = juce::Rectangle<float> (14, (float) lb.getBottom() + 22, (float) getWidth() - 28, 84);
    g.setColour (col::well);
    g.fillRoundedRectangle (box, 14.0f);
    g.setColour (col::amber);
    g.setFont (spaced (condensedBold (15.0f), 0.08f));
    g.drawText ("YOUR OWN SOUNDS (SPLICE, ANYWHERE)", box.reduced (14, 6).withHeight (20), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xfff3eadb));
    g.setFont (small (12.5f));
    g.drawFittedText (U8("Files app › On My iPhone › Kaleidobox Pocket › Drop Sounds Here: anything you put there comes in when you open this. Or send sounds from the Mac over the link."),
                      box.reduced (14, 6).withTrimmedTop (22).toNearestInt(), juce::Justification::topLeft, 4);
}
