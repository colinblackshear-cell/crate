#include "Controllers.h"

namespace crate::controllers
{
juce::String kindName (Kind k)
{
    switch (k)
    {
        case Kind::launchkey:   return "Novation Launchkey";
        case Kind::keysAndPads: return "Keyboard with drum pads";
        case Kind::keys:        return "Keyboard (keys only)";
        case Kind::pads:        return "Pad controller";
        case Kind::off:         return "Don't listen";
        case Kind::triggers:    return "Drum triggers";
        default:                return {};
    }
}

juce::String kindHint (Kind k)
{
    switch (k)
    {
        case Kind::launchkey:   return "Keys play the picked track; the 16 drum pads (Drum mode, channel 10) play the MPC; knobs 1-8 turn the track's knobs.";
        case Kind::keysAndPads: return "MPK mini, KeyLab, MiniLab, Oxygen...: keys play the picked track, the pads (on their own channel) play the MPC.";
        case Kind::keys:        return "Every note is a key: it plays the picked track, even on channel 10.";
        case Kind::pads:        return "Launchpad, MPD, APC, Maschine...: every pad plays an MPC pad, up to 64 (four banks at once).";
        case Kind::off:         return "Kaleidobox ignores it (handy for a second port that sends clock or lights).";
        case Kind::triggers:    return "Kaleidobox Trigger box or an e-drum kit: hit each drum to learn it onto a pad or a loop layer.";
        default:                return {};
    }
}

juce::String layoutName (Layout l)
{
    return l == Layout::rows ? "Rows of 8 (APC mini, Programmer mode)" : "4x4 blocks (MPC, MPD, Launchpad Drum mode)";
}

juce::String triggerTargetName (int t)
{
    if (juce::isPositiveAndBelow (t, kMaxPads))
        return "Pad " + juce::String (t + 1) + " (" + juce::String::charToString ((juce::juce_wchar) ('A' + t / 16)) + juce::String (t % 16 + 1) + ")";
    if (t >= kTriggerFirstLoop && t < kTriggerNumTargets)
        return "Loop " + juce::String (t - kTriggerFirstLoop + 1) + " Rec";
    return "Nothing";
}

juce::String feelName (Feel f)
{
    switch (f)
    {
        case Feel::soft: return "Light hands (soft hits louder)";
        case Feel::hard: return "Heavy hands (soft hits softer)";
        case Feel::even:
        default:         return "As played";
    }
}

int applyFeel (Feel f, int velocity)
{
    velocity = juce::jlimit (1, 127, velocity);
    if (f == Feel::even)
        return velocity;
    const double x = velocity / 127.0, y = std::pow (x, f == Feel::soft ? 0.6 : 1.7);
    return juce::jlimit (1, 127, (int) std::lround (y * 127.0));
}

Profile Profile::defaultsFor (Kind k)
{
    Profile p;
    p.kind = k;
    if (k == Kind::pads || k == Kind::triggers)
        p.knobs.fill (-1);   // pad controllers' knobs differ too much: learn them (triggers have none)
    return p;
}

int Profile::triggerFor (int note) const
{
    if (! juce::isPositiveAndBelow (note, 128))
        return kTriggerNothing;
    const int t = triggers[(size_t) note];
    if (t != kTriggerByNote)
        return juce::isPositiveAndBelow (t, kTriggerNumTargets) ? t : kTriggerNothing;
    const int pad = note - kRigFirstPadNote;
    return juce::isPositiveAndBelow (pad, kMaxPads) ? pad : kTriggerNothing;
}

std::vector<int> Profile::learnedTriggers() const
{
    std::vector<int> notes;
    for (int n = 0; n < 128; ++n)
        if (triggers[(size_t) n] != kTriggerByNote)
            notes.push_back (n);
    return notes;
}

int Profile::padFor (int channel, int note) const
{
    if (! hasPads())
        return -1;
    if (kind != Kind::pads && channel != padChannel)
        return -1;
    const int i = note - firstPadNote;
    if (! juce::isPositiveAndBelow (i, kMaxPads))
        return -1;
    if (layout == Layout::rows)
    {
        // Rows of eight from the bottom left, into the MPC's 4x4 banks: the bottom-left block is
        // bank one, then bottom right, top left, top right (as a Launchpad in Drum mode).
        const int row = i / 8, col = i % 8;
        return ((row / 4) * 2 + col / 4) * 16 + (row % 4) * 4 + col % 4;
    }
    return i;
}

juce::ValueTree Profile::toTree() const
{
    juce::ValueTree t ("Controller");
    t.setProperty ("kind", (int) kind, nullptr);
    t.setProperty ("padChannel", padChannel, nullptr);
    t.setProperty ("firstPadNote", firstPadNote, nullptr);
    t.setProperty ("layout", (int) layout, nullptr);
    t.setProperty ("octave", octave, nullptr);
    juce::StringArray k;
    for (int cc : knobs)
        k.add (juce::String (cc));
    t.setProperty ("knobs", k.joinIntoString (","), nullptr);
    if (kind == Kind::triggers)
    {
        // "note:target" for each learned trigger.
        juce::StringArray learned;
        for (int n : learnedTriggers())
            learned.add (juce::String (n) + ":" + juce::String ((int) triggers[(size_t) n]));
        t.setProperty ("triggers", learned.joinIntoString (","), nullptr);
        t.setProperty ("feel", (int) feel, nullptr);
    }
    return t;
}

Profile Profile::fromTree (const juce::ValueTree& t)
{
    Profile p;
    p.kind = (Kind) juce::jlimit (0, (int) Kind::numKinds - 1, (int) t.getProperty ("kind", (int) Kind::keysAndPads));
    p.padChannel = juce::jlimit (1, 16, (int) t.getProperty ("padChannel", 10));
    p.firstPadNote = juce::jlimit (0, 127, (int) t.getProperty ("firstPadNote", 36));
    p.layout = (int) t.getProperty ("layout", 0) == (int) Layout::rows ? Layout::rows : Layout::blocks;
    p.octave = juce::jlimit (-3, 3, (int) t.getProperty ("octave", 0));
    if (t.hasProperty ("knobs"))
    {
        const auto k = juce::StringArray::fromTokens (t.getProperty ("knobs").toString(), ",", {});
        for (int i = 0; i < kNumKnobs; ++i)
            p.knobs[(size_t) i] = i < k.size() ? juce::jlimit (-1, 119, k[i].getIntValue()) : -1;
    }
    for (const auto& pair : juce::StringArray::fromTokens (t.getProperty ("triggers").toString(), ",", {}))
    {
        const int note = pair.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int target = pair.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (pair.contains (":") && juce::isPositiveAndBelow (note, 128))
            p.triggers[(size_t) note] = (juce::int8) juce::jlimit (kTriggerNothing, kTriggerNumTargets - 1, target);
    }
    p.feel = (Feel) juce::jlimit (0, 2, (int) t.getProperty ("feel", (int) Feel::even));
    return p;
}

bool Profile::operator== (const Profile& o) const
{
    return kind == o.kind && padChannel == o.padChannel && firstPadNote == o.firstPadNote && layout == o.layout
           && octave == o.octave && knobs == o.knobs && triggers == o.triggers && feel == o.feel;
}

Profile guessFor (const juce::String& deviceName)
{
    const auto n = deviceName.toLowerCase();
    auto any = [&n] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (n.contains (w))
                return true;
        return false;
    };

    if (n.contains ("launchkey"))
        return Profile::defaultsFor (Kind::launchkey);

    // Drum triggers: the Kaleidobox Trigger box (or a DIY one on an Arduino), and e-drum modules.
    if (any ({ "kaleidobox trigger", "drum trigger", "trigger box", "arduino leonardo", "arduino micro", "pro micro",
               "e-drum", "edrum", "td-", "alesis strike", "alesis nitro", "alesis dm", "simmons", "2box",
               "trigger io", "megadrum", "ddrum", "spd-sx", "spd sx", "octapad" })
        || (n.contains ("trigger") && ! n.contains ("trigger finger")))
        return Profile::defaultsFor (Kind::triggers);

    if (n.contains ("apc mini") || n.contains ("apc_mini"))
    {
        // Its 8x8 grid sends notes 0-63 in rows from the bottom left.
        auto p = Profile::defaultsFor (Kind::pads);
        p.firstPadNote = 0;
        p.layout = Layout::rows;
        p.knobs = { 48, 49, 50, 51, 52, 53, 54, 55 };   // its first eight faders
        return p;
    }
    if (any ({ "launchpad", "mpd", "apc", "maschine", "lpd8", "padkontrol", "nanopad", "beatstep", "mpc studio", "mpc one",
               "mpc live", "trigger finger", "lightpad", "atom", "push" }))
        return Profile::defaultsFor (Kind::pads);

    if (n.contains ("mpk mini") || n.contains ("mpk"))
    {
        // Pads on channel 10 from C1, knobs CC 70-77 (Akai's factory program).
        auto p = Profile::defaultsFor (Kind::keysAndPads);
        p.knobs = { 70, 71, 72, 73, 74, 75, 76, 77 };
        return p;
    }
    if (any ({ "keylab", "minilab", "oxygen", "impulse", "mpk", "kontrol a", "mk3 keys" }))
        return Profile::defaultsFor (Kind::keysAndPads);

    if (any ({ "keystep", "keystation", "komplete kontrol", "seaboard", "digital piano", "nord", "osmose", "microkorg", "piano" }))
        return Profile::defaultsFor (Kind::keys);

    // Unknown: as the Rig has always read every keyboard (pads on channel 10 from note 36).
    return Profile::defaultsFor (Kind::keysAndPads);
}

//==============================================================================
void Translator::setProfile (const Profile& p)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    profile = p;
}

Profile Translator::getProfile() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return profile;
}

void Translator::learn (Learn what, int firstTarget)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    caughtKnobs.fill (-1);
    knobsSoFar = 0;
    caughtTriggers.fill (false);
    triggersSoFar = 0;
    triggerTarget = juce::jlimit (kTriggerNothing, kTriggerNumTargets - 1, firstTarget);
    learnWhat = (int) what;
}

void Translator::stopLearning()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (learnWhat.load() == (int) Learn::knobs && knobsSoFar.load() > 0)
    {
        // Keep the knobs turned so far; the rest have none.
        profile.knobs = caughtKnobs;
        learned = true;
    }
    learnWhat = (int) Learn::none;
}

bool Translator::translate (juce::MidiMessage& m, int rigPadChannel, int* loopHit)
{
    heard = juce::Time::getMillisecondCounter();
    if (m.isNoteOn())
        noteHeard[(size_t) m.getNoteNumber()] = heard.load();
    const juce::SpinLock::ScopedLockType sl (lock);
    const auto& p = profile;
    if (p.kind == Kind::off)
        return false;
    rigPadChannel = juce::jlimit (1, 16, rigPadChannel);
    const int channel = m.getChannel();

    // Learning.
    const auto what = (Learn) learnWhat.load();
    if (what == Learn::pad && m.isNoteOn())
    {
        profile.firstPadNote = m.getNoteNumber();
        if (profile.kind != Kind::pads)
            profile.padChannel = channel;
        learnWhat = (int) Learn::none;
        learned = true;
        return false;
    }
    if (what == Learn::knobs && m.isController() && m.getControllerNumber() < 120)
    {
        int n = knobsSoFar.load();
        const int cc = m.getControllerNumber();
        if (std::find (caughtKnobs.begin(), caughtKnobs.begin() + n, cc) == caughtKnobs.begin() + n && n < kNumKnobs)
        {
            caughtKnobs[(size_t) n++] = cc;
            knobsSoFar = n;
        }
        if (n >= kNumKnobs)
        {
            profile.knobs = caughtKnobs;
            learnWhat = (int) Learn::none;
            learned = true;
        }
        return false;   // turning knobs to learn them doesn't move anything
    }

    if (p.kind == Kind::triggers)
    {
        if (! m.isNoteOnOrOff())
            return ! m.isAftertouch();   // a hi-hat pedal's CC and the like come through; pressure doesn't
        const int note = m.getNoteNumber();
        if (what == Learn::triggers && m.isNoteOn() && ! caughtTriggers[(size_t) note])
        {
            // Hit to learn: this drum gets the next target, and still plays it now.
            caughtTriggers[(size_t) note] = true;
            const int target = triggerTarget.load();
            profile.triggers[(size_t) note] = (juce::int8) target;
            triggersSoFar = triggersSoFar.load() + 1;
            const bool loop = target >= kTriggerFirstLoop;
            const int next = target + 1;
            if (target >= 0 && next < (loop ? kTriggerNumTargets : kMaxPads))
                triggerTarget = next;
            learned = true;
        }
        const int target = p.triggerFor (note);
        if (target < 0)
            return false;
        if (target >= kTriggerFirstLoop)
        {
            // A loop layer's Rec: pressed on the hit, nothing on the let go.
            if (m.isNoteOn() && loopHit != nullptr)
                *loopHit = target - kTriggerFirstLoop;
            return false;
        }
        m.setChannel (rigPadChannel);
        m.setNoteNumber (kRigFirstPadNote + target);
        if (m.isNoteOn())
            m.setVelocity ((float) applyFeel (p.feel, m.getVelocity()) / 127.0f);
        return true;
    }

    if (m.isNoteOnOrOff() || m.isAftertouch())
    {
        const int note = m.getNoteNumber();
        if (const int pad = p.padFor (channel, note); pad >= 0)
        {
            m.setChannel (rigPadChannel);
            m.setNoteNumber (kRigFirstPadNote + pad);
            return true;
        }
        if (p.kind != Kind::pads && p.hasPads() && channel == p.padChannel)
        {
            // Another note on the pads' channel (a pad beyond the 64): on the pads' channel, as it was.
            m.setChannel (rigPadChannel);
            return true;
        }

        // A key. Off the Rig's pad channel, so it plays the keys and not the pads.
        if (channel == rigPadChannel)
            m.setChannel (rigPadChannel == 1 ? 2 : 1);
        auto& wasSent = sent[(size_t) (channel - 1)][(size_t) note];
        int out = note;
        if (m.isNoteOn())
        {
            out = juce::jlimit (0, 127, note + 12 * p.octave);
            wasSent = (juce::uint8) (out + 1);
        }
        else if (wasSent > 0)
        {
            // Let go (or pressed harder): the note it started, whatever the octave is now.
            out = wasSent - 1;
            if (m.isNoteOff())
                wasSent = 0;
        }
        else
            out = juce::jlimit (0, 127, note + 12 * p.octave);
        m.setNoteNumber (out);
        return true;
    }

    if (m.isController() && m.getControllerNumber() < 120)
    {
        const int cc = m.getControllerNumber();
        for (int k = 0; k < kNumKnobs; ++k)
            if (p.knobs[(size_t) k] == cc)
            {
                if (cc != kRigFirstKnobCc + k)
                {
                    const double time = m.getTimeStamp();
                    m = juce::MidiMessage::controllerEvent (channel, kRigFirstKnobCc + k, m.getControllerValue());
                    m.setTimeStamp (time);
                }
                return true;
            }
        return true;
    }
    return true;
}

//==============================================================================
Setup::Setup (juce::File f) : file (std::move (f))
{
    if (file.existsAsFile())
        if (auto xml = juce::parseXML (file))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            if (t.hasType ("Controllers"))
                saved = t;
        }
}

juce::File Setup::defaultFile()
{
    // Beside the app's other settings (Application Support/Crate Rig): they belong to this Mac.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Crate Rig").getChildFile ("MIDI controllers.xml");
}

Profile Setup::profileFor (const juce::String& name)
{
    const auto t = saved.getChildWithProperty ("name", name);
    if (t.isValid() && t.hasProperty ("kind"))
        return Profile::fromTree (t);
    return guessFor (name);
}

bool Setup::isKnown (const juce::String& name) const
{
    return saved.getChildWithProperty ("name", name).isValid();
}

void Setup::markKnown (const juce::String& name)
{
    if (isKnown (name))
        return;
    auto t = profileFor (name).toTree();
    t.setProperty ("name", name, nullptr);
    saved.appendChild (t, nullptr);
    save();
}

void Setup::setProfile (const juce::String& name, const Profile& p)
{
    auto t = p.toTree();
    t.setProperty ("name", name, nullptr);
    const auto old = saved.getChildWithProperty ("name", name);
    if (old.isValid())
        saved.removeChild (old, nullptr);
    saved.appendChild (t, nullptr);
    translatorFor (name).setProfile (p);
    save();
}

Setup::Slot* Setup::find (const juce::String& name)
{
    for (auto& s : slots)
        if (s.name == name)
            return &s;
    return nullptr;
}

Translator& Setup::translatorFor (const juce::String& name)
{
    {
        const juce::SpinLock::ScopedLockType sl (slotsLock);
        if (auto* s = find (name))
            return *s->translator;
    }
    auto t = std::make_unique<Translator>();
    t->setProfile (profileFor (name));
    auto* made = t.get();
    // The translator lives on the heap, so a pointer the MIDI thread holds stays good when the
    // list grows.
    const juce::SpinLock::ScopedLockType sl (slotsLock);
    slots.push_back ({ name, std::move (t) });
    return *made;
}

bool Setup::saveLearned()
{
    std::vector<std::pair<juce::String, Profile>> changed;
    {
        const juce::SpinLock::ScopedLockType sl (slotsLock);
        for (auto& s : slots)
            if (s.translator->takeLearned())
                changed.push_back ({ s.name, s.translator->getProfile() });
    }
    for (auto& [name, p] : changed)
        setProfile (name, p);
    return ! changed.empty();
}

bool Setup::translate (const juce::String& name, juce::MidiMessage& m, int rigPadChannel, int* loopHit)
{
    Translator* t = nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (slotsLock);
        if (auto* s = find (name))
            t = s->translator.get();
    }
    // Not set up yet (just plugged in, for a moment): as it comes.
    return t == nullptr || t->translate (m, rigPadChannel, loopHit);
}

void Setup::save()
{
    if (file == juce::File())
        return;
    file.getParentDirectory().createDirectory();
    if (auto xml = saved.createXml())
        xml->writeTo (file);
}
}
