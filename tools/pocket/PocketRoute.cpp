#include "PocketRoute.h"

#include <juce_core/juce_core.h>

namespace pocket
{
bool headphonesConnected()
{
    return juce::SystemStats::getEnvironmentVariable ("POCKET_NO_HEADPHONES", {}).isEmpty();
}
}
