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

const char* routeName (Route);

// Which mic records. Picked again after every route change: a wired headset's own mic when it has one
// (EarPods, a USB-C headset, or a USB interface's line in), otherwise the phone's built-in mic. A
// Bluetooth mic is never picked: on AirPods it would drop the whole sound to phone-call quality, so
// the beat stays in full quality in your ears and the phone's mic records you. Returns true when it
// changed the input. Message thread; does nothing on a computer.
bool pickMic();
// The mic now recording was heard giving nothing but silence: stop picking it (until it's unplugged)
// and use the phone's built-in mic instead. Returns true when it moved to the built-in mic.
bool avoidSilentMic();
bool micIsBuiltIn();     // true on a computer
const char* micName();   // "IPHONE MIC", "HEADSET MIC", "BLUETOOTH MIC", "USB INPUT", "NO MIC"   // "WIRED HEADPHONES", "AIRPODS / BLUETOOTH", "NO HEADPHONES"
}
