#pragma once

// MPC style NOTE REPEAT as a button you hold: down = repeat on, up = off. Press it twice quickly (a
// double tap) and it latches on; the next press unlatches it. A small message-thread state machine with
// no JUCE in it, so the Rig's screens, the Mac MPC and the phone all behave the same way.
namespace noterepeat
{
constexpr double kDoubleTapMs = 300.0;

class Hold
{
public:
    // `nowMs`: any steady clock in milliseconds. Each returns whether repeat is on afterwards.
    bool press (double nowMs)
    {
        if (latched)
        {
            // A single press while latched turns it off (and the release that follows does nothing).
            latched = down = false;
            lastPress = -1.0e12;
            return false;
        }
        const bool second = nowMs - lastPress <= kDoubleTapMs;
        down = true;
        if (second)
        {
            latched = true;
            lastPress = -1.0e12;   // (a third quick press is a new first tap)
        }
        else
            lastPress = nowMs;
        return true;
    }

    bool release()
    {
        down = false;
        return latched;
    }

    bool isOn() const { return down || latched; }
    bool isHeld() const { return down; }
    bool isLatched() const { return latched; }

    // Set from code (a menu, a test): latched on, or everything off.
    void latch (bool on)
    {
        down = false;
        latched = on;
        lastPress = -1.0e12;
    }

private:
    bool down = false, latched = false;
    double lastPress = -1.0e12;
};

// The rates the buttons offer, in the order they step: 1/4 to 1/64.
struct Rate
{
    const char* name;
    double beats;   // a quarter note is 1 beat
};
constexpr int kNumRates = 8;
constexpr int kDefaultRate = 3;   // 1/16
constexpr Rate kRates[kNumRates] = {
    { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 },
    { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/32T", 1.0 / 12.0 }, { "1/64", 0.0625 } };

// Screens list the chips in this order (the common ones first).
constexpr int kChipOrder[kNumRates] = { 1, 2, 3, 4, 5, 6, 0, 7 };
}
