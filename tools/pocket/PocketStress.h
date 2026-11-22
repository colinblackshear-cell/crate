#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

class PocketApp;

// POCKET_STRESS (desktop preview): the app's renderer on a real-time thread while a script taps the real UI, with a
// watchdog for a stuck message thread. See PocketStress.cpp. Keep the returned object alive for the run; `done`
// gets true when it finished cleanly (a stall quits the process with exit code 2 instead).
std::shared_ptr<void> startPocketStress (PocketApp&, const juce::File& folder, int randomSeconds, std::function<void (bool ok)> done);
