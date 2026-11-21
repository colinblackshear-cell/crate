#include <algorithm>
#include "BeatsScreen.h"

using namespace pocket;

namespace
{
const char* const voiceLabels[crate::drums::kVoices] = { "Kick", "Snare", "Clap", "Rim", "Lo tom", "Mid tom", "Hi tom", "Clave", "Cowbell", "Cl hat", "Op hat", "Cymbal" };
const char* const voiceTags[crate::drums::kVoices] = { "KK", "SN", "CP", "RS", "LT", "MT", "HT", "CL", "CB", "CH", "OH", "CY" };


struct BelowSwitch : juce::Component {};   // (the tool row picks the view now)
}

//==============================================================================
// The 16 steps of the picked voice, in two rows of 8. Tap to set a hit, hold to set the accent.
class StepGrid : public juce::Component, private juce::Timer
{
public:
    std::function<int()> voice;
    std::function<crate::drums::Pattern&()> pattern;
    std::function<int()> playhead;
    std::function<void (int, bool)> onEdit;   // step, accent changed

    juce::Rectangle<float> cell (int step) const
    {
        const float gap = 5.0f, w = ((float) getWidth() - gap * 7.0f) / 8.0f, h = 46.0f;
        return { (float) (step % 8) * (w + gap), (float) (step / 8) * (h + 6.0f), w, h };
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pattern();
        const int v = voice(), now = playhead();
        for (int s = 0; s < 16; ++s)
        {
            auto r = cell (s);
            const bool on = p.hits[(size_t) v][(size_t) s] > 0;
            drawRaised (g, r, on ? Look::amber : Look::dark, false, 11.0f);
            if (on && p.accent[(size_t) s])
            {
                g.setColour (col::red);
                g.fillEllipse (r.getX() + 8, r.getY() + 8, 8, 8);
                g.setColour (juce::Colours::black.withAlpha (0.3f));
                g.drawEllipse (r.getX() + 8, r.getY() + 8, 8, 8, 1.0f);
            }
            if (s == now)
            {
                g.setColour (col::green);
                g.drawRoundedRectangle (r.reduced (1).withTrimmedBottom (3), 11.0f, 2.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        held = stepAt (e.position);
        longPress = false;
        if (held >= 0)
            startTimer (420);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        if (held >= 0 && ! longPress && stepAt (e.position) == held)
        {
            auto& cellValue = pattern().hits[(size_t) voice()][(size_t) held];
            cellValue = cellValue > 0 ? 0 : 1;
            if (onEdit)
                onEdit (held, false);
        }
        held = -1;
    }

private:
    int stepAt (juce::Point<float> p) const
    {
        for (int s = 0; s < 16; ++s)
            if (cell (s).expanded (3, 3).contains (p))
                return s;
        return -1;
    }

    void timerCallback() override
    {
        stopTimer();
        if (held < 0)
            return;
        longPress = true;
        auto& pat = pattern();
        auto& hit = pat.hits[(size_t) voice()][(size_t) held];
        if (hit == 0)
            hit = 1;
        pat.accent[(size_t) held] = ! pat.accent[(size_t) held];
        if (onEdit)
            onEdit (held, true);
    }

    int held = -1;
    bool longPress = false;
};

// All 12 voices by 16 steps, with the playing step outlined. Tapping a row picks that voice.
class StepOverview : public juce::Component
{
public:
    std::function<const crate::drums::Pattern&()> pattern;
    std::function<int()> playhead, voice;
    std::function<void (int)> onPick;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawWell (g, r, 14.0f);
        const auto& p = pattern();
        const float left = 36.0f, top = 8.0f, rowH = (r.getHeight() - 16.0f) / 12.0f;
        const float colW = (r.getWidth() - left - 12.0f) / 16.0f;
        const int now = playhead(), picked = voice();

        for (int v = 0; v < 12; ++v)
        {
            const float y = top + (float) v * rowH;
            if (v == picked)
            {
                g.setColour (juce::Colour (0xff2a241d));
                g.fillRoundedRectangle (6, y, r.getWidth() - 12, rowH, 2.0f);
            }
            g.setColour (v == picked ? col::lcdAmber : col::inkFaint);
            g.setFont (condensedBold (9.5f));
            g.drawText (voiceTags[v], juce::Rectangle<float> (10, y, 26, rowH), juce::Justification::centredLeft);
            for (int s = 0; s < 16; ++s)
            {
                const bool on = p.hits[(size_t) v][(size_t) s] > 0;
                g.setColour (on ? col::amber : juce::Colour (0xff3a332c));
                g.fillRoundedRectangle (left + (float) s * colW + 1.5f, y + 1.6f, colW - 3.0f, rowH - 3.2f, 1.4f);
            }
        }
        if (now >= 0)
        {
            g.setColour (col::green);
            g.drawRoundedRectangle (left + (float) now * colW, top - 3, colW, rowH * 12 + 6, 2.5f, 1.6f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const float top = 8.0f, rowH = ((float) getHeight() - 16.0f) / 12.0f;
        const int v = (int) ((e.position.y - top) / rowH);
        if (v >= 0 && v < 12 && onPick)
            onPick (v);
    }
};

//==============================================================================
class BeatsScreen::DrumView : public BelowSwitch
{
public:
    explicit DrumView (BeatsScreen& o) : owner (o), d (o.drumData)
    {
        play.setLook (Look::green);
        // The drum machine on its own, live (over the loops too): the thumb bar's PLAY is the whole song.
        play.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
        {
            auto c = juce::Point<float> (r.getX() + 24, r.getCentreY());
            g.setColour (ink);
            const bool on = owner.beatsEngine.isPlaying();
            if (on)
                g.fillRoundedRectangle (c.x - 7, c.y - 7, 14, 14, 2.5f);
            else
            {
                juce::Path t;
                t.addTriangle (c.x - 6, c.y - 9, c.x - 6, c.y + 9, c.x + 9, c.y);
                g.fillPath (t);
            }
            g.setFont (spaced (condensed (16.0f), 0.08f));
            g.drawText (on ? "STOP DRUMS" : "PLAY DRUMS", r.withTrimmedLeft (40).withTrimmedRight (6), juce::Justification::centredLeft);
        };
        play.onClick = [this]
        {
            owner.beatsEngine.setPlaying (! owner.beatsEngine.isPlaying());
            if (owner.beatsEngine.isPlaying() && owner.clickWanted != nullptr)
                owner.beatsEngine.setClick (owner.clickWanted());
            play.repaint();
        };
        addAndMakeVisible (play);

        for (int i = 0; i < kPatternSlots; ++i)
        {
            patterns[i].setText (juce::String (i + 1));
            patterns[i].setFontHeight (16.0f);
            patterns[i].onClick = [this, i]
            {
                d.slot = i;
                owner.drumsChanged();
                refresh();
            };
            addAndMakeVisible (patterns[i]);
        }

        variation.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
        {
            static const char* text[] = { "VAR A", "VAR B", "A THEN B" };
            g.setColour (ink);
            g.setFont (spaced (condensed (16.0f)));
            g.drawFittedText (text[d.variation], r.withTrimmedRight (24).withTrimmedLeft (6).toNearestInt(), juce::Justification::centred, 1, 0.8f);
            juce::Path t;
            t.addTriangle (r.getRight() - 22, r.getCentreY() - 3, r.getRight() - 13, r.getCentreY() - 3, r.getRight() - 17.5f, r.getCentreY() + 3);
            g.fillPath (t);
        };
        variation.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "A: play and edit A", true, d.variation == 0);
            m.addItem (2, "B: play and edit B", true, d.variation == 1);
            m.addItem (3, "A then B: play both", true, d.variation == 2);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&variation).withMinimumWidth (200), [this] (int r)
            {
                if (r == 0)
                    return;
                d.variation = r - 1;
                if (r == 1) d.editB = false;
                if (r == 2) d.editB = true;
                owner.drumsChanged();
                refresh();
            });
        };
        addAndMakeVisible (variation);

        grid.voice = [this] { return owner.voice; };
        grid.pattern = [this]() -> crate::drums::Pattern& { return d.edited(); };
        grid.playhead = [this] { return playhead(); };
        grid.onEdit = [this] (int step, bool accent)
        {
            const bool on = d.edited().hits[(size_t) owner.voice][(size_t) step] > 0;
            owner.drumsChanged();
            if (on)
                owner.beatsEngine.hitVoice (owner.voice, 0.85f, accent && d.edited().accent[(size_t) step]);
            repaint();
        };
        addAndMakeVisible (grid);

        overview.pattern = [this]() -> const crate::drums::Pattern& { return d.edited(); };
        overview.playhead = [this] { return playhead(); };
        overview.voice = [this] { return owner.voice; };
        overview.onPick = [this] (int v) { owner.selectVoice (v); };
        addAndMakeVisible (overview);

        auto add = [this] (const char* name, juce::Colour arc, double lo, double hi, double start, std::function<juce::String (double)> fmt)
        {
            auto* k = new KnobView (name, arc, lo, hi, start, 0, std::move (fmt), [] (double) { return true; });
            knobs.add (k);
            addAndMakeVisible (k);
        };
        add ("LEVEL", col::red, 0, 1, 0.8, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)); });
        add ("TUNE", col::red, -12, 12, 0, [] (double v) { const int t = juce::roundToInt (v); return t > 0 ? "+" + juce::String (t) : juce::String (t); });
        add ("DECAY", col::red, 0, 1, 0.5, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)); });
        add ("TONE", col::red, 0, 1, 0.5, [] (double v) { return juce::String (juce::roundToInt (v * 100.0)); });
        add ("SWING", col::amber, 0, 1, 0, [] (double v) { return juce::String (50 + juce::roundToInt (v * 25.0)) + "%"; });
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->onChange = [this, i]
            {
                auto& vs = d.settings.voices[(size_t) owner.voice];
                const auto v = (float) knobs[i]->getValue();
                switch (i)
                {
                    case 0: vs.level = v; break;
                    case 1: vs.tune = std::round (v); break;
                    case 2: vs.decay = v; break;
                    case 3: vs.tone = v; break;
                    default: d.settings.swing = v; break;
                }
                owner.drumsChanged();
            };
        refresh();
    }

    void refresh()
    {
        for (int i = 0; i < kPatternSlots; ++i)
            patterns[i].setLook (d.slot == i ? Look::amber : Look::cream);
        const auto& vs = d.settings.voices[(size_t) owner.voice];
        knobs[0]->setValue (vs.level);
        knobs[1]->setValue (vs.tune);
        knobs[2]->setValue (vs.decay);
        knobs[3]->setValue (vs.tone);
        knobs[4]->setValue (d.settings.swing);
        variation.repaint();
        play.repaint();
        repaint();
    }

    int playhead() const
    {
        if (owner.demoStep >= 0)
            return owner.demoStep;
        const int s = owner.beatsEngine.currentStep();
        return s >= 0 && owner.beatsEngine.playingVariation() == (d.editB ? 1 : 0) ? s : -1;
    }

    void tick()
    {
        const int now = playhead();
        if (now != lastPlayhead)
        {
            lastPlayhead = now;
            grid.repaint();
            overview.repaint();
        }
        if (playingShown != owner.beatsEngine.isPlaying())
        {
            playingShown = owner.beatsEngine.isPlaying();
            play.repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto well = juce::Rectangle<float> (16, (float) wellY, (float) getWidth() - 32, 100);
        drawWell (g, well, 16.0f);
        g.setColour (col::inkDim);
        g.setFont (spaced (condensed (13.0f), 0.1f));
        g.drawFittedText ("PATTERN", juce::Rectangle<int> (play.getRight() + 8, 8, variation.getX() - play.getRight() - 16, 44), juce::Justification::centred, 1, 0.8f);
        const int v = owner.voice;
        const auto& pat = d.edited();
        for (int i = 0; i < 12; ++i)
        {
            auto r = voiceCell (i);
            bool hasHits = false;
            for (int s = 0; s < 16; ++s)
                hasHits = hasHits || pat.hits[(size_t) i][(size_t) s] > 0;
            drawRaised (g, r, i == v ? Look::green : Look::dark, false, 11.0f);
            g.setColour (i == v ? col::ink : hasHits ? col::lcdAmber : juce::Colour (0xfff1e7d1));
            g.setFont (condensed (15.0f));
            g.drawText (voiceLabels[i], r.withTrimmedBottom (3), juce::Justification::centred);
        }

        g.setColour (col::inkDim);
        g.setFont (spaced (condensed (12.5f), 0.1f));
        const float ly = (float) grid.getY() - 26.0f, rx = (float) getWidth() - 16.0f;
        g.drawFittedText (juce::String (juce::String (voiceLabels[v]).toUpperCase()) + (d.editB ? U8(" · B") : juce::String()) + U8(" · 16 STEPS · TAP TO SET, HOLD FOR ACCENT"),
                          juce::Rectangle<float> (16, ly, rx - 16 - 74, 22).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (col::red);
        g.fillEllipse (rx - 66, ly + 6, 9, 9);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawEllipse (rx - 66, ly + 6, 9, 9, 1.0f);
        g.setColour (col::inkDim);
        g.drawText ("ACCENT", juce::Rectangle<float> (rx - 54, ly, 54, 22), juce::Justification::centredLeft);
    }

    void resized() override
    {
        // Settings up top, the steps and knobs low (under the thumb).
        const int W = getWidth() - 32, H = getHeight();
        play.setBounds (16, 8, 150, 44);
        variation.setBounds (getWidth() - 16 - 104, 8, 104, 44);
        const float slotW = ((float) W - 5.0f * 6.0f) / 6.0f;
        for (int i = 0; i < kPatternSlots; ++i)
            patterns[i].setBounds (juce::roundToInt (16.0f + (float) i * (slotW + 6.0f)), 60, juce::roundToInt (slotW), 44);
        wellY = 112;
        const int knobY = H - 112;
        grid.setBounds (16, wellY + 136, W, 98);
        overview.setBounds (16, grid.getBottom() + 10, W, juce::jmax (96, knobY - 8 - grid.getBottom() - 10));
        const int w = W / 5;
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (16 + i * w, knobY, w, 108);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 12; ++i)
            if (voiceCell (i).contains (e.position))
                owner.selectVoice (i);
    }

    juce::Rectangle<float> voiceCell (int i) const
    {
        const float w = ((float) getWidth() - 32.0f - 16.0f - 5.0f * 6.0f) / 6.0f;
        return { 24.0f + (float) (i % 6) * (w + 6.0f), (float) wellY + 8.0f + (float) (i / 6) * 44.0f, w, 40.0f };
    }

    BeatsScreen& owner;
    DrumData& d;
    PButton play, patterns[kPatternSlots], variation;
    int wellY = 112;
    StepGrid grid;
    StepOverview overview;
    juce::OwnedArray<KnobView> knobs;
    int lastPlayhead = -2;
    bool playingShown = false;
};

//==============================================================================
class BeatsScreen::PadsView : public BelowSwitch, private juce::Timer
{
public:
    explicit PadsView (BeatsScreen& o) : owner (o)
    {
        exportButton.setFontHeight (15.0f);
        exportButton.onClick = [this] { owner.exportForMpc(); };
        soundsButton.setFontHeight (16.0f);
        soundsButton.onClick = [this] { owner.host.openSoundBank(); };
        addAndMakeVisible (soundsButton);
        addAndMakeVisible (exportButton);

        for (int i = 0; i < 4; ++i)
        {
            banks[i].setText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
            banks[i].setFontHeight (17.0f);
            banks[i].onClick = [this, i] { owner.setBank (i); };
            addAndMakeVisible (banks[i]);
        }
        editPads.setFontHeight (15.0f);
        editPads.onClick = [this]
        {
            const int pad = owner.selected;
            if (pad < 0 || owner.padKit.pads[(size_t) pad].empty())
                return owner.host.toast ("Tap a pad that has a sound, then EDIT PADS");
            owner.host.openPadSheet (pad);
        };
        addAndMakeVisible (editPads);

        toMac.setFontHeight (15.0f);
        // The kit onto the Mac's MPC as a new kit (or its sounds into the Mac's Sounds).
        toMac.onClick = [this]
        {
            pocket::MacSend send;
            for (int i = 0; i < (int) owner.padKit.pads.size(); ++i)
                if (const auto& pad = owner.padKit.pads[(size_t) i]; ! pad.empty())
                    send.items.push_back ({ pad.name.isNotEmpty() ? pad.name : pocket::Kit::padLabel (i), pad.audio, 0.0 });
            if (send.items.empty())
                return owner.host.toast ("The pads are empty. Put sounds on them first (TO PAD in Sounds or Record).");
            send.kitName = "Pocket kit";
            send.title = "Pocket kit";
            send.detail = juce::String ((int) send.items.size()) + (send.items.size() == 1 ? " pad" : " pads");
            send.dests = { "kit", "sounds" };
            send.dest = "kit";
            owner.host.sendToMac (std::move (send));
        };
        addAndMakeVisible (toMac);

        for (auto* b : { &barsButton, &quantButton, &undoButton, &recordSound })
        {
            b->setFontHeight (15.0f);
            addAndMakeVisible (*b);
        }
        barsButton.onClick = [this]
        {
            const int b = owner.drumData.padPattern.bars;
            owner.setPatternBars (b == 1 ? 2 : b == 2 ? 4 : b == 4 ? 8 : 1);
        };
        recordSound.onClick = [this] { owner.host.openRecorder(); };
        quantButton.onClick = [this] { owner.cycleQuantize(); };
        undoButton.onClick = [this]
        {
            juce::PopupMenu m;
            m.addItem ("Undo the last change", ! owner.patternUndo.empty(), false, [this] { owner.undoPatternHit(); });
            m.addItem ("Clear the whole pattern", ! owner.drumData.padPattern.hits.empty(), false, [this] { owner.clearPattern(); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&undoButton));
        };
        refresh();
    }

    void refresh()
    {
        for (int i = 0; i < 4; ++i)
            banks[i].setLook (owner.bank == i ? Look::amber : Look::ghost);
        const auto& p = owner.drumData.padPattern;
        barsButton.setText (juce::String (p.bars) + (p.bars == 1 ? " bar" : " bars"));
        quantButton.setText (p.quantize == 4 ? "Snap 1/16" : p.quantize == 2 ? "Snap 1/8" : "Snap off");
        repaint();
    }

    void tick()
    {
        const auto mask = owner.beatsEngine.soundingPads();
        if (mask != lastMask)
        {
            lastMask = mask;
            repaint (wellBounds().toNearestInt());
        }
        if (owner.beatsEngine.patternPlaying() || wasPlaying)
        {
            wasPlaying = owner.beatsEngine.patternPlaying();
            repaint (stripBounds().expanded (2).toNearestInt());
        }
    }

    // Settings up top, the pads low (under the thumb).
    juce::Rectangle<float> lcdBounds() const { return { 16, 8, 116, 44 }; }
    juce::Rectangle<float> stripBounds() const { return { 16, 112, (float) getWidth() - 32, 46 }; }
    juce::Rectangle<float> wellBounds() const { return { 16, 218, (float) getWidth() - 32, (float) getHeight() - 226 }; }

    void paintPattern (juce::Graphics& g)
    {
        auto r = stripBounds();
        drawWell (g, r, 12.0f);
        auto inner = r.reduced (10, 7);
        const auto& p = owner.drumData.padPattern;
        const double len = p.lengthBeats();
        // the beats and bars
        for (int b = 0; ! p.hits.empty() && b <= (int) len; ++b)   // (an empty one shows its hint instead)
        {
            const float x = inner.getX() + inner.getWidth() * (float) (b / len);
            g.setColour (juce::Colour (b % 4 == 0 ? 0x55f1e7d1 : 0x22f1e7d1));
            g.fillRect (x, inner.getY(), 1.0f, inner.getHeight());
        }
        // the pads in it, one row each (in the order they first come)
        std::vector<int> rows;
        for (auto& h : p.hits)
            if (std::find (rows.begin(), rows.end(), h.pad) == rows.end())
                rows.push_back (h.pad);
        const int numRows = juce::jmax (1, juce::jmin (6, (int) rows.size()));
        const float rowH = inner.getHeight() / (float) numRows;
        const juce::Colour colours[] = { col::lcdText, col::amber, col::blue, col::green, juce::Colour (0xffb9a3dc), juce::Colour (0xfff6dfa0) };
        for (auto& h : p.hits)
        {
            const int row = (int) (std::find (rows.begin(), rows.end(), h.pad) - rows.begin()) % numRows;
            const float x = inner.getX() + inner.getWidth() * (float) (h.beat / len);
            g.setColour (colours[row % 6].withAlpha (0.5f + 0.5f * h.velocity));
            g.fillRoundedRectangle (x - 1.5f, inner.getY() + rowH * (float) row + 1.0f, 3.5f, juce::jmax (2.0f, rowH - 2.0f), 1.0f);
        }
        if (p.hits.empty())
        {
            g.setColour (juce::Colour (0x99f1e7d1));
            g.setFont (small (13.5f));
            g.drawText (owner.recPads ? "Play the pads: they go into the pattern" : U8("Press REC, then play the pads: a beat that loops"),
                        inner, juce::Justification::centred);
        }
        // where it's playing
        const double pos = owner.patternPosition();
        if (pos >= 0)
        {
            const float x = inner.getX() + inner.getWidth() * (float) (pos / len);
            g.setColour (owner.recPads ? col::red : col::amber);
            g.fillRect (x - 1.0f, r.getY() + 3.0f, 2.0f, r.getHeight() - 6.0f);
        }
    }

    juce::Rectangle<float> padBounds (int index) const
    {
        const int i = index % 16, row = 3 - i / 4, col = i % 4;   // A13..A16 on top, A01..A04 at the bottom
        auto inner = wellBounds().reduced (9.5f);
        const float gap = 8.5f, w = (inner.getWidth() - 3 * gap) / 4.0f, h = (inner.getHeight() - 3 * gap) / 4.0f;
        return { inner.getX() + (float) col * (w + gap), inner.getY() + (float) row * (h + gap), w, h };
    }

    int padAt (juce::Point<float> p) const
    {
        for (int i = 0; i < 16; ++i)
            if (padBounds (i).contains (p))
                return owner.bank * 16 + i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        // the kit name (tap to rename)
        auto lcd = lcdBounds();
        drawWell (g, lcd, 12.0f);
        g.setColour (col::lcdAmber);
        g.setFont (spaced (lcdFont (26.0f), 0.04f));
        g.drawFittedText (owner.padKit.name, lcd.reduced (10, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);

        paintPattern (g);

        drawWell (g, wellBounds(), 18.0f);
        const auto sounding = owner.beatsEngine.soundingPads();
        for (int i = 0; i < 16; ++i)
        {
            const int index = owner.bank * 16 + i;
            const auto& pad = owner.padKit.pads[(size_t) index];
            auto r = padBounds (i);
            const bool lit = index == owner.selected || (sounding >> index & 1) != 0;
            drawRaised (g, r, lit ? Look::amber : Look::pad, false, 11.0f);
            auto inner = r.withTrimmedBottom (3.0f);
            g.setFont (lcdFont (17.0f));
            g.setColour (lit ? col::ink.withAlpha (0.6f) : juce::Colour (0x99f1e7d1));
            g.drawText (Kit::padLabel (index), inner.reduced (9, 6).withHeight (18), juce::Justification::topLeft);
            g.setFont (condensed (15.0f));
            g.setColour (lit ? col::ink : pad.empty() ? juce::Colour (0x88f1e7d1) : juce::Colour (0xfff1e7d1));
            g.drawFittedText (pad.empty() ? juce::String (U8("—")) : pad.name, inner.reduced (9, 7).toNearestInt(), juce::Justification::bottomLeft, 2, 0.9f);
        }

        // how much of the MPC's memory the kit takes, between the banks and EDIT PADS
        const int used = owner.padKit.usedPads();
        const double mb = (double) owner.padKit.memoryBytes() / 1048576.0;
        const auto bytes = owner.padKit.memoryBytes();
        const auto dot = bytes > Kit::mpcMemoryBytes ? col::red : bytes > Kit::mpcMemoryBytes * 8 / 10 ? col::amber : col::green;
        const float mx = (float) banks[3].getRight() + 8.0f, mw = (float) editPads.getX() - 6.0f - mx;
        g.setColour (dot);
        g.fillEllipse (mx, 77, 9, 9);
        g.setColour (col::inkDim);
        g.setFont (small (12.5f));
        g.drawFittedText (juce::String (used) + (used == 1 ? " pad" : " pads") + "\n" + juce::String (mb, 1) + " / 32 MB",
                          juce::Rectangle<float> (mx + 13, 60, mw - 13, 44).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
    }

    void resized() override
    {
        const int w = getWidth();
        soundsButton.setBounds (138, 8, 72, 44);
        exportButton.setBounds (216, 8, 84, 44);
        toMac.setBounds (306, 8, w - 16 - 306, 44);
        for (int i = 0; i < 4; ++i)
            banks[i].setBounds (16 + i * 44, 60, 42, 44);
        editPads.setBounds (w - 16 - 88, 60, 88, 44);
        barsButton.setBounds (16, 166, 70, 44);
        quantButton.setBounds (92, 166, 84, 44);
        undoButton.setBounds (182, 166, 60, 44);
        recordSound.setBounds (248, 166, w - 16 - 248, 44);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        held = -1;
        if (lcdBounds().contains (e.position))
        {
            renaming = true;
            return;
        }
        if (stripBounds().contains (e.position))
        {
            owner.togglePatternPlay();
            return;
        }
        const int pad = padAt (e.position);
        if (pad < 0)
            return;
        owner.selected = pad;
        held = pad;
        longPress = false;
        if (! owner.padKit.pads[(size_t) pad].empty())
        {
            owner.beatsEngine.padOn (pad);
            owner.recordHit (pad, 1.0f);
            if (! owner.recPads)
                startTimer (550);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        if (renaming)
        {
            renaming = false;
            if (lcdBounds().contains (e.position))
                owner.renameKit();
            return;
        }
        if (held >= 0)
            owner.beatsEngine.padOff (held);
        held = -1;
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (held < 0)
            return;
        longPress = true;
        owner.beatsEngine.padOff (held);
        owner.host.openPadSheet (held);
        held = -1;
    }

    BeatsScreen& owner;
    PButton exportButton { "Export MPC" }, soundsButton { "Sounds", Look::amber }, banks[4], editPads { "Edit pads" }, toMac { "To mac" };
    PButton barsButton { "2 bars" }, quantButton { "1/16" }, undoButton { "Undo" }, recordSound { U8("\xe2\x97\x8f Record a sound") };
    bool wasPlaying = false;
    juce::uint64 lastMask = 0;
    int held = -1;
    bool longPress = false, renaming = false;
};

//==============================================================================
BeatsScreen::BeatsScreen (PocketHost& h, Library& lib) : host (h), library (lib), padKit (lib)
{
    auto drumFile = lib.ideaRoot().getChildFile ("Beats").getChildFile ("drums.json");
    if (drumFile.existsAsFile())
        drumData = DrumData::fromVar (juce::JSON::parse (drumFile));
    beatsEngine.setDrums (drumData);
    beatsEngine.setPadPattern (drumData.padPattern);

    padKit.load();
    pushPads();

    drumView = std::make_unique<DrumView> (*this);
    padsView = std::make_unique<PadsView> (*this);
    addAndMakeVisible (*drumView);
    addChildComponent (*padsView);
    startTimerHz (30);
}

BeatsScreen::~BeatsScreen()
{
    stopTimer();
    if (drumsDirty)
        drumsChanged();
    if (saveCountdown > 0)
        timerCallback();
}

void BeatsScreen::pushPads()
{
    for (int i = 0; i < kPads; ++i)
        beatsEngine.setPad (i, padKit.pads[(size_t) i].setup());
}

void BeatsScreen::showMode (int m)
{
    screenMode = m;
    drumView->setVisible (m == 0);
    padsView->setVisible (m == 1);   // the drum machine keeps going: moving between tools never stops the clock
    repaint();
}

void BeatsScreen::setBank (int b)
{
    bank = juce::jlimit (0, 3, b);
    padsView->refresh();
}

void BeatsScreen::selectPad (int pad)
{
    selected = pad;
    if (pad >= 0)
        bank = pad / 16;
    padsView->refresh();
}

void BeatsScreen::selectVoice (int v)
{
    voice = juce::jlimit (0, crate::drums::kVoices - 1, v);
    drumView->refresh();
    beatsEngine.hitVoice (voice);
}

void BeatsScreen::setBpm (double b)
{
    drumData.bpm = juce::jlimit (40.0, 240.0, b);
    beatsEngine.setBpm (drumData.bpm);
    drumsDirty = true;
    saveSoon();
    drumView->repaint();
}

void BeatsScreen::drumsChanged()
{
    beatsEngine.setDrums (drumData);
    drumsDirty = true;
    saveSoon();
    if (drumView != nullptr)
        drumView->refresh();
}

void BeatsScreen::padChanged (int pad)
{
    beatsEngine.setPad (pad, padKit.pads[(size_t) pad].setup());
    kitDirty = true;
    saveSoon();
    padsView->repaint();
}

void BeatsScreen::padsChanged()
{
    pushPads();
    kitDirty = true;
    saveSoon();
    padsView->repaint();
}

void BeatsScreen::leave()
{
    beatsEngine.setPlaying (false);
    beatsEngine.setPatternPlaying (false);
    beatsEngine.setClick (false);
    recPads = false;
    beatsEngine.stopPads();
    if (padsView != nullptr)
        padsView->refresh();
}

void BeatsScreen::reloadIdea (const juce::String& ideaName)
{
    leave();
    auto drumFile = library.ideaRoot().getChildFile ("Beats").getChildFile ("drums.json");
    const double tempo = drumData.bpm;
    drumData = drumFile.existsAsFile() ? DrumData::fromVar (juce::JSON::parse (drumFile)) : DrumData();
    if (! drumFile.existsAsFile())
        drumData.bpm = tempo;   // a new idea starts at the tempo you were at
    drumsDirty = kitDirty = false;
    patternUndo.clear();
    beatsEngine.setDrums (drumData);
    beatsEngine.setPadPattern (drumData.padPattern);
    setBpm (drumData.bpm);
    padKit.pads = {};
    padKit.name = Kit::mpcName (ideaName.toUpperCase());
    if (padKit.name.isEmpty())
        padKit.name = "IDEA";
    padKit.load();
    pushPads();
    bank = 0;
    selected = 0;
    if (drumView != nullptr)
        drumView->refresh();
    if (padsView != nullptr)
        padsView->refresh();
    repaint();
}

juce::String BeatsScreen::summary() const
{
    juce::StringArray bits;
    bits.add (juce::String (juce::roundToInt (drumData.bpm)) + " BPM");
    if (const int n = padKit.usedPads(); n > 0)
        bits.add (juce::String (n) + (n == 1 ? " pad" : " pads"));
    if (! drumData.padPattern.hits.empty())
        bits.add ("beat recorded");
    return bits.joinIntoString (U8(" · "));
}

void BeatsScreen::refreshPads()
{
    if (padsView != nullptr)
        padsView->refresh();
}

//==============================================================================
// The pad pattern
double BeatsScreen::patternPosition() const
{
    if (! beatsEngine.patternPlaying() || ! beatsEngine.clockRunning())
        return -1.0;
    return std::fmod (beatsEngine.clockBeat(), drumData.padPattern.lengthBeats());
}

void BeatsScreen::patternChanged()
{
    beatsEngine.setPadPattern (drumData.padPattern);
    drumsDirty = true;
    saveSoon();
    if (padsView != nullptr)
        padsView->refresh();
}

void BeatsScreen::setRecordingPads (bool on, double fromBeat)
{
    recPads = on;
    recFrom = fromBeat;
    if (recPads)
        beatsEngine.setPatternPlaying (true);
    padsView->refresh();
}

void BeatsScreen::togglePatternPlay()
{
    const bool on = ! beatsEngine.patternPlaying();
    beatsEngine.setPatternPlaying (on);
    if (on && clickWanted != nullptr)
        beatsEngine.setClick (clickWanted());
    if (! on)
        recPads = false;
    padsView->refresh();
}

void BeatsScreen::setPatternBars (int bars)
{
    auto& p = drumData.padPattern;
    if (bars == p.bars)
        return;
    patternUndo.push_back (p);
    const double oldLen = p.lengthBeats();
    const int oldBars = p.bars;
    p.bars = bars;
    const double len = p.lengthBeats();
    if (bars > oldBars)
    {
        // Longer: what's there repeats to fill it, like the MPC.
        const auto first = p.hits;
        for (double at = oldLen; at < len - 1.0e-9; at += oldLen)
            for (auto h : first)
                if ((int) p.hits.size() < kMaxPadHits)
                {
                    h.beat += at;
                    h.skipRun = -1;
                    p.hits.push_back (h);
                }
    }
    else
        p.hits.erase (std::remove_if (p.hits.begin(), p.hits.end(), [len] (const PadHit& h) { return h.beat >= len; }), p.hits.end());
    patternChanged();
}

void BeatsScreen::cycleQuantize()
{
    auto& q = drumData.padPattern.quantize;
    q = q == 4 ? 2 : q == 2 ? 0 : 4;
    host.toast (q == 4 ? "New hits snap to 1/16 notes" : q == 2 ? "New hits snap to 1/8 notes" : "New hits land where you play them");
    patternChanged();
}

void BeatsScreen::undoPatternHit()
{
    if (patternUndo.empty())
        return host.toast ("Nothing to undo in the pattern");
    drumData.padPattern = patternUndo.back();
    patternUndo.pop_back();
    patternChanged();
}

void BeatsScreen::clearPattern()
{
    if (drumData.padPattern.hits.empty())
        return;
    patternUndo.push_back (drumData.padPattern);
    drumData.padPattern.hits.clear();
    patternChanged();
}

void BeatsScreen::recordHit (int pad, float velocity)
{
    if (! recPads || ! beatsEngine.clockRunning() || beatsEngine.clockBeat() < recFrom - 0.25)
        return;   // (not during the count-in)
    auto& p = drumData.padPattern;
    if ((int) p.hits.size() >= kMaxPadHits)
        return host.toast ("The pattern is full");
    const double len = p.lengthBeats();
    const double now = beatsEngine.clockBeat();
    double beat = std::fmod (now, len);
    if (p.quantize > 0)
    {
        const double grid = 1.0 / p.quantize;
        beat = std::round (beat / grid) * grid;
        if (beat >= len - 1.0e-9)
            beat -= len;
    }
    patternUndo.push_back (p);
    if (patternUndo.size() > 64)
        patternUndo.erase (patternUndo.begin());
    // The same pad on the same spot again just replaces it.
    p.hits.erase (std::remove_if (p.hits.begin(), p.hits.end(), [&] (const PadHit& h) { return h.pad == pad && std::abs (h.beat - beat) < 1.0e-6; }), p.hits.end());
    PadHit h;
    h.pad = pad;
    h.beat = beat;
    h.velocity = velocity;
    // It was just heard by hand: if the grid moved it a little later, don't play it again this time round.
    h.skipRun = beatsEngine.clockRun();
    h.skipUntil = now + 0.5;
    p.hits.push_back (h);
    patternChanged();
}

void BeatsScreen::timerCallback()
{
    if (screenMode == 0)
        drumView->tick();
    else
        padsView->tick();

    if (saveCountdown > 0 && --saveCountdown == 0)
        flush();
}

void BeatsScreen::flush()
{
    saveCountdown = 0;
    {
        if (drumsDirty)
        {
            auto dir = library.ideaRoot().getChildFile ("Beats");
            dir.createDirectory();
            dir.getChildFile ("drums.json").replaceWithText (juce::JSON::toString (drumData.toVar()));
            drumsDirty = false;
        }
        if (kitDirty)
        {
            padKit.save();
            kitDirty = false;
        }
    }
}

//==============================================================================
void BeatsScreen::renameKit()
{
    auto* w = new juce::AlertWindow ("Name this kit", "Up to 16 letters, numbers, spaces, - and _ so the MPC can show it.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", padKit.name);
    if (auto* editor = w->getTextEditor ("name"))
        editor->setInputRestrictions (16, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_");
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1)
            return;
        auto name = Kit::mpcName (w->getTextEditorContents ("name"));
        if (name.isNotEmpty() && name != padKit.name)
        {
            padKit.kitsFolder().getChildFile (juce::File::createLegalFileName (padKit.name) + ".json").deleteFile();
            padKit.name = name;
            kitDirty = true;
            saveSoon();
            padsView->repaint();
        }
    }), true);
}

void BeatsScreen::exportForMpc()
{
    if (padKit.usedPads() == 0)
        return host.toast ("Put some sounds on pads first");
    auto work = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-export");
    work.deleteRecursively();
    auto result = padKit.exportForMpc (work);
    if (! result.ok)
        return host.toast ("Couldn't export: " + result.error);

   #if JUCE_IOS || JUCE_ANDROID
    sharing = juce::ContentSharer::shareFilesScoped ({ juce::URL (result.zip) }, [this] (bool ok, const juce::String& error)
    {
        if (! ok && error.isNotEmpty())
            host.toast ("Couldn't share: " + error);
    }, getTopLevelComponent());
   #else
    host.toast ("Exported " + result.folderName + ".zip. Sharing works on the phone: " + result.zip.getFullPathName());
   #endif
    if (result.bytes > Kit::mpcMemoryBytes)
        host.toast ("This kit is bigger than the MPC's 32 MB. Remove some pads or shorten them.");
}

juce::String BeatsScreen::selfCheckExport (juce::File work)
{
    work.deleteRecursively();
    auto result = padKit.exportForMpc (work);
    return padKit.verify (result);
}

juce::String BeatsScreen::selfCheckSound()
{
    auto peakOf = [] (const std::vector<float>& l) { float p = 0; for (float s : l) p = juce::jmax (p, std::abs (s)); return p; };
    const int n = 44100;
    std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
    beatsEngine.prepare (44100.0);
    beatsEngine.setPlaying (true);
    for (int i = 0; i < n; i += 512)
        beatsEngine.render (left.data() + i, right.data() + i, juce::jmin (512, n - i));
    const float drumsPeak = peakOf (left);
    const int step = beatsEngine.currentStep();
    beatsEngine.setPlaying (false);
    std::fill (left.begin(), left.end(), 0.0f);
    beatsEngine.padOn (12);
    for (int i = 0; i < n; i += 512)
        beatsEngine.render (left.data() + i, nullptr, juce::jmin (512, n - i));
    const float padPeak = peakOf (left);
    if (drumsPeak < 0.05f)
        return "the drum machine is silent";
    if (step < 0)
        return "the drum machine's step doesn't move";
    if (padPeak < 0.01f)
        return "pad A13 is silent";
    return {};
}

juce::String BeatsScreen::selfCheckPattern()
{
    const auto saved = drumData.padPattern;
    const double sr = 44100.0;
    const double bpm = beatsEngine.getBpm();
    PadPattern p;
    p.bars = 1;
    PadHit h;
    h.pad = 12;
    h.beat = 1.0;
    p.hits.push_back (h);
    beatsEngine.setPlaying (false);
    beatsEngine.stopPads();
    beatsEngine.setClick (false);
    beatsEngine.prepare (sr);
    const int n = (int) (sr * 60.0 / bpm * 5.5);   // the hit on beat 2 plays, then again a bar later
    std::vector<float> left ((size_t) n, 0.0f);
    for (int i = 0; i < (int) sr * 4; i += 512)   // let anything still sounding (an 808 tail) fade out first
    {
        std::fill (left.begin(), left.begin() + 512, 0.0f);
        beatsEngine.render (left.data(), nullptr, 512);
        float peak = 0;
        for (int j = 0; j < 512; ++j)
            peak = juce::jmax (peak, std::abs (left[(size_t) j]));
        if (peak < 0.001f)
            break;
    }
    std::fill (left.begin(), left.end(), 0.0f);
    beatsEngine.setPadPattern (p);
    beatsEngine.setPatternPlaying (true);
    const int hitsBefore = beatsEngine.patternHitsPlayed();
    for (int i = 0; i < n; i += 512)
        beatsEngine.render (left.data() + i, nullptr, juce::jmin (512, n - i));
    const int played = beatsEngine.patternHitsPlayed() - hitsBefore;
    beatsEngine.setPatternPlaying (false);
    beatsEngine.stopPads();
    beatsEngine.setPadPattern (saved);
    std::vector<int> onsets;
    bool quiet = true;
    int lastLoud = -100000;
    for (int i = 0; i < n; ++i)
        if (std::abs (left[(size_t) i]) > 0.01f)
        {
            if (quiet && i - lastLoud > (int) sr / 4)
                onsets.push_back (i);
            quiet = false;
            lastLoud = i;
        }
        else if (i - lastLoud > 64)
            quiet = true;
    const int beatLen = (int) std::lround (sr * 60.0 / bpm);
    if (onsets.empty())
        return "the pattern's hit never played";
    if (std::abs (onsets[0] - beatLen) > 600)
        return "the pattern's hit played at sample " + juce::String (onsets[0]) + ", not on beat 2 (" + juce::String (beatLen) + ")";
    if (played != 2)
        return "the hit played " + juce::String (played) + " times in 5.5 beats, not twice (it should loop back round)";
    return {};
}

void BeatsScreen::fillDemoPattern()
{
    auto& p = drumData.padPattern;
    p = {};
    p.bars = 2;
    auto add = [&p] (int pad, double beat) { PadHit h; h.pad = pad; h.beat = beat; p.hits.push_back (h); };
    for (double b : { 0.0, 4.0 }) add (0, b);            // horn
    for (double b : { 2.5, 6.5 }) add (1, b);
    for (double b : { 0.0, 1.5, 4.0, 5.5 }) add (12, b); // kick
    for (double b : { 2.0, 6.0 }) add (13, b);           // snare
    for (double b = 0.5; b < 8.0; b += 1.0) add (14, b); // hat
    patternChanged();
}

void BeatsScreen::fillStarterBeat()
{
    setBpm (90.0);
    auto& p = drumData.padPattern;
    p = {};
    p.bars = 2;
    auto add = [&p] (int pad, double beat, float velocity = 0.9f) { PadHit h; h.pad = pad; h.beat = beat; h.velocity = velocity; p.hits.push_back (h); };
    for (double b : { 0.0, 2.5, 4.0, 6.5, 6.75 }) add (0, b);        // kick
    for (double b : { 1.0, 3.0, 5.0, 7.0 }) add (1, b);              // snare
    for (double b = 0.0; b < 7.5; b += 0.5) add (4, b, std::fmod (b, 1.0) < 0.25 ? 0.8f : 0.55f);   // hats
    add (5, 7.5);                                                     // open hat
    for (double b : { 0.0, 4.0 }) add (12, b);                        // 808
    patternChanged();
}

juce::Rectangle<int> BeatsScreen::tourSpot (int which) const
{
    if (padsView == nullptr)
        return {};
    const auto r = which == 0 ? padsView->padBounds (12).toNearestInt() : juce::Rectangle<int> (12, 108, padsView->getWidth() - 24, 106);
    return getLocalArea (padsView.get(), r);
}

void BeatsScreen::fillDemoKit()
{
    // A demo kit of 14 pads like Colin's design, built from the sounds in the library.
    padKit.name = "PORCHKIT";
    padKit.pads = {};
    auto find = [this] (const char* name) -> const SoundItem*
    {
        for (auto& item : library.items())
            if (item.name == name)
                return &item;
        return nullptr;
    };
    const int twoSeconds = (int) (2.0 * crate::kSampleRate);
    struct Demo { int pad; const char* sound; const char* name; int slice; };   // slice: which 2 second piece, or -1 for all
    const Demo demo[] = {
        { 0, "Kids choir \"ooh\"", "Choir ooh", -1 }, { 1, "Thumb piano", "Thumb pno", -1 },
        { 4, "Porch rain", "Porch 1", 0 }, { 5, "Porch rain", "Porch 2", 1 }, { 6, "Porch rain", "Porch 3", 2 }, { 7, "Porch rain", "Porch 4", 3 },
        { 8, "Kitchen pots hit", "Pots hit", -1 }, { 9, "Tape snare", "Clap", -1 }, { 10, "Tape snare", "Rim", -1 }, { 11, "Rhodes stab Cm", "Shaker", -1 },
        { 12, "Dusty kick", "Dusty kick", -1 }, { 13, "Tape snare", "Tape snare", -1 }, { 14, "Tape snare", "Hat cl", -1 }, { 15, "Tape snare", "Hat op", -1 } };
    for (auto& d : demo)
        if (auto* item = find (d.sound))
        {
            if (d.slice >= 0)
                padKit.assign (d.pad, item->id, d.slice * twoSeconds, (d.slice + 1) * twoSeconds, d.name);
            else
                padKit.assign (d.pad, item->id, 0, -1, d.name);
        }
    pushPads();
    selected = 12;
    bank = 0;
    padsView->refresh();
}

//==============================================================================
void BeatsScreen::resized()
{
    drumView->setBounds (getLocalBounds());
    padsView->setBounds (getLocalBounds());
}
