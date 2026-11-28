#include "RigLaunch.h"
#include "RigLastShow.h"

namespace riglaunch
{
namespace
{
juce::String hashBytes (const void* data, size_t n)
{
    // FNV-1a, 64 bits: enough to tell one saved XML from another (not a security check).
    auto* p = static_cast<const juce::uint8*> (data);
    juce::uint64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return juce::String::toHexString ((juce::int64) h);
}

// The loops' revision the XML names: an attribute of its first element.
juce::String revisionIn (const juce::MemoryBlock& bytes)
{
    const auto head = juce::String::fromUTF8 (static_cast<const char*> (bytes.getData()), (int) juce::jmin ((size_t) 65536, bytes.getSize()));
    const auto start = head.indexOf ("<" + juce::String (RigEngine::kShowType));
    if (start < 0)
        return {};
    const auto end = head.indexOfChar (start, '>');
    const auto tag = head.substring (start, end < 0 ? head.length() : end);
    const auto key = juce::String (rigshow::kRevisionAttribute) + "=\"";
    const auto at = tag.indexOf (key);
    if (at < 0)
        return {};
    const auto from = at + key.length();
    return tag.substring (from, tag.indexOfChar (from, '"'));
}

juce::String name (const juce::File& f) { return f.getFileNameWithoutExtension(); }
} // namespace

Identity statOf (const juce::File& f)
{
    Identity id;
    if (f.existsAsFile())
    {
        id.size = f.getSize();
        id.modified = f.getLastModificationTime().toMilliseconds();
    }
    return id;
}

Identity identify (const juce::File& f, const juce::MemoryBlock& bytes)
{
    auto id = statOf (f);
    id.hash = hashBytes (bytes.getData(), bytes.getSize());
    id.size = (juce::int64) bytes.getSize();
    id.revision = revisionIn (bytes);
    return id;
}

Identity identify (const juce::File& f)
{
    juce::MemoryBlock bytes;
    if (! f.existsAsFile() || ! f.loadFileAsData (bytes))
        return {};
    return identify (f, bytes);
}

//==============================================================================
Record read (juce::PropertiesFile& settings)
{
    Record r;
    const auto xml = settings.getXmlValue (kKey);
    if (xml == nullptr)
        return r;
    r.present = true;
    r.intent = xml->getStringAttribute ("intent") == "reopenDocument" ? Record::Intent::reopenDocument : Record::Intent::empty;
    if (const auto path = xml->getStringAttribute ("path"); juce::File::isAbsolutePath (path))
        r.document = juce::File (path);
    r.identity.revision = xml->getStringAttribute ("revision");
    r.identity.hash = xml->getStringAttribute ("hash");
    r.identity.size = xml->getStringAttribute ("size", "-1").getLargeIntValue();
    r.identity.modified = xml->getStringAttribute ("modified", "0").getLargeIntValue();
    r.cleanShutdown = xml->getBoolAttribute ("clean", false);
    if (const auto path = xml->getStringAttribute ("saving"); juce::File::isAbsolutePath (path))
        r.saving = juce::File (path);
    return r;
}

namespace
{
std::unique_ptr<juce::XmlElement> toXml (const Record& r)
{
    auto xml = std::make_unique<juce::XmlElement> ("LaunchDocument");
    xml->setAttribute ("intent", r.intent == Record::Intent::reopenDocument ? "reopenDocument" : "empty");
    if (r.document != juce::File())
        xml->setAttribute ("path", r.document.getFullPathName());
    if (r.identity.known())
    {
        xml->setAttribute ("revision", r.identity.revision);
        xml->setAttribute ("hash", r.identity.hash);
        xml->setAttribute ("size", juce::String (r.identity.size));
        xml->setAttribute ("modified", juce::String (r.identity.modified));
    }
    xml->setAttribute ("clean", r.cleanShutdown);
    if (r.saving != juce::File())
        xml->setAttribute ("saving", r.saving.getFullPathName());
    return xml;
}
} // namespace

//==============================================================================
Tracker::Tracker (juce::PropertiesFile& s) : settings (s), rec (read (s)), before (rec) {}

bool Tracker::store()
{
    const auto xml = toXml (rec);
    settings.setValue (kKey, xml.get());
    const bool ok = ! failWritesForTests && settings.saveIfNeeded();
    ++writes;
    if (! ok)
        ++failedWrites;
    return ok;
}

void Tracker::opened (const juce::File& f)
{
    opened (f, identify (f), false);
}

void Tracker::opened (const juce::File& f, const Identity& id, bool confirmFirstSave)
{
    if (f == juce::File() || riglastshow::isLastShow (f))
        return untitled();
    rec.intent = Record::Intent::reopenDocument;
    rec.document = f;
    rec.identity = id;
    rec.saving = juce::File();
    confirmFirst = confirmFirstSave;
    store();
}

void Tracker::untitled()
{
    rec.intent = Record::Intent::empty;
    rec.document = juce::File();
    rec.identity = {};
    rec.saving = juce::File();
    confirmFirst = false;
    store();
}

void Tracker::renamed (const juce::File& to)
{
    if (rec.document == juce::File())
        return;
    rec.document = to;
    const auto now = statOf (to);
    rec.identity.size = now.size;
    rec.identity.modified = now.modified;
    store();
}

void Tracker::saving (const juce::File& f)
{
    rec.saving = f;
    store();
}

void Tracker::saved (const juce::File& f, const rigshow::Result& result)
{
    rec.intent = Record::Intent::reopenDocument;
    rec.document = f;
    rec.identity = identify (f);
    if (result.revision.isNotEmpty())
        rec.identity.revision = result.revision;
    rec.saving = juce::File();
    confirmFirst = false;
    store();
}

void Tracker::launched()
{
    rec.cleanShutdown = false;
    store();
}

bool Tracker::quitting (const juce::File& current)
{
    if (current != juce::File() && ! riglastshow::isLastShow (current))
    {
        if (rec.document != current || ! rec.identity.known())
        {
            rec.document = current;
            rec.identity = identify (current);
        }
        rec.intent = Record::Intent::reopenDocument;
    }
    else
    {
        rec.intent = Record::Intent::empty;
        rec.document = juce::File();
        rec.identity = {};
    }
    rec.saving = juce::File();
    rec.cleanShutdown = true;
    return store();
}

bool Tracker::confirmBeforeSaving (const juce::File& f) const
{
    if (f == juce::File() || f != rec.document || rec.intent != Record::Intent::reopenDocument)
        return false;
    if (confirmFirst)
        return true;
    return rec.identity.known() && ! rec.identity.sameStat (statOf (f));
}

//==============================================================================
Launch launch (RigEngine& engine, Tracker& tracker)
{
    Launch out;
    const auto r = tracker.previous();
    out.interrupted = r.present && ! r.cleanShutdown;
    const auto f = r.document;
    if (r.present && r.intent == Record::Intent::reopenDocument && f != juce::File() && ! riglastshow::isLastShow (f))
    {
        const auto where = " (" + f.getFullPathName() + ")";
        juce::MemoryBlock bytes;
        std::unique_ptr<juce::XmlElement> xml;
        if (! f.existsAsFile())
        {
            out.title = name (f) + " wasn't found";
            out.notice = "Kaleidobox couldn't find " + name (f) + where + ", so it started a new song. Nothing was made in its place. "
                         "If you moved it, open it from where it is now.";
        }
        else if (! f.loadFileAsData (bytes) || (xml = juce::parseXML (bytes.toString())) == nullptr)
        {
            out.title = "Couldn't open " + name (f);
            out.notice = "Kaleidobox couldn't read " + name (f) + where + ", so it started a new song. The file was left as it is.";
        }
        else if (const auto tree = juce::ValueTree::fromXml (*xml); ! tree.hasType (RigEngine::kShowType))
        {
            out.title = "Couldn't open " + name (f);
            out.notice = name (f) + where + " isn't a Kaleidobox song, so Kaleidobox started a new song. The file was left as it is.";
        }
        else
        {
            const auto id = identify (f, bytes);
            out.changed = ! id.sameContent (r.identity);
            engine.loadShow (tree);
            engine.readLoopAudio (f);
            out.opened = true;
            out.document = f;
            juce::StringArray said;
            if (out.changed && r.saving == f)
                said.add ("Kaleidobox stopped while it was saving " + name (f) + " last time. It opened " + name (f)
                          + " as it is in the file now (a save is all or nothing: that save or the one before).");
            else if (out.changed)
                said.add (name (f) + " changed outside Kaleidobox since you last saved it here. Kaleidobox opened it as it is now.");
            else if (out.interrupted)
                said.add ("Kaleidobox didn't quit properly last time. " + name (f) + " is open as you last saved it; changes after that weren't kept.");
            if (out.changed)
                said.add ("Its next Save asks before saving over it.");
            if (engine.getLoopAudioProblem().isNotEmpty())
                said.add (engine.getLoopAudioProblem());
            out.notice = said.joinIntoString (" ");
            if (out.notice.isNotEmpty())
                out.title = out.changed ? name (f) + " changed" : "Kaleidobox didn't quit properly";
            tracker.opened (f, id, out.changed);
        }
    }
    if (! out.opened)
    {
        engine.newShow();   // (the new song is what the dirty model compares with)
        tracker.untitled();
    }
    tracker.launched();
    return out;
}
} // namespace riglaunch
