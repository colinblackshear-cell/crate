#pragma once

#include "RigEngine.h"

// The standalone app's "Last show" (saved on quit, opened at launch). A save that fails on quit
// is never silent: the app asks before quitting (see Main.cpp), and if it quits anyway (or the
// save ran where it couldn't ask), the reason is kept in the settings and shown at the next
// launch. The previous Last show is left as it was either way (RigShowFiles).
namespace riglastshow
{
static constexpr const char* kFailedKey = "lastShowSaveFailed";

inline rigshow::Result save (RigEngine& engine, const juce::File& file, juce::PropertiesFile& settings)
{
    const auto result = engine.saveShowFile (file);
    if (result.ok)
        settings.removeValue (kFailedKey);
    else
        settings.setValue (kFailedKey, result.error.isNotEmpty() ? result.error : juce::String ("The save failed."));
    settings.saveIfNeeded();
    return result;
}

inline juce::String quitMessage (const rigshow::Result& result)
{
    return "Kaleidobox couldn't save your show for next time: " + result.error
         + "\n\nThe Last show from before is still there, so quitting now loses only the changes since then. "
           "Keep Kaleidobox open to save your show somewhere else first (Save As).";
}

// What to tell the user at launch about a Last show that wasn't saved at the last quit (empty
// when it was). Forgets it.
inline juce::String takeFailureFromLastQuit (juce::PropertiesFile& settings)
{
    const auto why = settings.getValue (kFailedKey);
    if (why.isEmpty())
        return {};
    settings.removeValue (kFailedKey);
    settings.saveIfNeeded();
    return "Your show wasn't saved when Kaleidobox last quit (" + why + "). This is the Last show from before that.";
}
} // namespace riglastshow
