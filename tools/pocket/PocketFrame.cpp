#include "PocketFrame.h"

namespace pocket
{
namespace
{
const juce::Colour kPaper { 0xfff1e7d1 }, kRecTop { 0xffe9573f }, kRecBottom { 0xffc93a26 }, kRecEdge { 0xff8f2617 },
                   kStopTop { 0xff2e2822 }, kStopBottom { 0xff1d1915 }, kPlayGreen { 0xff3c8f56 },
                   kGreenBg { 0xffe6f2e2 }, kGreenEdge { 0xff8fc06a }, kAmberBg { 0xfffbe9c4 };

void label2 (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& top, const juce::String& bottom, juce::Colour ink, float topSize = 19.0f)
{
    g.setColour (ink);
    g.setFont (spaced (condensedBold (topSize), 0.04f));
    g.drawFittedText (top, r.withHeight (r.getHeight() * 0.58f).withTrimmedTop (4).reduced (3, 0).toNearestInt(), juce::Justification::centredBottom, 1, 0.7f);
    g.setColour (ink.withAlpha (0.75f));
    g.setFont (spaced (condensedBold (11.0f), 0.1f));
    g.drawFittedText (bottom, r.withTrimmedTop (r.getHeight() * 0.6f).withTrimmedBottom (4).reduced (3, 0).toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
}
}

const char* toolName (Tool t)
{
    switch (t)
    {
        case Tool::loops: return "LOOPS";
        case Tool::drums: return "DRUMS";
        case Tool::pads: return "PADS";
        case Tool::synth: return "SYNTH";
        case Tool::chords: return "CHORDS";
        case Tool::vocoder: return "VOCODER";
        case Tool::effects: return "EFFECTS";
    }
    return "";
}

juce::Colour toolColour (Tool t)
{
    switch (t)
    {
        case Tool::loops: return col::ink;
        case Tool::drums: return juce::Colour (0xfff0674f);
        case Tool::pads: return col::amber;
        case Tool::synth: return juce::Colour (0xff5fb3c4);
        case Tool::chords: return juce::Colour (0xffb9a3dc);
        case Tool::vocoder: return juce::Colour (0xff82cf6a);
        case Tool::effects: return juce::Colour (0xffe8c27a);
    }
    return col::ink;
}

void drawToolIcon (juce::Graphics& g, Tool t, juce::Point<float> c, juce::Colour ink)
{
    g.setColour (ink);
    const juce::PathStrokeType stroke (2.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    switch (t)
    {
        case Tool::loops:   // two tape reels
        {
            g.drawEllipse (c.x - 11, c.y - 5, 10, 10, 2.3f);
            g.drawEllipse (c.x + 1, c.y - 5, 10, 10, 2.3f);
            g.drawLine (c.x - 6, c.y + 5, c.x + 6, c.y + 5, 2.3f);
            break;
        }
        case Tool::drums:   // a drum from the side
        {
            g.drawEllipse (c.x - 11, c.y - 9, 22, 8, 2.3f);
            juce::Path p;
            p.startNewSubPath (c.x - 11, c.y - 5);
            p.lineTo (c.x - 11, c.y + 6);
            p.quadraticTo (c.x, c.y + 12, c.x + 11, c.y + 6);
            p.lineTo (c.x + 11, c.y - 5);
            g.strokePath (p, stroke);
            break;
        }
        case Tool::pads:
            for (int k = 0; k < 4; ++k)
                g.fillRoundedRectangle (c.x - 10 + (float) (k % 2) * 11.0f, c.y - 10 + (float) (k / 2) * 11.0f, 9, 9, 2.0f);
            break;
        case Tool::synth:   // a wave
        {
            juce::Path p;
            p.startNewSubPath (c.x - 12, c.y);
            p.cubicTo (c.x - 8, c.y - 14, c.x - 3, c.y - 14, c.x, c.y);
            p.cubicTo (c.x + 3, c.y + 14, c.x + 8, c.y + 14, c.x + 12, c.y);
            g.strokePath (p, stroke);
            break;
        }
        case Tool::chords:
            for (int k = 0; k < 3; ++k)
                g.fillRoundedRectangle (c.x - 10 + (float) k * 7.5f, c.y - 10, 5, 20, 1.5f);
            break;
        case Tool::vocoder:   // a mic
        {
            g.fillRoundedRectangle (c.x - 4.5f, c.y - 12, 9, 15, 4.5f);
            juce::Path p;
            p.addCentredArc (c.x, c.y - 1, 8, 8, 0, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f, true);
            p.startNewSubPath (c.x, c.y + 7);
            p.lineTo (c.x, c.y + 12);
            g.strokePath (p, stroke);
            break;
        }
        case Tool::effects:   // two pedal knobs, linked
        {
            g.drawEllipse (c.x - 11, c.y - 11, 11, 11, 2.3f);
            g.drawEllipse (c.x + 0, c.y + 0, 11, 11, 2.3f);
            g.drawLine (c.x - 5.5f, c.y - 5.5f, c.x - 5.5f, c.y - 9.5f, 2.3f);
            g.drawLine (c.x + 5.5f, c.y + 5.5f, c.x + 8.5f, c.y + 2.5f, 2.3f);
            g.drawLine (c.x - 1.5f, c.y + 1.5f, c.x + 1.5f, c.y - 1.5f, 2.3f);
            break;
        }
    }
}

void showSongKeyMenu (juce::Component& target, juce::Rectangle<int> area, SongKey current, std::function<void (SongKey)> picked)
{
    juce::PopupMenu m;
    m.addSectionHeader (U8("THE SONG'S KEY Â· SYNTH, CHORDS AND VOCODER"));
    m.addItem (101, "Minor", true, current.minor);
    m.addItem (102, "Major", true, ! current.minor);
    m.addSeparator();
    for (int k = 0; k < 12; ++k)
        m.addItem (1 + k, SongKey::pitchName (k), true, k == current.root);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withTargetScreenArea (target.localAreaToGlobal (area)),
                     [current, picked] (int r)
                     {
                         if (r == 0 || ! picked)
                             return;
                         auto k = current;
                         if (r == 101 || r == 102)
                             k.minor = r == 101;
                         else
                             k.root = r - 1;
                         picked (k);
                     });
}

void drawChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool lit, bool pressed, float fontHeight, juce::Colour litColour)
{
    if (lit && ! litColour.isTransparent())
    {
        const float lift = pressed ? 1.0f : 3.0f;
        auto body = r.withTrimmedBottom (3.0f).translated (0, pressed ? 2.0f : 0.0f);
        g.setColour (litColour.darker (0.45f));
        g.fillRoundedRectangle (body.translated (0, lift), 10.0f);
        g.setColour (litColour);
        g.fillRoundedRectangle (body, 10.0f);
    }
    else
        drawRaised (g, r, lit ? Look::amber : Look::cream, pressed, 10.0f);
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (fontHeight), 0.06f));
    g.drawFittedText (text, r.withTrimmedBottom (3).translated (0, pressed ? 2.0f : 0.0f).reduced (4, 0).toNearestInt(), juce::Justification::centred, 2, 0.7f);
}

//==============================================================================
juce::Rectangle<float> ThumbBar::buttonRect (int b) const
{
    // the mockup's proportions: BPM, CLICK, the big REC, PLAY, UNDO
    const float x0 = 12.0f, total = (float) getWidth() - 24.0f;
    const float weights[count] = { 62, 62, 76, 74, 62 }, gap = 7.0f;
    float sum = 0;
    for (auto w : weights)
        sum += w;
    const float scale = (total - gap * (count - 1)) / sum;
    float x = x0;
    for (int i = 0; i < b; ++i)
        x += weights[i] * scale + gap;
    const float h = b == rec ? 58.0f : 50.0f;
    return { x, (float) getHeight() * 0.5f - h * 0.5f, weights[b] * scale, h };
}

int ThumbBar::buttonAt (juce::Point<float> p) const
{
    for (int b = 0; b < count; ++b)
        if (buttonRect (b).expanded (3, 8).contains (p))
            return b;
    return -1;
}

void ThumbBar::mouseDown (const juce::MouseEvent& e)
{
    pressed = buttonAt (e.position);
    repaint();
}

void ThumbBar::mouseUp (const juce::MouseEvent& e)
{
    const int was = pressed;
    pressed = -1;
    repaint();
    if (was >= 0 && was == buttonAt (e.position) && onPress)
        onPress (was);
}

void ThumbBar::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);
    g.setColour (col::rule);
    g.fillRect (0, 0, getWidth(), 2);
    const auto s = state ? state() : State();

    auto bpmR = buttonRect (bpm);
    drawRaised (g, bpmR, Look::cream, pressed == bpm, 12.0f);
    label2 (g, bpmR.withTrimmedBottom (3).translated (0, pressed == bpm ? 2.0f : 0.0f), juce::String (juce::roundToInt (s.bpm)), U8("BPM \xc2\xb7 TAP"), col::ink);

    auto clickR = buttonRect (click);
    drawRaised (g, clickR, s.click ? Look::amber : Look::cream, pressed == click, 12.0f);
    label2 (g, clickR.withTrimmedBottom (3).translated (0, pressed == click ? 2.0f : 0.0f), "CLICK", s.click ? "ON" : "OFF", col::ink);

    // REC: red; dark STOP while it counts in or records
    {
        auto r = buttonRect (rec);
        const bool busy = s.recBusy;
        const float down = pressed == rec ? 2.0f : 0.0f;
        auto body = r.withTrimmedBottom (4).translated (0, down);
        const float rad = body.getHeight() * 0.5f;
        g.setColour (busy ? juce::Colours::black : kRecEdge);
        g.fillRoundedRectangle (r.withTrimmedBottom (4).translated (0, 4), rad);
        g.setGradientFill (juce::ColourGradient (busy ? kStopTop : kRecTop, 0, body.getY(), busy ? kStopBottom : kRecBottom, 0, body.getBottom(), false));
        g.fillRoundedRectangle (body, rad);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (body.reduced (0.8f), rad, 1.0f);
        const juce::String text = busy ? "STOP" : "REC";
        g.setFont (spaced (condensedBold (17.0f), 0.04f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        const float x = body.getCentreX() - (tw + 26.0f) * 0.5f;
        if (busy)
        {
            g.setColour (col::red);
            g.fillRoundedRectangle (x, body.getCentreY() - 9, 18, 18, 3.0f);
        }
        else
        {
            g.setColour (kPaper);
            g.fillEllipse (x, body.getCentreY() - 9, 18, 18);
        }
        g.setColour (kPaper);
        g.drawText (text, juce::Rectangle<float> (x + 25, body.getY(), tw + 4, body.getHeight()), juce::Justification::centredLeft);
    }

    // PLAY / STOP for the whole song
    {
        auto r = buttonRect (play);
        drawRaised (g, r, s.playing ? Look::cream : Look::green, pressed == play, 12.0f);
        auto c = r.withTrimmedBottom (3).getCentre().translated (0, pressed == play ? 2.0f : 0.0f);
        if (s.playing)
        {
            g.setColour (col::ink);
            g.fillRoundedRectangle (c.x - 8, c.y - 8, 16, 16, 2.5f);
        }
        else
        {
            juce::Path tri;
            tri.addTriangle (c.x - 7, c.y - 10, c.x - 7, c.y + 10, c.x + 10, c.y);
            g.setColour (col::ink);
            g.fillPath (tri);
        }
    }

    auto undoR = buttonRect (undo);
    drawRaised (g, undoR, Look::cream, pressed == undo, 12.0f);
    label2 (g, undoR.withTrimmedBottom (3).translated (0, pressed == undo ? 2.0f : 0.0f), "UNDO", "LAST TAKE", col::ink, 18.0f);
}

//==============================================================================
juce::Rectangle<float> ToolRow::cellRect (int i) const
{
    const int n = juce::jmax (1, (int) tools.size());
    const float w = ((float) getWidth() - 16.0f) / (float) n;
    return { 8.0f + (float) i * w, 4.0f, w, (float) (getHeight() - bottomInset) - 4.0f };
}

void ToolRow::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);
    for (int i = 0; i < (int) tools.size(); ++i)
    {
        auto cell = cellRect (i);
        const bool on = i == selected;
        const auto t = tools[(size_t) i];
        const float wellW = juce::jmin (56.0f, cell.getWidth() - 4.0f);   // seven tools on a 375-point phone: 47 wide
        auto well = juce::Rectangle<float> (cell.getCentreX() - wellW * 0.5f, cell.getY() + 2, wellW, 40);
        if (on)
        {
            g.setColour (col::well);
            g.fillRoundedRectangle (well, 13.0f);
            if (t != Tool::loops)
            {
                g.setColour (toolColour (t));
                g.drawRoundedRectangle (well.reduced (1.2f), 13.0f, 2.4f);
            }
        }
        else
        {
            g.setColour (col::creamDeep);
            g.fillRoundedRectangle (well, 13.0f);
            if (glow)
            {
                g.setColour (col::amber);
                g.drawRoundedRectangle (well.reduced (1.2f), 13.0f, 3.0f);
            }
        }
        drawToolIcon (g, t, well.getCentre(), on ? kPaper : col::inkDim.withAlpha (0.8f));
        g.setColour (on ? col::ink : col::inkDim);
        g.setFont (spaced (condensedBold (12.0f), 0.1f));
        g.drawFittedText (toolName (t), juce::Rectangle<float> (cell.getX(), well.getBottom() + 2, cell.getWidth(), 16).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
}

void ToolRow::flash()
{
    glow = true;
    repaint();
    juce::Timer::callAfterDelay (2800, [safe = juce::Component::SafePointer<ToolRow> (this)]
    {
        if (safe != nullptr)
        {
            safe->glow = false;
            safe->repaint();
        }
    });
}

void ToolRow::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) tools.size(); ++i)
        if (cellRect (i).contains (e.position) && onSelect)
            return onSelect (tools[(size_t) i]);
}

//==============================================================================
void CountInPanel::set (int beatInBar, int bar, int bars, const juce::String& after)
{
    if (beatInBar == beat && bar == barNow && bars == totalBars && after == afterText)
        return;
    beat = beatInBar;
    barNow = bar;
    totalBars = bars;
    afterText = after;
    repaint();
}

void CountInPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0x55000000));
    g.fillRoundedRectangle (r.translated (0, 3), 16.0f);
    drawWell (g, r, 16.0f);
    g.setColour (juce::Colour (0xffd9cfb8));
    g.setFont (spaced (lcdFont (19.0f), 0.08f));
    const juce::String head = totalBars > 1 ? U8("COUNT-IN \xc2\xb7 BAR ") + juce::String (barNow + 1) + " OF " + juce::String (totalBars) + U8(" \xc2\xb7 CLICK")
                                            : U8("COUNT-IN \xc2\xb7 THE CLICK IS PLAYING");
    g.drawFittedText (head, r.withHeight (32).withTrimmedTop (8).reduced (12, 0).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    const float d = juce::jmin (56.0f, (r.getWidth() - 60.0f) / 4.0f - 12.0f);
    const float gap = 12.0f, total = 4.0f * d + 3.0f * gap;
    for (int i = 0; i < 4; ++i)
    {
        auto c = juce::Rectangle<float> (r.getCentreX() - total * 0.5f + (float) i * (d + gap), r.getY() + 38, d, d);
        const bool lit = i <= beat;
        g.setColour (lit ? juce::Colour (0xfff0846c) : juce::Colour (0xff2e2823));
        g.fillEllipse (c);
        if (i == beat)
        {
            g.setColour (juce::Colour (0xfff0846c).withAlpha (0.35f));
            g.drawEllipse (c.expanded (3), 3.0f);
        }
        g.setColour (lit ? juce::Colour (0xff3a160f) : juce::Colour (0xff6b6155));
        g.setFont (lcdFont (d * 0.75f));
        g.drawText (juce::String (i + 1), c, juce::Justification::centred);
    }
    g.setColour (col::lcdAmber);
    g.setFont (spaced (lcdFont (18.0f), 0.06f));
    g.drawFittedText (afterText, r.withTrimmedTop (r.getHeight() - 30).withTrimmedBottom (6).reduced (12, 0).toNearestInt(), juce::Justification::centred, 1, 0.65f);
}

//==============================================================================
RecSetupSheet::RecSetupSheet()
{
    static const char* counts[3] = { "Off", "1 bar", "2 bars" };
    static const char* lengths[5] = { "1 bar", "2 bars", "4 bars", "8 bars", "Free" };
    for (int i = 0; i < 3; ++i)
    {
        countButtons[i].setText (counts[i]);
        countButtons[i].setFontHeight (17.0f);
        countButtons[i].onClick = [this, i] { if (setCountIn) setCountIn (i); refresh(); };
        addAndMakeVisible (countButtons[i]);
    }
    for (int i = 0; i < 5; ++i)
    {
        lengthButtons[i].setText (lengths[i]);
        lengthButtons[i].setFontHeight (16.0f);
        lengthButtons[i].onClick = [this, i] { if (setLength) setLength (i); refresh(); };
        addAndMakeVisible (lengthButtons[i]);
    }
    done.setFontHeight (17.0f);
    done.onClick = [this] { if (onDone) onDone(); };
    addAndMakeVisible (done);
}

void RecSetupSheet::refresh()
{
    const int c = getCountIn ? getCountIn() : 1, l = getLength ? getLength() : 2;
    for (int i = 0; i < 3; ++i)
        countButtons[i].setLook (i == c ? Look::amber : Look::cream);
    for (int i = 0; i < 5; ++i)
        lengthButtons[i].setLook (i == l ? Look::amber : Look::cream);
    repaint();
}

void RecSetupSheet::resized()
{
    const int w = getWidth();
    done.setBounds (w - 108, 14, 84, 44);
    const int cw = (w - 48 - 16) / 3;
    for (int i = 0; i < 3; ++i)
        countButtons[i].setBounds (24 + i * (cw + 8), 104, cw, 48);
    const int lw = (w - 48 - 4 * 6) / 5;
    for (int i = 0; i < 5; ++i)
        lengthButtons[i].setBounds (24 + i * (lw + 6), 196, lw, 48);
}

void RecSetupSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    g.setColour (col::rule);
    g.fillRoundedRectangle ((float) getWidth() / 2 - 20, 8, 40, 5, 2.5f);
    g.setColour (col::ink);
    g.setFont (condensedBold (26.0f));
    g.drawText ("Rec setup", 24, 18, 200, 36, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.12f));
    g.drawText (U8("COUNT-IN \xc2\xb7 WHEN REC STARTS THE SONG"), 24, 74, getWidth() - 48, 24, juce::Justification::centredLeft);
    g.drawText (U8("LENGTH \xc2\xb7 HOW LONG A TAKE IS"), 24, 166, getWidth() - 48, 24, juce::Justification::centredLeft);
    g.setFont (small (14.0f));
    g.drawFittedText (U8("The same on every tool. The click plays through the count-in even with CLICK off. "
                         "When the song is already playing, a take starts on the next bar. FREE runs until you press STOP."),
                      juce::Rectangle<int> (24, 258, getWidth() - 48, 64), juce::Justification::topLeft, 3);
}

//==============================================================================
HeadphonesSheet::HeadphonesSheet()
{
    wearing.setFontHeight (16.0f);
    without.setFontHeight (14.5f);
    wearing.onClick = [this] { if (onHeadphones) onHeadphones(); };
    without.onClick = [this] { if (onNoHeadphones) onNoHeadphones(); };
    addAndMakeVisible (wearing);
    addAndMakeVisible (without);
}

juce::Rectangle<float> HeadphonesSheet::cardRect (int i) const
{
    const float heights[3] = { 56, 82, 52 };
    float y = 96;
    for (int k = 0; k < i; ++k)
        y += heights[k] + 8;
    return { 18, y, (float) getWidth() - 36, heights[i] };
}

void HeadphonesSheet::resized()
{
    const int w = getWidth();
    const int y = (int) cardRect (2).getBottom() + 12;
    wearing.setBounds (18, y, w - 36, 52);
    without.setBounds (18, y + 60, w - 36, 44);
}

void HeadphonesSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    g.setColour (col::rule);
    g.fillRoundedRectangle ((float) getWidth() / 2 - 20, 8, 40, 5, 2.5f);

    // headphones
    {
        const juce::Point<float> c (38, 50);
        juce::Path band;
        band.addCentredArc (c.x, c.y + 4, 13, 13, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.setColour (col::ink);
        g.strokePath (band, juce::PathStrokeType (2.8f));
        g.fillRoundedRectangle (c.x - 16, c.y + 2, 7, 12, 2.5f);
        g.fillRoundedRectangle (c.x + 9, c.y + 2, 7, 12, 2.5f);
    }
    g.setColour (col::ink);
    g.setFont (condensedBold (27.0f));
    g.drawText ("Headphones on first", 62, 22, getWidth() - 80, 32, juce::Justification::centredLeft);
    g.setFont (small (14.5f));
    g.setColour (col::inkDim);
    g.drawFittedText ("The mic is off. Without headphones the speaker goes back into the mic and squeals.",
                      juce::Rectangle<int> (62, 52, getWidth() - 80, 38), juce::Justification::topLeft, 2);

    const Route now = route ? route() : Route::wired;
    static const char* blurbs[3] = { "Best. You hear yourself with no delay.",
                                     "They add a delay. Fine for hearing the beat, but your own voice comes back late, so Hear Myself is off by default. The take is still lined up in time.",
                                     "The phone's speaker. The mic still records, but nothing from it goes to the speaker." };
    const Route order[3] = { Route::wired, Route::bluetooth, Route::none };
    for (int i = 0; i < 3; ++i)
    {
        auto r = cardRect (i);
        const bool found = order[i] == now;
        const auto bg = ! found ? col::creamLight : i == 0 ? kGreenBg : kAmberBg;
        const auto edge = ! found ? col::rule : i == 0 ? kGreenEdge : col::amber;
        g.setColour (bg);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, found ? 2.0f : 1.2f);
        g.setColour (found ? col::ink : col::inkDim);
        g.setFont (spaced (condensedBold (16.0f), 0.06f));
        g.drawText (routeName (order[i]), r.reduced (12, 0).withHeight (26).translated (0, 5), juce::Justification::centredLeft);
        if (found)
        {
            g.setColour (i == 0 ? juce::Colour (0xff3c8f56) : col::amberDark);
            g.setFont (spaced (condensedBold (13.0f), 0.1f));
            const auto t = r.reduced (12, 0).withHeight (26).translated (0, 5);
            g.drawText (i == 2 ? "NOW" : "FOUND", t.withTrimmedRight (18), juce::Justification::centredRight);
            juce::Path tick;   // (the fonts have no tick mark)
            tick.startNewSubPath (t.getRight() - 13, t.getCentreY());
            tick.lineTo (t.getRight() - 8, t.getCentreY() + 5);
            tick.lineTo (t.getRight(), t.getCentreY() - 5);
            g.strokePath (tick, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (found ? col::ink : col::inkDim);
        g.setFont (small (13.0f));
        g.drawFittedText (blurbs[i], r.reduced (12, 0).withTrimmedTop (29).withTrimmedBottom (4).toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
    }
}
}
