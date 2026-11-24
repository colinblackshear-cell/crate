#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

int runOverdubUndoBench (int argc, char** argv);   // OverdubUndoBench.cpp

// CrateTests runs every test; CrateTests "Some test name" runs just the tests with that name.
// CrateTests --overdub-undo-bench ... runs the overdub/undo benchmark instead (see its file).
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc > 1 && juce::String (argv[1]) == "--overdub-undo-bench")
        return runOverdubUndoBench (argc, argv);

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

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }
    std::cout << runner.getNumResults() << " test sections, " << passes << " checks passed, " << failures << " failed" << std::endl;

    std::cout << (failures == 0 ? "All tests passed" : juce::String (failures) + " failure(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
