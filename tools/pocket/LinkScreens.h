#pragma once

#include "PocketLinkClient.h"
#include "PocketTheme.h"

// The three Mac screens of Colin's design: Link to your Mac (More > Mac link), the Send to Mac sheet
// (every TO MAC button), and the Remote (More > Remote), which runs Kaleidobox on the Mac.

// Link to your Mac: the four numbers from the Mac's Settings > iPhone, and the Macs on this Wi-Fi.
class MacLinkScreen : public juce::Component
{
public:
    explicit MacLinkScreen (pocket::LinkClient&);

    std::function<void()> onClose, onRemote;
    std::function<void (const juce::String&)> onSay;

    void refresh();          // the client changed
    void shown();            // opened: picks a Mac, clears the numbers
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // Screenshot mode: as if these numbers were typed.
    void setDemoCode (const juce::String& c) { entry.setText (c, false); repaint(); }

private:
    juce::Rectangle<float> closeRect() const { return { (float) getWidth() - 96, 0, 84, 40 }; }
    juce::Rectangle<float> digitRect (int i) const { return { 43.0f + (float) i * 80.0f, 214, 64, 82 }; }
    juce::Rectangle<float> rowRect (int i) const { return { 16, 344 + (float) i * 58, (float) getWidth() - 32, 54 }; }
    juce::Rectangle<float> infoRect() const { return { 16, 524, (float) getWidth() - 32, 68 }; }
    juce::Rectangle<float> leftButton() const { return { 16, 602, 160, 50 }; }
    juce::Rectangle<float> rightButton() const { return { 186, 602, (float) getWidth() - 202, 50 }; }
    void pressLink();

    pocket::LinkClient& client;
    juce::TextEditor entry;
    juce::String picked;
};

// Send to Mac: where a sound (or loops, or a kit) goes on the Mac.
class SendToMacSheet : public juce::Component
{
public:
    explicit SendToMacSheet (pocket::LinkClient&);

    void show (pocket::MacSend);
    std::function<void()> onClose, onLink;
    std::function<void (const juce::String&)> onSay;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh() { repaint(); }

private:
    juce::Rectangle<float> cancelRect() const { return { (float) getWidth() - 110, 22, 96, 40 }; }
    juce::Rectangle<float> macRect() const { return { 16, 72, (float) getWidth() - 32, 36 }; }
    juce::Rectangle<float> linkRect() const { return { (float) getWidth() - 96, 72, 80, 36 }; }
    juce::Rectangle<float> cardRect() const { return { 16, 114, (float) getWidth() - 32, 52 }; }
    juce::Rectangle<float> destRect (int i) const { return { 16, 204 + (float) i * 72, (float) getWidth() - 32, 66 }; }
    juce::Rectangle<float> sendRect() const { return { 16, 204 + (float) juce::jmax (2, what.dests.size()) * 72 + 8, (float) getWidth() - 32, 58 }; }
    juce::Rectangle<float> shareRect() const { auto s = sendRect(); return { 16, s.getBottom() + 6, (float) getWidth() - 32, 48 }; }
    void send();
    void share();
    static juce::String destTitle (const juce::String&);
    juce::String destBlurb (const juce::String&) const;

    pocket::LinkClient& client;
    pocket::MacSend what;
    juce::String dest;
    std::vector<float> peaks;
    bool sending = false;
    juce::ScopedMessageBox sharing;
};

// The Remote: Kaleidobox on the Mac from the phone, so you can run the set while you sing.
class RemoteScreen : public juce::Component
{
public:
    explicit RemoteScreen (pocket::LinkClient&);

    std::function<void()> onLink;
    std::function<void (const juce::String&)> onSay;

    void refresh() { repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // The tracks, in the Mac's remote order, and their colours.
    static const juce::StringArray& trackNames();

private:
    enum Id { none, bpm, play, stop, rec, recWhat, back, next, unmuteAll, recLayer, undoLayer, linkButton, sectionBase = 100, trackBase = 200 };
    int idAt (juce::Point<float>) const;
    void press (int id);
    juce::Rectangle<float> rectFor (int id) const;
    juce::String recTarget = "set";
    int pressed = none;
    double dragStartBpm = 0;
    float dragStartY = 0;
    bool dragged = false;
    std::vector<double> taps;

    pocket::LinkClient& client;
};
