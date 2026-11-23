#include "SamplerScreen.h"

#include "PocketFactory.h"
#include "core/AudioFiles.h"
#include "core/Edits.h"
#include "core/SoundFx.h"

using namespace pocket;

namespace
{
const juce::Colour kPadDark { 0xff4a4037 }, kPadText { 0xfff1e7d1 };

juce::String clockOf (double s)
{
    const int m = (int) (s / 60.0);
    return juce::String (m) + ":" + juce::String (s - m * 60.0, 2).paddedLeft ('0', 5);
}
}

SamplerScreen::SamplerScreen (PocketHost& h, PocketAudio& a, Library& lib, Kit& k)
    : host (h), audio (a), library (lib), kit (k)
{
    setOpaque (false);
    for (auto* c : std::initializer_list<juce::Component*> { &wave, &soundsButton, &allButton, &undoButton, &moreButton, &hearButton, &trimButton,
                                                             &sendButton, &pickButton, &clearButton, &chopButton, &toolButton })
        addAndMakeVisible (c);
    wave.setEmptyText ("Press REC to sample a sound from the mic, or SOUNDS to open one");
    wave.onSelectionChange = [this] { updateLabels(); };

    soundsButton.setFontHeight (16.0f);
    soundsButton.onClick = [this] { showSounds(); };
    static const char* stepNames[5] = { "1 Get", "2 Shape", "3 Chop", "4 Pads", "5 Save set" };
    for (int i = 0; i < 5; ++i)
    {
        steps[i].setText (stepNames[i]);
        steps[i].setFontHeight (13.0f);
        steps[i].onClick = [this, i] { pressStep (i); };
        addAndMakeVisible (steps[i]);
    }
    toolButton.setFontHeight (16.0f);
    toolButton.onClick = [this] { if (onSampleTool) onSampleTool(); };
    allButton.setFontHeight (16.0f);
    allButton.onClick = [this] { selectAll(); };
    undoButton.setFontHeight (16.0f);
    undoButton.onClick = [this] { undo(); };
    clearButton.setFontHeight (16.0f);
    clearButton.onClick = [this] { clearSound(); };
    chopButton.setFontHeight (16.0f);
    chopButton.onClick = [this] { showChopMenu(); };
    moreButton.setFontHeight (16.0f);
    moreButton.onClick = [this]
    {
        if (! needSound() || ! ensureSaved())
            return;
        if (onMoreEdits)
            onMoreEdits (currentId, targetPad);
    };
    hearButton.setFontHeight (18.0f);
    hearButton.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        const bool on = audio.isPlaying();
        g.setFont (spaced (condensedBold (18.0f), 0.06f));
        const juce::String text = on ? "STOP" : "HEAR";
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        const float x = r.getCentreX() - (tw + 22.0f) * 0.5f, cy = r.getCentreY();
        g.setColour (ink);
        juce::Path p;
        if (on)
            p.addRoundedRectangle (x, cy - 6, 12.0f, 12.0f, 2.0f);
        else
            p.addTriangle (x, cy - 7, x, cy + 7, x + 12, cy);
        g.fillPath (p);
        g.drawText (text, juce::Rectangle<float> (x + 20, r.getY(), tw + 4, r.getHeight()), juce::Justification::centredLeft);
    };
    hearButton.onClick = [this] { hear(); };
    trimButton.setFontHeight (16.0f);
    trimButton.onClick = [this] { trim(); };
    sendButton.setFontHeight (20.0f);
    sendButton.onClick = [this]
    {
        if (! needSound())
            return;
        if (targetPad < 0)
        {
            if (onPickPad)
                onPickPad();
            return;
        }
        sendTo (targetPad);
    };
    pickButton.glyph = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        // a 4 x 4 grid, and PICK PAD under it
        const float cell = 5.0f, gap = 2.0f, side = 4 * cell + 3 * gap;
        const float x0 = r.getCentreX() - side * 0.5f, y0 = r.getY() + 7.0f;
        g.setColour (ink);
        for (int i = 0; i < 16; ++i)
            g.fillRoundedRectangle (x0 + (float) (i % 4) * (cell + gap), y0 + (float) (i / 4) * (cell + gap), cell, cell, 1.0f);
        g.setFont (spaced (condensedBold (12.0f), 0.08f));
        g.drawText ("PICK PAD", juce::Rectangle<float> (r.getX(), y0 + side + 2.0f, r.getWidth(), 14.0f), juce::Justification::centred);
    };
    pickButton.onClick = [this]
    {
        if (! needSound())
            return;
        if (onPickPad)
            onPickPad();
    };
    updateLabels();
    startTimerHz (30);
}

SamplerScreen::~SamplerScreen()
{
    stopTimer();
    if (recording)
        audio.stopRecording();
}

juce::String SamplerScreen::clockText (double seconds) const { return clockOf (seconds); }

bool SamplerScreen::needSound()
{
    if (hasSound())
        return true;
    host.toast ("Press REC to sample a sound first, or SOUNDS to open one");
    return false;
}

bool SamplerScreen::ensureSaved()
{
    if (currentId.isNotEmpty() && library.find (currentId) != nullptr)
        return true;
    if (! hasSound())
        return false;
    if (name.trim().isEmpty())
        name = "Sample " + juce::Time::getCurrentTime().formatted ("%H:%M");
    currentId = library.add (name, kTagRecorded, *wave.getAudio(), {}, {});
    if (currentId.isEmpty())
        host.toast ("Couldn't save that sound");
    return currentId.isNotEmpty();
}

void SamplerScreen::updateLabels()
{
    const auto padText = targetPad >= 0 ? Kit::padLabel (targetPad) : juce::String();
    juce::StringArray title;
    if (targetPad >= 0)
        title.add ("PAD " + padText);
    if (hasSound())
        title.add (name.toUpperCase() + U8(" \xc2\xb7 ") + clockOf (crate::seconds (wave.getAudio()->getNumSamples())));
    wave.setTitle (title.joinIntoString (U8(" \xc2\xb7 ")));
    sendButton.setText (targetPad >= 0 ? "Send to pad " + padText : juce::String (U8("Send to a pad\xe2\x80\xa6")));
    const auto sel = wave.getSelection();
    const bool partial = hasSound() && sel.getLength() > 0 && sel.getLength() < wave.getAudio()->getNumSamples();
    trimButton.setLook (partial ? Look::amber : Look::cream);
    undoButton.setLook (undoStack.empty() ? Look::ghost : Look::cream);
    clearButton.setLook (hasSound() ? Look::cream : Look::ghost);
    chopButton.setLook (hasSound() ? Look::cream : Look::ghost);
    for (int i = 0; i < 5; ++i)
        steps[i].setLook (i == currentStep() ? Look::dark : Look::ghost);
    repaint();
}

//==============================================================================
void SamplerScreen::setTarget (int pad)
{
    targetPad = juce::isPositiveAndBelow (pad, kPads) ? pad : -1;
    updateLabels();
}

void SamplerScreen::editForPad (int pad)
{
    setTarget (pad);
    if (targetPad < 0)
        return;
    const auto& p = kit.pads[(size_t) targetPad];
    if (p.empty() || library.find (p.soundId) == nullptr)
    {
        updateLabels();
        return host.toast ("Pad " + Kit::padLabel (targetPad) + U8(" is empty: press REC to sample a sound for it, or SOUNDS to open one"));
    }
    loadSound (p.soundId);
    // the pad's own part of the sound, as IN and OUT
    if (hasSound() && (p.from > 0 || p.to >= 0))
    {
        const int n = wave.getAudio()->getNumSamples();
        const int from = juce::jlimit (0, n - 1, p.from), to = p.to < 0 ? n : juce::jlimit (from + 1, n, p.to);
        wave.setSelection ({ from, to });
        updateLabels();
    }
}

void SamplerScreen::loadSound (const juce::String& id)
{
    chopped = false;
    auto* item = library.find (id);
    auto loaded = library.load (id);
    if (item == nullptr || loaded == nullptr)
        return host.toast ("That sound couldn't be opened");
    audio.stopPlaying();
    if (hasSound())
        undoStack.push_back ({ wave.getAudio(), wave.getSelection(), currentId, 0, false });
    currentId = id;
    name = item->name;
    wave.setAudio (loaded);
    wave.setMarkers ({});
    wave.selectAll();
    updateLabels();
}

void SamplerScreen::clear()
{
    chopped = false;
    audio.stopPlaying();
    currentId = {};
    name = {};
    undoStack.clear();
    wave.setAudio (nullptr);
    updateLabels();
}

void SamplerScreen::showDemo (crate::SharedAudio a, const juce::String& soundName, juce::Range<int> sel, int pad)
{
    currentId = {};
    name = soundName;
    targetPad = pad;
    wave.setAudio (std::move (a));
    wave.setSelection (sel);
    updateLabels();
}

void SamplerScreen::leave()
{
    if (recording)
        stopRecording();   // the take is kept, not lost
}

//==============================================================================
// REC on the thumb bar: a one-shot from the mic, at once
void SamplerScreen::startRecording()
{
    if (recording)
        return;
    if (! audio.isRunning())
        return host.toast ("The microphone isn't ready. Check Settings > Kaleidobox Pocket.");
    audio.stopPlaying();
    takeFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-sampler-take.wav");
    if (! audio.startRecording (takeFile))
        return host.toast ("No microphone found, or access is off");
    recording = true;
    repaint();
}

double SamplerScreen::recordedSeconds() const { return recording ? audio.recordedSeconds() : 0.0; }

void SamplerScreen::stopRecording()
{
    if (! recording)
        return;
    recording = false;
    audio.stopRecording();
    juce::String error;
    auto loaded = crate::AudioFiles::load (takeFile, library.formats(), error);
    takeFile.deleteFile();
    repaint();
    if (loaded == nullptr || loaded->getNumSamples() < 100)
        return host.toast ("That recording was empty");
    useTake (loaded);
    host.toast ("Sampled " + clockOf (crate::seconds (loaded->getNumSamples())) + ". Drag IN and OUT, then "
                + (targetPad >= 0 ? "SEND TO PAD " + Kit::padLabel (targetPad) : juce::String ("SEND TO A PAD")) + ".");
}

void SamplerScreen::useTake (crate::SharedAudio loaded, const juce::String& takeName)
{
    if (loaded == nullptr)
        return;
    chopped = false;
    if (hasSound())
        undoStack.push_back ({ wave.getAudio(), wave.getSelection(), currentId, 0, false });
    currentId = {};
    name = takeName.isNotEmpty() ? takeName : "Sample " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    wave.setAudio (loaded);
    wave.setMarkers ({});
    wave.selectAll();
    updateLabels();
}

//==============================================================================
void SamplerScreen::selectAll()
{
    if (! needSound())
        return;
    wave.selectAll();
    updateLabels();
}

// What SEND TO PAD puts on a pad: IN to OUT, with the sound's effects
void SamplerScreen::hear()
{
    if (audio.isPlaying())
        return audio.stopPlaying();
    if (recording || ! needSound())
        return;
    auto a = wave.getAudio();
    const auto sel = crate::Edits::orWhole (*a, wave.getSelection());
    const auto* item = library.find (currentId);
    if (item != nullptr && item->fx.anyOn())
    {
        auto part = crate::share (crate::SoundFx::apply (crate::Edits::copy (*a, sel), item->fx.toSettings()));
        audio.play (part, 0, part->getNumSamples());
    }
    else
        audio.play (a, sel.getStart(), sel.getEnd());
    if (! audio.isRunning())
        host.toast ("The speaker isn't ready");
}

// TRIM: the sound itself becomes IN..OUT, saved, and every pad playing it plays the cut at once
void SamplerScreen::trim()
{
    if (! needSound())
        return;
    auto a = wave.getAudio();
    const auto sel = wave.getSelection();
    if (sel.getLength() <= 0 || sel.getLength() >= a->getNumSamples())
        return host.toast ("Drag IN and OUT first: TRIM keeps what's between them");
    if (! ensureSaved())
        return;
    const auto* item = library.find (currentId);
    if (item == nullptr)
        return;
    audio.stopPlaying();
    auto cut = crate::Edits::crop (*a, sel);
    auto cropped = crate::share (std::move (cut.audio));
    std::vector<int> markers;
    for (int m : item->markers)
        if (m > sel.getStart() && m < sel.getEnd())
            markers.push_back (m - sel.getStart());
    const auto fx = item->fx;
    const auto oldId = currentId;
    juce::String newId = oldId;
    if (item->tag == kTagFactory)
    {
        // a factory kit's sound is on that kit in every song: the cut becomes your own copy
        newId = library.add (name, kTagRecorded, *cropped, fx, markers);
        if (newId.isEmpty())
            return host.toast ("Couldn't save that sound");
    }
    else if (! library.update (oldId, name, *cropped, fx, markers))
        return host.toast ("Couldn't save that sound");
    undoStack.push_back ({ a, sel, oldId, sel.getStart(), true });
    currentId = newId;
    wave.setAudio (cropped);
    wave.selectAll();
    if (onRepad)
        onRepad (oldId, newId, -sel.getStart());
    updateLabels();
    int playing = 0;
    for (auto& p : kit.pads)
        playing += (! p.empty() && p.soundId == newId) ? 1 : 0;
    host.toast ("Trimmed to " + clockOf (crate::seconds (cropped->getNumSamples()))
                + (playing == 0 ? juce::String (". SEND TO PAD puts it on a pad.")
                   : playing == 1 ? juce::String (". The pad plays it now.") : ". " + juce::String (playing) + " pads play it now."));
}

void SamplerScreen::undo()
{
    if (undoStack.empty())
        return host.toast ("Nothing to undo");
    audio.stopPlaying();
    auto s = undoStack.back();
    undoStack.pop_back();
    if (s.saved)
    {
        // a TRIM: the uncut sound goes back where it was, and the pads with it
        if (s.id == currentId)
        {
            if (const auto* item = library.find (s.id))
            {
                std::vector<int> markers;
                for (int m : item->markers)
                    markers.push_back (m + s.trimStart);
                library.update (s.id, name, *s.audio, item->fx, markers);
            }
        }
        if (onRepad)
            onRepad (currentId, s.id, s.trimStart);
    }
    const bool wasCleared = ! hasSound();
    currentId = s.id;
    if (const auto* item = library.find (currentId))
        name = item->name;
    else if (s.name.isNotEmpty())
        name = s.name;
    wave.setAudio (s.audio);
    wave.setMarkers ({});
    wave.setSelection (s.sel);
    updateLabels();
    host.toast (s.saved ? juce::String ("The trim is undone, on the pads too")
                : wasCleared ? "\"" + name + "\" is back" : juce::String ("Back to the sound before"));
}

bool SamplerScreen::sendTo (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kPads) || ! needSound() || ! ensureSaved())
        return false;
    audio.stopPlaying();
    const int n = wave.getAudio()->getNumSamples();
    const auto sel = crate::Edits::orWhole (*wave.getAudio(), wave.getSelection());
    const bool whole = sel.getStart() <= 0 && sel.getEnd() >= n;
    const auto old = kit.pads[(size_t) pad];
    if (! kit.assign (pad, currentId, whole ? 0 : sel.getStart(), whole ? -1 : sel.getEnd(), name))
    {
        host.toast ("That sound couldn't be opened");
        return false;
    }
    if (! old.empty())
    {
        auto& now = kit.pads[(size_t) pad];
        now.volume = old.volume;
        now.pan = old.pan;
        now.tune = old.tune;
        now.mode = old.mode;
    }
    targetPad = pad;
    updateLabels();
    if (onPadSet)
        onPadSet (pad);
    host.toastAction ("Pad " + Kit::padLabel (pad) + " plays \"" + name + "\"" + (whole ? juce::String() : U8(" (IN to OUT)")), "UNDO",
                      [safe = juce::Component::SafePointer<SamplerScreen> (this), pad, old]
                      {
                          if (safe == nullptr)
                              return;
                          safe->kit.pads[(size_t) pad] = old;
                          if (safe->onPadSet)
                              safe->onPadSet (pad);
                      });
    return true;
}

void SamplerScreen::clearSound()
{
    if (recording)
        return;
    if (! hasSound())
        return host.toast ("The sampler is already empty: press REC to sample, or SOUNDS to open one");
    audio.stopPlaying();
    undoStack.push_back ({ wave.getAudio(), wave.getSelection(), currentId, 0, false, name });
    const auto was = name;
    currentId = {};
    name = {};
    wave.setAudio (nullptr);
    wave.setMarkers ({});
    updateLabels();
    host.toastAction ("Cleared \"" + was + "\". Sounds and the pads keep their copies.", "UNDO",
                      [safe = juce::Component::SafePointer<SamplerScreen> (this)] { if (safe != nullptr) safe->undo(); });
}

int SamplerScreen::chopToPads (int slices)
{
    if (recording || ! needSound() || ! ensureSaved())
        return 0;
    const auto* item = library.find (currentId);
    if (item == nullptr)
        return 0;
    audio.stopPlaying();
    const auto& a = *wave.getAudio();
    const int n = a.getNumSamples();
    std::vector<int> markers;
    if (slices <= 0)
    {
        for (int m : crate::SoundFx::findHits (a, 0.5f, 16))
            if (m > 64 && m < n - 64)
                markers.push_back (m);
    }
    else
        for (int i = 1; i < slices; ++i)
            markers.push_back ((int) ((juce::int64) n * i / slices));
    if (markers.empty())
    {
        host.toast ("No separate hits found. Try 8 or 16 equal slices.");
        return 0;
    }
    if ((int) markers.size() > 15)
        markers.resize (15);   // one bank: 16 pads

    // The slices live on the sound as its chop markers (a factory sound gets your own copy, as TRIM does).
    const auto fx = item->fx;
    if (item->tag == kTagFactory)
    {
        const auto copy = library.add (name, kTagRecorded, a, fx, markers);
        if (copy.isEmpty())
        {
            host.toast ("Couldn't save that sound");
            return 0;
        }
        currentId = copy;
    }
    else if (! library.update (currentId, name, a, fx, markers))
    {
        host.toast ("Couldn't save that sound");
        return 0;
    }

    const int bank = targetPad >= 0 ? targetPad / 16 : (currentBank != nullptr ? juce::jlimit (0, 3, currentBank()) : 0);
    const int first = bank * 16;
    std::array<KitPad, 16> before;
    for (int i = 0; i < 16; ++i)
        before[(size_t) i] = kit.pads[(size_t) (first + i)];
    const int used = kit.chop (first, currentId, false);
    if (used <= 0)
    {
        host.toast ("That sound couldn't be opened");
        return 0;
    }
    wave.setMarkers (markers);
    targetPad = first;
    updateLabels();
    if (onPadSet)
        onPadSet (first);
    host.toastAction ("Chopped into " + juce::String (used) + " pads, " + Kit::padLabel (first) + " to " + Kit::padLabel (first + used - 1)
                          + ". Hold a pad > EDIT THIS SLICE to edit one on its own.",
                      "UNDO",
                      [safe = juce::Component::SafePointer<SamplerScreen> (this), first, before]
                      {
                          if (safe == nullptr)
                              return;
                          for (int i = 0; i < 16; ++i)
                              safe->kit.pads[(size_t) (first + i)] = before[(size_t) i];
                          if (safe->onPadSet)
                              safe->onPadSet (first);
                      });
    chopped = used > 0;
    updateLabels();
    return used;
}

void SamplerScreen::showChopMenu()
{
    if (recording || ! needSound())
        return;
    const int bank = targetPad >= 0 ? targetPad / 16 : (currentBank != nullptr ? juce::jlimit (0, 3, currentBank()) : 0);
    const auto range = Kit::padLabel (bank * 16) + U8("\xe2\x80\x93") + Kit::padLabel (bank * 16 + 15);
    HoldSpec s;
    s.title = "CHOP " + name.toUpperCase() + " ONTO PADS " + range;
    s.spotlight = false;
    s.items.push_back ({ "FIND THE HITS", "drums, notes, words", false, true, [this] { chopToPads (0); } });
    for (int n : { 4, 8, 16 })
        s.items.push_back ({ juce::String (n) + " EQUAL SLICES", n == 16 ? juce::String ("a loop in time") : juce::String(), false, true,
                             [this, n] { chopToPads (n); } });
    host.holdMenu (s, *this, chopButton.getBounds().getCentre().toFloat(), chopButton.getBounds().toFloat());
}

void SamplerScreen::showSounds()
{
    HoldSpec s;
    s.title = targetPad >= 0 ? "SOUNDS FOR PAD " + Kit::padLabel (targetPad) : juce::String ("OPEN A SOUND");
    s.spotlight = false;
    auto items = library.items();
    std::stable_sort (items.begin(), items.end(), [] (const SoundItem& a, const SoundItem& b) { return a.date > b.date; });
    for (auto& item : items)
    {
        const auto id = item.id;
        s.items.push_back ({ item.name.toUpperCase(), Kit::lengthText (item.seconds), false, true, [this, id] { loadSound (id); } });
    }
    if (items.empty())
        s.items.push_back ({ "NO SOUNDS YET", "press REC to sample one", false, false, {} });
    host.holdMenu (s, *this, soundsButton.getBounds().getCentre().toFloat(), soundsButton.getBounds().toFloat());
}

//==============================================================================
void SamplerScreen::timerCallback()
{
    if (! isVisible())
        return;
    if (recording)
    {
        level = juce::jmax (audio.takeLevel(), level * 0.82f);
        if (audio.recordedSeconds() >= crate::AudioFiles::kMaxSeconds)
            stopRecording();
        repaint (infoArea.expanded (2).toNearestInt());
    }
    if (audio.isPlaying() && hasSound() && library.find (currentId) != nullptr && library.find (currentId)->fx.anyOn())
        wave.setPlayhead (-1);   // (the copy with effects plays from its own start)
    else
        wave.setPlayhead (audio.isPlaying() ? audio.position() : -1);
    hearButton.repaint();
}

void SamplerScreen::pressStep (int step)
{
    switch (step)
    {
        case 0:
        {
            HoldSpec s;
            s.title = "1 GET A SOUND";
            s.items.push_back ({ "REC FROM THE MIC", "at once, a one-shot", false, true, [this] { startRecording(); } });
            s.items.push_back ({ "A TOOL", "a loop layer, or the loops mixed", false, true, [this] { if (onSampleTool) onSampleTool(); } });
            s.items.push_back ({ "SOUNDS", "yours and the factory's", false, true, [this] { showSounds(); } });
            host.holdMenu (s, *this, steps[0].getBounds().getCentre().toFloat(), steps[0].getBounds().toFloat());
            break;
        }
        case 1:
            if (needSound())
                host.toast (U8("Shape it: drag IN and OUT, then TRIM. MORE EDITS \xe2\x80\xba has fades, effects and the name."));
            break;
        case 2: if (needSound()) showChopMenu(); break;
        case 3: if (onShowPads) onShowPads(); break;
        default: if (onSaveSet) onSaveSet(); break;
    }
}

void SamplerScreen::paint (juce::Graphics& g)
{
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (19.0f), 0.02f));
    g.drawFittedText ("Sampler" + (targetPad >= 0 ? U8(" \xc2\xb7 for pad ") + Kit::padLabel (targetPad) : juce::String (U8(" \xc2\xb7 sounds for the pads"))),
                      titleArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

    // IN, OUT and the length, or the take's clock and level while REC runs
    auto r = infoArea;
    if (recording)
    {
        g.setColour (col::red);
        g.fillEllipse (r.getX() + 2, r.getCentreY() - 6, 12, 12);
        g.setFont (lcdFont (21.0f));
        g.setColour (col::ink);
        g.drawText ("REC " + clockOf (audio.recordedSeconds()) + U8(" \xc2\xb7 REC AGAIN STOPS"), r.withTrimmedLeft (20), juce::Justification::centredLeft);
        auto meter = juce::Rectangle<float> (r.getRight() - 120, r.getCentreY() - 4, 120, 8);
        const float db = level > 0.0001f ? juce::Decibels::gainToDecibels (level) : -60.0f;
        const int lit = juce::jlimit (0, 12, (int) std::round ((db + 48.0f) / 48.0f * 12.0f));
        for (int i = 0; i < 12; ++i)
        {
            g.setColour (i < lit ? (i < 9 ? col::green : i < 11 ? col::amber : col::red) : col::rule);
            g.fillRoundedRectangle (meter.getX() + (float) i * 10.0f, meter.getY(), 8.0f, meter.getHeight(), 1.5f);
        }
        return;
    }
    if (! hasSound())
    {
        g.setColour (col::inkDim);
        g.setFont (small (14.0f));
        g.drawFittedText (targetPad >= 0 ? "REC samples a sound for pad " + Kit::padLabel (targetPad) + U8(". Nothing goes on it until SEND.")
                                         : juce::String ("REC samples from the mic (no count-in): then IN, OUT, TRIM and SEND."),
                          r.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
        return;
    }
    const auto sel = crate::Edits::orWhole (*wave.getAudio(), wave.getSelection());
    g.setColour (col::inkDim);
    g.setFont (spaced (lcdFont (20.0f), 0.04f));
    g.drawText ("IN " + clockOf (crate::seconds (sel.getStart())) + "   OUT " + clockOf (crate::seconds (sel.getEnd()))
                    + U8("   \xc2\xb7   ") + clockOf (crate::seconds (sel.getLength())),
                r, juce::Justification::centredLeft);
}

void SamplerScreen::resized()
{
    const int w = getWidth(), h = getHeight(), x0 = 16, iw = w - 32;
    {
        const float widths[5] = { 1.0f, 1.2f, 1.1f, 1.0f, 1.5f };
        const int gap = 6;
        float x = (float) x0;
        const float unit = (float) (iw - 4 * gap) / 5.8f;
        for (int i = 0; i < 5; ++i)
        {
            steps[i].setBounds (juce::roundToInt (x), 4, juce::roundToInt (unit * widths[i]), 32);
            x += unit * widths[i] + (float) gap;
        }
    }
    soundsButton.setBounds (x0 + iw - 104, 42, 104, 44);
    toolButton.setBounds (x0 + iw - 104 - 8 - 88, 42, 88, 44);
    titleArea = { (float) x0, 42.0f, (float) (iw - 104 - 8 - 88 - 8), 44.0f };
    auto sendRow = juce::Rectangle<int> (x0, h - 8 - 56, iw, 56);
    auto hearRow = sendRow.withY (sendRow.getY() - 8 - 52).withHeight (52);
    auto row1 = juce::Rectangle<int> (x0, hearRow.getY() - 8 - 44, iw, 44);
    infoArea = { (float) x0, (float) (row1.getY() - 6 - 24), (float) iw, 24.0f };
    wave.setBounds (x0, 94, iw, juce::jmax (90, (int) infoArea.getY() - 4 - 94));

    pickButton.setBounds (sendRow.removeFromRight (92));
    sendRow.removeFromRight (8);
    sendButton.setBounds (sendRow);
    hearButton.setBounds (hearRow.removeFromLeft (hearRow.getWidth() * 3 / 10));
    hearRow.removeFromLeft (8);
    chopButton.setBounds (hearRow.removeFromRight (hearRow.getWidth() * 11 / 20));
    hearRow.removeFromRight (8);
    trimButton.setBounds (hearRow);
    const int small = juce::jmin (72, (row1.getWidth() - 3 * 8) / 4);
    allButton.setBounds (row1.removeFromLeft (small));
    row1.removeFromLeft (8);
    undoButton.setBounds (row1.removeFromLeft (small));
    row1.removeFromLeft (8);
    clearButton.setBounds (row1.removeFromLeft (small));
    row1.removeFromLeft (8);
    moreButton.setBounds (row1);
}

//==============================================================================
SamplerPadPicker::SamplerPadPicker (Kit& k) : kit (k)
{
    setDragZone (70);
    for (int i = 0; i < 4; ++i)
    {
        banks[i].setText ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + i)));
        banks[i].setFontHeight (16.0f);
        banks[i].onClick = [this, i] { shownBank = i; for (int b = 0; b < 4; ++b) banks[b].setLook (b == i ? Look::amber : Look::cream); repaint(); };
        addAndMakeVisible (banks[i]);
    }
    cancel.setFontHeight (17.0f);
    cancel.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancel);
}

void SamplerPadPicker::show (int targetPad, int bank, const juce::String& soundName)
{
    target = targetPad;
    shownBank = juce::jlimit (0, 3, targetPad >= 0 ? targetPad / 16 : bank);
    what = soundName;
    pressed = -1;
    for (int b = 0; b < 4; ++b)
        banks[b].setLook (b == shownBank ? Look::amber : Look::cream);
    repaint();
}

juce::Rectangle<float> SamplerPadPicker::padRect (int i) const
{
    const float x0 = 16.0f, top = 132.0f, gap = 6.0f;
    const float cw = ((float) getWidth() - 32.0f - 3.0f * gap) / 4.0f, ch = 64.0f;
    return { x0 + (float) (i % 4) * (cw + gap), top + (float) (i / 4) * (ch + gap), cw, ch };
}

int SamplerPadPicker::padAt (juce::Point<float> p) const
{
    for (int i = 0; i < 16; ++i)
        if (padRect (i).expanded (3.0f).contains (p))
            return i;
    return -1;
}

void SamplerPadPicker::resized()
{
    const float w = (float) getWidth();
    const float bw = (w - 32.0f - 18.0f) / 4.0f;
    for (int i = 0; i < 4; ++i)
        banks[i].setBounds (juce::Rectangle<float> (16.0f + (float) i * (bw + 6.0f), 76.0f, bw, 44.0f).toNearestInt());
    cancel.setBounds (16, (int) padRect (12).getBottom() + 12, getWidth() - 32, 48);
}

void SamplerPadPicker::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);
    drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (fraunces (24.0f));
    g.drawText ("Send to a pad", 18, 20, getWidth() - 36, 32, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (small (14.0f));
    g.drawFittedText (U8("\"") + what + U8("\" \xc2\xb7 tap the pad it goes on") + (target >= 0 ? U8(" \xc2\xb7 lit: pad ") + Kit::padLabel (target) : juce::String()),
                      juce::Rectangle<int> (18, 50, getWidth() - 36, 22), juce::Justification::centredLeft, 1, 0.8f);
    for (int i = 0; i < 16; ++i)
    {
        const int pad = shownBank * 16 + i;
        const auto& p = kit.pads[(size_t) pad];
        auto r = padRect (i);
        const float down = pressed == i ? 2.0f : 0.0f;
        g.setColour (juce::Colour (0xff120f0c));
        g.fillRoundedRectangle (r.translated (0, 3), 10.0f);
        g.setColour (pad == target ? col::amber : p.empty() ? juce::Colour (0xff6a5e52) : kPadDark);
        g.fillRoundedRectangle (r.translated (0, down), 10.0f);
        const auto ink = pad == target ? col::ink : kPadText;
        g.setColour (ink.withAlpha (0.8f));
        g.setFont (lcdFont (17.0f));
        g.drawText (Kit::padLabel (pad), r.translated (0, down).reduced (8, 4).withHeight (20), juce::Justification::topLeft);
        g.setColour (p.empty() ? ink.withAlpha (0.55f) : ink);
        g.setFont (condensedBold (14.0f));
        g.drawFittedText (p.empty() ? juce::String ("empty") : p.name.toUpperCase(), r.translated (0, down).reduced (7, 6).withTrimmedTop (22).toNearestInt(),
                          juce::Justification::bottomLeft, 2, 0.7f);
    }
}

void SamplerPadPicker::mouseDown (const juce::MouseEvent& e)
{
    pressed = padAt (e.position);
    repaint();
}

void SamplerPadPicker::mouseUp (const juce::MouseEvent& e)
{
    const int was = pressed;
    pressed = -1;
    repaint();
    if (was >= 0 && padAt (e.position) == was && ! isDragging() && onPick)
        onPick (shownBank * 16 + was);
}
