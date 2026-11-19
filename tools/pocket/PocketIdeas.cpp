#include "PocketIdeas.h"

#include "PocketTheme.h"

namespace pocket
{
const char* stepName (int s)
{
    static const char* names[kSteps] = { "RECORD", "SHAPE", "PADS", "BEAT", "SING", "SONG", "SEND" };
    return names[juce::jlimit (0, kSteps - 1, s)];
}

const char* stepHint (int s)
{
    static const char* hints[kSteps] = {
        "record a sound: tap the red button",
        "trim it and add effects, or chop it",
        "put your sounds on the pads",
        "record the pads into a beat, then lock it",
        "record your voice or the vocoder over the beat",
        "bring layers in and out to make a song",
        "send it to your Mac, Logic or Ableton" };
    return hints[juce::jlimit (0, kSteps - 1, s)];
}

juce::Colour stepColour (int s)
{
    static const juce::uint32 cols[kSteps] = { 0xfff08a74, 0xfff2b632, 0xff8fc06a, 0xff5fb3c4, 0xff8a6fb3, 0xffe0785a, 0xffc9a472 };
    return juce::Colour (cols[juce::jlimit (0, kSteps - 1, s)]);
}

//==============================================================================
Ideas::Ideas (Library& lib) : library (lib), folder (lib.getRoot().getChildFile ("Ideas"))
{
    const bool fresh = ! folder.isDirectory();
    folder.createDirectory();
    if (fresh)
    {
        // What was on the phone before ideas becomes the first idea.
        const auto root = lib.getRoot();
        bool anything = false;
        for (auto* sub : { "Beats", "Kits", "Loops" })
            anything = anything || root.getChildFile (sub).isDirectory();
        const auto id = create (anything ? "My first idea" : "Idea 1");
        for (auto* sub : { "Beats", "Kits", "Loops" })
            if (root.getChildFile (sub).isDirectory())
                root.getChildFile (sub).moveFileTo (folderFor (id).getChildFile (sub));
        if (anything)
            if (auto* info = findMutable (id))
            {
                info->reached = stepPads;
                save (*info);
            }
        folder.getChildFile ("current.txt").replaceWithText (id);
    }
    load();
    auto wanted = folder.getChildFile ("current.txt").loadFileAsString().trim();
    if (find (wanted) == nullptr)
        wanted = ideas.empty() ? create ("Idea 1") : ideas.front().id;
    open (wanted);
}

void Ideas::load()
{
    ideas.clear();
    for (auto& dir : folder.findChildFiles (juce::File::findDirectories, false))
    {
        auto v = juce::JSON::parse (dir.getChildFile ("idea.json"));
        if (! v.isObject())
            continue;
        IdeaInfo i;
        i.id = dir.getFileName();
        i.name = v.getProperty ("name", "Idea").toString();
        i.created = juce::Time ((juce::int64) v.getProperty ("created", 0));
        i.modified = juce::Time ((juce::int64) v.getProperty ("modified", 0));
        i.reached = juce::jlimit (-1, kSteps - 1, (int) v.getProperty ("reached", -1));
        i.sent = (bool) v.getProperty ("sent", false);
        i.summary = v.getProperty ("summary", {}).toString();
        ideas.push_back (i);
    }
    sortList();
}

void Ideas::sortList()
{
    std::stable_sort (ideas.begin(), ideas.end(), [] (const IdeaInfo& a, const IdeaInfo& b) { return a.modified > b.modified; });
}

void Ideas::save (const IdeaInfo& i) const
{
    auto dir = folderFor (i.id);
    dir.createDirectory();
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", i.name);
    o->setProperty ("created", i.created.toMilliseconds());
    o->setProperty ("modified", i.modified.toMilliseconds());
    o->setProperty ("reached", i.reached);
    o->setProperty ("sent", i.sent);
    o->setProperty ("summary", i.summary);
    dir.getChildFile ("idea.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}

const IdeaInfo* Ideas::find (const juce::String& id) const
{
    for (auto& i : ideas)
        if (i.id == id)
            return &i;
    return nullptr;
}

IdeaInfo* Ideas::findMutable (const juce::String& id)
{
    for (auto& i : ideas)
        if (i.id == id)
            return &i;
    return nullptr;
}

const IdeaInfo& Ideas::current() const
{
    if (auto* i = find (currentId))
        return *i;
    static const IdeaInfo none;
    return none;
}

juce::String Ideas::create (const juce::String& name)
{
    const auto now = juce::Time::getCurrentTime();
    auto id = now.formatted ("%Y%m%d-%H%M%S");
    for (int n = 2; folderFor (id).exists(); ++n)
        id = now.formatted ("%Y%m%d-%H%M%S") + "-" + juce::String (n);
    IdeaInfo i;
    i.id = id;
    i.name = name.trim().isEmpty() ? nextName() : name.trim();
    i.created = i.modified = now;
    save (i);
    ideas.insert (ideas.begin(), i);
    return id;
}

juce::String Ideas::nextName() const
{
    int n = (int) ideas.size() + 1;
    auto taken = [this] (const juce::String& s) { for (auto& i : ideas) if (i.name == s) return true; return false; };
    while (taken ("Idea " + juce::String (n)))
        ++n;
    return "Idea " + juce::String (n);
}

void Ideas::open (const juce::String& id)
{
    if (find (id) == nullptr)
        return;
    currentId = id;
    folder.getChildFile ("current.txt").replaceWithText (id);
    library.setIdeaRoot (folderFor (id));
}

void Ideas::rename (const juce::String& id, const juce::String& name)
{
    if (auto* i = findMutable (id); i != nullptr && name.trim().isNotEmpty())
    {
        i->name = name.trim();
        save (*i);
    }
}

bool Ideas::remove (const juce::String& id)
{
    if (id == currentId || find (id) == nullptr)
        return false;
    folderFor (id).deleteRecursively();
    ideas.erase (std::remove_if (ideas.begin(), ideas.end(), [&] (const IdeaInfo& i) { return i.id == id; }), ideas.end());
    return true;
}

void Ideas::reachedStep (int step)
{
    if (auto* i = findMutable (currentId))
    {
        i->reached = juce::jmax (i->reached, step);
        i->modified = juce::Time::getCurrentTime();
        save (*i);
        sortList();
    }
}

void Ideas::markSent()
{
    if (auto* i = findMutable (currentId))
    {
        i->sent = true;
        i->reached = stepSend;
        i->modified = juce::Time::getCurrentTime();
        save (*i);
        sortList();
    }
}

void Ideas::setSummary (const juce::String& s)
{
    if (auto* i = findMutable (currentId); i != nullptr && i->summary != s)
    {
        i->summary = s;
        save (*i);
    }
}

void Ideas::setDemo (const juce::String& id, int reached, bool sent, const juce::String& summary, juce::Time modified)
{
    if (auto* i = findMutable (id))
    {
        i->reached = reached;
        i->sent = sent;
        i->summary = summary;
        i->modified = modified;
        save (*i);
        sortList();
    }
}
}
