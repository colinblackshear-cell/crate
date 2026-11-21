#include "PocketRoute.h"

#include <juce_core/juce_core.h>

// The desktop preview: there is no audio session, so the route comes from the environment.
namespace pocket
{
namespace
{
int demoRoute = -1;
}

Route currentRoute()
{
    if (demoRoute >= 0)
        return (Route) juce::jlimit (0, 2, demoRoute);
    if (juce::SystemStats::getEnvironmentVariable ("POCKET_NO_HEADPHONES", {}).isNotEmpty())
        return Route::none;
    const auto r = juce::SystemStats::getEnvironmentVariable ("POCKET_ROUTE", {}).toLowerCase();
    if (r == "bluetooth")
        return Route::bluetooth;
    if (r == "none")
        return Route::none;
    return Route::wired;
}

void setDemoRoute (int route) { demoRoute = route; }

void watchRoute (std::function<void()>) {}

int routeLatencySamples (double) { return -1; }

const char* routeName (Route r)
{
    switch (r)
    {
        case Route::wired: return "WIRED HEADPHONES";
        case Route::bluetooth: return "AIRPODS / BLUETOOTH";
        default: return "NO HEADPHONES";
    }
}
}
