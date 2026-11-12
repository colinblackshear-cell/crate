#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

// CrateTests runs every test; CrateTests "Some test name" runs just the tests with that name.
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    if (argc > 1)
    {
        juce::Array<juce::UnitTest*> picked;
        for (auto* t : juce::UnitTest::getAllTests())
            for (int a = 1; a < argc; ++a)
                if (t->getName() == juce::String (argv[a]))
                    picked.add (t);
        runner.runTests (picked);
    }
    else
        runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "All tests passed" : juce::String (failures) + " failure(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
