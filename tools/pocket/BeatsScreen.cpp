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
    std::function<juce::uint32()> guide;      // the Make a beat lesson's hints: steps to glow (drawn only, never written)

    juce::Rectangle<float> cell (int step) const
    {
        const float gap = 5.0f, w = ((float) getWidth() - gap * 7.0f) / 8.0f, h = 46.0f;
        return { (float) (step % 8) * (w + gap), (float) (step / 8) * (h + 6.0f), w, h };
    }

    // What a tap does (also what the self-check taps with).
    void toggle (int step)
    {
        auto& cellValue = pattern().hits[(size_t) voice()][(size_t) step];
        cellValue = cellValue > 0 ? 0 : 1;
        if (onEdit)
            onEdit (step, false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pattern();
        const int v = voice(), now = playhead();
        const auto hints = guide ? guide() : 0u;
        for (int s = 0; s < 16; ++s)
        {
            auto r = cell (s);
            const bool on = p.hits[(size_t) v][(size_t) s] > 0;
            drawRaised (g, r, on ? Look::amber : Look::dark, false, 11.0f);
            if (! on && (hints >> s & 1u) != 0)
            {
                auto body = r.withTrimmedBottom (3);
                g.setColour (col::amber.withAlpha (0.2f));
                g.fillRoundedRectangle (body, 11.0f);
                g.setColour (col::amber.withAlpha (0.35f));
                g.drawRoundedRectangle (body.expanded (2.5f), 13.0f, 2.0f);
                g.setColour (col::amber);
                g.drawRoundedRectangle (body.reduced (1.5f), 10.0f, 2.6f);
            }
            g.setFont (condensed (10.5f));
            g.setColour (on ? col::ink.withAlpha (0.45f) : juce::Colour (0x55f1e7d1));
            g.drawText (juce::String (s + 1), r.withTrimmedBottom (5).reduced (7, 3), juce::Justification::bottomRight);
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
            toggle (held);
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

// All 12 voices by 16 steps, with the playing step outlined. Tapping a row picks that voice; the picked row
// is the one being edited, so it is boxed in amber.
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
        const float left = 40.0f, top = 8.0f, rowH = (r.getHeight() - 16.0f) / 12.0f;
        const float colW = (r.getWidth() - left - 12.0f) / 16.0f;
        const int now = playhead(), picked = voice();

        for (int v = 0; v < 12; ++v)
        {
            const float y = top + (float) v * rowH;
            const bool isPicked = v == picked;
            if (isPicked)
            {
                g.setColour (juce::Colour (0xff3a3226));
                g.fillRoundedRectangle (4, y - 0.5f, r.getWidth() - 8, rowH + 1.0f, 3.0f);
                g.setColour (col::amber);
                g.drawRoundedRectangle (4, y - 0.5f, r.getWidth() - 8, rowH + 1.0f, 3.0f, 1.4f);
                juce::Path t;   // a pointer at the row being edited
                t.addTriangle (6.5f, y + rowH * 0.5f - 3.0f, 6.5f, y + rowH * 0.5f + 3.0f, 10.5f, y + rowH * 0.5f);
                g.fillPath (t);
            }
            g.setColour (isPicked ? col::lcdAmber : col::inkFaint);
            g.setFont (isPicked ? condensedBold (10.5f) : condensedBold (9.5f));
            g.drawText (voiceTags[v], juce::Rectangle<float> (13, y, 26, rowH), juce::Justification::centredLeft);
            for (int s = 0; s < 16; ++s)
            {
                const bool on = p.hits[(size_t) v][(size_t) s] > 0;
                g.setColour (on ? (isPicked ? col::amber : col::amber.withAlpha (0.8f)) : juce::Colour (isPicked ? 0xff4d4437 : 0xff3a332c));
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
        const int v = (int) std::floor ((e.position.y - top) / rowH);
        if (v >= 0 && v < 12 && onPick)
            onPick (v);
    }
};

// PLAY LIVE: the 12 voices as big pads, three across. A hit sounds at once on touch-down.
class LivePads : public juce::Component
{
public:
    std::function<void (int)> onHit;
    std::function<bool (int)> hasHits;

    // Finger order: the pads you hit most (kick, snare, clap) are on the bottom row, under the thumbs.
    static int voiceAt (int slot)
    {
        static const int order[12] = { 3, 7, 8, 4, 5, 6, 9, 10, 11, 0, 1, 2 };
        return order[slot];
    }

    juce::Rectangle<float> padRect (int slot) const
    {
        const float gap = 8.0f, w = ((float) getWidth() - gap * 2.0f) / 3.0f, h = ((float) getHeight() - gap * 3.0f) / 4.0f;
        return { (float) (slot % 3) * (w + gap), (float) (slot / 3) * (h + gap), w, h };
    }

    bool flashing() const
    {
        const auto now = juce::Time::getMillisecondCounter();
        for (auto t : flashUntil)
            if (t > now)
                return true;
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        const auto now = juce::Time::getMillisecondCounter();
        for (int slot = 0; slot < 12; ++slot)
        {
            const int v = voiceAt (slot);
            const bool lit = flashUntil[(size_t) v] > now;
            auto r = padRect (slot);
            drawRaised (g, r, lit ? Look::amber : Look::dark, lit, 14.0f);
            const bool used = hasHits != nullptr && hasHits (v);
            g.setColour (lit ? col::ink : used ? col::lcdAmber : juce::Colour (0xfff1e7d1));
            g.setFont (condensedBold (r.getHeight() > 60.0f ? 22.0f : 18.0f));
            g.drawText (voiceLabels[v], r.withTrimmedBottom (4), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int slot = 0; slot < 12; ++slot)
            if (padRect (slot).expanded (4, 4).contains (e.position))
            {
                const int v = voiceAt (slot);
                flashUntil[(size_t) v] = juce::Time::getMillisecondCounter() + 140;
                if (onHit)
                    onHit (v);
                repaint (padRect (slot).expanded (6).toNearestInt());
                return;
            }
    }

    void mouseUp (const juce::MouseEvent&) override { repaint(); }

private:
    std::array<juce::uint32, 12> flashUntil {};
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
            auto c = juce::Point<float> (r.getX() + 19, r.getCentreY());
            g.setColour (ink);
            const bool on = owner.beatsEngine.isPlaying();
            if (on)
                g.fillRoundedRectangle (c.x - 6, c.y - 6, 12, 12, 2.5f);
            else
            {
                juce::Path t;
                t.addTriangle (c.x - 5, c.y - 8, c.x - 5, c.y + 8, c.x + 8, c.y);
                g.fillPath (t);
            }
            g.setFont (spaced (condensedBold (16.0f), 0.06f));
            g.drawText (on ? "STOP" : "PLAY", r.withTrimmedLeft (32).withTrimmedRight (4), juce::Justification::centredLeft);
        };
        play.onClick = [this]
        {
            owner.beatsEngine.setPlaying (! owner.beatsEngine.isPlaying());
            if (owner.beatsEngine.isPlaying() && owner.clickWanted != nullptr)
                owner.beatsEngine.setClick (owner.clickWanted());
            play.repaint();
        };
        addAndMakeVisible (play);

        for (auto* c : { &stepsChip, &liveChip })
        {
            c->setFontHeight (16.0f);
            addAndMakeVisible (*c);
        }
        stepsChip.setText ("STEPS");
        liveChip.setText ("PLAY LIVE");
        stepsChip.onClick = [this] { owner.setDrumMode (0); };
        liveChip.onClick = [this] { owner.setDrumMode (1); };

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
            g.setFont (spaced (condensed (15.0f)));
            g.drawFittedText (text[d.variation], r.withTrimmedRight (22).withTrimmedLeft (4).toNearestInt(), juce::Justification::centred, 1, 0.8f);
            juce::Path t;
            t.addTriangle (r.getRight() - 20, r.getCentreY() - 3, r.getRight() - 11, r.getCentreY() - 3, r.getRight() - 15.5f, r.getCentreY() + 3);
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
        grid.guide = [this] { return owner.guideVoice == owner.voice ? owner.guideMask : 0u; };
        grid.onEdit = [this] (int step, bool accent)
        {
            // a box tapped on (or held for an accent) sounds, so a stopped machine still answers the finger
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

        pads.hasHits = [this] (int v)
        {
            const auto& pat = d.edited();
            for (int s = 0; s < 16; ++s)
                if (pat.hits[(size_t) v][(size_t) s] > 0)
                    return true;
            return false;
        };
        pads.onHit = [this] (int v)
        {
            owner.voice = v;
            owner.hitLive (v);
            overview.repaint();
        };
        addChildComponent (pads);

        undoHit.setText ("UNDO HIT");
        clearPattern.setText ("CLEAR PATTERN");
        for (auto* b : { &undoHit, &clearPattern })
        {
            b->setFontHeight (16.0f);
            addChildComponent (*b);
        }
        undoHit.onClick = [this] { owner.undoLiveHit(); };
        clearPattern.onClick = [this] { owner.clearDrumPattern(); };

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

    // The mode or the lesson layout changed: show the parts that belong, and lay them out again.
    void layoutChanged()
    {
        const bool live = owner.drumModeValue == 1, compact = owner.lessonLayout && ! live;
        grid.setVisible (! live);
        for (auto* k : knobs)
            k->setVisible (! live && ! compact);
        for (auto& p : patterns)
            p.setVisible (! compact);
        pads.setVisible (live);
        undoHit.setVisible (live);
        clearPattern.setVisible (live);
        overview.setInterceptsMouseClicks (! live, false);   // (a stray touch on the strip must not hit a voice)
        resized();
        refresh();
    }

    void refresh()
    {
        const bool live = owner.drumModeValue == 1;
        stepsChip.setLook (live ? Look::cream : Look::amber);
        liveChip.setLook (live ? Look::amber : Look::cream);
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
        if (pads.isVisible() && (pads.flashing() || padsFlashed))
        {
            padsFlashed = pads.flashing();
            pads.repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (owner.drumModeValue == 1)
            return;   // (the strip, the buttons and the pads draw themselves)
        auto well = juce::Rectangle<float> (16, (float) wellY, (float) getWidth() - 32, 100);
        drawWell (g, well, 16.0f);
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

        const float ly = (float) grid.getY() - 26.0f, rx = (float) getWidth() - 16.0f;
        auto line = juce::Rectangle<float> (16, ly, rx - 16 - 74, 22);
        const auto name = juce::String (voiceLabels[v]).toUpperCase() + (d.editB ? U8(" · B") : juce::String());
        g.setFont (spaced (condensedBold (13.5f), 0.1f));
        const float nameW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "EDITING " + name) + 6.0f;
        g.setColour (col::amberDark);
        g.drawText ("EDITING " + name, line.withWidth (nameW), juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (spaced (condensed (12.5f), 0.1f));
        g.drawFittedText (U8("TAP A BOX \xc2\xb7 HOLD FOR ACCENT"), line.withTrimmedLeft (nameW).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (col::red);
        g.fillEllipse (rx - 66, ly + 6, 9, 9);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawEllipse (rx - 66, ly + 6, 9, 9, 1.0f);
        g.setColour (col::inkDim);
        g.drawText ("ACCENT", juce::Rectangle<float> (rx - 54, ly, 54, 22), juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int W = getWidth() - 32, H = getHeight();
        const bool live = owner.drumModeValue == 1, compact = owner.lessonLayout && ! live;
        // The mode chips and the drum machine's own PLAY on top (small, once per song), the pattern slots below.
        stepsChip.setBounds (16, 8, 68, 44);
        liveChip.setBounds (90, 8, 94, 44);
        play.setBounds (getWidth() - 16 - 82, 8, 82, 44);
        variation.setBounds (190, 8, juce::jmax (70, play.getX() - 6 - 190), 44);
        const float slotW = ((float) W - 5.0f * 6.0f) / 6.0f;
        for (int i = 0; i < kPatternSlots; ++i)
            patterns[i].setBounds (juce::roundToInt (16.0f + (float) i * (slotW + 6.0f)), 58, juce::roundToInt (slotW), 44);

        if (live)
        {
            // What it writes (the whole pattern, read-only), UNDO HIT and CLEAR PATTERN, then the pads low.
            overview.setBounds (16, 108, W, 92);
            const int half = (W - 8) / 2;
            undoHit.setBounds (16, 206, half, 44);
            clearPattern.setBounds (16 + half + 8, 206, W - half - 8, 44);
            pads.setBounds (16, 258, W, juce::jmax (120, H - 258 - 6));
            return;
        }

        // Settings up top, the steps and knobs low (under the thumb).
        wellY = compact ? 58 : 108;
        const int knobY = H - 112;
        grid.setBounds (16, wellY + 136, W, 98);
        const int overviewBottom = compact ? H - owner.lessonReserve - 8 : knobY - 8;
        const int overviewTop = grid.getBottom() + 10;
        overview.setVisible (! compact || overviewBottom - overviewTop >= 72);
        overview.setBounds (16, overviewTop, W, compact ? juce::jmax (40, overviewBottom - overviewTop) : juce::jmax (96, overviewBottom - overviewTop));
        const int w = W / 5;
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (16 + i * w, knobY, w, 108);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (owner.drumModeValue == 1)
            return;
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
    PButton play, stepsChip, liveChip, patterns[kPatternSlots], variation, undoHit, clearPattern;
    int wellY = 108;
    StepGrid grid;
    StepOverview overview;
    LivePads pads;
    juce::OwnedArray<KnobView> knobs;
    int lastPlayhead = -2;
    bool playingShown = false, padsFlashed = false;
};

//==============================================================================
class BeatsScreen::PadsView : public BelowSwitch
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
        modeExit.setFontHeight (15.0f);
        modeExit.onClick = [this] { owner.endPadMode(); };
        addChildComponent (modeExit);
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
        modeExit.setVisible (owner.levelsOf >= 0 || owner.copyFrom >= 0);
        modeExit.setText (owner.copyFrom >= 0 ? "Cancel" : "Exit");
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

        if (owner.levelsOf >= 0 || owner.copyFrom >= 0)
            paintModeBanner (g);
        else
            paintPattern (g);

        drawWell (g, wellBounds(), 18.0f);
        const auto sounding = owner.beatsEngine.soundingPads();
        for (int i = 0; i < 16; ++i)
        {
            const int index = owner.bank * 16 + i;
            const auto& pad = owner.padKit.pads[(size_t) index];
            auto r = padBounds (i);
            const bool levels = owner.levelsOf >= 0, copying = owner.copyFrom >= 0;
            const bool lit = levels ? levelDown == i : copying ? index == owner.copyFrom : (index == owner.selected || (sounding >> index & 1) != 0);
            drawRaised (g, r, lit ? Look::amber : Look::pad, false, 11.0f);
            auto inner = r.withTrimmedBottom (3.0f);
            if (levels)
            {
                // 16 LEVELS: how loud each pad plays, as a bar and a number
                auto bar = juce::Rectangle<float> (inner.getX() + 8, inner.getBottom() - 10, inner.getWidth() - 16, 4.0f);
                g.setColour (juce::Colour (0x33f1e7d1));
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (lit ? col::ink : col::amber);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) (i + 1) / 16.0f), 2.0f);
                g.setFont (condensedBold (22.0f));
                g.setColour (lit ? col::ink : juce::Colour (0xfff1e7d1));
                g.drawText (juce::String (i + 1), inner.withTrimmedBottom (10), juce::Justification::centred);
                continue;
            }
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
        modeExit.setBounds (w - 16 - 84, 112, 84, 46);
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
            if (owner.levelsOf < 0 && owner.copyFrom < 0)
                owner.togglePatternPlay();   // (16 LEVELS and COPY have their own banner there)
            return;
        }
        touchPad (padAt (e.position), e.position);
    }

    // A finger lands on a pad (the mouse, and the self-checks).
    void touchPad (int pad, juce::Point<float> at)
    {
        if (pad < 0)
            return;
        if (owner.copyFrom >= 0)
            return owner.copyPadTo (pad);   // COPY: this is the pad it goes to (it doesn't sound)
        if (owner.levelsOf >= 0)
        {
            // 16 LEVELS: the picked pad's sound, quiet to loud along the bank
            levelDown = pad % 16;
            held = pad;
            const float velocity = (float) (levelDown + 1) / 16.0f;
            owner.beatsEngine.padOn (owner.levelsOf, velocity);
            owner.recordHit (owner.levelsOf, velocity);
            repaint();
            return;
        }
        owner.selected = pad;
        held = pad;
        longPress = false;
        // the sound starts on the touch, before anything else; the hold only decides later whether a menu follows
        if (! owner.padKit.pads[(size_t) pad].empty())
        {
            owner.beatsEngine.padOn (pad);
            owner.recordHit (pad, 1.0f);
        }
        if (! owner.recPads)
            hold.press (at);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override { hold.move (e.position); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        hold.release();
        if (renaming)
        {
            renaming = false;
            if (lcdBounds().contains (e.position))
                owner.renameKit();
            return;
        }
        liftPad();
    }

    void liftPad()
    {
        hold.release();
        if (held >= 0)
            owner.beatsEngine.padOff (owner.levelsOf >= 0 ? owner.levelsOf : held);
        held = -1;
        if (levelDown >= 0)
        {
            levelDown = -1;
            repaint();
        }
    }

    // The pad's hold menu. The finger is still on the pad: its sound plays on, and stops when the finger lifts.
    void holdFired()
    {
        if (held < 0)
            return;
        longPress = true;
        owner.holdPad (held);
    }

    juce::Rectangle<float> padRectFor (int pad) const { return padBounds (pad % 16); }
    void fireHoldNow() { hold.fireNow(); }
    bool holdIsArmed() const { return hold.isArmed(); }

private:
    void paintModeBanner (juce::Graphics& g)
    {
        auto r = stripBounds();
        drawWell (g, r, 12.0f);
        auto in = r.reduced (12, 0).withTrimmedRight (92);
        const int src = owner.levelsOf >= 0 ? owner.levelsOf : owner.copyFrom;
        const auto& p = owner.padKit.pads[(size_t) juce::jlimit (0, kPads - 1, src)];
        g.setColour (col::lcdAmber);
        g.setFont (spaced (condensedBold (14.5f), 0.08f));
        g.drawFittedText (owner.levelsOf >= 0 ? U8("16 LEVELS \xc2\xb7 ") + Kit::padLabel (src) + " " + p.name.toUpperCase()
                                              : U8("COPY ") + Kit::padLabel (src) + " " + p.name.toUpperCase() + U8(" TO\xe2\x80\xa6"),
                          in.withHeight (r.getHeight() * 0.5f + 4.0f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
        g.setColour (juce::Colour (0xffcdbfa5));
        g.setFont (small (12.5f));
        g.drawFittedText (owner.levelsOf >= 0 ? "Quiet on the first pad, loud on the last. Hits record at the level you play."
                                              : "Tap the pad it goes to. Any bank: pick the bank first.",
                          in.withTrimmedTop (r.getHeight() * 0.5f + 4.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.75f);
    }

    BeatsScreen& owner;
    PButton exportButton { "Export MPC" }, soundsButton { "Sounds", Look::amber }, banks[4], editPads { "Edit pads" }, toMac { "To mac" };
    PButton barsButton { "2 bars" }, quantButton { "1/16" }, undoButton { "Undo" }, recordSound { U8("\xe2\x97\x8f Record a sound") },
            modeExit { "Exit", Look::amber };
    pocket::HoldDetector hold { [this] { holdFired(); } };
    bool wasPlaying = false;
    juce::uint64 lastMask = 0;
    int held = -1, levelDown = -1;
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

//==============================================================================
// Press and hold a pad
HoldSpec BeatsScreen::padMenuSpec (int pad)
{
    HoldSpec s;
    const auto& p = padKit.pads[(size_t) pad];
    s.title = "PAD " + Kit::padLabel (pad) + U8(" \xc2\xb7 ") + (p.empty() ? juce::String ("EMPTY") : p.name.toUpperCase());
    auto swap = [this, pad] { host.holdMenu (swapMenuSpec (pad), *padsView, padsView->padRectFor (pad).getCentre(), padsView->padRectFor (pad)); };
    if (p.empty())
    {
        s.items.push_back ({ U8("PUT A SOUND ON IT\xe2\x80\xa6"), {}, false, true, swap });
        s.items.push_back ({ "RECORD A NEW SOUND", {}, false, true, [this] { host.openRecorder(); } });
        return s;
    }
    s.items.push_back ({ "SOUND PAGE", U8("trim \xc2\xb7 effects \xc2\xb7 chop"), false, true, [this, pad] { host.openSamplePage (pad); } });
    s.items.push_back ({ U8("SWAP SOUND\xe2\x80\xa6"), {}, false, true, swap });
    s.items.push_back ({ "RECORD A NEW SOUND", {}, false, true, [this] { host.openRecorder(); } });
    s.items.push_back ({ U8("VOLUME \xc2\xb7 PAN \xc2\xb7 TUNE"), {}, false, true, [this, pad] { host.openPadSheet (pad); } });
    s.items.push_back ({ "16 LEVELS", {}, false, true, [this, pad] { startLevels (pad); } });
    s.items.push_back ({ "COPY TO ANOTHER PAD", {}, false, true, [this, pad] { startCopy (pad); } });
    s.items.push_back ({ "REMOVE SOUND", "undo brings it back", true, true, [this, pad] { removePad (pad); } });
    return s;
}

// SWAP SOUND: your sounds, newest first, in the same card (it scrolls)
HoldSpec BeatsScreen::swapMenuSpec (int pad)
{
    HoldSpec s;
    s.title = "SOUND FOR PAD " + Kit::padLabel (pad);
    s.spotlight = false;
    auto items = library.items();
    std::stable_sort (items.begin(), items.end(), [] (const SoundItem& a, const SoundItem& b) { return a.date > b.date; });
    for (auto& item : items)
    {
        const auto id = item.id;
        s.items.push_back ({ item.name.toUpperCase(), Kit::lengthText (item.seconds), false, true, [this, pad, id] { swapSound (pad, id); } });
    }
    if (items.empty())
        s.items.push_back ({ "NO SOUNDS YET", "record or import one", false, false, {} });
    return s;
}

void BeatsScreen::swapSound (int pad, const juce::String& soundId)
{
    if (! juce::isPositiveAndBelow (pad, kPads))
        return;
    const auto old = padKit.pads[(size_t) pad];
    if (! padKit.assign (pad, soundId))
        return host.toast ("That sound couldn't be opened");
    auto& now = padKit.pads[(size_t) pad];
    if (! old.empty())
    {
        now.volume = old.volume;
        now.pan = old.pan;
        now.tune = old.tune;
        now.mode = old.mode;
    }
    padsChanged();
    selectPad (pad);
    host.toast ("Pad " + Kit::padLabel (pad) + " has \"" + now.name + "\"");
}

void BeatsScreen::removePad (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kPads) || padKit.pads[(size_t) pad].empty())
        return;
    removed = padKit.pads[(size_t) pad];
    removedPad = pad;
    haveRemoved = true;
    padKit.clear (pad);
    beatsEngine.padOff (pad);
    if (levelsOf == pad || copyFrom == pad)
        endPadMode();
    padsChanged();
    host.toastAction ("Removed \"" + removed.name + "\" from pad " + Kit::padLabel (pad), "UNDO", [this] { undoRemovePad(); });
}

void BeatsScreen::undoRemovePad()
{
    if (! haveRemoved || ! juce::isPositiveAndBelow (removedPad, kPads))
        return;
    padKit.pads[(size_t) removedPad] = removed;
    haveRemoved = false;
    padsChanged();
    selectPad (removedPad);
    host.toast ("Pad " + Kit::padLabel (removedPad) + " has its sound back");
}

void BeatsScreen::startLevels (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kPads) || padKit.pads[(size_t) pad].empty())
        return;
    copyFrom = -1;
    levelsOf = pad;
    selectPad (pad);
    host.toast ("16 LEVELS: pad " + Kit::padLabel (pad) + " on all 16 pads, quiet to loud. EXIT puts the pads back.");
}

void BeatsScreen::startCopy (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kPads) || padKit.pads[(size_t) pad].empty())
        return;
    levelsOf = -1;
    copyFrom = pad;
    selectPad (pad);
    host.toast ("Tap the pad to copy " + Kit::padLabel (pad) + " to. CANCEL stops.");
}

void BeatsScreen::copyPadTo (int target)
{
    const int from = copyFrom;
    if (from < 0 || ! juce::isPositiveAndBelow (target, kPads))
        return;
    if (target == from)
        return endPadMode();
    copiedWas = padKit.pads[(size_t) target];
    copiedTo = target;
    haveCopied = true;
    padKit.pads[(size_t) target] = padKit.pads[(size_t) from];
    copyFrom = -1;
    padsChanged();
    selectPad (target);
    host.toastAction ("Copied " + Kit::padLabel (from) + " to " + Kit::padLabel (target), "UNDO", [this] { undoCopy(); });
}

void BeatsScreen::undoCopy()
{
    if (! haveCopied || ! juce::isPositiveAndBelow (copiedTo, kPads))
        return;
    padKit.pads[(size_t) copiedTo] = copiedWas;
    haveCopied = false;
    padsChanged();
    host.toast ("Pad " + Kit::padLabel (copiedTo) + " is as it was");
}

void BeatsScreen::endPadMode()
{
    levelsOf = copyFrom = -1;
    if (padsView != nullptr)
    {
        padsView->refresh();
        padsView->repaint();
    }
}

void BeatsScreen::testTouchPad (int pad) { padsView->touchPad (pad, padsView->padRectFor (pad).getCentre()); }
void BeatsScreen::testHoldElapsed() { padsView->fireHoldNow(); }
void BeatsScreen::testLiftPad() { padsView->liftPad(); }

void BeatsScreen::holdPad (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kPads))
        return;
    host.holdMenu (padMenuSpec (pad), *padsView, padsView->padRectFor (pad).getCentre(), padsView->padRectFor (pad));
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
    recDrumsOn = false;
    levelsOf = copyFrom = -1;
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
    haveRemoved = haveCopied = false;   // (another song's pads: nothing to put back here)
    patternUndo.clear();
    liveUndo.clear();
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

void BeatsScreen::fillDemoDrums()
{
    drumData.patterns[0][0] = DrumData::demoPattern();
    drumsChanged();
}

//==============================================================================
// DRUMS: STEPS and PLAY LIVE
void BeatsScreen::setDrumMode (int mode)
{
    mode = mode == 1 ? 1 : 0;
    if (mode == drumModeValue)
        return;
    drumModeValue = mode;
    drumView->layoutChanged();
    if (onDrumModeChanged)
        onDrumModeChanged();
}

void BeatsScreen::setLessonLayout (bool on, int reservePx)
{
    lessonLayout = on;
    lessonReserve = reservePx;
    drumView->layoutChanged();
}

void BeatsScreen::setGuide (int v, juce::uint32 mask)
{
    guideVoice = v;
    guideMask = mask;
    drumView->repaint();
}

juce::Rectangle<int> BeatsScreen::stepsBounds() const
{
    return getLocalArea (drumView.get(), drumView->grid.getBounds());
}

void BeatsScreen::tapStep (int step)
{
    drumView->grid.toggle (juce::jlimit (0, 15, step));
}

int BeatsScreen::hitLive (int v)
{
    v = juce::jlimit (0, crate::drums::kVoices - 1, v);
    voice = v;
    int written = -1;
    bool soundNow = true;
    auto& e = beatsEngine;
    const bool counting = inCountIn != nullptr && inCountIn();
    const bool beforePass = recDrumsOn && e.clockBeat() < recDrumsFrom - 0.25;
    if (e.clockRunning() && ! counting && ! beforePass)
    {
        auto& pat = drumData.edited();
        const double stepLen = crate::drums::stepBeats (drumData.settings.stepRate);
        const double pos = e.clockBeat() / stepLen;                      // steps since the clock started
        const long long target = (long long) std::floor (pos + 0.5);     // the nearest step line
        written = (int) (target % juce::jmax (1, pat.length));
        auto& cellValue = pat.hits[(size_t) v][(size_t) written];
        if (cellValue == 0)
        {
            liveUndo.push_back ({ drumData.slot, drumData.editB, pat });
            if (liveUndo.size() > 64)
                liveUndo.erase (liveUndo.begin());
            cellValue = 1;
            drumsChanged();
        }
        // A hit a little before its step line: it sounds now, and the grid skips that voice on that step this once,
        // so it isn't heard again when the grid gets there (the next time round it plays as written).
        if (e.isPlaying() && (double) target > pos)
            e.skipStepOnce (v, drumData.slot, drumData.editB, written, ((double) target + 0.9) * stepLen);
    }
    if (soundNow)
        e.hitVoice (v);
    if (drumView != nullptr)
        drumView->refresh();
    return written;
}

void BeatsScreen::undoLiveHit()
{
    if (liveUndo.empty())
        return host.toast ("No hit to undo");
    auto last = liveUndo.back();
    liveUndo.pop_back();
    drumData.patterns[(size_t) last.slot][last.b ? 1 : 0] = last.pattern;
    drumsChanged();
}

void BeatsScreen::clearDrumPattern()
{
    auto& pat = drumData.edited();
    if (pat.isEmpty())
        return host.toast ("The pattern is already empty");
    liveUndo.push_back ({ drumData.slot, drumData.editB, pat });
    pat.hits = {};
    pat.accent = {};
    drumsChanged();
    host.toast ("Pattern cleared. UNDO HIT brings it back.");
}

void BeatsScreen::setRecordingDrums (bool on, double fromBeat)
{
    recDrumsOn = on;
    recDrumsFrom = fromBeat;
}

juce::String BeatsScreen::selfCheckLive()
{
    // Runs the engine by hand (no sound card).
    const auto savedData = drumData;
    const auto savedUndo = liveUndo;
    const auto savedCount = inCountIn;
    const bool savedRec = recDrumsOn;
    inCountIn = nullptr;
    recDrumsOn = false;
    juce::String why;
    std::vector<float> l (512), r (512);
    auto pump = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); beatsEngine.render (l.data(), r.data(), 512); };

    // A new song's drum machine and pad pattern are empty.
    {
        const DrumData fresh;
        for (auto& pair : fresh.patterns)
            for (auto& p : pair)
                if (! p.isEmpty())
                    why = "a new song's drum patterns are not empty";
        if (! fresh.padPattern.hits.empty())
            why = "a new song's pad pattern is not empty";
    }

    if (why.isEmpty())
    {
        drumData = DrumData();
        liveUndo.clear();
        beatsEngine.prepare (44100.0);
        beatsEngine.setBpm (drumData.bpm);
        beatsEngine.setDrums (drumData);
        beatsEngine.setClick (false);
        beatsEngine.setPlaying (true);
        for (int i = 0; i < 400 && ! (beatsEngine.clockRunning() && beatsEngine.clockBeat() >= 5.1); ++i)
            pump();
        const double beat = beatsEngine.clockBeat();
        const int expected = (int) ((long long) std::floor (beat * 4.0 + 0.5) % 16);
        const int got = hitLive (0);
        int count = 0;
        for (int s = 0; s < 16; ++s)
            count += drumData.edited().hits[0][(size_t) s] > 0 ? 1 : 0;
        if (got != expected || drumData.edited().hits[0][(size_t) expected] == 0 || count != 1)
            why = "a PLAY LIVE hit at beat " + juce::String (beat, 2) + " should land on step " + juce::String (expected + 1) + " alone, got step "
                  + juce::String (got + 1) + " and " + juce::String (count) + " steps set";
        else
        {
            undoLiveHit();
            if (! drumData.edited().isEmpty())
                why = "UNDO HIT did not take the live hit off";
            else if (! (beatsEngine.setPlaying (false), pump(), pump(), hitLive (1) == -1 && drumData.edited().isEmpty()))
                why = "a PLAY LIVE hit with the song stopped was written into the pattern";
        }
    }

    // A hit just before its step line sounds once: now, and not again when the grid reaches that step. The next
    // time round the grid plays it.
    if (why.isEmpty())
    {
        drumData = DrumData();
        liveUndo.clear();
        beatsEngine.setPlaying (false);
        pump();
        beatsEngine.setDrums (drumData);
        beatsEngine.setPlaying (true);
        const double stepLen = crate::drums::stepBeats (drumData.settings.stepRate);
        auto aheadMs = [&]
        {
            const double pos = beatsEngine.clockBeat() / stepLen;
            return (std::floor (pos + 0.5) - pos) * stepLen * 60000.0 / beatsEngine.getBpm();
        };
        for (int i = 0; i < 2000 && ! (beatsEngine.clockRunning() && beatsEngine.clockBeat() > 4.0 && aheadMs() > 12.0 && aheadMs() < 24.0); ++i)
            pump();
        const double ahead = aheadMs();
        const int before = beatsEngine.voiceHits (0);
        const int step = hitLive (0);
        const double stepBeat = beatsEngine.clockBeat();
        for (int i = 0; i < 400 && beatsEngine.clockBeat() < stepBeat + 3.0 * stepLen; ++i)
            pump();
        const int thisPass = beatsEngine.voiceHits (0) - before;
        for (int i = 0; i < 800 && beatsEngine.clockBeat() < stepBeat + 3.0 * stepLen + drumData.edited().length * stepLen; ++i)
            pump();
        const int nextPass = beatsEngine.voiceHits (0) - before - thisPass;
        liveReport = "a hit " + juce::String (ahead, 1) + " ms before step " + juce::String (step + 1) + " sounded " + juce::String (thisPass)
                     + (thisPass == 1 ? " time" : " times") + " on that pass, and " + juce::String (nextPass) + " the next time round";
        if (step < 0 || thisPass != 1 || nextPass != 1)
            why = "PLAY LIVE double hit: " + liveReport;
    }

    beatsEngine.setPlaying (false);
    pump();
    drumData = savedData;
    liveUndo = savedUndo;
    inCountIn = savedCount;
    recDrumsOn = savedRec;
    beatsEngine.setDrums (drumData);
    drumView->refresh();
    return why;
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
