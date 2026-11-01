#include "MpcProcessor.h"

// Called by the plugin wrappers to make the plugin. Kept apart from MpcProcessor.cpp so the
// tests can build the MPC tool alongside the other tools.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MpcProcessor();
}
