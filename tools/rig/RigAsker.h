#pragma once

#include <juce_core/juce_core.h>

#include <functional>

// How the standalone app's document flow (RigDocFlow) talks to you: a question with Save /
// Don't Save / Cancel, the Save As and Open file windows, the folder window, and a problem to
// show. Every one answers later, through its callback, on the message thread (or never: a
// window closed with the app). The app's one is RigView's (juce::AlertWindow and
// juce::FileChooser); the tests' answers when they like, and can change the show first.
namespace rigdirty
{
enum class Choice { save, dontSave, cancel };

struct Question
{
    juce::String title, message;
    juce::String saveLabel = "Save", dontSaveLabel = "Don't save", cancelLabel = "Cancel";
};

class Asker
{
public:
    virtual ~Asker() = default;
    virtual void ask (const Question&, std::function<void (Choice)>) = 0;
    // Save As: where to save (`suggested` is the name it starts with). juce::File() if you
    // cancelled. It warns before replacing a file that's there.
    virtual void chooseSaveFile (const juce::File& suggested, std::function<void (const juce::File&)>) = 0;
    // A show to open (from `folder`); juce::File() if you cancelled.
    virtual void chooseOpenFile (const juce::File& folder, std::function<void (const juce::File&)>) = 0;
    // A folder (Import a song from Logic); juce::File() if you cancelled.
    virtual void chooseFolder (const juce::String& title, const juce::File& start, std::function<void (const juce::File&)>) = 0;
    // A problem to show (a save that failed), with nothing to answer.
    virtual void showError (const juce::String& title, const juce::String& message) = 0;
};
} // namespace rigdirty
