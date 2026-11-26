#include "RecordScreen.h"
#include "PocketFactory.h"
#include "PocketKit.h"

#include "core/AudioFiles.h"
#include "core/SoundFx.h"

using namespace pocket;

namespace
{
juce::String clock (double s)
{
    const int m = (int) (s / 60.0);
    return juce::String (m) + ":" + juce::String (s - m * 60.0, 1).paddedLeft ('0', 4);
}

void drawArrow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool forward)
{
    // an open circle with an arrowhead: undo turns one way, redo the other
    const float rad = 7.5f, a1 = 4.7f;
    const float flip = forward ? -1.0f : 1.0f;
    auto ctr = r.getCentre().translated (0, 1);
    juce::Path p;
    p.addCentredArc (ctr.x, ctr.y, rad, rad, 0, 0.5f, a1, true);
    if (forward)
        p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, ctr.x, ctr.y));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.2f));
    const float ex = ctr.x + flip * std::sin (a1) * rad, ey = ctr.y - std::cos (a1) * rad;
    const float tx = flip * std::cos (a1), ty = std::sin (a1);
    juce::Path head;
    head.addTriangle (ex + tx * 5.5f, ey + ty * 5.5f, ex - ty * 4.0f, ey + tx * 4.0f, ex + ty * 4.0f, ey - tx * 4.0f);
    g.fillPath (head);
}
}

RecordScreen::RecordScreen (PocketHost& h, PocketAudio& a, Library& lib, EffectsSheet& s)
    : host (h), audio (a), library (lib), sheet (s)
{
    for (auto* c : std::initializer_list<juce::Component*> { &wave, &nameField, &recButton, &play_, &selectAll, &undoButton, &redoButton, &keep,
                                                             &del, &trim, &fadeIn, &fadeOut, &normal, &reverse, &effectsRow, &chopRow,
                                                             &saveButton, &toPad, &toMac })
        addAndMakeVisible (c);

    wave.setTitle ("");
    wave.onSelectionChange = [this] { updateLabels(); };

    nameField.setText ("", false);
    nameField.setFont (small (18.0f));
    nameField.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    nameField.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    nameField.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    nameField.setColour (juce::TextEditor::textColourId, col::ink);
    nameField.setColour (juce::CaretComponent::caretColourId, col::amberDark);
    nameField.setIndents (0, 0);
    nameField.setJustification (juce::Justification::centredLeft);
    nameField.setTextToShowWhenEmpty ("Name this sound", col::inkFaint);
    nameField.setReturnKeyStartsNewLine (false);
    nameField.onReturnKey = [this] { nameField.giveAwayKeyboardFocus(); };
    nameField.onTextChange = [this] { dirty = dirty || currentId.isNotEmpty(); updateLabels(); };

    recButton.setLook (Look::ghost);
    recButton.onClick = [this] { toggleRecord(); };
    recButton.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        const float d = juce::jmin (r.getWidth(), r.getHeight()) - 4;
        auto c = r.getCentre();
        auto circle = juce::Rectangle<float> (d, d).withCentre (c);
        g.setColour (juce::Colour (0xff7a1a42));
        g.fillEllipse (circle.translated (0, 4));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff27ba8), c.x, circle.getY(), juce::Colour (0xffec4e8a), c.x, circle.getBottom(), false));
        g.fillEllipse (circle);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawEllipse (circle.reduced (1), 1.2f);
        const bool rec = audio.isRecording() || recordingLook;
        if (rec)
        {
            g.setColour (juce::Colour (0xff23100c));
            g.fillRoundedRectangle (juce::Rectangle<float> (18, 18).withCentre (c.translated (0, -8)), 3.0f);
        }
        else
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (juce::Rectangle<float> (24, 24).withCentre (c.translated (0, -8)), 3.0f);
            g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (c.translated (0, -8)));
        }
        g.setColour (juce::Colour (0xff3a120b));
        g.setFont (spaced (condensed (14.0f), 0.08f));
        g.drawText (rec ? "STOP" : "REC", juce::Rectangle<float> (60, 16).withCentre (c.translated (0, 20)), juce::Justification::centred);
    };

    play_.glyph = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        const bool on = audio.isPlaying();
        g.setColour (ink);
        const float cy = r.getCentreY();
        const float x = r.getX() + 16;
        juce::Path p;
        if (on)
            p.addRoundedRectangle (x, cy - 6, 11.0f, 12.0f, 2.0f);
        else
            p.addTriangle (x, cy - 7, x, cy + 7, x + 12, cy);
        g.fillPath (p);
        g.setFont (spaced (condensed (17.0f)));
        g.drawText (on ? "STOP" : "PLAY", r.withTrimmedLeft (x - r.getX() + 18).withTrimmedRight (6), juce::Justification::centredLeft);
    };
    play_.onClick = [this] { togglePlay(); };

    selectAll.onClick = [this]
    {
        if (! needSound())
            return;
        wave.selectAll();
        updateLabels();
    };
    undoButton.glyph = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { drawArrow (g, r, c, false); };
    redoButton.glyph = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { drawArrow (g, r, c, true); };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };

    keep.onClick = [this]
    {
        if (! needSound())
            return;
        if (wave.getSelection().getLength() <= 0)
            return host.toast ("Drag the handles to pick the part to keep");
        const int start = wave.getSelection().getStart();
        auto markers = std::vector<int>();
        for (int m : wave.getMarkers())
            if (m > start && m < wave.getSelection().getEnd())
                markers.push_back (m - start);
        auto r = crate::Edits::crop (*wave.getAudio(), wave.getSelection());
        commit (crate::share (std::move (r.audio)), r.selection, markers);
        host.toast ("Kept the selection");
    };
    del.onClick = [this]
    {
        if (! needSound())
            return;
        if (wave.getSelection().getLength() <= 0)
            return host.toast ("Drag the handles to pick the part to delete");
        auto r = crate::Edits::remove (*wave.getAudio(), wave.getSelection());
        commit (crate::share (std::move (r.audio)), {}, {});
        host.toast ("Deleted the selection");
    };
    trim.onClick = [this]
    {
        if (! needSound())
            return;
        auto r = crate::Edits::trimSilence (*wave.getAudio());
        if (r.audio.getNumSamples() == wave.getAudio()->getNumSamples())
            return host.toast ("No silence to trim");
        commit (crate::share (std::move (r.audio)), {}, {});
        host.toast ("Trimmed the silence");
    };
    fadeIn.onClick = [this] { edit ("Faded in", [] (auto& b, auto sel) { return crate::Edits::fade (b, crate::Edits::orWhole (b, sel), true, crate::Edits::FadeCurve::smooth); }, true); };
    fadeOut.onClick = [this] { edit ("Faded out", [] (auto& b, auto sel) { return crate::Edits::fade (b, crate::Edits::orWhole (b, sel), false, crate::Edits::FadeCurve::smooth); }, true); };
    normal.onClick = [this] { edit ("Normalized", [] (auto& b, auto sel) { return crate::Edits::normalize (b, sel); }, true); };
    reverse.onClick = [this] { edit ("Reversed", [] (auto& b, auto sel) { return crate::Edits::reverse (b, sel); }, false); };

    effectsRow.onClick = [this] { host.openSheet(); };
    chopRow.onClick = [this] { host.openSheet(); };
    effectsRow.setLeftAligned (true);
    chopRow.setLeftAligned (true);
    effectsRow.setFontHeight (18.0f);
    chopRow.setFontHeight (18.0f);
    effectsRow.setText ("Effects");
    chopRow.setText ("Chop");
    effectsRow.setLamp (true, true);
    chopRow.setLamp (true, false);

    saveButton.setFontHeight (17.0f);
    saveButton.onClick = [this]
    {
        saveToSounds();
        if (targetPad >= 0 && currentId.isNotEmpty())
            host.padSoundEdited (currentId);
    };
    toPad.onClick = [this]
    {
        if (targetPad >= 0)
        {
            savePadEdits();   // KEEP, TRIM SILENCE, a fade...: what you did is what the pad plays
            return host.backToPads (targetPad);
        }
        saveToSounds();   // the pad plays the saved sound, with its chop markers and effects
        if (currentId.isNotEmpty())
            host.sendToPad (currentId);
    };
    toMac.onClick = [this]
    {
        saveToSounds();   // what goes is the saved sound, with its effects
        if (currentId.isNotEmpty())
            host.sendSoundToMac (currentId);
    };

    for (auto* b : { &keep, &del, &trim, &fadeIn, &fadeOut, &normal, &reverse, &selectAll, &toPad, &toMac })
        b->setFontHeight (16.0f);

    updateLabels();
    startTimerHz (30);
}

RecordScreen::~RecordScreen() { stopTimer(); }

RecordScreen::State RecordScreen::current() const
{
    return { wave.getAudio(), wave.getSelection(), wave.getMarkers() };
}

void RecordScreen::restore (const State& s)
{
    wave.setAudio (s.audio);
    wave.setSelection (s.selection);
    wave.setMarkers (s.markers);
    audio.stopPlaying();
    updateLabels();
}

void RecordScreen::commit (crate::SharedAudio a, juce::Range<int> selection, std::vector<int> markers)
{
    undoStack.push_back (current());
    redoStack.clear();
    restore ({ std::move (a), selection, std::move (markers) });
    dirty = true;
}

void RecordScreen::undo()
{
    if (undoStack.empty())
        return host.toast ("Nothing to undo");
    redoStack.push_back (current());
    restore (undoStack.back());
    undoStack.pop_back();
    dirty = true;
}

void RecordScreen::redo()
{
    if (redoStack.empty())
        return host.toast ("Nothing to redo");
    undoStack.push_back (current());
    restore (redoStack.back());
    redoStack.pop_back();
    dirty = true;
}

bool RecordScreen::needSound()
{
    if (wave.getAudio() != nullptr && wave.getAudio()->getNumSamples() > 0)
        return true;
    host.toast ("Record or open a sound first");
    return false;
}

void RecordScreen::edit (const juce::String& what,
                         std::function<crate::Edits::Result (const juce::AudioBuffer<float>&, juce::Range<int>)> fn, bool keepMarkers)
{
    if (! needSound())
        return;
    auto r = fn (*wave.getAudio(), wave.getSelection());
    commit (crate::share (std::move (r.audio)), wave.getSelection(), keepMarkers ? wave.getMarkers() : std::vector<int>());
    host.toast (what + (wave.getSelection().getLength() > 0 ? " the selection" : ""));
}

void RecordScreen::updateLabels()
{
    auto a = wave.getAudio();
    juce::String title = (targetPad >= 0 ? "PAD " + pocket::Kit::padLabel (targetPad) + U8(" · ") : juce::String()) + nameField.getText().toUpperCase();
    if (a != nullptr)
        title += U8(" · ") + clock (crate::seconds (a->getNumSamples()));
    wave.setTitle (title);

    const int on = fx.onCount();
    effectsRow.setDetail (on == 0 ? juce::String ("off") : juce::String (on) + " on" + (sheet.hearEffects() ? U8(" · hearing them") : "") + U8("  ›"));
    effectsRow.setLamp (true, on > 0);
    const int n = (int) wave.getMarkers().size();
    chopRow.setDetail ((n == 0 ? juce::String ("no markers") : juce::String (n) + (n == 1 ? " marker" : " markers")) + U8("  ›"));
    chopRow.setLamp (true, n > 0);
    undoButton.setEnabled (true);
    repaint();
}

//==============================================================================
void RecordScreen::toggleRecord()
{
    if (audio.isRecording())
    {
        finishRecording();
        return;
    }
    if (! audio.isRunning())
        return host.toast ("The microphone isn't ready. Check Settings > Kaleidobox Pocket.");

    audio.stopPlaying();
    takeFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-take.wav");
    if (! audio.startRecording (takeFile))
        return host.toast ("No microphone found, or access is off");
    recButton.repaint();
    repaint();
}

void RecordScreen::finishRecording()
{
    audio.stopRecording();
    juce::String error;
    auto loaded = crate::AudioFiles::load (takeFile, library.formats(), error);
    takeFile.deleteFile();
    if (loaded == nullptr || loaded->getNumSamples() < 100)
    {
        host.toast ("That recording was empty");
        return;
    }
    undoStack.push_back (current());
    redoStack.clear();
    currentId = {};
    dirty = true;
    if (nameField.getText().isEmpty() || nameField.getText().startsWith ("Take "))
        nameField.setText ("Take " + juce::Time::getCurrentTime().formatted ("%H:%M"), false);
    wave.setAudio (loaded);
    wave.selectAll();
    fx = {};
    sheet.setKnobs (fx);
    updateLabels();
    host.toast ("Recorded " + clock (crate::seconds (loaded->getNumSamples())));
}

void RecordScreen::togglePlay()
{
    if (audio.isPlaying())
        return audio.stopPlaying();
    if (audio.isRecording())
        return host.toast ("Stop recording first");
    if (! needSound())
        return;
    play (sheet.hearEffects() && fx.anyOn());
}

void RecordScreen::play (bool withFx)
{
    auto a = wave.getAudio();
    if (a == nullptr)
        return;
    auto sel = crate::Edits::orWhole (*a, wave.getSelection());
    playingWithFx = withFx;
    if (withFx)
    {
        auto part = crate::SoundFx::apply (crate::Edits::copy (*a, sel), fx.toSettings());
        auto shared = crate::share (std::move (part));
        playOffset = 0;
        audio.play (shared, 0, shared->getNumSamples());
    }
    else
    {
        playOffset = 0;
        audio.play (a, sel.getStart(), sel.getEnd());
    }
    if (! audio.isRunning())
        host.toast ("The speaker isn't ready");
}

void RecordScreen::previewSoon()
{
    previewCountdown = 10;   // ~1/3 s of no knob movement, then play
}

void RecordScreen::saveToSounds()
{
    if (! needSound())
        return;
    auto name = nameField.getText().trim();
    if (name.isEmpty())
        name = "Take " + juce::Time::getCurrentTime().formatted ("%H:%M");
    nameField.setText (name, false);
    if (auto* item = library.find (currentId); currentId.isNotEmpty() && item != nullptr)
    {
        if (item->tag == pocket::kTagFactory)
        {
            // A factory kit's sound is on that kit in every song: the edit becomes your own copy, and this song's pads
            // that played the factory one play the copy.
            const auto old = currentId;
            const auto id = library.add (name, kTagRecorded, *wave.getAudio(), fx, wave.getMarkers());
            if (id.isEmpty())
                return host.toast ("Couldn't save that sound");
            currentId = id;
            dirty = false;
            host.padSoundSwapped (old, id);
            host.toast ("Saved as your own \"" + name + "\" (the factory kit stays as it was)");
            return;
        }
        if (library.update (currentId, name, *wave.getAudio(), fx, wave.getMarkers()))
        {
            dirty = false;
            host.toast ("Updated \"" + name + "\" in Sounds");
        }
        else
            host.toast ("Couldn't save that sound");
        return;
    }
    currentId = library.add (name, kTagRecorded, *wave.getAudio(), fx, wave.getMarkers());
    dirty = currentId.isEmpty();
    host.toast (currentId.isEmpty() ? juce::String ("Couldn't save that sound") : "Saved \"" + name + "\" to Sounds");
}

bool RecordScreen::savePadEdits()
{
    if (targetPad < 0 || ! dirty || wave.getAudio() == nullptr || wave.getAudio()->getNumSamples() == 0)
        return false;
    saveToSounds();
    if (dirty || currentId.isEmpty())
        return false;
    host.padSoundEdited (currentId);
    return true;
}

void RecordScreen::editForPad (int pad, const juce::String& id)
{
    loadSound (id);
    if (currentId != id)
        return;
    targetPad = pad;
    const auto label = pocket::Kit::padLabel (pad);
    saveButton.setText ("Save to pad " + label);
    toPad.setText (U8("\xe2\x80\xb9 Sampler"));
    updateLabels();
    host.toast ("Pad " + label + U8("'s sound. Edit it, then SAVE TO PAD ") + label + ". Every pad playing it picks it up.");
}

void RecordScreen::endPadMode()
{
    if (targetPad < 0)
        return;
    targetPad = -1;
    saveButton.setText ("Save to sounds");
    toPad.setText ("To pad");
    updateLabels();
}

void RecordScreen::loadSound (const juce::String& id)
{
    endPadMode();
    auto* item = library.find (id);
    auto loaded = library.load (id);
    if (item == nullptr || loaded == nullptr)
        return host.toast ("That sound couldn't be opened");
    audio.stopPlaying();
    undoStack.clear();
    redoStack.clear();
    currentId = id;
    dirty = false;
    nameField.setText (item->name, false);
    fx = item->fx;
    sheet.setKnobs (fx);
    wave.setAudio (loaded);
    wave.setMarkers (item->markers);
    wave.selectAll();
    updateLabels();
}

void RecordScreen::showDemoTake (crate::SharedAudio a, const juce::String& name, bool rec)
{
    nameField.setText (name, false);
    recordingLook = rec;
    wave.setAudio (std::move (a));
    const int n = wave.getAudio()->getNumSamples();
    wave.setSelection ({ n * 22 / 100, n * 76 / 100 });
    fx = {};
    sheetClosed();
    updateLabels();
}

//==============================================================================
void RecordScreen::wireSheet()
{
    sheet.onKnobsChange = [this]
    {
        fx = sheet.getKnobs();
        dirty = true;
        updateLabels();
        if (sheet.hearEffects())
            previewSoon();
    };
    sheet.onHearChanged = [this]
    {
        updateLabels();
        if (sheet.hearEffects() && fx.anyOn())
            previewSoon();
        else
            audio.stopPlaying();
    };
    sheet.onApply = [this]
    {
        if (! needSound())
            return;
        fx = sheet.getKnobs();
        if (! fx.anyOn())
            return host.toast ("No effects are on yet. Turn a knob first.");
        audio.stopPlaying();
        auto rendered = crate::SoundFx::apply (*wave.getAudio(), fx.toSettings());
        commit (crate::share (std::move (rendered)), {}, {});
        fx = {};
        sheet.resetKnobs();
        sheet.chopWave.setAudio (wave.getAudio());
        sheet.chopWave.setMarkers ({});
        updateLabels();
        host.toast ("Effects applied. Undo takes them back.");
    };
    sheet.onFindHits = [this]
    {
        if (! needSound())
            return;
        auto hits = crate::SoundFx::findHits (*wave.getAudio(), sheet.sensitivity());
        hits.erase (std::remove_if (hits.begin(), hits.end(), [] (int h) { return h <= 0; }), hits.end());
        std::sort (hits.begin(), hits.end());
        wave.setMarkers (hits);
        sheet.chopWave.setMarkers (hits);
        dirty = true;
        updateLabels();
        host.toast (hits.empty() ? juce::String ("No hits found. Try a higher sensitivity.") : "Found " + juce::String ((int) hits.size()) + " hits");
    };
    sheet.onEven = [this] (int slices)
    {
        if (! needSound())
            return;
        auto m = crate::SoundFx::gridMarkers (wave.getAudio()->getNumSamples(), slices);
        m.erase (std::remove_if (m.begin(), m.end(), [] (int x) { return x <= 0; }), m.end());
        wave.setMarkers (m);
        sheet.chopWave.setMarkers (m);
        dirty = true;
        updateLabels();
    };
    sheet.onClear = [this]
    {
        wave.setMarkers ({});
        sheet.chopWave.setMarkers ({});
        dirty = true;
        updateLabels();
        host.toast ("Markers cleared");
    };
    sheet.onDone = [this] { host.closeSheet(); };
    sheet.chopWave.onMarkersChange = [this]
    {
        wave.setMarkers (sheet.chopWave.getMarkers());
        dirty = true;
        updateLabels();
    };
}

void RecordScreen::sheetOpened()
{
    sheetShown = true;
    wireSheet();
    sheet.setKnobs (fx);
    sheet.chopWave.setAudio (wave.getAudio());
    sheet.chopWave.setMarkers (wave.getMarkers());
}

void RecordScreen::sheetClosed()
{
    sheetShown = false;
    previewCountdown = 0;
    if (playingWithFx)
        audio.stopPlaying();
    updateLabels();
}

//==============================================================================
void RecordScreen::timerCallback()
{
    const bool rec = audio.isRecording();
    const float lv = audio.takeLevel();
    level = juce::jmax (lv, level * 0.82f);

    if (rec && audio.recordedSeconds() >= crate::AudioFiles::kMaxSeconds)
        finishRecording();

    if (previewCountdown > 0 && --previewCountdown == 0 && sheet.hearEffects() && fx.anyOn() && wave.getAudio() != nullptr)
        play (true);

    if (audio.isPlaying() && wave.getAudio() != nullptr && ! playingWithFx)
        wave.setPlayhead (audio.position());
    else
        wave.setPlayhead (-1);

    play_.repaint();
    recButton.repaint();
    repaint (lcdArea.toNearestInt());
}

void RecordScreen::paint (juce::Graphics& g)
{
    drawWell (g, lcdArea, 16.0f);
    const bool rec = audio.isRecording() || recordingLook;
    const double secs = audio.isRecording() ? audio.recordedSeconds()
                      : recordingLook       ? 12.4
                      : wave.getAudio() != nullptr ? crate::seconds (wave.getAudio()->getNumSamples()) : 0.0;

    auto lcd = lcdArea.reduced (14, 8);
    g.setColour (col::lcdText);
    g.setFont (lcdFont (50.0f));
    g.drawText (clock (secs), lcd.removeFromTop (46), juce::Justification::centredLeft);

    auto lamp = lcdArea.reduced (14, 12).removeFromTop (28).removeFromRight (60);
    g.setColour (rec ? col::lcdAmber : col::lcdAmber.withAlpha (0.22f));
    g.fillEllipse (lamp.removeFromLeft (14).withSizeKeepingCentre (12, 12));
    g.setFont (lcdFont (22.0f));
    g.drawText ("REC", lamp.withTrimmedLeft (3), juce::Justification::centredLeft);

    g.setColour (col::lcdAmber);
    g.setFont (lcdFont (17.0f));
    g.drawText (U8("MIC · NATURAL · 44.1 KHZ"), lcdArea.reduced (14, 0).withTrimmedTop (lcdArea.getHeight() - 46).withHeight (18), juce::Justification::centredLeft);

    // level meter: 20 segments
    auto meter = juce::Rectangle<float> (lcdArea.getX() + 14, lcdArea.getBottom() - 20, lcdArea.getWidth() - 28, 9);
    const float db = level > 0.0001f ? juce::Decibels::gainToDecibels (level) : -60.0f;
    const int lit = recordingLook && ! audio.isRecording() ? 15 : juce::jlimit (0, 20, (int) std::round ((db + 48.0f) / 48.0f * 20.0f));
    const float seg = meter.getWidth() / 20.0f;
    for (int i = 0; i < 20; ++i)
    {
        auto c = i < 15 ? col::green : i < 18 ? col::amber : col::red;
        g.setColour (i < lit ? c : juce::Colour (0xff3a2f4f));
        g.fillRoundedRectangle (meter.getX() + (float) i * seg, meter.getY(), seg - 2.5f, meter.getHeight(), 1.5f);
    }

    // the name field's box
    g.setColour (col::creamLight);
    g.fillRoundedRectangle (nameBox, 14.0f);
    g.setColour (col::rule.withAlpha (0.8f));
    g.drawRoundedRectangle (nameBox, 14.0f, 1.0f);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (16.0f), 0.1f));
    g.drawText ("NAME", nameBox.withTrimmedLeft (16).withWidth (60), juce::Justification::centredLeft);
}

void RecordScreen::resized()
{
    const int w = getWidth(), pad = 16;
    const int h = getHeight();
    // Everything below the waveform has a fixed height, so the waveform takes what's left.
    const int fixedBelow = 44 + 6 + 44 + 6 + 44 + 10 + 44 + 6 + 44 + 10 + 48 + 10 + 48;
    const int waveH = juce::jlimit (118, 150, h - 10 - 88 - 8 - fixedBelow - 12);
    int y = 8;

    auto lcdRow = juce::Rectangle<int> (pad, y, w - 2 * pad, 88);
    recButton.setBounds (lcdRow.removeFromLeft (96).expanded (0, 4).translated (-4, 0));
    lcdRow.removeFromLeft (4);
    lcdArea = lcdRow.toFloat();
    y += 88 + 8;

    wave.setBounds (pad, y, w - 2 * pad, waveH);
    y += waveH + 8;

    {
        auto row = juce::Rectangle<int> (pad, y, w - 2 * pad, 44);
        play_.setBounds (row.removeFromLeft (96));
        row.removeFromLeft (8);
        redoButton.setBounds (row.removeFromRight (46));
        row.removeFromRight (8);
        undoButton.setBounds (row.removeFromRight (46));
        row.removeFromRight (8);
        selectAll.setBounds (row);
        y += 44 + 6;
    }
    {
        auto row = juce::Rectangle<int> (pad, y, w - 2 * pad, 44);
        const int each = (row.getWidth() - 12) / 4;
        keep.setBounds (row.removeFromLeft (each));
        row.removeFromLeft (6);
        del.setBounds (row.removeFromLeft (each));
        row.removeFromLeft (6);
        trim.setBounds (row);
        y += 44 + 6;
    }
    {
        auto row = juce::Rectangle<int> (pad, y, w - 2 * pad, 44);
        const int each = (row.getWidth() - 18) / 4;
        for (auto* b : { &fadeIn, &fadeOut, &normal })
        {
            b->setBounds (row.removeFromLeft (each));
            row.removeFromLeft (6);
        }
        reverse.setBounds (row);
        y += 44 + 10;
    }
    effectsRow.setBounds (pad, y, w - 2 * pad, 44);
    y += 44 + 6;
    chopRow.setBounds (pad, y, w - 2 * pad, 44);
    y += 44 + 10;
    nameBox = juce::Rectangle<float> ((float) pad, (float) y, (float) (w - 2 * pad), 48.0f);
    nameField.setBounds (nameBox.toNearestInt().withTrimmedLeft (74).withTrimmedRight (14).reduced (0, 6));
    y += 48 + 10;
    {
        auto row = juce::Rectangle<int> (pad, y, w - 2 * pad, 48);
        toMac.setBounds (row.removeFromRight (88));
        row.removeFromRight (8);
        toPad.setBounds (row.removeFromRight (88));
        row.removeFromRight (8);
        saveButton.setBounds (row);
    }
}
