#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "All tests passed" : juce::String (failures) + " failure(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
