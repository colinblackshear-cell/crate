#include "EditDocument.h"

void EditDocument::load (crate::SharedAudio audio, const juce::String& newName, const juce::String& actionName,
                         const juce::File& file, bool isSaved)
{
    if (index < 0)
    {
        steps.clear();
        savedIndex = -2;
    }

    const auto before = getSelection();
    steps.resize ((size_t) (index + 1));
    if (savedIndex > index)
        savedIndex = -2;

    steps.push_back ({ std::move (audio), actionName, {}, before, newName, file });
    index = (int) steps.size() - 1;
    if (isSaved)
        savedIndex = index;
    lastWasUndo = false;
    trim();
    sendChangeMessage();
}

void EditDocument::close()
{
    steps.clear();
    index = savedIndex = -1;
    lastWasUndo = false;
    sendChangeMessage();
}

void EditDocument::apply (crate::Edits::Result result, const juce::String& actionName, juce::Range<int> selectionBefore)
{
    // a redo branch is lost once you make a new edit
    steps.resize ((size_t) (index + 1));
    if (savedIndex > index)
        savedIndex = -2;   // the saved version can no longer be reached

    steps.push_back ({ crate::share (std::move (result.audio)), actionName, result.selection, selectionBefore,
                       getName(), getSourceFile() });
    index = (int) steps.size() - 1;
    lastWasUndo = false;
    trim();
    sendChangeMessage();
}

void EditDocument::trim()
{
    auto bytes = [this]
    {
        size_t total = 0;
        for (const auto& s : steps)
            if (s.audio != nullptr)
                total += (size_t) s.audio->getNumSamples() * (size_t) s.audio->getNumChannels() * sizeof (float);
        return total;
    };

    // always keep the current step and the one before it
    while (steps.size() > 2 && ((int) steps.size() > kMaxSteps || bytes() > kMaxBytes))
    {
        steps.erase (steps.begin());
        --index;
        --savedIndex;
        if (savedIndex < 0)
            savedIndex = -2;
    }
}

bool EditDocument::undo()
{
    if (! canUndo())
        return false;
    --index;
    lastWasUndo = true;
    sendChangeMessage();
    return true;
}

bool EditDocument::redo()
{
    if (! canRedo())
        return false;
    ++index;
    lastWasUndo = false;
    sendChangeMessage();
    return true;
}

const crate::SharedAudio& EditDocument::getAudio() const
{
    static const crate::SharedAudio none;
    return index >= 0 ? steps[(size_t) index].audio : none;
}

juce::Range<int> EditDocument::getSelection() const
{
    if (index < 0)
        return {};
    // After an undo, the step above the current one tells us what was selected before it.
    if (lastWasUndo && index + 1 < (int) steps.size())
        return steps[(size_t) index + 1].selectionBefore;
    return steps[(size_t) index].selectionAfter;
}

juce::String EditDocument::getName() const
{
    return index >= 0 ? steps[(size_t) index].name : juce::String();
}

juce::File EditDocument::getSourceFile() const
{
    return index >= 0 ? steps[(size_t) index].source : juce::File();
}

void EditDocument::setName (const juce::String& n)
{
    if (index >= 0)
    {
        steps[(size_t) index].name = n;
        sendChangeMessage();
    }
}
