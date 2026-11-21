#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

// Macros: eight big knobs for the show, each turning many settings at once (one knob that
// opens the bass's filter, adds the delay and thins the drums, for a build).
//
// Assign: press Assign on a macro, then move any settings on any tool or its effects from
// where they should start to where they should end up. Each one you move joins the macro:
// at 0 it's where you started, at 1 where you left it, and in between, in between. Press
// Assign again (or Done) to finish. A setting can be on several macros.
//
// Settings are named the way knob Learn and automation name them: a tool (RigEngine::ToolId)
// and the index in its parameter list. The values are the settings' own 0..1.
//
// Threads: value is any thread; process() is the audio thread (it only try-locks); the rest is
// the message thread. Turning a macro while Rec moves records what it moves, like any knob.
class RigMacros
{
public:
    static constexpr int kMacros = 8, kTargets = 12;

    struct Target
    {
        int tool = 0, parameter = 0;
        juce::String paramId;   // so a saved show finds it again
        float from = 0, to = 1;
    };

    std::function<juce::AudioProcessorParameter* (int tool, int parameter)> getParameter;
    std::function<int (int tool)> numParameters;
    std::function<juce::String (int tool)> toolName;

    RigMacros();

    // Where each macro is, 0..1. Any thread (the screen, a knob).
    std::array<std::atomic<float>, kMacros> value;

    juce::String getName (int macro) const;      // "Macro 1" until named
    void setName (int macro, const juce::String&);
    std::vector<Target> getTargets (int macro) const;
    int getNumTargets (int macro) const;
    juce::String targetName (const Target&) const;   // "Bass Cutoff"
    // Adds (or, for a setting it has, changes) a target. False when it's full.
    bool setTarget (int macro, Target);
    void removeTarget (int macro, int index);
    void clear (int macro);
    void clearAll();
    int getVersion() const { return version.load(); }

    // Assigning: which macro is listening for settings to move (-1 none). While it is, the
    // engine's knob-touch callbacks report each setting's start and end here.
    void startAssigning (int macro);
    void stopAssigning();
    int getAssigning() const { return assigning; }
    // A setting was touched (its value as the touch began) / let go of (its value now).
    void touchBegan (int tool, int parameter, float valueNow);
    // Returns true if it joined the macro being assigned.
    bool touchEnded (int tool, int parameter, float valueNow);

    // Shows.
    static constexpr const char* kTreeType = "Macros";
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);

    // Audio thread, before automation: moves the settings of any macro that's been turned.
    void process();

private:
    struct Macro
    {
        juce::String name;
        std::vector<Target> targets;
        std::vector<juce::AudioProcessorParameter*> params;   // resolved
    };
    void resolve (Macro&);

    mutable juce::SpinLock lock;
    std::array<Macro, kMacros> macros;
    std::array<float, kMacros> applied {};
    std::atomic<int> version { 0 };
    int assigning = -1;
    // Touches while assigning: (tool, parameter) -> value as the touch began.
    struct Touch
    {
        int tool, parameter;
        float began;
    };
    std::vector<Touch> touches;
};
