#pragma once

#include <functional>

namespace pocket
{
// Where the sound goes, as the phone's audio session reports it.
//   wired:     headphones on a cable, Lightning or USB-C (also USB audio and line out): no delay to speak of
//   bluetooth: AirPods and other Bluetooth (A2DP, hands-free, LE): it hears you late
//   none:      the phone's own speaker or earpiece (or AirPlay): the mic would hear the speaker
enum class Route { none, wired, bluetooth };

// Message thread. On a computer it says wired; POCKET_ROUTE=bluetooth|none (or POCKET_NO_HEADPHONES=1)
// shows the others, and setDemoRoute overrides it for screenshots.
Route currentRoute();
void setDemoRoute (int route);   // -1 = back to the real one, else (int) Route
inline bool headphonesConnected() { return currentRoute() != Route::none; }

// Calls `changed` on the message thread whenever the route changes (headphones in or out, AirPods
// connected). Call once; a second call replaces the first.
void watchRoute (std::function<void()> changed);

// The audio session's input + output latency right now, in samples at `sampleRate` (Bluetooth adds a
// lot), or -1 when it can't be asked (a computer).
int routeLatencySamples (double sampleRate);

const char* routeName (Route);   // "WIRED HEADPHONES", "AIRPODS / BLUETOOTH", "NO HEADPHONES"
}
