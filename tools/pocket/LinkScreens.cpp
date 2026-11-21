#include "LinkScreens.h"

#include "core/AudioFiles.h"

using namespace pocket;

namespace
{
const juce::Colour darkBg { 0xff1f1b17 }, darkKey { 0xff3a332c }, darkKeyTop { 0xff453d35 }, creamText { 0xfff1e7d6 }, dimText { 0xffa89c88 };
const juce::Colour lcdGreen { 0xff7cf29a }, muteBlue { 0xff4aa8e8 };

void drawDot (juce::Graphics& g, juce::Point<float> c, juce::Colour colour, bool glow)
{
    if (glow)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (c.x - 9, c.y - 9, 18, 18);
    }
    g.setColour (colour);
    g.fillEllipse (c.x - 5.5f, c.y - 5.5f, 11, 11);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse (c.x - 5.5f, c.y - 5.5f, 11, 11, 1.0f);
}

// A key on the dark Remote panel.
void darkKeyShape (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, bool down, float radius = 10.0f)
{
    auto body = r.withTrimmedBottom (3.0f).translated (0, down ? 2.0f : 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (body.translated (0, down ? 1.0f : 3.0f), radius);
    g.setGradientFill (juce::ColourGradient (top, 0, body.getY(), bottom, 0, body.getBottom(), false));
    g.fillRoundedRectangle (body, radius);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (body.reduced (0.6f), radius, 1.0f);
}

juce::String upper (const juce::String& s) { return s.toUpperCase(); }

juce::String lengthText (const crate::SharedAudio& a)
{
    if (a == nullptr)
        return {};
    const int secs = juce::roundToInt ((double) a->getNumSamples() / crate::kSampleRate);
    return juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
}
}

//==============================================================================
MacLinkScreen::MacLinkScreen (LinkClient& c) : client (c)
{
    // The numbers go into a see-through text box over the four boxes, so the phone's number pad comes up.
    entry.setInputRestrictions (4, "0123456789");
    entry.setKeyboardType (juce::TextInputTarget::numericKeyboard);
    for (auto id : { juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId, juce::TextEditor::focusedOutlineColourId,
                     juce::TextEditor::textColourId, juce::TextEditor::highlightColourId, juce::TextEditor::highlightedTextColourId })
        entry.setColour (id, juce::Colours::transparentBlack);
    entry.setCaretVisible (false);
    entry.setPopupMenuEnabled (false);
    entry.onTextChange = [this] { repaint(); };
    entry.onReturnKey = [this] { pressLink(); };
    addAndMakeVisible (entry);
}

void MacLinkScreen::resized()
{
    entry.setBounds (digitRect (0).getUnion (digitRect (3)).toNearestInt());
}

void MacLinkScreen::shown()
{
    entry.setText ({}, false);
    picked = {};
    refresh();
}

void MacLinkScreen::refresh()
{
    const auto rows = client.macs();
    bool stillThere = false;
    for (auto& r : rows)
        stillThere = stillThere || r.name == picked;
    if (! stillThere)
    {
        picked = {};
        // The linked Mac, else the first one with Kaleidobox open.
        for (auto& r : rows)
            if (r.name == client.getMacName())
                picked = r.name;
        for (auto& r : rows)
            if (picked.isEmpty() && r.open)
                picked = r.name;
    }
    entry.setVisible (! client.isLinked());
    repaint();
}

void MacLinkScreen::paint (juce::Graphics& g)
{
    const bool linked = client.isLinked();
    const auto w = (float) getWidth();

    g.setColour (col::ink);
    g.setFont (spaced (condensed (16.0f), 0.12f));
    g.drawText ("CLOSE", closeRect(), juce::Justification::centredRight);

    // the Mac, showing its numbers
    auto screen = juce::Rectangle<float> (w * 0.5f - 52, 30, 104, 64);
    g.setColour (col::ink);
    g.fillRoundedRectangle (screen, 8.0f);
    drawWell (g, screen.reduced (6), 4.0f);
    g.setColour (col::lcdAmber);
    g.setFont (lcdFont (26.0f));
    juce::String shown;
    const auto typed = entry.getText();
    for (int i = 0; i < 4; ++i)
        shown << (i > 0 ? " " : "") << (i < typed.length() ? typed.substring (i, i + 1) : juce::String ("-"));
    if (linked)
        shown = "LINKED";
    g.drawText (shown, screen.reduced (6), juce::Justification::centred);
    juce::Path base;
    base.addQuadrilateral (screen.getX() - 6, screen.getBottom() + 2, screen.getRight() + 6, screen.getBottom() + 2,
                           screen.getRight() + 18, screen.getBottom() + 12, screen.getX() - 18, screen.getBottom() + 12);
    g.setColour (col::inkFaint);
    g.fillPath (base);

    g.setColour (col::ink);
    g.setFont (fraunces (31.0f));
    g.drawText (linked ? "Linked to your Mac" : "Link to your Mac", juce::Rectangle<float> (0, 116, w, 42), juce::Justification::centred);
    g.setColour (col::inkDim);
    g.setFont (small (16.0f));
    const juce::String how = linked ? "Sounds and kits go straight to " + client.getMacName() + ", and the Remote runs Kaleidobox there."
                                    : U8("On the Mac, open Kaleidobox, then Settings › iPhone. Type the four numbers it shows.");
    g.drawFittedText (how, juce::Rectangle<int> (30, 160, getWidth() - 60, 46), juce::Justification::centredTop, 2);

    if (linked)
    {
        auto card = juce::Rectangle<float> (16, 214, w - 32, 82);
        drawRaised (g, card, Look::cream, false, 16.0f);
        drawDot (g, { card.getX() + 26, card.getCentreY() - 1 }, col::green, true);
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (22.0f), 0.06f));
        g.drawText (upper (client.getMacName()), card.withTrimmedLeft (48).withTrimmedTop (14).withHeight (28), juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (small (15.0f));
        const auto show = client.getShow();
        g.drawText (show.isNotEmpty() ? U8("Kaleidobox open · ") + show : juce::String ("Kaleidobox open"),
                    card.withTrimmedLeft (48).withTrimmedTop (44).withHeight (22), juce::Justification::centredLeft);
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            auto r = digitRect (i);
            drawWell (g, r, 10.0f);
            const bool cursor = i == typed.length() && client.getStatus() != LinkClient::Status::connecting;
            if (i < typed.length())
            {
                g.setColour (col::lcdAmber);
                g.setFont (lcdFont (60.0f));
                g.drawText (typed.substring (i, i + 1), r, juce::Justification::centred);
            }
            else if (cursor)
            {
                g.setColour (col::lcdAmber);
                g.fillRect (r.getCentreX() - 12, r.getBottom() - 24, 24.0f, 4.0f);
                g.setColour (col::amber);
                g.drawRoundedRectangle (r.expanded (1.5f), 11.0f, 2.5f);
            }
        }
    }

    // what went wrong, if anything
    if (client.getError().isNotEmpty() && ! linked)
    {
        g.setColour (col::red);
        g.setFont (small (14.0f));
        g.drawFittedText (client.getError(), juce::Rectangle<int> (16, 300, getWidth() - 32, 20), juce::Justification::centred, 1);
    }

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.14f));
    g.drawText ("MACS ON THIS WI-FI", juce::Rectangle<float> (18, 320, w - 36, 20), juce::Justification::centredLeft);

    const auto rows = client.macs();
    if (rows.empty())
    {
        g.setColour (col::inkDim);
        g.setFont (small (15.0f));
        g.drawFittedText (U8("Looking for Kaleidobox on this Wi-Fi… Open it on your Mac, on the same network as this iPhone."),
                          rowRect (0).reduced (6, 0).toNearestInt(), juce::Justification::centredLeft, 2);
    }
    for (int i = 0; i < (int) rows.size() && i < 3; ++i)
    {
        const auto& m = rows[(size_t) i];
        auto r = rowRect (i);
        if (m.name == picked)
        {
            g.setColour (col::amber.withAlpha (0.22f));
            g.fillRoundedRectangle (r, 14.0f);
        }
        const bool isLinked = linked && m.name == client.getMacName();
        drawDot (g, { r.getX() + 20, r.getCentreY() - 7 }, m.open ? col::green : col::ink, m.open);
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (19.0f), 0.05f));
        g.drawText (upper (m.name), juce::Rectangle<float> (r.getX() + 40, r.getY() + 5, r.getWidth() - 50, 24), juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (small (14.0f));
        juce::String sub = m.open ? (m.show.isNotEmpty() ? U8("Kaleidobox open · ") + m.show : juce::String ("Kaleidobox open")) : juce::String ("Kaleidobox not open");
        if (isLinked)
            sub = U8("Linked · ") + sub;
        else if (m.known && m.open)
            sub = U8("Linked before · ") + sub;
        g.drawText (sub, juce::Rectangle<float> (r.getX() + 40, r.getY() + 28, r.getWidth() - 50, 20), juce::Justification::centredLeft);
    }

    // what linking is for
    auto info = infoRect();
    g.setColour (col::well);
    g.fillRoundedRectangle (info, 14.0f);
    g.setColour (col::inkFaint);
    g.setFont (spaced (condensedBold (12.0f), 0.14f));
    g.drawText (linked ? "LINKED, THIS PHONE CAN" : "LINKED, THE PHONE CAN", info.withTrimmedLeft (14).withTrimmedTop (8).withHeight (18), juce::Justification::centredLeft);
    g.setColour (creamText);
    g.setFont (small (15.0f));
    g.drawFittedText (U8("Send sounds and kits straight to the Mac · run the Rig from the Remote page while you sing"),
                      info.reduced (14, 0).withTrimmedTop (28).withTrimmedBottom (6).toNearestInt(), juce::Justification::topLeft, 2);

    // NOT NOW / LINK, or UNLINK / REMOTE
    g.setColour (col::ink);
    g.setFont (spaced (condensed (18.0f), 0.1f));
    g.drawText (linked ? "UNLINK" : "NOT NOW", leftButton(), juce::Justification::centred);
    drawRaised (g, rightButton(), Look::amber, false, 14.0f);
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (19.0f), 0.1f));
    const auto right = linked ? juce::String ("REMOTE") : client.getStatus() == LinkClient::Status::connecting ? U8("LINKING…") : juce::String ("LINK");
    g.drawText (right, rightButton().withTrimmedBottom (3), juce::Justification::centred);
}

void MacLinkScreen::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (closeRect().contains (p))
        return onClose ? onClose() : void();
    const auto rows = client.macs();
    for (int i = 0; i < (int) rows.size() && i < 3; ++i)
        if (rowRect (i).contains (p))
        {
            picked = rows[(size_t) i].name;
            if (! rows[(size_t) i].open && onSay)
                onSay ("Open Kaleidobox on " + picked + " first, on this Wi-Fi.");
            if (! client.isLinked())
                entry.grabKeyboardFocus();
            return repaint();
        }
    if (leftButton().contains (p))
    {
        if (client.isLinked())
        {
            const auto name = client.getMacName();
            client.unlink();
            if (onSay)
                onSay ("Unlinked from " + name + ". Link again with new numbers from its Settings > iPhone.");
            return refresh();
        }
        return onClose ? onClose() : void();
    }
    if (rightButton().contains (p))
    {
        if (client.isLinked())
            return onRemote ? onRemote() : void();
        return pressLink();
    }
    if (! client.isLinked() && digitRect (0).getUnion (digitRect (3)).contains (p))
        entry.grabKeyboardFocus();
}

void MacLinkScreen::pressLink()
{
    if (client.getStatus() == LinkClient::Status::connecting)
        return;
    auto say = [this] (const juce::String& s) { if (onSay) onSay (s); };
    const auto rows = client.macs();
    bool open = false;
    for (auto& r : rows)
        open = open || (r.name == picked && r.open);
    if (rows.empty())
        return say ("No Mac with Kaleidobox open on this Wi-Fi yet. Open it on your Mac.");
    if (picked.isEmpty() || ! open)
        return say ("Pick a Mac with Kaleidobox open in the list.");
    if (entry.getText().length() != 4)
    {
        entry.grabKeyboardFocus();
        return say ("Type the four numbers from the Mac's Settings > iPhone.");
    }
    client.link (picked, entry.getText());
    entry.setText ({}, false);
    repaint();
}

//==============================================================================
SendToMacSheet::SendToMacSheet (LinkClient& c) : client (c) {}

void SendToMacSheet::show (MacSend s)
{
    what = std::move (s);
    dest = what.dests.contains (what.dest) ? what.dest : what.dests[0];
    sending = false;
    peaks.clear();
    if (! what.items.empty() && what.items.front().audio != nullptr)
    {
        const auto& a = *what.items.front().audio;
        const int n = 44, len = a.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const int from = len * i / n, to = juce::jmax (from + 1, len * (i + 1) / n);
            float m = 0;
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                m = juce::jmax (m, a.getMagnitude (ch, from, juce::jmin (to, len) - from));
            peaks.push_back (m);
        }
        const float top = juce::jmax (0.001f, *std::max_element (peaks.begin(), peaks.end()));
        for (auto& p : peaks)
            p /= top;
    }
    repaint();
}

juce::String SendToMacSheet::destTitle (const juce::String& d)
{
    if (d == "pad") return "ONTO AN MPC PAD";
    if (d == "sampler") return "INTO THE SAMPLER";
    if (d == "loop") return "ONTO A LOOP LAYER";
    if (d == "kit") return "ONTO THE MPC AS A KIT";
    return "INTO SOUNDS";
}

juce::String SendToMacSheet::destBlurb (const juce::String& d) const
{
    const bool many = what.items.size() > 1;
    if (d == "pad") return many ? "Each onto the next empty pad of the bank you play" : "The next empty pad of the bank you play";
    if (d == "sampler") return U8("Replaces the Sampler’s sound");
    if (d == "loop") return many ? U8("Each layer onto the next free layer · stretched to the Rig’s tempo")
                                 : U8("Stretched to the Rig’s tempo · next free layer");
    if (d == "kit") return "Every pad, from A01, as a new kit on the Mac's MPC";
    return U8("On the Mac’s sound bank, ready for any tool");
}

void SendToMacSheet::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth();
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-30), 24.0f);
    pocket::drawGrabber (g, getWidth());

    g.setColour (col::ink);
    g.setFont (fraunces (30.0f));
    g.drawText ("Send to Mac", juce::Rectangle<float> (18, 20, w - 140, 44), juce::Justification::centredLeft);
    g.setFont (spaced (condensed (16.0f), 0.12f));
    g.drawText ("CANCEL", cancelRect(), juce::Justification::centredRight);

    // the Mac it goes to
    const bool linked = client.isLinked();
    auto mac = macRect();
    drawDot (g, { mac.getX() + 8, mac.getCentreY() }, linked ? col::green : col::grey, linked);
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (19.0f), 0.05f));
    g.drawText (linked ? upper (client.getMacName()) : juce::String ("NOT LINKED"), mac.withTrimmedLeft (24), juce::Justification::centredLeft);
    if (linked)
    {
        g.setColour (col::inkDim);
        g.setFont (small (15.0f));
        g.drawText ("same Wi-Fi", mac, juce::Justification::centredRight);
    }
    else
    {
        drawRaised (g, linkRect(), Look::amber, false, 10.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (15.0f), 0.1f));
        g.drawText ("LINK", linkRect().withTrimmedBottom (3), juce::Justification::centred);
    }

    // what goes
    auto card = cardRect();
    g.setColour (col::well);
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (creamText);
    g.setFont (condensedBold (19.0f));
    const float titleW = juce::jmin (card.getWidth() * 0.45f, (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), what.title) + 4.0f);
    g.drawFittedText (what.title, card.withTrimmedLeft (14).withWidth (titleW).toNearestInt(), juce::Justification::centredLeft, 1);
    g.setColour (col::lcdAmber.withAlpha (0.85f));
    g.setFont (lcdFont (20.0f));
    g.drawText (what.detail, card.withTrimmedLeft (22 + titleW).withWidth (110), juce::Justification::centredLeft);
    auto wave = card.withTrimmedLeft (card.getWidth() - 128).reduced (8, 10);
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const float x = wave.getX() + wave.getWidth() * (float) i / (float) peaks.size();
        const float h = juce::jmax (1.5f, peaks[i] * wave.getHeight());
        g.setColour (col::amber);
        g.fillRect (x, wave.getCentreY() - h * 0.5f, 1.8f, h);
    }

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.14f));
    g.drawText ("WHERE IT GOES", juce::Rectangle<float> (18, 176, w - 36, 22), juce::Justification::centredLeft);

    for (int i = 0; i < what.dests.size(); ++i)
    {
        auto r = destRect (i);
        const auto d = what.dests[i];
        if (d == dest)
        {
            g.setColour (col::amber.withAlpha (0.22f));
            g.fillRoundedRectangle (r, 14.0f);
            drawDot (g, { r.getX() + 26, r.getY() + 23 }, col::amber, false);
        }
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (19.0f), 0.05f));
        g.drawText (destTitle (d), juce::Rectangle<float> (r.getX() + 52, r.getY() + 10, r.getWidth() - 60, 24), juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (small (14.5f));
        g.drawFittedText (destBlurb (d), juce::Rectangle<int> ((int) r.getX() + 52, (int) r.getY() + 34, (int) r.getWidth() - 60, 22), juce::Justification::centredLeft, 1);
    }

    drawRaised (g, sendRect(), Look::amber, sending, 14.0f);
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (21.0f), 0.12f));
    g.drawText (sending ? U8("SENDING…") : juce::String ("SEND"), sendRect().withTrimmedBottom (3), juce::Justification::centred);

    g.setColour (col::inkDim);
    g.setFont (small (14.5f));
    g.drawFittedText (U8("Not on the same Wi-Fi? Use ⇧ Share to AirDrop the WAV instead."), shareRect().toNearestInt(), juce::Justification::centred, 2);
}

void SendToMacSheet::mouseUp (const juce::MouseEvent& e)
{
    if (isDragging())
        return;   // the sheet is being flicked away, not tapped
    const auto p = e.position;
    if (cancelRect().contains (p))
        return onClose ? onClose() : void();
    if (! client.isLinked() && linkRect().contains (p))
        return onLink ? onLink() : void();
    for (int i = 0; i < what.dests.size(); ++i)
        if (destRect (i).contains (p))
        {
            dest = what.dests[i];
            return repaint();
        }
    if (sendRect().contains (p))
        return send();
    if (shareRect().contains (p))
        return share();
}

void SendToMacSheet::send()
{
    auto say = [this] (const juce::String& s) { if (onSay) onSay (s); };
    if (sending)
        return;
    if (! client.isLinked())
    {
        say ("Link to your Mac first: tap LINK");
        return;
    }
    sending = true;
    repaint();
    const bool ok = client.send (what, dest, [safe = juce::Component::SafePointer<SendToMacSheet> (this)] (bool fine, juce::String text)
    {
        if (safe == nullptr)
            return;
        safe->sending = false;
        if (safe->onSay)
            safe->onSay (text);
        if (fine && safe->onClose)
            safe->onClose();
        safe->repaint();
    });
    if (! ok)
    {
        sending = false;
        repaint();
        say ("Nothing to send");
    }
}

void SendToMacSheet::share()
{
    // The WAVs, from a temporary folder, through the iPhone's Share sheet (AirDrop, Files, Messages...).
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Send to Mac");
    folder.deleteRecursively();
    folder.createDirectory();
    juce::Array<juce::URL> files;
    for (auto& it : what.items)
        if (it.audio != nullptr)
        {
            auto f = folder.getNonexistentChildFile (juce::File::createLegalFileName (it.name.isNotEmpty() ? it.name : juce::String ("Sound")), ".wav", false);
            if (crate::AudioFiles::write (f, *it.audio))
                files.add (juce::URL (f));
        }
    if (files.isEmpty())
    {
        if (onSay)
            onSay ("Nothing to share");
        return;
    }
    sharing = juce::ContentSharer::shareFilesScoped (files, [safe = juce::Component::SafePointer<SendToMacSheet> (this)] (bool ok, const juce::String& error)
    {
        if (safe != nullptr && ! ok && error.isNotEmpty() && safe->onSay)
            safe->onSay ("Couldn't share: " + error);
    }, this);
}

//==============================================================================
RemoteScreen::RemoteScreen (LinkClient& c) : client (c) {}

const juce::StringArray& RemoteScreen::trackNames()
{
    static const juce::StringArray names { "MPC", "DRUMS", "BASS", "SYNTH", "VOCODER", "SAMPLER", "LOOPS", "LINE IN" };
    return names;
}

static juce::Colour trackColour (int i)
{
    static const juce::Colour c[] = { juce::Colour (0xffe8432f), juce::Colour (0xffe85a3a), juce::Colour (0xfff39b33), juce::Colour (0xffefcb45),
                                      juce::Colour (0xff5fb3c4), juce::Colour (0xffc58fd6), juce::Colour (0xff9cc27a), juce::Colour (0xffd8c49a) };
    return c[juce::jlimit (0, 7, i)];
}

juce::Rectangle<float> RemoteScreen::rectFor (int id) const
{
    const float w = (float) getWidth();
    switch (id)
    {
        case bpm:       return { 16, 10, 118, 76 };
        case play:      return { 16, 98, 84, 70 };
        case stop:      return { 106, 98, 80, 70 };
        case rec:       return { 192, 98, 80, 70 };
        case recWhat:   return { 278, 98, w - 294, 70 };
        case back:      return { w - 172, 176, 76, 34 };
        case next:      return { w - 88, 176, 72, 34 };
        case unmuteAll: return { w - 136, 346, 120, 26 };
        case recLayer:  return { 16, 582, w - 162, 58 };
        case undoLayer: return { w - 140, 582, 124, 58 };
        case linkButton: return { 60, 360, w - 120, 54 };
        default: break;
    }
    if (id >= sectionBase && id < sectionBase + 8)
    {
        const int i = id - sectionBase;
        const float cw = (w - 32 - 18) / 4.0f;
        return { 16 + (float) (i % 4) * (cw + 6), 218 + (float) (i / 4) * 64, cw, 58 };
    }
    if (id >= trackBase && id < trackBase + 8)
    {
        const int i = id - trackBase;
        const float cw = (w - 32 - 10) / 2.0f;
        return { 16 + (float) (i % 2) * (cw + 10), 378 + (float) (i / 2) * 50, cw, 44 };
    }
    return {};
}

int RemoteScreen::idAt (juce::Point<float> p) const
{
    if (! client.isLinked())
        return rectFor (linkButton).contains (p) ? linkButton : none;
    for (int id : { bpm, play, stop, rec, recWhat, back, next, unmuteAll, recLayer, undoLayer })
        if (rectFor (id).contains (p))
            return id;
    for (int i = 0; i < 8; ++i)
    {
        if (rectFor (sectionBase + i).contains (p)) return sectionBase + i;
        if (rectFor (trackBase + i).contains (p)) return trackBase + i;
    }
    return none;
}

void RemoteScreen::paint (juce::Graphics& g)
{
    g.fillAll (darkBg);
    const auto& s = client.state();
    const bool linked = client.isLinked();
    const auto w = (float) getWidth();
    const bool playing = (bool) s.get ("playing", false);

    // tempo
    auto b = rectFor (bpm);
    drawWell (g, b, 12.0f);
    g.setColour (col::lcdAmber);
    g.setFont (lcdFont (46.0f));
    g.drawText (juce::String ((double) s.get ("bpm", 90.0), 1), b.withTrimmedBottom (22), juce::Justification::centred);
    g.setFont (spaced (condensed (12.0f), 0.18f));
    g.drawText (U8("BPM · TAP"), b.withTrimmedTop (b.getHeight() - 26), juce::Justification::centred);

    // where the song is
    auto lcd = juce::Rectangle<float> (142, 10, w - 158, 76);
    drawWell (g, lcd, 12.0f);
    const auto sections = s.get ("sections");
    const int nSections = sections.isArray() ? sections.size() : 0;
    const int at = (int) s.get ("section", -1);
    juce::String top, bottom;
    if (! linked)
        top = "NOT LINKED", bottom = "LINK IN MORE > MAC LINK";
    else if (nSections == 0)
    {
        top = playing ? "JAM" : "STOPPED";
        bottom = "BAR " + juce::String ((double) s.get ("bar", 0.0) + 1.0, 1) + U8(" · NO SECTIONS YET");
    }
    else if (juce::isPositiveAndBelow (at, nSections))
    {
        top = juce::String::charToString ((juce::juce_wchar) ('A' + at)) + U8(" · ") + upper (sections[at].toString());
        bottom = "BAR " + juce::String ((double) s.get ("barIn", 1.0), 1) + " / " + juce::String ((int) s.get ("sectionBars", 0));
        const int nx = (int) s.get ("next", -1);
        if (nx >= 0)
            bottom << U8(" · NEXT ") << juce::String::charToString ((juce::juce_wchar) ('A' + nx));
    }
    else
    {
        top = playing ? "JAM" : "STOPPED";
        bottom = "BAR " + juce::String ((double) s.get ("bar", 0.0) + 1.0, 1) + U8(" · TAP A SECTION");
    }
    g.setColour (lcdGreen);
    g.setFont (lcdFont (34.0f));
    g.drawFittedText (top, lcd.reduced (14, 0).withTrimmedTop (8).withHeight (34).toNearestInt(), juce::Justification::centredLeft, 1);
    g.setColour (col::lcdAmber);
    g.setFont (spaced (condensed (15.0f), 0.1f));
    g.drawFittedText (bottom, lcd.reduced (14, 0).withTrimmedTop (44).withHeight (22).toNearestInt(), juce::Justification::centredLeft, 1);

    // transport
    const auto recs = s.get ("rec");
    const int recNow = recs.getDynamicObject() != nullptr ? (int) recs[juce::Identifier (recTarget)] : 0;
    auto key = [&] (int id, juce::Colour topC, juce::Colour botC)
    {
        darkKeyShape (g, rectFor (id), topC, botC, pressed == id);
    };
    key (play, playing ? juce::Colour (0xff7be89a) : darkKeyTop, playing ? juce::Colour (0xff3fbf68) : darkKey);
    key (stop, darkKeyTop, darkKey);
    key (rec, recNow == 2 ? juce::Colour (0xffef6a55) : darkKeyTop, recNow == 2 ? juce::Colour (0xffc4321f) : darkKey);
    key (recWhat, darkKeyTop, darkKey);
    {
        auto r = rectFor (play).withTrimmedBottom (3);
        juce::Path tri;
        tri.addTriangle (r.getCentreX() - 6, r.getY() + 14, r.getCentreX() - 6, r.getY() + 32, r.getCentreX() + 9, r.getY() + 23);
        g.setColour (playing ? col::ink : creamText);
        g.fillPath (tri);
        g.setFont (spaced (condensedBold (18.0f), 0.1f));
        g.drawText ("PLAY", r.withTrimmedTop (36), juce::Justification::centredTop);

        r = rectFor (stop).withTrimmedBottom (3);
        g.setColour (creamText);
        g.fillRoundedRectangle (r.getCentreX() - 8, r.getY() + 15, 16, 16, 2.0f);
        g.drawText ("STOP", r.withTrimmedTop (36), juce::Justification::centredTop);

        r = rectFor (rec).withTrimmedBottom (3);
        g.setColour (recNow == 2 ? creamText : col::red);
        g.fillEllipse (r.getCentreX() - 9, r.getY() + 14, 18, 18);
        if (recNow == 1)
        {
            g.setColour (col::red);
            g.drawEllipse (r.getCentreX() - 13, r.getY() + 10, 26, 26, 2.0f);
        }
        g.setColour (creamText);
        g.drawText (recNow == 2 ? "STOP" : recNow == 1 ? "ARMED" : "REC", r.withTrimmedTop (36), juce::Justification::centredTop);

        r = rectFor (recWhat).withTrimmedBottom (3);
        g.setColour (dimText);
        g.setFont (spaced (condensedBold (11.0f), 0.12f));
        g.drawText ("REC RECORDS", r.withTrimmedTop (12).withHeight (16), juce::Justification::centred);
        g.setColour (creamText);
        g.setFont (spaced (condensedBold (17.0f), 0.06f));
        const juce::String what = recTarget == "take" ? "A TAKE" : recTarget == "moves" ? "MOVES" : "THE SET";
        g.drawText (what + U8(" ▾"), r.withTrimmedTop (32).withHeight (24), juce::Justification::centred);
    }

    // sections
    g.setColour (dimText);
    g.setFont (spaced (condensedBold (12.5f), 0.14f));
    g.drawText (U8("SECTIONS · TAP TO GO NEXT"), juce::Rectangle<float> (18, 176, 200, 34), juce::Justification::centredLeft);
    for (int id : { (int) back, (int) next })
    {
        darkKeyShape (g, rectFor (id), darkKeyTop, darkKey, pressed == id, 8.0f);
        g.setColour (creamText);
        g.setFont (spaced (condensedBold (14.0f), 0.1f));
        g.drawText (id == back ? U8("‹ BACK") : U8("NEXT ›"), rectFor (id).withTrimmedBottom (3), juce::Justification::centred);
    }
    const int nextIndex = (int) s.get ("next", -1);
    for (int i = 0; i < 8; ++i)
    {
        auto r = rectFor (sectionBase + i);
        const bool exists = i < nSections;
        const bool current = exists && i == at;
        const bool upNext = exists && i == nextIndex && playing;
        if (current)
            darkKeyShape (g, r, juce::Colour (0xff7be89a), juce::Colour (0xff3fbf68), pressed == sectionBase + i);
        else if (upNext)
            darkKeyShape (g, r, juce::Colour (0xfff9c768), col::amberDark, pressed == sectionBase + i);
        else
            darkKeyShape (g, r, exists ? darkKeyTop : juce::Colour (0xff2c2621), exists ? darkKey : juce::Colour (0xff26211d), pressed == sectionBase + i);
        const auto ink = current || upNext ? col::ink : exists ? creamText : juce::Colour (0xff5d544a);
        g.setColour (ink);
        g.setFont (condensedBold (24.0f));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)), r.withTrimmedLeft (12).withTrimmedTop (4).withHeight (28), juce::Justification::centredLeft);
        g.setFont (spaced (condensedBold (13.0f), 0.06f));
        g.drawFittedText (exists ? upper (sections[i].toString()) : juce::String ("EMPTY"), r.withTrimmedLeft (12).withTrimmedRight (6).withTrimmedTop (32).withHeight (18).toNearestInt(),
                          juce::Justification::centredLeft, 1);
    }

    // tracks
    g.setColour (dimText);
    g.setFont (spaced (condensedBold (12.5f), 0.14f));
    g.drawText (U8("TRACKS · TAP TO MUTE"), juce::Rectangle<float> (18, 346, 200, 26), juce::Justification::centredLeft);
    g.setColour (creamText.withAlpha (0.85f));
    g.setFont (spaced (condensedBold (14.0f), 0.1f));
    g.drawText ("UNMUTE ALL", rectFor (unmuteAll), juce::Justification::centredRight);
    const auto mutes = s.get ("mutes");
    for (int i = 0; i < 8; ++i)
    {
        auto r = rectFor (trackBase + i);
        const bool muted = mutes.isArray() && i < mutes.size() && (bool) mutes[i];
        if (muted)
            darkKeyShape (g, r, juce::Colour (0xff6cc0f0), juce::Colour (0xff2f86c4), pressed == trackBase + i, 9.0f);
        else
            darkKeyShape (g, r, darkKeyTop, darkKey, pressed == trackBase + i, 9.0f);
        g.setColour (trackColour (i));
        g.fillRoundedRectangle (r.getX() + 12, r.getY() + 10, 6, r.getHeight() - 23, 3.0f);
        g.setColour (muted ? col::ink : creamText);
        g.setFont (spaced (condensedBold (17.0f), 0.08f));
        g.drawText (trackNames()[i], r.withTrimmedLeft (28).withTrimmedBottom (3), juce::Justification::centredLeft);
        if (muted)
            g.drawText ("M", r.withTrimmedRight (14).withTrimmedBottom (3), juce::Justification::centredRight);
    }

    // loops
    const int layer = (int) s.get ("remoteLayer", -1);
    const auto layers = s.get ("layers");
    const int layerState = layers.isArray() && juce::isPositiveAndBelow (layer, layers.size()) ? (int) layers[layer] : 0;
    juce::String recLayerText = U8("● REC NEXT LOOP LAYER");
    if (layerState == 1)
        recLayerText = U8("● LAYER ") + juce::String (layer + 1) + U8(" · NEXT BAR");
    else if (layerState == 2)
        recLayerText = U8("■ CLOSE LAYER ") + juce::String (layer + 1);
    darkKeyShape (g, rectFor (recLayer), layerState == 2 ? juce::Colour (0xffef6a55) : juce::Colour (0xfff9c768),
                  layerState == 2 ? juce::Colour (0xffc4321f) : col::amberDark, pressed == recLayer, 12.0f);
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (17.0f), 0.08f));
    g.drawFittedText (recLayerText, rectFor (recLayer).withTrimmedBottom (3).toNearestInt(), juce::Justification::centred, 1);
    darkKeyShape (g, rectFor (undoLayer), darkKeyTop, darkKey, pressed == undoLayer, 12.0f);
    g.setColour (creamText);
    g.drawText ("UNDO LAYER", rectFor (undoLayer).withTrimmedBottom (3), juce::Justification::centred);

    if (! linked)
    {
        g.setColour (darkBg.withAlpha (0.82f));
        g.fillRect (getLocalBounds().withTrimmedTop (96));
        auto card = juce::Rectangle<float> (30, 230, w - 60, 200);
        g.setColour (juce::Colour (0xff2b2520));
        g.fillRoundedRectangle (card, 18.0f);
        g.setColour (creamText);
        g.setFont (fraunces (26.0f));
        g.drawText ("Run your Mac from here", card.withTrimmedTop (20).withHeight (36), juce::Justification::centred);
        g.setColour (dimText);
        g.setFont (small (15.0f));
        g.drawFittedText ("Link this iPhone to Kaleidobox on your Mac (same Wi-Fi) to play, change sections, mute tracks and record loops while you sing.",
                          card.reduced (22, 0).withTrimmedTop (60).withHeight (64).toNearestInt(), juce::Justification::centredTop, 3);
        darkKeyShape (g, rectFor (linkButton), juce::Colour (0xfff9c768), col::amberDark, pressed == linkButton, 12.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (18.0f), 0.1f));
        g.drawText ("LINK TO MAC", rectFor (linkButton).withTrimmedBottom (3), juce::Justification::centred);
    }
}

void RemoteScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    dragged = false;
    dragStartY = e.position.y;
    dragStartBpm = (double) client.state().get ("bpm", 90.0);
    repaint();
}

void RemoteScreen::mouseDrag (const juce::MouseEvent& e)
{
    // Drag the tempo up or down: half a BPM every 6 points.
    if (pressed != bpm)
        return;
    const float dy = dragStartY - e.position.y;
    if (std::abs (dy) < 6 && ! dragged)
        return;
    dragged = true;
    const double v = juce::jlimit (40.0, 240.0, dragStartBpm + std::round (dy / 6.0f) * 0.5);
    if (std::abs (v - (double) client.state().get ("bpm", 90.0)) > 0.01)
        client.remote ("bpm", v);
}

void RemoteScreen::mouseUp (const juce::MouseEvent& e)
{
    const int id = idAt (e.position);
    const int was = std::exchange (pressed, none);
    repaint();
    if (id == was && id != none && ! (id == bpm && dragged))
        press (id);
}

void RemoteScreen::press (int id)
{
    if (id == linkButton)
        return onLink ? onLink() : void();
    if (id == bpm)
    {
        // Tap tempo: the beat between the last few taps.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0)
            taps.clear();
        taps.push_back (now);
        if (taps.size() > 5)
            taps.erase (taps.begin());
        if (taps.size() >= 2)
        {
            const double beat = (taps.back() - taps.front()) / (double) (taps.size() - 1);
            client.remote ("bpm", juce::jlimit (40.0, 240.0, std::round (60000.0 / beat * 2.0) / 2.0));
        }
        else if (onSay)
            onSay ("Keep tapping on the beat. Drag up or down to nudge the tempo.");
        return;
    }
    if (id == play) return client.remote ("play");
    if (id == stop) return client.remote ("stop");
    if (id == rec) return client.remote ("rec", recTarget);
    if (id == back) return client.remote ("back");
    if (id == next) return client.remote ("next");
    if (id == unmuteAll) return client.remote ("unmuteAll");
    if (id == recLayer) return client.remote ("recLayer");
    if (id == undoLayer) return client.remote ("undoLayer");
    if (id >= sectionBase && id < sectionBase + 8)
    {
        const auto sections = client.state().get ("sections");
        const int i = id - sectionBase;
        if (! sections.isArray() || i >= sections.size())
        {
            if (onSay)
                onSay ("Section " + juce::String::charToString ((juce::juce_wchar) ('A' + i)) + " is empty. Add sections on the Mac's Song screen.");
            return;
        }
        return client.remote ("section", i);
    }
    if (id >= trackBase && id < trackBase + 8)
        return client.remote ("mute", id - trackBase);
    if (id == recWhat)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Rec on the Mac records");
        menu.addItem (1, "The set (every track, to WAVs)", true, recTarget == "set");
        menu.addItem (2, "A take (the picked track, onto a loop)", true, recTarget == "take");
        menu.addItem (3, "Knob moves (automation)", true, recTarget == "moves");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (rectFor (recWhat).toNearestInt())),
                            [safe = juce::Component::SafePointer<RemoteScreen> (this)] (int r)
                            {
                                if (safe == nullptr || r == 0)
                                    return;
                                safe->recTarget = r == 2 ? "take" : r == 3 ? "moves" : "set";
                                safe->repaint();
                            });
    }
}
