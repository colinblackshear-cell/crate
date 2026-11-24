#include "SongScreen.h"

#include "PocketLoopTools.h"

#include <cmath>

using namespace pocket;

namespace
{
const juce::Colour kSong { 0xff8fb84a };   // the Mac's Song screen: leaf green
constexpr float kLabelW = 34.0f;
constexpr float kEdge = 14.0f;   // a block's ends, for trimming

juce::String barNumber (double bar)
{
    return juce::String ((int) std::floor (bar + 1.0e-6) + 1);
}

juce::String fadeText (double bars)
{
    if (bars <= 1.0e-6)
        return "OFF";
    if (std::abs (bars - 0.5) < 1.0e-6)
        return U8("\xc2\xbd BAR");
    return juce::String ((int) std::round (bars)) + (bars < 1.5 ? " BAR" : " BARS");
}

double nextFade (double now)
{
    static const double steps[] = { 0.0, 0.5, 1.0, 2.0, 4.0, 8.0 };
    for (double s : steps)
        if (s > now + 1.0e-6)
            return s;
    return 0.0;
}
}

bool SongScreen::hasLoop (int l) const
{
    if (! juce::isPositiveAndBelow (l, kLoopLayers))
        return false;
    const auto st = loops.state (l);
    return st == LoopState::playing || st == LoopState::overdubWait || st == LoopState::overdubbing;
}

SongScreen::SongScreen (PocketSong& s, LoopsEngine& l) : song (s), loops (l)
{
    setOpaque (false);
    startTimerHz (30);
}

SongScreen::~SongScreen()
{
    stopTimer();
}

void SongScreen::changed()
{
    if (onChanged)
        onChanged();
    layout();
    repaint();
}

//==============================================================================
void SongScreen::pressRecLive()
{
    if (song.isLive())
    {
        song.stopAndKeep();
        selected = -1;
        say (song.message());
        return changed();
    }
    juce::String why;
    if (! song.startLive (&why))
        return say (why);
    selected = -1;
    say (song.countInBars > 0 && ! loops.transportRunning() ? "Counting in: then tap the layers in and out." : "REC LIVE from the next phrase: tap the layers in and out.");
    layout();
    repaint();
}

void SongScreen::pressPlay()
{
    if (song.isLive())
        return pressRecLive();
    if (song.isOn())
    {
        song.stopPlaying();
        return repaint();
    }
    if (song.blocks.empty())
        return say ("Nothing in the song yet: REC LIVE, or tap a lane to put its loop there.");
    song.play (playFrom);
    repaint();
}

void SongScreen::setPlayhead (double bar)
{
    playFrom = juce::jmax (0.0, std::floor (bar + 1.0e-6));
    repaint();
}

//==============================================================================
void SongScreen::layout()
{
    hits.clear();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x0 = 16.0f, iw = w - 32.0f;
    const bool compact = h < 480.0f;
    const float rowH = compact ? 34.0f : 40.0f, gap = compact ? 4.0f : 6.0f;
    auto addHit = [this] (juce::Rectangle<float> r, int id) { hits.push_back ({ r, id }); };
    // chips sharing a row by weight
    auto row = [&] (float y, std::initializer_list<std::pair<float, int>> cells)
    {
        float sum = 0;
        for (auto& c : cells)
            sum += c.first;
        const float g = 5.0f, scale = (iw - g * (float) (cells.size() - 1)) / sum;
        float x = x0;
        for (auto& c : cells)
        {
            if (c.second != 0)
                addHit ({ x, y, c.first * scale, rowH }, c.second);
            x += c.first * scale + g;
        }
    };

    float y = 6.0f;
    row (y, { { 1.15f, recId }, { 1.0f, playId }, { 1.0f, takesId } });
    y += rowH + gap;
    statusR = { x0, y, iw, compact ? 26.0f : 30.0f };
    y = statusR.getBottom() + gap;

    if (song.isLive())
    {
        const float bottomH = rowH + 8.0f;
        wellR = { x0, y, iw, juce::jmax (120.0f, h - y - bottomH - gap) };
        const float tw = (wellR.getWidth() - 8.0f) / 2.0f, th = (wellR.getHeight() - 8.0f * 3.0f) / 4.0f;
        for (int l = 0; l < kLoopLayers; ++l)
        {
            tileR[(size_t) l] = { wellR.getX() + (float) (l % 2) * (tw + 8.0f), wellR.getY() + (float) (l / 2) * (th + 8.0f), tw, th };
            addHit (tileR[(size_t) l], tileBase + l);
        }
        row (wellR.getBottom() + gap, { { 1.0f, markId }, { 1.0f, cancelId }, { 1.3f, keepId } });
        return;
    }

    const float toolsH = (rowH + gap) * 3.0f;
    wellR = { x0, y, iw, juce::jmax (150.0f, h - y - toolsH - 4.0f) };
    auto inner = wellR.reduced (6.0f, 6.0f);
    rulerR = inner.removeFromTop (22.0f).withTrimmedLeft (kLabelW);
    labelsR = inner.removeFromLeft (kLabelW);
    lanesR = inner;
    addHit (rulerR, rulerArea);
    addHit (lanesR, laneArea);
    y = wellR.getBottom() + gap;
    row (y, { { 1.0f, splitId }, { 1.0f, copyId }, { 1.0f, deleteId }, { 1.0f, slipId } });
    y += rowH + gap;
    row (y, { { 1.0f, fadeInId }, { 1.0f, fadeOutId }, { 1.0f, levelId } });
    y += rowH + gap;
    row (y, { { 1.15f, punchId }, { 1.0f, bounceId }, { 0.42f, zoomOutId }, { 0.42f, zoomInId } });
}

juce::Rectangle<float> SongScreen::hitRect (int id) const
{
    for (auto& h : hits)
        if (h.id == id)
            return h.r;
    return {};
}

int SongScreen::idAt (juce::Point<float> p) const
{
    for (auto& h : hits)
        if (h.r.contains (p))
            return h.id;
    return none;
}

double SongScreen::barAtX (float x) const
{
    return scroll + (double) ((x - lanesR.getX()) / barW);
}

float SongScreen::xOfBar (double bar) const
{
    return lanesR.getX() + (float) (bar - scroll) * barW;
}

int SongScreen::laneAtY (float y) const
{
    if (lanesR.isEmpty())
        return -1;
    const int l = (int) std::floor ((y - lanesR.getY()) / (lanesR.getHeight() / (float) kLoopLayers));
    return juce::isPositiveAndBelow (l, kLoopLayers) ? l : -1;
}

juce::Rectangle<float> SongScreen::blockRect (const SongBlock& b) const
{
    const float laneH = lanesR.getHeight() / (float) kLoopLayers;
    return { xOfBar (b.start), lanesR.getY() + laneH * (float) b.layer + 2.0f, (float) b.length * barW, laneH - 4.0f };
}

int SongScreen::blockAtPoint (juce::Point<float> p) const
{
    // the picked one first (so its ends can be grabbed where it overlaps another)
    if (juce::isPositiveAndBelow (selected, (int) song.blocks.size()) && blockRect (song.blocks[(size_t) selected]).expanded (kEdge * 0.5f, 0).contains (p))
        return selected;
    for (int i = (int) song.blocks.size(); --i >= 0;)
        if (blockRect (song.blocks[(size_t) i]).contains (p))
            return i;
    return -1;
}

//==============================================================================
void SongScreen::press (int id)
{
    const bool picked = juce::isPositiveAndBelow (selected, (int) song.blocks.size());
    auto needBlock = [&]
    {
        if (! picked)
            say ("Tap a block first.");
        return picked;
    };
    switch (id)
    {
        case recId: pressRecLive(); break;
        case playId: pressPlay(); break;
        case takesId: showTakesMenu(); break;
        case markId:
            song.mark();
            say ("A new section from bar " + barNumber (song.markers.empty() ? 0.0 : song.markers.back().bar) + ".");
            break;
        case cancelId:
            song.cancel();
            say (song.message());
            changed();
            break;
        case keepId: pressRecLive(); break;
        case splitId:
            if (needBlock())
            {
                const auto& b = song.blocks[(size_t) selected];
                // at the playhead when it's inside the block, else in the middle (on a bar)
                double at = song.isOn() ? std::floor (song.barNow()) : playFrom;
                if (at <= b.start + 1.0e-6 || at >= b.end() - 1.0e-6)
                    at = b.start + juce::jmax (1.0, std::floor (b.length / 2.0));
                if (song.split (selected, at))
                {
                    selected += 1;
                    say ("Split at bar " + barNumber (at) + ": the second half carries on in the loop.");
                    changed();
                }
                else
                    say ("Too short to split.");
            }
            break;
        case copyId:
            if (needBlock())
            {
                if (const int c = song.copyBlock (selected); c >= 0)
                {
                    selected = c;
                    say ("Copied: it plays again from bar " + barNumber (song.blocks[(size_t) c].start) + ".");
                    changed();
                }
            }
            break;
        case deleteId:
            if (needBlock())
            {
                song.deleteBlock (selected);
                selected = -1;
                changed();
            }
            break;
        case slipId:
            if (needBlock())
            {
                auto& b = song.blocks[(size_t) selected];
                const double lb = song.loopBars (b.layer);
                if (lb <= 1.0 + 1.0e-6)
                    return say ("Its loop is one bar: there's nothing to slip.");
                b.offset = PocketSong::wrap (b.offset + 1.0, lb);
                song.changed();
                say (b.offset < 1.0e-6 ? "From the top of its loop." : "Starts from bar " + barNumber (b.offset) + " of its loop.");
                changed();
            }
            break;
        case fadeInId:
        case fadeOutId:
            if (needBlock())
            {
                auto& b = song.blocks[(size_t) selected];
                auto& f = id == fadeInId ? b.fadeIn : b.fadeOut;
                double next = nextFade (f);
                if (next > b.length - (id == fadeInId ? b.fadeOut : b.fadeIn) + 1.0e-6)
                    next = 0.0;
                f = next;
                song.changed();
                changed();
            }
            break;
        case levelId:
            if (needBlock())
            {
                auto& b = song.blocks[(size_t) selected];
                const int pct = (int) std::round (b.level * 100.0f);
                b.level = pct > 80 ? 0.8f : pct > 60 ? 0.6f : pct > 40 ? 0.4f : pct > 20 ? 0.2f : 1.0f;
                song.changed();
                changed();
            }
            break;
        case punchId:
        {
            const auto secs = song.sections();
            if (song.blocks.empty())
                return say ("Nothing to punch yet: REC LIVE first.");
            if (! juce::isPositiveAndBelow (section, (int) secs.size()))
                return say ("Tap a section on the ruler, then PUNCH plays it again.");
            juce::String why;
            const auto& sec = secs[(size_t) section];
            if (! song.startPunch (sec.start, sec.end, &why))
                return say (why);
            selected = -1;
            say ("Punch: 2 bars in, then bars " + barNumber (sec.start) + "-" + juce::String ((int) std::round (sec.end)) + " again.");
            layout();
            repaint();
            break;
        }
        case endId:
        {
            juce::String why;
            const double end = std::ceil (song.lengthBars() - 1.0e-6);
            if (song.blocks.empty())
                return pressRecLive();
            if (! song.startPunch (end, -1.0, &why))
                return say (why);
            selected = -1;
            say ("On from bar " + barNumber (end) + ": keep going, STOP + KEEP when it's done.");
            layout();
            repaint();
            break;
        }
        case bounceId: showBounceMenu(); break;
        case zoomOutId: barW = juce::jmax (6.0f, barW / 1.5f); repaint(); break;
        case zoomInId: barW = juce::jmin (90.0f, barW * 1.5f); repaint(); break;
        default:
            if (id >= tileBase && id < tileBase + kLoopLayers)
            {
                const int l = id - tileBase;
                if (! hasLoop (l))
                    return say ("Layer " + juce::String (l + 1) + " is empty.");
                song.toggle (l);
                repaint();
            }
            break;
    }
}

void SongScreen::showTakesMenu()
{
    if (song.isLive())
        return say ("Takes: after STOP + KEEP.");
    juce::PopupMenu m;
    m.addSectionHeader ("TAKES");
    const auto& takes = song.takes();
    if (takes.empty())
        m.addItem (-1, "No takes yet: REC LIVE makes one", false);
    for (int i = 0; i < (int) takes.size(); ++i)
        m.addItem (100 + i, takes[(size_t) i].name, true, i == song.currentTake());
    m.addSeparator();
    juce::PopupMenu count;
    for (int b = 0; b <= 2; ++b)
        count.addItem (10 + b, b == 0 ? "None" : juce::String (b) + (b == 1 ? " bar" : " bars"), true, song.countInBars == b);
    m.addSubMenu ("Count-in", count);
    m.addItem (20, "REC LIVE from the end");
    if (juce::isPositiveAndBelow (song.currentTake(), (int) takes.size()))
        m.addItem (21, "Delete " + takes[(size_t) song.currentTake()].name);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (hitRect (takesId).toNearestInt())),
                     [safe = juce::Component::SafePointer<SongScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         auto& s = safe->song;
                         if (r >= 100)
                         {
                             if (s.useTake (r - 100))
                             {
                                 safe->selected = -1;
                                 safe->say (s.takes()[(size_t) (r - 100)].name + " is the song.");
                             }
                         }
                         else if (r >= 10 && r <= 12)
                             s.countInBars = r - 10;
                         else if (r == 20)
                             return safe->press (endId);
                         else if (r == 21)
                         {
                             const auto name = s.takes()[(size_t) s.currentTake()].name;
                             s.deleteTake (s.currentTake());
                             safe->say (name + " deleted. The song on screen stays.");
                         }
                         safe->changed();
                     });
}

//==============================================================================
void SongScreen::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    downAt = p;
    moved = false;
    drag = Drag::none;
    pressed = idAt (p);
    if (pressed == laneArea && ! song.isLive())
    {
        const int b = blockAtPoint (p);
        if (b >= 0)
        {
            const auto r = blockRect (song.blocks[(size_t) b]);
            const bool wasPicked = b == selected;
            selected = b;
            dragOrig = song.blocks[(size_t) b];
            drag = wasPicked && p.x < r.getX() + kEdge && r.getWidth() > kEdge * 2.0f ? Drag::trimStart
                 : wasPicked && p.x > r.getRight() - kEdge && r.getWidth() > kEdge * 2.0f ? Drag::trimEnd
                 : Drag::move;
        }
        else
        {
            drag = Drag::pan;
            scrollOrig = scroll;
        }
    }
    else if (pressed == rulerArea)
    {
        drag = Drag::pan;
        scrollOrig = scroll;
    }
    repaint();
}

void SongScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (e.position.getDistanceFrom (downAt) > 6.0f)
        moved = true;
    if (! moved || drag == Drag::none)
        return;
    const double dBar = (double) ((e.position.x - downAt.x) / barW);
    if (drag == Drag::pan)
    {
        scroll = juce::jmax (0.0, scrollOrig - dBar);
        return repaint();
    }
    if (! juce::isPositiveAndBelow (selected, (int) song.blocks.size()))
        return;
    auto& b = song.blocks[(size_t) selected];
    if (drag == Drag::move)
        b.start = juce::jmax (0.0, std::round (dragOrig.start + dBar));
    else if (drag == Drag::trimEnd)
        b.length = juce::jmax (1.0, std::round (dragOrig.end() + dBar) - b.start);
    else if (drag == Drag::trimStart)
    {
        b = dragOrig;
        song.trimStart (selected, std::round (dragOrig.start + dBar));
        return repaint();
    }
    song.changed();
    repaint();
}

void SongScreen::mouseUp (const juce::MouseEvent& e)
{
    const int id = pressed;
    pressed = 0;
    const auto p = e.position;
    if (moved)
    {
        if (drag == Drag::move || drag == Drag::trimStart || drag == Drag::trimEnd)
            changed();
        drag = Drag::none;
        return repaint();
    }
    drag = Drag::none;
    if (id == rulerArea)
    {
        // a tap on a section's name picks it; anywhere on the ruler moves the playhead
        const double bar = barAtX (p.x);
        const auto secs = song.sections();
        int hit = -1;
        if (! song.markers.empty())
            for (int i = 0; i < (int) secs.size(); ++i)
                if (bar >= secs[(size_t) i].start && bar < secs[(size_t) i].end)
                    hit = i;
        if (p.y < rulerR.getY() + rulerR.getHeight() * 0.5f && hit >= 0)
            section = section == hit ? -1 : hit;
        else
            setPlayhead (bar);
        return repaint();
    }
    if (id == laneArea && ! song.isLive())
    {
        if (blockAtPoint (p) >= 0)
            return repaint();   // picked on the way down
        const int lane = laneAtY (p.y);
        if (lane < 0)
            return;
        if (selected >= 0)
        {
            selected = -1;   // a tap on the empty lane lets go first
            return repaint();
        }
        if (! hasLoop (lane))
            return say ("Layer " + juce::String (lane + 1) + " is empty: record a loop on it in LOOPS.");
        if ((int) song.blocks.size() >= PocketSong::kMaxBlocks)
            return say ("The song is full.");
        SongBlock b;
        b.layer = lane;
        b.start = juce::jmax (0.0, std::floor (barAtX (p.x)));
        b.length = juce::jmax (1.0, std::round (song.loopBars (lane)));
        song.blocks.push_back (b);
        song.changed();
        selected = (int) song.blocks.size() - 1;
        say ("Layer " + juce::String (lane + 1) + " from bar " + barNumber (b.start) + ". Drag it to move it, its ends to trim it.");
        return changed();
    }
    if (id != none && id == idAt (p))
        press (id);
    repaint();
}

//==============================================================================
void SongScreen::timerCallback()
{
    const bool live = song.isLive();
    if (live != shownLive || song.edits() != shownEdits)
    {
        shownLive = live;
        shownEdits = song.edits();
        if (! juce::isPositiveAndBelow (selected, (int) song.blocks.size()))
            selected = -1;
        layout();
        repaint();
        return;
    }
    if (! isVisible())
        return;
    if (song.isOn() || live)
    {
        // keep the playhead in view
        if (! lanesR.isEmpty() && drag == Drag::none)
        {
            const double now = song.barNow(), shown = (double) (lanesR.getWidth() / barW);
            if (now > scroll + shown * 0.85 || now < scroll)
                scroll = juce::jmax (0.0, std::floor (now - shown * 0.25));
        }
        repaint();
    }
}

juce::String SongScreen::statusText() const
{
    if (song.isLive())
    {
        const double now = song.barNow();
        if (song.getState() == PocketSong::State::waiting)
            return song.isPunch() ? U8("RUN-UP \xc2\xb7 PUNCH FROM BAR ") + barNumber (song.liveStart()) : U8("COUNTING IN \xe2\x80\xa6");
        const double bar = now - (song.isPunch() ? 0.0 : song.liveStart());
        return U8("\xe2\x97\x8f REC LIVE \xc2\xb7 BAR ") + barNumber (bar) + U8(" \xc2\xb7 ") + juce::String (song.changes()) + " CHANGES";
    }
    const double len = std::ceil (song.lengthBars() - 1.0e-6);
    juce::String take = juce::isPositiveAndBelow (song.currentTake(), (int) song.takes().size()) ? song.takes()[(size_t) song.currentTake()].name.toUpperCase()
                                                                                                 : juce::String ("SONG");
    if (song.isOn())
        return U8("\xe2\x96\xb6 ") + take + U8(" \xc2\xb7 BAR ") + barNumber (song.barNow()) + " OF " + juce::String ((int) len);
    if (song.blocks.empty())
        return "NO SONG YET";
    return take + U8(" \xc2\xb7 ") + juce::String ((int) len) + (len == 1 ? " BAR" : " BARS") + U8(" \xc2\xb7 FROM BAR ") + barNumber (playFrom);
}

void SongScreen::paint (juce::Graphics& g)
{
    const bool live = song.isLive(), playing = song.isOn();
    for (auto& h : hits)
    {
        const bool down = pressed == h.id;
        switch (h.id)
        {
            case recId:
                drawChip (g, h.r, live ? U8("\xe2\x96\xa0 STOP + KEEP") : U8("\xe2\x97\x8f REC LIVE"), live, down, 14.0f, col::red);
                break;
            case playId:
                drawChip (g, h.r, playing && ! live ? U8("\xe2\x96\xa0 STOP") : U8("\xe2\x96\xb6 PLAY SONG"), playing && ! live, down, 14.0f, col::green);
                break;
            case takesId:
            {
                const auto& t = song.takes();
                const auto name = juce::isPositiveAndBelow (song.currentTake(), (int) t.size()) ? t[(size_t) song.currentTake()].name : juce::String ("TAKES");
                drawChip (g, h.r, name.toUpperCase() + U8(" \xe2\x96\xbe"), false, down, 13.0f);
                break;
            }
            case markId: drawChip (g, h.r, "MARK", false, down); break;
            case cancelId: drawChip (g, h.r, "CANCEL", false, down); break;
            case keepId: drawChip (g, h.r, U8("\xe2\x96\xa0 STOP + KEEP"), true, down, 14.0f, col::red); break;
            case splitId: drawChip (g, h.r, "SPLIT", false, down); break;
            case copyId: drawChip (g, h.r, "COPY", false, down); break;
            case deleteId: drawChip (g, h.r, "DELETE", false, down); break;
            case slipId:
            {
                juce::String t = "SLIP";
                if (juce::isPositiveAndBelow (selected, (int) song.blocks.size()) && song.blocks[(size_t) selected].offset > 1.0e-6)
                    t << " " << barNumber (song.blocks[(size_t) selected].offset);
                drawChip (g, h.r, t, false, down);
                break;
            }
            case fadeInId:
            case fadeOutId:
            case levelId:
            {
                juce::String t = h.id == fadeInId ? "FADE IN" : h.id == fadeOutId ? "FADE OUT" : "LEVEL";
                if (juce::isPositiveAndBelow (selected, (int) song.blocks.size()))
                {
                    const auto& b = song.blocks[(size_t) selected];
                    t << " " << (h.id == fadeInId ? fadeText (b.fadeIn) : h.id == fadeOutId ? fadeText (b.fadeOut)
                                                                                             : juce::String ((int) std::round (b.level * 100.0f)) + "%");
                }
                drawChip (g, h.r, t, false, down, 13.0f);
                break;
            }
            case punchId:
            {
                const auto secs = song.sections();
                juce::String t = "PUNCH";
                if (juce::isPositiveAndBelow (section, (int) secs.size()))
                    t << " " << barNumber (secs[(size_t) section].start) << "-" << juce::String ((int) std::round (secs[(size_t) section].end));
                drawChip (g, h.r, t, juce::isPositiveAndBelow (section, (int) secs.size()), down, 13.0f, col::red);
                break;
            }
            case endId: drawChip (g, h.r, "REC FROM END", false, down, 13.0f); break;
            case bounceId: drawChip (g, h.r, U8("BOUNCE \xe2\x96\xbe"), false, down, 13.0f); break;
            case zoomOutId: drawChip (g, h.r, U8("\xe2\x88\x92"), false, down, 18.0f); break;
            case zoomInId: drawChip (g, h.r, "+", false, down, 18.0f); break;
            default: break;
        }
    }

    // the status line
    drawWell (g, statusR, 8.0f);
    g.setColour (live ? col::lcdText : col::lcdAmber);
    g.setFont (lcdFont (statusR.getHeight() * 0.8f));
    g.drawFittedText (statusText(), statusR.reduced (10.0f, 0).toNearestInt(), juce::Justification::centredLeft, 1);

    if (live)
        paintTiles (g);
    else
        paintLanes (g);
}

void SongScreen::paintTiles (juce::Graphics& g)
{
    const double now = song.barNow();
    const bool blink = std::fmod (now * 2.0, 1.0) < 0.5;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        const auto r = tileR[(size_t) l];
        const bool has = hasLoop (l);
        const auto take = loops.takeOf (l);
        const juce::Colour c = take != nullptr ? layerToolColour (take->tool) : col::grey;
        const bool in = song.isIn (l);
        const double change = song.changeAt (l);
        drawRaised (g, r, in ? Look::dark : Look::cream, pressed == tileBase + l, 14.0f);
        if (! has)
        {
            g.setColour (col::inkFaint);
            g.setFont (condensedBold (15.0f));
            g.drawText ("LAYER " + juce::String (l + 1) + U8(" \xc2\xb7 EMPTY"), r, juce::Justification::centred);
            continue;
        }
        if (in)
        {
            g.setColour (c.withAlpha (0.85f));
            g.fillRoundedRectangle (r.reduced (4.0f), 11.0f);
        }
        if (change >= 0 && blink)
        {
            g.setColour (col::amber);
            g.drawRoundedRectangle (r.reduced (2.0f), 13.0f, 3.0f);
        }
        auto inner = r.reduced (12.0f, 8.0f);
        g.setColour (in ? col::ink : col::inkDim);
        g.setFont (spaced (condensedBold (13.0f), 0.1f));
        g.drawText ("LAYER " + juce::String (l + 1), inner.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setFont (condensedBold (juce::jmin (26.0f, inner.getHeight() * 0.45f)));
        g.setColour (in ? col::ink : c.darker (0.2f));
        g.drawText (layerToolName (take != nullptr ? take->tool : 0), inner.removeFromTop (inner.getHeight() * 0.55f), juce::Justification::centredLeft);
        g.setFont (condensedBold (13.0f));
        juce::String state = in ? "IN" : "OUT";
        if (change >= 0)
            state = (in ? "IN AT BAR " : "OUT AT BAR ") + barNumber (change - (song.isPunch() ? 0.0 : song.liveStart()));
        g.drawText (state, inner, juce::Justification::centredLeft);
    }
}

void SongScreen::paintLanes (juce::Graphics& g)
{
    drawWell (g, wellR, 12.0f);
    const float laneH = lanesR.getHeight() / (float) kLoopLayers;
    juce::Graphics::ScopedSaveState saved (g);

    // lane names
    for (int l = 0; l < kLoopLayers; ++l)
    {
        const auto take = loops.takeOf (l);
        const juce::Rectangle<float> r { labelsR.getX(), lanesR.getY() + laneH * (float) l, labelsR.getWidth(), laneH };
        g.setColour (take != nullptr ? layerToolColour (take->tool) : col::inkDim);
        g.setFont (condensedBold (juce::jmin (14.0f, laneH * 0.55f)));
        g.drawText ("L" + juce::String (l + 1), r, juce::Justification::centred);
        g.setColour (col::wellEdge);
        g.drawHorizontalLine ((int) (r.getBottom()), lanesR.getX(), lanesR.getRight());
    }

    g.reduceClipRegion (rulerR.getUnion (lanesR).toNearestInt());
    // bars, sections
    const double first = std::floor (scroll), last = barAtX (lanesR.getRight()) + 1.0;
    const int every = barW < 9.0f ? 8 : barW < 16.0f ? 4 : barW < 30.0f ? 2 : 1;
    for (double bar = first; bar <= last; bar += 1.0)
    {
        const float x = xOfBar (bar);
        const bool major = (int) bar % 4 == 0;
        g.setColour (juce::Colours::white.withAlpha (major ? 0.10f : 0.04f));
        g.drawVerticalLine ((int) x, lanesR.getY(), lanesR.getBottom());
        if ((int) bar % every == 0)
        {
            g.setColour (col::inkFaint);
            g.setFont (lcdFont (14.0f));
            g.drawText (barNumber (bar), juce::Rectangle<float> (x + 2.0f, rulerR.getCentreY(), 30.0f, rulerR.getHeight() * 0.5f), juce::Justification::centredLeft);
        }
    }
    if (! song.markers.empty())
    {
        const auto secs = song.sections();
        for (int i = 0; i < (int) secs.size(); ++i)
        {
            const auto& s = secs[(size_t) i];
            const juce::Rectangle<float> r { xOfBar (s.start) + 1.0f, rulerR.getY(), (float) (s.end - s.start) * barW - 2.0f, rulerR.getHeight() * 0.5f };
            g.setColour (i == section ? col::red.withAlpha (0.8f) : kSong.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (i == section ? col::creamLight : col::cream);
            g.setFont (condensedBold (11.0f));
            g.drawText (s.name.toUpperCase(), r.reduced (3.0f, 0), juce::Justification::centredLeft, true);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawVerticalLine ((int) xOfBar (s.start), lanesR.getY(), lanesR.getBottom());
        }
    }

    // the blocks
    for (int i = 0; i < (int) song.blocks.size(); ++i)
    {
        const auto& b = song.blocks[(size_t) i];
        const auto r = blockRect (b);
        if (r.getRight() < lanesR.getX() || r.getX() > lanesR.getRight())
            continue;
        const auto take = loops.takeOf (b.layer);
        const juce::Colour c = take != nullptr ? layerToolColour (take->tool) : col::grey;
        const bool picked = i == selected;
        g.setColour (c.withAlpha (0.35f + 0.5f * b.level));
        g.fillRoundedRectangle (r, 4.0f);
        // where its loop comes round again
        const double lb = song.loopBars (b.layer);
        if (lb > 0.0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            double top = b.start + (b.offset > 1.0e-6 ? lb - b.offset : lb);
            for (; top < b.end() - 1.0e-6; top += lb)
                g.drawVerticalLine ((int) xOfBar (top), r.getY() + 3.0f, r.getBottom() - 3.0f);
        }
        // the fades
        juce::Path fade;
        if (b.fadeIn > 1.0e-6)
        {
            fade.startNewSubPath (r.getX(), r.getBottom());
            fade.lineTo (r.getX() + (float) b.fadeIn * barW, r.getY());
            fade.lineTo (r.getX(), r.getY());
            fade.closeSubPath();
        }
        if (b.fadeOut > 1.0e-6)
        {
            fade.startNewSubPath (r.getRight(), r.getBottom());
            fade.lineTo (r.getRight() - (float) b.fadeOut * barW, r.getY());
            fade.lineTo (r.getRight(), r.getY());
            fade.closeSubPath();
        }
        g.setColour (col::well.withAlpha (0.55f));
        g.fillPath (fade);
        g.setColour (picked ? col::amber : c.brighter (0.3f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, picked ? 2.5f : 1.0f);
        if (picked && r.getWidth() > kEdge * 2.0f)
        {
            g.setColour (col::amber);
            g.fillRoundedRectangle (r.getX() + 2.0f, r.getCentreY() - 7.0f, 3.0f, 14.0f, 1.5f);
            g.fillRoundedRectangle (r.getRight() - 5.0f, r.getCentreY() - 7.0f, 3.0f, 14.0f, 1.5f);
        }
    }

    // the playhead
    const double head = song.isOn() ? song.barNow() : playFrom;
    const float hx = xOfBar (head);
    g.setColour (song.isOn() ? col::green : col::amber);
    g.fillRect (hx - 1.0f, rulerR.getY(), 2.0f, lanesR.getBottom() - rulerR.getY());

    if (song.blocks.empty())
    {
        g.setColour (col::cream.withAlpha (0.8f));
        g.setFont (small (14.0f));
        const bool any = [this] { for (int l = 0; l < kLoopLayers; ++l) if (hasLoop (l)) return true; return false; }();
        g.drawFittedText (any ? "Tap REC LIVE and bring your layers in and out, or tap a lane to put its loop there."
                              : "Record some loops in LOOPS first. Then REC LIVE brings them in and out to build the song.",
                          lanesR.reduced (16.0f).toNearestInt(), juce::Justification::centred, 4);
    }
}

void SongScreen::showBounceMenu()
{
    if (song.isLive())
        return say ("BOUNCE after STOP + KEEP.");
    auto& b = bounceSettings;
    const auto secs = song.sections();
    const bool sectionPicked = juce::isPositiveAndBelow (section, (int) secs.size()) && ! song.markers.empty();
    juce::PopupMenu m;
    m.addSectionHeader ("BOUNCE");
    m.addItem (1, "The song: each layer + the mix");
    m.addItem (2, "The song: the mix");
    if (sectionPicked)
    {
        m.addItem (3, secs[(size_t) section].name + ": each layer + the mix");
        m.addItem (4, secs[(size_t) section].name + ": the mix");
    }
    else
        m.addItem (-1, "A section: tap one on the ruler first", false);
    m.addItem (5, "Layers playing now: each + the mix");
    m.addItem (6, "Layers playing now: the mix");
    m.addSeparator();
    m.addItem (10, "WAV (for Logic)", true, b.wav);
    m.addItem (11, "MP3 (to share)", true, b.mp3);
    m.addItem (12, "Echo tail (2 s)", true, b.tailSeconds > 0.0);
    m.addSeparator();
    m.addItem (20, "TO MAC: the last bounce");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (hitRect (bounceId).toNearestInt())),
                     [safe = juce::Component::SafePointer<SongScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         auto& o = safe->bounceSettings;
                         if (r >= 10 && r <= 12)
                         {
                             if (r == 10 && (! o.wav || o.mp3))
                                 o.wav = ! o.wav;
                             else if (r == 11 && (! o.mp3 || o.wav))
                                 o.mp3 = ! o.mp3;
                             else if (r == 12)
                                 o.tailSeconds = o.tailSeconds > 0.0 ? 0.0 : 2.0;
                             return safe->showBounceMenu();   // open again, to pick what
                         }
                         if (r == 20)
                         {
                             if (safe->onBounceToMac)
                                 safe->onBounceToMac();
                             return;
                         }
                         o.what = r <= 2 ? Bounce::song : r <= 4 ? Bounce::part : Bounce::loops;
                         o.eachLayer = r % 2 == 1;
                         o.section = safe->section;
                         if (safe->onBounce)
                             safe->onBounce (o);
                     });
}
