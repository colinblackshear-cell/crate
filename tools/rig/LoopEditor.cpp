#include "LoopEditor.h"
#include "Vintage.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

namespace
{
const juce::Colour startColour { 0xff81c784 };
const juce::Colour endColour { 0xffff6e6e };
LoopEditor::Snap lastSnap = LoopEditor::snapBeats;   // remembered while the Rig is open

juce::String beatsLongText (double beats, int beatsPerBar)
{
    const double bars = beats / juce::jmax (1, beatsPerBar);
    if (std::abs (bars - std::round (bars)) < 1.0e-6)
        return juce::String ((int) std::round (bars)) + (std::abs (bars - 1.0) < 1.0e-6 ? " bar" : " bars");
    if (std::abs (beats - std::round (beats)) < 1.0e-6)
        return juce::String ((int) std::round (beats)) + (std::abs (beats - 1.0) < 1.0e-6 ? " beat" : " beats");
    return juce::String (beats, 2) + " beats";
}
}

double LoopEditor::snapUnit (Snap s, int beatsPerBar)
{
    switch (s)
    {
        case snapBars:       return juce::jmax (1, beatsPerBar);
        case snapBeats:      return 1.0;
        case snapSixteenths: return 0.25;
        case snapFree:       return 0.0;
    }
    return 0.0;
}

double LoopEditor::playBeatsFor (double s, double e)
{
    return juce::jmax (1.0, std::round (std::abs (e - s)));
}

juce::String LoopEditor::placeText (double beat, int beatsPerBar)
{
    const int bpb = juce::jmax (1, beatsPerBar);
    beat = juce::jmax (0.0, beat);
    const double rounded = std::round (beat * 4.0) / 4.0;
    const bool onGrid = std::abs (beat - rounded) < 1.0e-6;
    const double b = onGrid ? rounded : beat;
    const int bar = (int) std::floor (b / bpb + 1.0e-9);
    const double inBar = b - bar * bpb;
    const int beatNum = (int) std::floor (inBar + 1.0e-9);
    juce::String text = "bar " + juce::String (bar + 1) + " beat " + juce::String (beatNum + 1);
    const double rest = inBar - beatNum;
    if (! onGrid)
        text << " +" << juce::String (rest, 2);
    else if (rest > 1.0e-6)
        text << "." << juce::String ((int) std::round (rest * 4.0) + 1);
    return text;
}

//==============================================================================
// The whole loop, the part that plays, and the two handles.
class LoopEditor::Wave : public juce::Component
{
public:
    explicit Wave (LoopEditor& o) : owner (o)
    {
        const auto& trim = owner.engine.getLooper().getTrim (owner.layer);
        const auto& audio = trim.whole;
        const int n = audio.getNumSamples();
        peaks.assign (1000, 0.0f);
        if (n > 0)
        {
            const float* l = audio.getReadPointer (0);
            const float* r = audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1));
            for (size_t p = 0; p < peaks.size(); ++p)
            {
                const int from = (int) ((double) p / peaks.size() * n);
                const int to = juce::jmin (n, (int) ((double) (p + 1) / peaks.size() * n));
                float m = 0;
                for (int i = from; i < to; ++i)
                    m = juce::jmax (m, std::abs (l[i]), std::abs (r[i]));
                peaks[p] = juce::jmin (1.0f, m);
            }
        }
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const auto waveArea = area.withTrimmedTop (handleH);
        g.setColour (Theme::background);
        g.fillRoundedRectangle (waveArea, 6.0f);

        const double beats = wholeBeats();
        if (beats <= 0)
            return;
        const int bpb = owner.engine.getTransport().getBeatsPerBar();

        // Grid: bars bright, beats faint, with bar numbers.
        g.setFont (juce::FontOptions (11.0f));
        for (int b = 0; b <= (int) std::floor (beats + 1.0e-9); ++b)
        {
            const float x = xOf (b);
            const bool bar = b % juce::jmax (1, bpb) == 0;
            g.setColour (Theme::text.withAlpha (bar ? 0.22f : 0.08f));
            g.drawVerticalLine ((int) x, waveArea.getY(), waveArea.getBottom());
            if (bar && b < beats - 1.0e-9)
            {
                g.setColour (Theme::textDim);
                g.drawText (juce::String (b / juce::jmax (1, bpb) + 1), (int) x + 4, (int) waveArea.getY() + 2, 30, 14,
                            juce::Justification::centredLeft);
            }
        }

        // The waveform: bright inside the part that plays, dim outside it.
        const float mid = waveArea.getCentreY();
        const float half = waveArea.getHeight() * 0.42f;
        const float x0 = xOf (owner.start), x1 = xOf (owner.end);
        const int w = getWidth();
        for (int x = 0; x < w; ++x)
        {
            const auto p = peaks[(size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) ((double) x / juce::jmax (1, w - 1) * (peaks.size() - 1)))];
            const bool inside = x >= x0 && x <= x1;
            g.setColour (inside ? vintage::colours::loops : Theme::textDim.withAlpha (0.35f));
            g.drawVerticalLine (x, mid - juce::jmax (0.5f, p * half), mid + juce::jmax (0.5f, p * half) + 1);
        }

        // Shade what's cut off.
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (waveArea.getX(), waveArea.getY(), x0 - waveArea.getX(), waveArea.getHeight()));
        g.fillRect (juce::Rectangle<float> (x1, waveArea.getY(), waveArea.getRight() - x1, waveArea.getHeight()));
        g.setColour (vintage::colours::loops.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (x0, waveArea.getY(), x1 - x0, waveArea.getHeight()));

        // The playhead, where the Rig is in the part that plays now.
        auto& looper = owner.engine.getLooper();
        if (owner.engine.getTransport().isPlaying())
        {
            const auto& trim = looper.getTrim (owner.layer);
            const double pos = looper.getLayer (owner.layer).position.load();
            const float x = xOf (trim.start + pos * (trim.end - trim.start));
            g.setColour (Theme::text);
            g.fillRect (x - 1.0f, waveArea.getY(), 2.0f, waveArea.getHeight());
        }

        drawHandle (g, x0, startColour, "START", true);
        drawHandle (g, x1, endColour, "END", false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const float x0 = xOf (owner.start), x1 = xOf (owner.end);
        const float x = (float) e.position.x;
        const float grab = 12.0f;
        if (std::abs (x - x0) <= grab && std::abs (x - x0) <= std::abs (x - x1))
            dragging = Drag::start;
        else if (std::abs (x - x1) <= grab)
            dragging = Drag::end;
        else if (x > x0 && x < x1)
            dragging = Drag::slide;
        else
            dragging = x < x0 ? Drag::start : Drag::end;   // click outside: that handle jumps there
        downBeat = beatOf (x);
        downStart = owner.start;
        downEnd = owner.end;
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double beats = wholeBeats();
        const double unit = LoopEditor::snapUnit (lastSnap, owner.engine.getTransport().getBeatsPerBar());
        const double minLen = juce::jmax (unit, 0.25);
        const auto snapTo = [unit] (double b) { return unit > 0 ? std::round (b / unit) * unit : b; };
        const double beat = beatOf ((float) e.position.x);

        if (dragging == Drag::start)
            owner.start = juce::jlimit (0.0, owner.end - minLen, snapTo (beat));
        else if (dragging == Drag::end)
            owner.end = juce::jlimit (owner.start + minLen, beats, snapTo (beat));
        else if (dragging == Drag::slide)
        {
            const double len = downEnd - downStart;
            const double s = juce::jlimit (0.0, beats - len, snapTo (downStart + beat - downBeat));
            owner.start = s;
            owner.end = s + len;
        }
        owner.updateText();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging != Drag::none)
            owner.apply();
        dragging = Drag::none;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const float x = (float) e.position.x;
        const bool nearHandle = std::abs (x - xOf (owner.start)) <= 12.0f || std::abs (x - xOf (owner.end)) <= 12.0f;
        setMouseCursor (nearHandle ? juce::MouseCursor::LeftRightResizeCursor
                        : (x > xOf (owner.start) && x < xOf (owner.end)) ? juce::MouseCursor::DraggingHandCursor
                                                                         : juce::MouseCursor::PointingHandCursor);
    }

private:
    enum class Drag { none, start, end, slide };
    static constexpr float handleH = 20.0f;

    double wholeBeats() const { return owner.engine.getLooper().getTrim (owner.layer).wholeBeats; }
    float xOf (double beat) const
    {
        const double beats = wholeBeats();
        return beats > 0 ? (float) (beat / beats * getWidth()) : 0.0f;
    }
    double beatOf (float x) const { return juce::jlimit (0.0, 1.0, (double) x / juce::jmax (1, getWidth())) * wholeBeats(); }

    void drawHandle (juce::Graphics& g, float x, juce::Colour c, const juce::String& label, bool isStart) const
    {
        g.setColour (c);
        g.fillRect (x - 1.5f, handleH, 3.0f, (float) getHeight() - handleH);
        const float w = 46.0f;
        juce::Rectangle<float> flag (isStart ? x - 1.5f : x + 1.5f - w, 0.0f, w, handleH - 2.0f);
        flag = flag.withX (juce::jlimit (0.0f, (float) getWidth() - w, flag.getX()));
        g.fillRoundedRectangle (flag, 4.0f);
        g.setColour (Theme::background);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, flag, juce::Justification::centred);
    }

    LoopEditor& owner;
    std::vector<float> peaks;
    Drag dragging = Drag::none;
    double downBeat = 0, downStart = 0, downEnd = 0;
};

//==============================================================================
LoopEditor::LoopEditor (RigEngine& e, int l) : engine (e), layer (l)
{
    auto& looper = engine.getLooper();
    valid = looper.openTrim (layer);
    const auto& trim = looper.getTrim (layer);
    start = trim.start;
    end = trim.end;

    wave = std::make_unique<Wave> (*this);
    addAndMakeVisible (*wave);

    title.setText ("Edit loop " + juce::String (layer + 1), juce::dontSendNotification);
    title.setFont (vintage::cond (22.0f, vintage::Weight::extra, 0.06f));
    title.setColour (juce::Label::textColourId, Theme::text);
    info.setFont (juce::FontOptions (14.0f));
    info.setColour (juce::Label::textColourId, Theme::text);
    hint.setText ("Drag the green START and red END handles, or drag the middle to slide. "
                  "You hear it as you let go. It always stays in time with the Rig.",
                  juce::dontSendNotification);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setColour (juce::Label::textColourId, Theme::textDim);
    for (auto* c : { &title, &info, &hint })
        addAndMakeVisible (c);

    snap.addItem ("Snap to bars", snapBars + 1);
    snap.addItem ("Snap to beats", snapBeats + 1);
    snap.addItem ("Snap to 16ths", snapSixteenths + 1);
    snap.addItem ("Free (no snap)", snapFree + 1);
    snap.setSelectedId (lastSnap + 1, juce::dontSendNotification);
    snap.onChange = [this] { lastSnap = (Snap) (snap.getSelectedId() - 1); };
    snap.setTooltip ("What the handles stick to as you drag them. Free goes anywhere; the loop then "
                     "plays a hair faster or slower so it's still a whole number of beats.");
    addAndMakeVisible (snap);

    length.setTextWhenNothingSelected ("Length...");
    const int bpb = engine.getTransport().getBeatsPerBar();
    juce::StringArray lengths;
    for (double b : { 1.0, 2.0, (double) bpb, 2.0 * bpb, 4.0 * bpb, 8.0 * bpb })
        if (b <= trim.wholeBeats + 1.0e-9)
            lengths.addIfNotAlreadyThere (beatsLongText (b, bpb));
    length.addItemList (lengths, 1);
    length.onChange = [this]
    {
        const auto text = length.getText();
        const int bpbNow = engine.getTransport().getBeatsPerBar();
        const double n = text.getDoubleValue();
        setLength (text.contains ("bar") ? n * bpbNow : n);
        length.setSelectedId (0, juce::dontSendNotification);
    };
    length.setTooltip ("Make the loop this long, from its start.");
    addAndMakeVisible (length);

    whole.setTooltip ("Puts the loop back the way you recorded it.");
    whole.onClick = [this]
    {
        start = 0;
        end = engine.getLooper().getTrim (layer).wholeBeats;
        apply();
    };
    play.setClickingTogglesState (false);
    play.setTooltip ("Starts or stops the Rig, to hear the loop.");
    play.onClick = [this] { engine.getTransport().togglePlay(); };
    done.setColour (juce::TextButton::buttonColourId, Theme::accent);
    done.setColour (juce::TextButton::textColourOffId, Theme::background);
    done.onClick = [this] { if (onClose) onClose(); };
    for (auto* c : { &whole, &play, &done })
        addAndMakeVisible (c);

    startLabel.setText ("Start", juce::dontSendNotification);
    endLabel.setText ("End", juce::dontSendNotification);
    startLabel.setColour (juce::Label::textColourId, startColour);
    endLabel.setColour (juce::Label::textColourId, endColour);
    startLabel.setJustificationType (juce::Justification::centredRight);
    endLabel.setJustificationType (juce::Justification::centredRight);
    startLeft.onClick = [this] { nudge (true, -1); };
    startRight.onClick = [this] { nudge (true, 1); };
    endLeft.onClick = [this] { nudge (false, -1); };
    endRight.onClick = [this] { nudge (false, 1); };
    for (auto* b : { &startLeft, &startRight, &endLeft, &endRight })
        b->setTooltip ("Moves this handle by one snap step (a 64th of a beat in Free).");
    for (auto* c : std::initializer_list<juce::Component*> { &startLabel, &startLeft, &startRight, &endLabel, &endLeft, &endRight })
        addAndMakeVisible (c);

    updateText();
}

LoopEditor::~LoopEditor() = default;

void LoopEditor::tick()
{
    if (engine.getLooper().getState (layer) == RigLooper::State::empty)
        valid = false;
    play.setButtonText (engine.getTransport().isPlaying() ? "Stop" : "Play");
    wave->repaint();
}

void LoopEditor::setRange (double s, double e)
{
    const double beats = engine.getLooper().getTrim (layer).wholeBeats;
    start = juce::jlimit (0.0, beats, juce::jmin (s, e));
    end = juce::jlimit (0.0, beats, juce::jmax (s, e));
    apply();
}

void LoopEditor::apply()
{
    if (end - start < 1.0e-3)
        return;
    engine.getLooper().setTrim (layer, start, end, playBeatsFor (start, end));
    updateText();
    wave->repaint();
}

void LoopEditor::nudge (bool startHandle, int direction)
{
    const double unit = snapUnit (lastSnap, engine.getTransport().getBeatsPerBar());
    const double step = (unit > 0 ? unit : 1.0 / 64.0) * direction;
    const double beats = engine.getLooper().getTrim (layer).wholeBeats;
    const double minLen = juce::jmax (unit, 0.25);
    if (startHandle)
        start = juce::jlimit (0.0, end - minLen, start + step);
    else
        end = juce::jlimit (start + minLen, beats, end + step);
    apply();
}

void LoopEditor::setLength (double beatsLong)
{
    if (beatsLong <= 0)
        return;
    const double beats = engine.getLooper().getTrim (layer).wholeBeats;
    if (start + beatsLong > beats + 1.0e-9)
        start = juce::jmax (0.0, beats - beatsLong);
    end = juce::jmin (beats, start + beatsLong);
    apply();
}

void LoopEditor::updateText()
{
    const int bpb = engine.getTransport().getBeatsPerBar();
    const double len = end - start;
    const double plays = playBeatsFor (start, end);
    juce::String text = "Plays from " + placeText (start, bpb) + " up to " + placeText (end, bpb) + ": " + beatsLongText (plays, bpb);
    if (std::abs (len - plays) > 1.0e-6)
        text << " (stretched a touch from " << juce::String (len, 2) << " beats to stay in time)";
    info.setText (text, juce::dontSendNotification);
}

void LoopEditor::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    vintage::drawWell (g, getLocalBounds().toFloat().reduced (1.0f), 12.0f);
    g.setColour (vintage::colours::selected);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 12.0f, 2.0f);
}

void LoopEditor::resized()
{
    auto area = getLocalBounds().reduced (16, 12);
    auto top = area.removeFromTop (32);
    title.setBounds (top.removeFromLeft (150));
    done.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    play.setBounds (top.removeFromRight (70));
    top.removeFromRight (8);
    whole.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    length.setBounds (top.removeFromRight (110));
    top.removeFromRight (8);
    snap.setBounds (top.removeFromRight (150));

    area.removeFromTop (6);
    info.setBounds (area.removeFromTop (24));
    hint.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (4);
    auto nudges = area.removeFromBottom (30);
    startLabel.setBounds (nudges.removeFromLeft (44));
    nudges.removeFromLeft (4);
    startLeft.setBounds (nudges.removeFromLeft (32));
    nudges.removeFromLeft (2);
    startRight.setBounds (nudges.removeFromLeft (32));
    endRight.setBounds (nudges.removeFromRight (32));
    nudges.removeFromRight (2);
    endLeft.setBounds (nudges.removeFromRight (32));
    nudges.removeFromRight (4);
    endLabel.setBounds (nudges.removeFromRight (44));
    area.removeFromBottom (6);
    wave->setBounds (area);
}
