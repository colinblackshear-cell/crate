#pragma once

#include <juce_graphics/juce_graphics.h>

#include "PocketLibrary.h"

// Ideas: what the Record tab opens to, like Voice Memos. Each idea is a folder (Ideas/<id>) holding its
// pads, beat and loops, plus idea.json with its name and how far along the seven steps it has got.
// The sound library stays shared. Switching ideas points the Library's ideaRoot at another folder.
namespace pocket
{
enum Step { stepRecord, stepShape, stepPads, stepBeat, stepSing, stepSong, stepSend, kSteps };

const char* stepName (int step);    // "RECORD" ... "SEND"
const char* stepHint (int step);    // what to do there, one line
juce::Colour stepColour (int step);

struct IdeaInfo
{
    juce::String id, name;
    juce::Time created, modified;
    int reached = -1;     // the furthest step visited (-1 = none yet)
    bool sent = false;    // sent to the Mac (or out as stems) at least once
    juce::String summary; // "92 BPM · 4 pads · 2 loops", kept up to date by the app
};

class Ideas
{
public:
    // Finds or makes the Ideas folder under the library's root. The first time, the pads, beat and loops
    // that were already on the phone become the first idea. Points the library at the current idea.
    explicit Ideas (Library&);

    const std::vector<IdeaInfo>& list() const { return ideas; }   // newest first (by last change)
    const IdeaInfo* find (const juce::String& id) const;
    const IdeaInfo& current() const;
    juce::File folderFor (const juce::String& id) const { return folder.getChildFile (id); }

    // Makes a new empty idea (not switched to).
    juce::String create (const juce::String& name);
    // Points the library at another idea. The caller saves what's open first and reloads after.
    void open (const juce::String& id);
    void rename (const juce::String& id, const juce::String& name);
    // Deletes an idea and its folder. The current one can't be deleted (switch first).
    bool remove (const juce::String& id);

    // The current idea moved on: notes the step and the time.
    void reachedStep (int step);
    void markSent();
    void setSummary (const juce::String&);
    juce::String nextName() const;   // "Idea 5"
    // Screenshots: makes an idea look lived in.
    void setDemo (const juce::String& id, int reached, bool sent, const juce::String& summary, juce::Time modified);

private:
    void load();
    void save (const IdeaInfo&) const;
    void sortList();
    IdeaInfo* findMutable (const juce::String& id);

    Library& library;
    juce::File folder;
    std::vector<IdeaInfo> ideas;
    juce::String currentId;
};
}
