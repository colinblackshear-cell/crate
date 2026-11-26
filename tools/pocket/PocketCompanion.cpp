#include "PocketCompanion.h"

namespace pocket
{
//==============================================================================
// A big coloured tile: a name and one plain line about what it does.
class AddLayerSheet::Tile : public juce::Component
{
public:
    Tile (juce::String n, juce::String l, juce::Colour c) : name (std::move (n)), line (std::move (l)), colour (c) {}
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float sink = down ? 3.0f : 0.0f;
        g.setColour (colour.darker (0.55f));
        g.fillRoundedRectangle (r.withTrimmedTop (4.0f), 18.0f);
        auto face = r.withTrimmedBottom (4.0f).translated (0.0f, sink);
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.12f), face.getX(), face.getY(), colour.darker (0.08f), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 18.0f);
        auto in = face.reduced (14.0f, 10.0f);
        g.setColour (col::onBright);
        g.setFont (condensedBold (23.0f));
        g.drawText (name, in.removeFromTop (28.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setFont (small (13.5f));
        g.drawFittedText (line, in.toNearestInt(), juce::Justification::bottomLeft, 2, 0.85f);
    }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        down = false;
        repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
    void mouseExit (const juce::MouseEvent&) override { down = false; repaint(); }

private:
    juce::String name, line;
    juce::Colour colour;
    bool down = false;
};

AddLayerSheet::AddLayerSheet()
{
    struct Def { const char* name; const char* line; Tool colourOf; };
    const Def defs[kChoices] = {
        { "My voice", "Sing, rap, hum, or record any sound around you", Tool::loops },
        { "Pads", "Tap sounds: your own recordings or the kits", Tool::pads },
        { "Drums", "Tap boxes to make a beat, or play it in live", Tool::drums },
        { "Synth", "Pick a sound and play. Every key fits", Tool::synth },
        { "Vocoder", "Your voice turned into a robot choir", Tool::vocoder },
        { "Resample", "Grab any layer, or the whole mix, as a new sound", Tool::song },
    };
    for (int i = 0; i < kChoices; ++i)
    {
        auto* t = tiles.add (new Tile (defs[i].name, defs[i].line, toolColour (defs[i].colourOf)));
        t->onClick = [this, i] { pick (i); };
        addAndMakeVisible (t);
    }
    change.setFontHeight (15.0f);
    change.onClick = [this] { if (onRecSetup) onRecSetup(); };
    addAndMakeVisible (change);
    const std::pair<PButton*, Tool> chips[] = { { &chords, Tool::chords }, { &drone, Tool::drone }, { &song, Tool::song }, { &effects, Tool::effects } };
    for (auto [b, t] : chips)
    {
        b->setFontHeight (15.0f);
        b->onClick = [this, t = t] { if (onMoreTool) onMoreTool (t); };
        addAndMakeVisible (b);
    }
    setDragZone (80);
}

AddLayerSheet::~AddLayerSheet() = default;

void AddLayerSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 24.0f);
    drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (fraunces (28.0f));
    g.drawText ("Add a layer", 20, 16, getWidth() - 40, 36, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (small (14.5f));
    g.drawFittedText ("What do you want to record with? It plays along with your other layers.",
                      juce::Rectangle<int> (20, 52, getWidth() - 40, 36), juce::Justification::topLeft, 2);

    drawWell (g, setupCard, 14.0f);
    auto in = setupCard.reduced (14.0f, 8.0f).withTrimmedRight ((float) change.getWidth() + 8.0f);
    g.setColour (col::lcdAmber);
    g.setFont (spaced (condensedBold (16.0f), 0.06f));
    g.drawFittedText (setupText ? setupText().toUpperCase() : juce::String(), in.removeFromTop (in.getHeight() * 0.55f).toNearestInt(),
                      juce::Justification::bottomLeft, 1, 0.8f);
    g.setColour (col::inkDim);
    g.setFont (small (13.0f));
    g.drawFittedText ("Each take loops to fit the idea.", in.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.1f));
    g.drawText ("MORE TOOLS", 20, chords.getY() - 24, getWidth() - 40, 20, juce::Justification::centredLeft);
}

void AddLayerSheet::resized()
{
    const int w = getWidth(), x0 = 16, iw = w - 32, gap = 10;
    const int tileW = (iw - gap) / 2, tileH = 96;
    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds (x0 + (i % 2) * (tileW + gap), 94 + (i / 2) * (tileH + gap), tileW, tileH);
    const int setupY = 94 + 3 * tileH + 2 * gap + 12;
    setupCard = juce::Rectangle<float> ((float) x0, (float) setupY, (float) iw, 58.0f);
    change.setBounds (x0 + iw - 8 - 96, setupY + 7, 96, 44);
    const int chipY = setupY + 58 + 34;
    const int chipW = (iw - 3 * 8) / 4;
    PButton* chips[] = { &chords, &drone, &song, &effects };
    for (int i = 0; i < 4; ++i)
        chips[i]->setBounds (x0 + i * (chipW + 8), chipY, chipW, 44);
}

//==============================================================================
const char* TakeSheet::vibeName (int v)
{
    static const char* const names[kVibes] = { "None", "Space", "Echo", "Radio", "Underwater", "Crunch" };
    return names[juce::jlimit (0, kVibes - 1, v)];
}

LayerFxState TakeSheet::withVibe (LayerFxState fx, int v)
{
    const float volume = fx.knob[(size_t) fxVolume];
    fx = {};
    fx.knob[(size_t) fxVolume] = volume;
    auto set = [&fx] (int k, float x) { fx.knob[(size_t) k] = x; };
    switch (v)
    {
        case space:      set (fxReverb, 0.5f); break;
        case echo:       set (fxDelay, 0.42f); set (fxReverb, 0.12f); break;
        case radio:      set (fxFilter, 0.78f); set (fxDrive, 0.35f); break;   // thin and a little dirty
        case underwater: set (fxFilter, 0.18f); set (fxReverb, 0.35f); break;   // muffled and far away
        case crunch:     set (fxDrive, 0.62f); break;
        default: break;
    }
    return fx;
}

TakeSheet::TakeSheet()
{
    for (int i = 0; i < kVibes; ++i)
    {
        vibes[i].setText (vibeName (i));
        vibes[i].setFontHeight (17.0f);
        vibes[i].onClick = [this, i]
        {
            vibe = i;
            refresh();
            if (onVibe)
                onVibe (i);
        };
        addAndMakeVisible (vibes[i]);
    }
    more.setFontHeight (15.0f);
    more.setLeftAligned (true);
    again.setFontHeight (17.0f);
    makePad.setFontHeight (17.0f);
    keep.setFontHeight (20.0f);
    more.onClick = [this] { if (onMoreEffects) onMoreEffects(); };
    again.onClick = [this] { if (onTryAgain) onTryAgain(); };
    makePad.onClick = [this] { if (onMakePad) onMakePad(); };
    keep.onClick = [this] { if (onKeep) onKeep(); };
    for (auto* b : { &more, &again, &makePad, &keep })
        addAndMakeVisible (b);
    setDragZone (80);
}

TakeSheet::~TakeSheet() = default;

void TakeSheet::show (int l, const juce::String& what)
{
    shownLayer = l;
    takeName = what;
    vibe = none;
    keep.setText ("Keep it as layer " + juce::String (l + 1));
    refresh();
}

void TakeSheet::refresh()
{
    for (int i = 0; i < kVibes; ++i)
        vibes[i].setLook (i == vibe ? Look::amber : Look::cream);
    repaint();
}

void TakeSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 24.0f);
    drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (fraunces (28.0f));
    g.drawText ("Nice take", 20, 16, getWidth() - 40, 36, juce::Justification::centredLeft);
    g.setColour (col::lcdAmber);
    g.setFont (spaced (condensedBold (14.0f), 0.1f));
    g.drawText (takeName.toUpperCase() + U8(" \xc2\xb7 LOOPING"), 20, 16, getWidth() - 40, 36, juce::Justification::centredRight);
    g.setColour (col::ink);
    g.setFont (condensedBold (17.0f));
    g.drawText ("Add a vibe", 20, 62, 120, 24, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (small (14.0f));
    g.drawText ("tap one to hear it", 112, 62, getWidth() - 132, 24, juce::Justification::centredLeft);
}

void TakeSheet::resized()
{
    const int w = getWidth(), x0 = 16, iw = w - 32, gap = 8;
    const int vw = (iw - 2 * gap) / 3;
    for (int i = 0; i < kVibes; ++i)
        vibes[i].setBounds (x0 + (i % 3) * (vw + gap), 94 + (i / 3) * (52 + gap), vw, 52);
    more.setBounds (x0, 94 + 2 * 52 + gap + 6, iw, 40);
    const int half = (iw - 10) / 2;
    again.setBounds (x0, 262, half, 52);
    makePad.setBounds (x0 + half + 10, 262, half, 52);
    keep.setBounds (x0, 326, iw, 66);
}
}
