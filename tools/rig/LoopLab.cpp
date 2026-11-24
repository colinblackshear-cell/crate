#include "LoopLab.h"
#include "Vintage.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;
namespace Warp = crate::Warp;

namespace
{
const juce::Colour markerColour { 0xffffb23e };
const juce::Colour stretchedColour { 0xffffb23e };
const juce::Colour squeezedColour { 0xff5fb3e8 };
const juce::Colour waveColour { 0xff7cd992 };

LoopLab::Page lastPage = LoopLab::warpPage;   // remembered while Kaleidobox is open

void light (juce::Button& b, bool on)
{
    vintage::setLit (b, vintage::Lit::current);
    b.setToggleState (on, juce::dontSendNotification);
}

juce::String dot() { return juce::String::fromUTF8 ("\xc2\xb7"); }

constexpr double grids[] { 1.0, 0.5, 0.25, 0.125 };

juce::String barsText (double beats, int bpb)
{
    const double bars = beats / juce::jmax (1, bpb);
    if (std::abs (bars - std::round (bars)) < 1.0e-6)
        return juce::String ((int) std::round (bars)) + (std::abs (bars - 1.0) < 1.0e-6 ? " bar" : " bars");
    return juce::String (beats, 2) + " beats";
}

juce::String signedText (double v, int decimals = 0)
{
    const auto t = juce::String (std::abs (v), decimals);
    return v > 0 ? "+" + t : v < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + t : t;
}
}

juce::String LoopLab::gridName (double beats)
{
    if (beats >= 1.0)
        return "Beat";
    return "1/" + juce::String ((int) std::round (4.0 / beats));
}

//==============================================================================
// The recording as the warp plays it: beat lines of the warped loop, the hits it found, and
// the markers (amber) that pin a moment of the recording to a beat. A dashed line shows where a
// moved marker's moment was before. Blue stretches are squeezed, amber ones stretched.
class LoopLab::Wave : public juce::Component
{
public:
    explicit Wave (LoopLab& o) : owner (o) {}

    void setPeaksFrom (const juce::AudioBuffer<float>& audio)
    {
        const int n = audio.getNumSamples();
        if (n == sourceLength && audio.getReadPointer (0) == sourceData)
            return;
        sourceLength = n;
        sourceData = n > 0 ? audio.getReadPointer (0) : nullptr;
        peaks.assign (1200, 0.0f);
        if (n == 0)
            return;
        for (size_t p = 0; p < peaks.size(); ++p)
        {
            const int from = (int) ((double) p / peaks.size() * n);
            const int to = juce::jmin (n, (int) ((double) (p + 1) / peaks.size() * n));
            float m = 0;
            for (int c = 0; c < audio.getNumChannels(); ++c)
                for (int i = from; i < to; ++i)
                    m = juce::jmax (m, std::abs (audio.getSample (c, i)));
            peaks[p] = m;
        }
        float top = 0;
        for (float p : peaks)
            top = juce::jmax (top, p);
        if (top > 0)
            for (auto& p : peaks)
                p /= top;
    }

    const Warp::Plan& plan() const { return preview ? *preview : owner.lab().get (owner.layer).plan; }

    void paint (juce::Graphics& g) override
    {
        const auto& l = owner.lab().get (owner.layer);
        const auto& p = plan();
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff15120f));
        g.fillRoundedRectangle (r, 8.0f);
        if (! p.isValid())
            return;
        const float ruler = 20.0f;
        auto body = r.withTrimmedTop (ruler);

        // stretched / squeezed
        {
            std::vector<std::pair<double, double>> anchors { { 0.0, 0.0 } };
            for (const auto& m : p.markers)
                anchors.push_back ({ m.source, m.target });
            anchors.push_back ({ p.sourceBeats, p.targetBeats });
            const double straight = p.targetBeats / p.sourceBeats;
            for (size_t k = 1; k < anchors.size(); ++k)
            {
                const double ds = anchors[k].first - anchors[k - 1].first, dt = anchors[k].second - anchors[k - 1].second;
                if (ds <= 0)
                    continue;
                const double ratio = dt / ds / straight;
                if (std::abs (ratio - 1.0) < 0.01)
                    continue;
                const float x0 = xOf (anchors[k - 1].second), x1 = xOf (anchors[k].second);
                g.setColour ((ratio > 1.0 ? stretchedColour : squeezedColour).withAlpha (0.12f));
                g.fillRect (juce::Rectangle<float> (x0, body.getY(), x1 - x0, body.getHeight()));
            }
        }

        // beat lines and bar numbers
        const int bpb = juce::jmax (1, owner.engine.getTransport().getBeatsPerBar());
        const int beats = (int) std::ceil (p.targetBeats - 1.0e-6);
        const bool every = getWidth() / juce::jmax (1, beats) > 22;
        for (int b = 0; b < beats; ++b)
        {
            const bool bar = b % bpb == 0;
            if (! bar && ! every)
                continue;
            const float x = xOf (b);
            g.setColour (Theme::text.withAlpha (bar ? 0.30f : 0.09f));
            g.fillRect (x, body.getY(), 1.0f, body.getHeight());
            if (bar || getWidth() / juce::jmax (1, beats) > 40)
            {
                g.setColour (bar ? Theme::text : Theme::textDim.withAlpha (0.7f));
                g.setFont (vintage::cond (12.0f, vintage::Weight::bold, 0.06f));
                const auto label = bar ? juce::String (b / bpb + 1) : juce::String (b / bpb + 1) + "." + juce::String (b % bpb + 1);
                g.drawText (label, juce::Rectangle<float> (x + 4.0f, r.getY() + 2.0f, 40.0f, ruler - 4.0f), juce::Justification::centredLeft);
            }
        }

        // the sound, as the warp plays it
        if (! peaks.empty())
        {
            const float mid = body.getCentreY() - 6.0f, half = body.getHeight() * 0.42f;
            g.setColour (waveColour.withAlpha (l.playingOriginal ? 0.35f : 0.9f));
            for (int x = 0; x < getWidth(); x += 2)
            {
                const double s = p.sourceAt ((x + 0.5) / getWidth() * p.targetBeats);
                const auto idx = (size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) (s / p.sourceBeats * peaks.size()));
                const float h = juce::jmax (1.0f, std::sqrt (peaks[idx]) * half);
                g.fillRect ((float) x, mid - h, 1.5f, 2.0f * h);
            }
        }

        // hits
        g.setColour (Theme::text.withAlpha (0.55f));
        for (double h : l.hits)
        {
            const float x = xOf (p.targetAt (h));
            g.fillRect (x - 0.75f, body.getBottom() - 16.0f, 1.5f, 14.0f);
        }

        // markers
        for (int i = 0; i < (int) p.markers.size(); ++i)
        {
            const auto& m = p.markers[(size_t) i];
            const float x = xOf (m.target), was = xOf (m.source * p.targetBeats / p.sourceBeats);
            if (std::abs (x - was) > 2.0f)
            {
                g.setColour (markerColour.withAlpha (0.5f));
                const float dashes[] { 5.0f, 4.0f };
                g.drawDashedLine (juce::Line<float> (was, body.getY(), was, body.getBottom()), dashes, 2, 1.5f);
            }
            g.setColour (i == dragging ? juce::Colours::white : markerColour);
            g.fillRect (x - 1.0f, body.getY(), 2.0f, body.getHeight());
            juce::Path tri;
            tri.addTriangle (x - 7.0f, body.getY(), x + 7.0f, body.getY(), x, body.getY() + 10.0f);
            g.fillPath (tri);
            if (! m.manual)
            {
                g.setColour (juce::Colour (0xff15120f));
                g.fillEllipse (x - 2.0f, body.getY() + 2.0f, 4.0f, 4.0f);
            }
        }

        // where it's playing
        const auto& layer = owner.engine.getLooper().getLayer (owner.layer);
        if (owner.engine.getTransport().isPlaying() && ! l.playingOriginal)
        {
            const float x = (float) (layer.position.load() * getWidth());
            g.setColour (Theme::text);
            g.fillRect (x - 1.0f, body.getY(), 2.0f, body.getHeight());
        }

        g.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.1f));
        if (l.playingOriginal)
        {
            g.setColour (Theme::text);
            g.drawText ("PLAYING THE ORIGINAL", body.reduced (10.0f, 6.0f), juce::Justification::topRight);
        }
        else if (owner.lab().isRendering (owner.layer))
        {
            g.setColour (markerColour);
            g.drawText ("WORKING IT OUT...", body.reduced (10.0f, 6.0f), juce::Justification::topRight);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& l = owner.lab().get (owner.layer);
        const float x = e.position.x;
        int index = markerNear (x);
        if (e.mods.isPopupMenu())
        {
            if (index < 0)
                return;
            juce::PopupMenu menu;
            menu.addItem ("Remove this marker", [safe = juce::Component::SafePointer<Wave> (this), index]
            {
                if (safe != nullptr)
                    safe->owner.lab().removeMarker (safe->owner.layer, index);
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        preview = l.plan;
        if (index < 0)
        {
            // A hit's tick: pin it with a marker where it plays now, ready to drag.
            double best = 7.0;
            double hitTarget = -1;
            for (double h : l.hits)
            {
                const double t = preview->targetAt (h);
                if (std::abs (xOf (t) - x) < best)
                {
                    best = std::abs (xOf (t) - x);
                    hitTarget = t;
                }
            }
            if (hitTarget >= 0)
                index = Warp::addMarker (*preview, hitTarget);
        }
        dragging = index;
        if (dragging < 0)
            preview.reset();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0 || ! preview)
            return;
        double t = beatOf (e.position.x);
        if (! e.mods.isAltDown())
        {
            const double grid = juce::jmax (0.125, owner.lab().get (owner.layer).grid);
            const double line = std::round (t / grid) * grid;
            if (std::abs (xOf (line) - e.position.x) < 9.0f)
                t = line;
        }
        Warp::moveMarker (*preview, dragging, t);
        owner.info.setText (placeText (preview->markers[(size_t) dragging]), juce::dontSendNotification);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0 && preview)
        {
            const auto p = *preview;
            preview.reset();
            dragging = -1;
            owner.lab().setPlan (owner.layer, p);
            owner.refresh();
        }
        dragging = -1;
        preview.reset();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (markerNear (e.position.x) < 0)
            owner.addMarkerAt (beatOf (e.position.x));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (markerNear (e.position.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
    }

private:
    float xOf (double targetBeat) const
    {
        const auto& p = plan();
        return p.targetBeats > 0 ? (float) (targetBeat / p.targetBeats * getWidth()) : 0.0f;
    }
    double beatOf (float x) const { return juce::jlimit (0.0, 1.0, (double) x / juce::jmax (1, getWidth())) * plan().targetBeats; }

    int markerNear (float x) const
    {
        const auto& p = plan();
        int best = -1;
        float dist = 7.0f;
        for (int i = 0; i < (int) p.markers.size(); ++i)
            if (std::abs (xOf (p.markers[(size_t) i].target) - x) < dist)
            {
                dist = std::abs (xOf (p.markers[(size_t) i].target) - x);
                best = i;
            }
        return best;
    }

    juce::String placeText (const Warp::Marker& m) const
    {
        const auto& p = plan();
        const double ms = (m.target - m.source * p.targetBeats / p.sourceBeats) * 60000.0 / juce::jmax (20.0, owner.engine.getTransport().getBpm());
        return "This moment plays at " + LoopEditor::placeText (m.target, owner.engine.getTransport().getBeatsPerBar())
               + (std::abs (ms) < 0.5 ? juce::String (", where it was played.")
                                      : ", " + juce::String (std::abs (ms), 0) + " ms " + (ms < 0 ? "earlier" : "later") + " than played.");
    }

    LoopLab& owner;
    std::vector<float> peaks;
    int sourceLength = -1;
    const float* sourceData = nullptr;
    std::optional<Warp::Plan> preview;
    int dragging = -1;
};

//==============================================================================
LoopLab::LoopLab (RigEngine& e, int l, Page startPage) : engine (e), layer (l)
{
    title.setText (juce::String ("Loop Lab ") + dot() + juce::String (" layer ") + juce::String (layer + 1), juce::dontSendNotification);
    title.setFont (vintage::cond (22.0f, vintage::Weight::extra, 0.06f));
    title.setColour (juce::Label::textColourId, Theme::text);
    info.setFont (juce::FontOptions (14.0f));
    info.setColour (juce::Label::textColourId, Theme::text);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setColour (juce::Label::textColourId, Theme::textDim);
    for (auto* c : { &title, &info, &hint })
        addAndMakeVisible (c);

    warpTab.setTooltip ("Line its hits up with the beat, fit it to bars, keep it in time with the song, change its pitch.");
    inOutTab.setTooltip ("Where it starts and ends, slip, nudge, level, fades and reverse.");
    warpTab.onClick = [this] { showPage (warpPage); };
    inOutTab.onClick = [this] { showPage (inOutPage); };
    original.setTooltip ("Hear the loop as you recorded it. Press again for the reshaped one.");
    original.onClick = [this] { pressOriginal(); };
    undoButton.setTooltip ("Takes back the last change in Loop Lab.");
    undoButton.onClick = [this] { pressUndo(); };
    printButton.setTooltip ("Puts the loop as it sounds now on the next empty layer, so you can keep this one and stack another.");
    printButton.onClick = [this] { pressPrint(); };
    done.setColour (juce::TextButton::buttonColourId, Theme::accent);
    done.setColour (juce::TextButton::textColourOffId, Theme::background);
    done.onClick = [this] { if (onClose) onClose(); };
    for (auto* b : { &warpTab, &inOutTab, &original, &undoButton, &printButton, &done })
        addAndMakeVisible (b);

    wave = std::make_unique<Wave> (*this);
    addAndMakeVisible (*wave);

    const auto label = [this] (juce::Label& lb, const juce::String& text)
    {
        lb.setText (text, juce::dontSendNotification);
        lb.setFont (vintage::cond (13.0f, vintage::Weight::bold, 0.08f));
        lb.setColour (juce::Label::textColourId, Theme::textDim);
        addAndMakeVisible (lb);
    };
    label (findLabel, "Find hits");
    label (strengthLabel, "Strength");
    label (transposeLabel, "Transpose");
    label (fineLabel, "Fine");
    label (formantLabel, "Formant");
    label (styleLabel, "Stretch as");

    const auto slider = [this] (juce::Slider& s, double lo, double hi, double step, double reset, const juce::String& tip, std::function<void()> commit)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (lo, hi, step);
        s.setDoubleClickReturnValue (true, reset);
        s.setTooltip (tip);
        s.onDragEnd = commit;
        s.onValueChange = [this, &s, commit]
        {
            refreshLabels();
            if (! s.isMouseButtonDown())
                commit();
        };
        addAndMakeVisible (s);
    };
    slider (sensitivity, 0.0, 1.0, 0.01, 0.6, "How small a sound counts as a hit. Higher finds more.",
            [this] { lab().setSensitivity (layer, (float) sensitivity.getValue()); refresh(); });
    slider (strength, 0.0, 1.0, 0.01, 0.75, "How far Snap to beat pulls each hit onto the beat. 100% is dead on, less keeps some of your feel.",
            [this]
            {
                const auto& st = lab().get (layer);
                lab().setSnap (layer, st.snapOn, st.grid, (float) strength.getValue());
                refresh();
            });
    slider (transpose, -12.0, 12.0, 1.0, 0.0, "Moves the pitch in semitones, the timing stays.", [this] { commitPitch(); });
    slider (fine, -50.0, 50.0, 1.0, 0.0, "Fine tuning in cents.", [this] { commitPitch(); });
    slider (formant, -12.0, 12.0, 0.5, 0.0, "The voice's character (throat size) on its own: down is deeper and bigger, up is smaller and brighter.",
            [this] { commitPitch(); });

    snapButton.setTooltip ("Puts a marker on every hit and pulls it onto the nearest line of the grid, by the strength.");
    snapButton.onClick = [this] { pressSnap(); };
    followButton.setTooltip ("When you change the tempo, this loop is stretched to it again from your recording, keeping its pitch.");
    followButton.onClick = [this] { pressFollow(); };
    resetButton.setTooltip ("Takes every marker off and puts the timing and pitch back as recorded (Undo brings it back).");
    resetButton.onClick = [this] { lab().resetWarp (layer); refresh(); say ("Back to the timing you played. Undo brings the warp back."); };
    for (auto* b : { &snapButton, &followButton, &resetButton })
        addAndMakeVisible (b);

    for (double gb : grids)
        gridBox.addItem (gridName (gb), (int) std::round (4.0 / gb));
    gridBox.setTooltip ("The grid Snap to beat pulls hits onto (and markers stick to as you drag them).");
    gridBox.onChange = [this]
    {
        const double g = 4.0 / juce::jmax (1, gridBox.getSelectedId());
        const auto& st = lab().get (layer);
        if (std::abs (g - st.grid) > 1.0e-9)
        {
            lab().setSnap (layer, st.snapOn, g, st.strength);
            refresh();
        }
    };
    addAndMakeVisible (gridBox);

    fitBox.setTextWhenNothingSelected ("Fit to...");
    fitBox.setTooltip ("Stretches the loop to exactly this many bars, keeping its pitch (in Voice).");
    fitBox.onChange = [this]
    {
        const int id = fitBox.getSelectedId();
        fitBox.setSelectedId (0, juce::dontSendNotification);
        if (id > 0)
            pressFit (id / 100.0);
    };
    addAndMakeVisible (fitBox);

    const Warp::Style styles[] { Warp::Style::voice, Warp::Style::beats, Warp::Style::texture, Warp::Style::tape };
    const char* tips[] { "Keeps the pitch. Best for voices, melodies and most loops.",
                         "Keeps every hit exactly as played and moves it, without stretching. Best for drums.",
                         "Grains: smeary and dreamy. Great for pads, drones and slowed-down voices.",
                         "Pitch goes with speed, like a tape machine sped up or slowed down." };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = styleButtons[(size_t) i];
        b.setButtonText (Warp::styleName (styles[i]).toLowerCase().replaceSection (0, 1, Warp::styleName (styles[i]).substring (0, 1)));
        b.setTooltip (tips[i]);
        b.onClick = [this, s = styles[i]] { pressStyle (s); };
        addAndMakeVisible (b);
    }

    hint.setText ("Drag an amber marker onto a beat line (Option drags it free). Click a hit's tick to pin it, double-click to add "
                  "a marker, right-click one to remove it. You hear each change as soon as it's worked out.",
                  juce::dontSendNotification);

    showPage (startPage);
}

LoopLab::~LoopLab() = default;

LoopLab::Page LoopLab::lastPageUsed() { return lastPage; }

RigLoopLab& LoopLab::lab() { return engine.getLoopLab(); }

bool LoopLab::isValid() const
{
    return engine.getLooper().getState (layer) != RigLooper::State::empty;
}

void LoopLab::say (const juce::String& s)
{
    if (onSay)
        onSay (s);
}

void LoopLab::showPage (Page p)
{
    page = p;
    lastPage = p;
    const bool warp = p == warpPage;
    if (warp)
    {
        inOut.reset();
        opened = lab().open (layer);
        if (opened)
            wave->setPeaksFrom (lab().get (layer).original);
    }
    else
    {
        auto e = std::make_unique<LoopEditor> (engine, layer);
        e->onClose = [this] { if (onClose) onClose(); };
        inOut = std::move (e);
        addAndMakeVisible (*inOut);
    }
    for (auto* c : std::initializer_list<juce::Component*> { wave.get(), &info, &hint, &findLabel, &strengthLabel, &transposeLabel, &fineLabel,
                                                            &formantLabel, &styleLabel, &sensitivity, &strength, &transpose, &fine, &formant,
                                                            &snapButton, &followButton, &resetButton, &gridBox, &fitBox, &original,
                                                            &undoButton, &printButton })
        c->setVisible (warp);
    for (auto& b : styleButtons)
        b.setVisible (warp);
    light (warpTab, warp);
    light (inOutTab, ! (warp));
    refresh();
    resized();
}

void LoopLab::fillFitMenu()
{
    fitBox.clear (juce::dontSendNotification);
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    for (double bars : { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 })
        fitBox.addItem ("Fit to " + barsText (bars * bpb, bpb), (int) std::round (bars * bpb * 100.0));
}

void LoopLab::refresh()
{
    if (page != warpPage)
        return;
    const bool ok = lab().isOpen (layer);
    const auto& l = lab().get (layer);
    const int bpb = engine.getTransport().getBeatsPerBar();
    if (ok)
        wave->setPeaksFrom (l.original);

    if (! sensitivity.isMouseButtonDown())
        sensitivity.setValue (l.sensitivity, juce::dontSendNotification);
    if (! strength.isMouseButtonDown())
        strength.setValue (l.strength, juce::dontSendNotification);
    const float tr = l.plan.settings.transpose;
    if (! transpose.isMouseButtonDown() && ! fine.isMouseButtonDown())
    {
        transpose.setValue (std::round (tr), juce::dontSendNotification);
        fine.setValue (std::round ((tr - std::round (tr)) * 100.0f), juce::dontSendNotification);
    }
    if (! formant.isMouseButtonDown())
        formant.setValue (l.plan.settings.formant, juce::dontSendNotification);
    gridBox.setSelectedId ((int) std::round (4.0 / juce::jmax (0.125, l.grid)), juce::dontSendNotification);
    if (fitBox.getNumItems() == 0)
        fillFitMenu();

    light (snapButton, l.snapOn);
    light (followButton, l.follow);
    light (original, l.playingOriginal);
    for (int i = 0; i < 4; ++i)
        light (styleButtons[(size_t) i], (int) l.plan.settings.style == i);
    undoButton.setEnabled (lab().canUndo (layer));

    int empty = -1;
    for (int i = 0; i < RigLooper::kLayers && empty < 0; ++i)
        if (i != layer && engine.getLooper().getState (i) == RigLooper::State::empty)
            empty = i;
    printButton.setButtonText (empty >= 0 ? "Print to layer " + juce::String (empty + 1) : "Print (no empty layer)");
    printButton.setEnabled (ok && empty >= 0);

    for (auto* c : std::initializer_list<juce::Component*> { &sensitivity, &strength, &transpose, &fine, &formant, &snapButton,
                                                            &followButton, &resetButton, &gridBox, &fitBox, &original })
        c->setEnabled (ok);
    for (auto& b : styleButtons)
        b.setEnabled (ok);

    if (! ok)
        info.setText ("This layer is recording or empty. Loop Lab opens when it has a loop playing.", juce::dontSendNotification);
    else
    {
        const double bpm = engine.getTransport().getBpm();
        juce::String text = l.soundsLike > 0 ? "Sounds like " + juce::String (l.soundsLike, 1) + " BPM" : juce::String ("Tempo: can't tell from its hits");
        text << "  " << dot() << "  song " << juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1)
             << "  " << dot() << "  " << (int) l.hits.size() << ((int) l.hits.size() == 1 ? " hit" : " hits")
             << "  " << dot() << "  " << (int) l.plan.markers.size() << ((int) l.plan.markers.size() == 1 ? " marker" : " markers")
             << "  " << dot() << "  " << barsText (l.plan.targetBeats, bpb)
             << (std::abs (l.plan.targetBeats - l.originalBeats) > 1.0e-6 ? " (recorded " + barsText (l.originalBeats, bpb) + ")" : juce::String());
        info.setText (text, juce::dontSendNotification);
    }
    refreshLabels();
    wave->repaint();
}

void LoopLab::refreshLabels()
{
    strengthLabel.setText ("Strength " + juce::String ((int) std::round (strength.getValue() * 100.0)) + "%", juce::dontSendNotification);
    findLabel.setText ("Find hits " + juce::String ((int) std::round (sensitivity.getValue() * 100.0)) + "%", juce::dontSendNotification);
    transposeLabel.setText ("Transpose " + signedText (transpose.getValue()) + " st", juce::dontSendNotification);
    fineLabel.setText ("Fine " + signedText (fine.getValue()) + " ct", juce::dontSendNotification);
    formantLabel.setText ("Formant " + signedText (formant.getValue(), std::abs (formant.getValue() - std::round (formant.getValue())) > 0.01 ? 1 : 0),
                          juce::dontSendNotification);
}

void LoopLab::commitPitch()
{
    if (! lab().isOpen (layer))
        return;
    auto s = lab().get (layer).plan.settings;
    const float t = (float) (transpose.getValue() + fine.getValue() / 100.0);
    const float f = (float) formant.getValue();
    if (std::abs (s.transpose - t) < 1.0e-4f && std::abs (s.formant - f) < 1.0e-4f)
        return;
    s.transpose = t;
    s.formant = f;
    lab().setSettings (layer, s);
    refresh();
}

void LoopLab::tick()
{
    if (page == warpPage)
    {
        // A trim (In / Out) or anything else changed the loop: open it again as it is now.
        if (! lab().isOpen (layer) && ! lab().isRendering (layer) && engine.getLooper().getState (layer) == RigLooper::State::playing)
            showPage (warpPage);
        const auto& l = lab().get (layer);
        const auto stamp = juce::String (l.serial) + "/" + juce::String (l.appliedSerial) + "/" + juce::String (l.playingVersion) + "/"
                           + juce::String ((int) l.undo.size()) + "/" + juce::String ((int) l.playingOriginal) + "/"
                           + juce::String (engine.getTransport().getBpm(), 2);
        if (stamp != lastStamp)
        {
            lastStamp = stamp;
            refresh();
        }
        wave->repaint();
    }
    else if (inOut != nullptr)
        inOut->tick();
}

void LoopLab::pressStyle (Warp::Style s)
{
    auto settings = lab().get (layer).plan.settings;
    if (settings.style == s)
        return;
    settings.style = s;
    lab().setSettings (layer, settings);
    refresh();
    const char* says[] { "Voice: stretches keeping the pitch.", "Beats: every hit plays as recorded, moved onto its marker.",
                         "Texture: grains, smeary and dreamy.", "Tape: the pitch goes with the speed." };
    say (says[(int) s]);
}

void LoopLab::pressSnap()
{
    const auto& l = lab().get (layer);
    const bool on = ! l.snapOn;
    lab().setSnap (layer, on, l.grid, l.strength);
    refresh();
    say (on ? "Every hit pulled " + juce::String ((int) std::round (l.strength * 100.0f)) + "% of the way onto the " + gridName (l.grid)
                  + " grid. Drag any marker to fix one by hand."
            : "Snap off: only the markers you placed by hand are left.");
}

void LoopLab::pressFollow()
{
    lab().setFollow (layer, ! lab().get (layer).follow);
    refresh();
    say (lab().get (layer).follow ? "This loop keeps its pitch when the tempo changes." : "This loop speeds up and slows down with the tempo, like tape.");
}

void LoopLab::pressOriginal()
{
    lab().setOriginal (layer, ! lab().get (layer).playingOriginal);
    refresh();
}

void LoopLab::pressUndo()
{
    lab().undo (layer);
    refresh();
}

int LoopLab::pressPrint()
{
    const int to = lab().print (layer);
    refresh();
    say (to >= 0 ? "Printed onto layer " + juce::String (to + 1) + ". This layer stays open to keep shaping."
                 : "No empty layer to print onto. Clear one first.");
    return to;
}

void LoopLab::pressFit (double beats)
{
    lab().fitTo (layer, beats);
    refresh();
    say ("Stretched to " + barsText (beats, engine.getTransport().getBeatsPerBar()) + ".");
}

void LoopLab::setTranspose (float semitones, float cents)
{
    transpose.setValue (semitones, juce::dontSendNotification);
    fine.setValue (cents, juce::dontSendNotification);
    commitPitch();
}

void LoopLab::setFormant (float semitones)
{
    formant.setValue (semitones, juce::dontSendNotification);
    commitPitch();
}

void LoopLab::dragMarker (int index, double targetBeat)
{
    lab().moveMarker (layer, index, targetBeat);
    refresh();
}

void LoopLab::addMarkerAt (double targetBeat)
{
    lab().addMarker (layer, targetBeat);
    refresh();
}

void LoopLab::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    vintage::drawWell (g, getLocalBounds().toFloat().reduced (1.0f), 12.0f);
    g.setColour (vintage::colours::selected);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 12.0f, 2.0f);
}

void LoopLab::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    auto top = area.removeFromTop (32);
    title.setBounds (top.removeFromLeft (190));
    warpTab.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (6);
    inOutTab.setBounds (top.removeFromLeft (90));
    done.setBounds (top.removeFromRight (80));
    if (page == inOutPage)
    {
        area.removeFromTop (6);
        if (inOut != nullptr)
            inOut->setBounds (area.withTrimmedLeft (-16).withTrimmedRight (-16).withTrimmedBottom (-12));
        return;
    }
    top.removeFromRight (8);
    printButton.setBounds (top.removeFromRight (150));
    top.removeFromRight (8);
    undoButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (8);
    original.setBounds (top.removeFromRight (90));

    area.removeFromTop (6);
    info.setBounds (area.removeFromTop (22));
    hint.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (4);

    auto row2 = area.removeFromBottom (30);
    area.removeFromBottom (6);
    auto row1 = area.removeFromBottom (30);
    area.removeFromBottom (8);
    wave->setBounds (area);

    {
        auto r = row1;
        findLabel.setBounds (r.removeFromLeft (110));
        sensitivity.setBounds (r.removeFromLeft (110));
        r.removeFromLeft (14);
        snapButton.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (6);
        gridBox.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (10);
        strengthLabel.setBounds (r.removeFromLeft (100));
        strength.setBounds (r.removeFromLeft (120));
        followButton.setBounds (r.removeFromRight (160));
        r.removeFromRight (8);
        fitBox.setBounds (r.removeFromRight (140));
    }
    {
        auto r = row2;
        styleLabel.setBounds (r.removeFromLeft (80));
        for (auto& b : styleButtons)
        {
            b.setBounds (r.removeFromLeft (78));
            r.removeFromLeft (4);
        }
        resetButton.setBounds (r.removeFromRight (100));
        r.removeFromRight (10);
        const int w = r.getWidth() / 3;
        transposeLabel.setBounds (r.removeFromLeft (110));
        transpose.setBounds (r.removeFromLeft (w - 110));
        fineLabel.setBounds (r.removeFromLeft (72));
        fine.setBounds (r.removeFromLeft (w - 72));
        formantLabel.setBounds (r.removeFromLeft (92));
        formant.setBounds (r);
    }
}
