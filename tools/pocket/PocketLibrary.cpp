#include "PocketLibrary.h"

#include "core/AudioFiles.h"
#include "core/SoundBank.h"

namespace pocket
{
//==============================================================================
bool FxKnobs::isOn (int i, double x)
{
    switch (i)
    {
        case pitch:   return std::abs (x) >= 0.5;
        case lowCut:  return x > 25.0;
        case highCut: return x < 19000.0;
        case bits:    return x < 15.5;
        case rate:    return x < 41000.0;
        default:      return x > 0.01;
    }
}

int FxKnobs::onCount() const
{
    int n = 0;
    for (int i = 0; i < count; ++i)
        n += isOn (i, v[i]) ? 1 : 0;
    return n;
}

crate::SoundFx::Settings FxKnobs::toSettings() const
{
    crate::SoundFx::Settings s;
    if (isOn (pitch, v[pitch]))     s.pitch = (float) std::round (v[pitch]);
    if (isOn (lowCut, v[lowCut]))   s.lowCut = (float) v[lowCut];
    if (isOn (highCut, v[highCut])) s.highCut = (float) v[highCut];
    s.resonance = (float) v[reso];
    if (isOn (drive, v[drive]))     s.drive = (float) v[drive];
    if (isOn (bits, v[bits]))       s.bits = juce::jlimit (2, 16, (int) std::round (v[bits]));
    if (isOn (rate, v[rate]))       s.sampleRate = (float) v[rate];
    if (isOn (delay, v[delay]))     s.delayMix = (float) v[delay];
    if (isOn (reverb, v[reverb]))   s.reverbMix = (float) v[reverb];
    return s;
}

juce::var FxKnobs::toVar() const
{
    juce::Array<juce::var> a;
    for (double x : v)
        a.add (x);
    return a;
}

FxKnobs FxKnobs::fromVar (const juce::var& in)
{
    FxKnobs k;
    if (auto* a = in.getArray())
        for (int i = 0; i < count && i < a->size(); ++i)
            k.v[i] = (double) a->getReference (i);
    return k;
}

//==============================================================================
// The tempo from a sound's name ("Dusty Loop 92 BPM"), else a guess from its length if it's a loop.
static void fillTempoFromName (SoundItem& item)
{
    SoundBank::Sound t;
    t.file = juce::File::getCurrentWorkingDirectory().getChildFile (item.name + ".wav");
    t.seconds = (float) item.seconds;
    SoundBank::describe (t, item.name + ".wav");
    item.bpm = t.bpm;
    item.bpmGuessed = t.bpmGuessed;
}

juce::File Library::defaultRoot()
{
   #if JUCE_IOS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
   #else
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Kaleidobox Pocket");
   #endif
}

Library::Library (juce::File r)
    : root (std::move (r)), soundsFolder (root.getChildFile ("Sounds")), indexFile (root.getChildFile ("sounds.json")), ideaFolder (root)
{
    formatManager.registerBasicFormats();
    soundsFolder.createDirectory();
    dropFolder().createDirectory();
    loadIndex();
}

int Library::importDropFolder()
{
    int n = 0;
    for (auto& f : dropFolder().findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF;*.mp3;*.MP3;*.m4a;*.caf"))
    {
        juce::String error;
        if (importFile (f, error).isNotEmpty())
        {
            f.deleteFile();
            ++n;
        }
    }
    return n;
}

void Library::loadIndex()
{
    sounds.clear();
    auto parsed = juce::JSON::parse (indexFile);
    if (auto* arr = parsed.getProperty ("sounds", {}).getArray())
    {
        for (auto& e : *arr)
        {
            SoundItem s;
            s.id = e["id"].toString();
            s.name = e["name"].toString();
            s.tag = e["tag"].toString();
            s.file = e["file"].toString();
            s.date = juce::Time (juce::Time::fromISO8601 (e["date"].toString()));
            s.seconds = (double) e["seconds"];
            s.fx = FxKnobs::fromVar (e["fx"]);
            if (e.hasProperty ("bpm"))
            {
                s.bpm = (double) e["bpm"];
                s.bpmGuessed = (bool) e["bpmGuessed"];
            }
            else
                fillTempoFromName (s);
            if (auto* m = e["markers"].getArray())
                for (auto& x : *m)
                    s.markers.push_back ((int) x);
            if (s.id.isNotEmpty() && soundsFolder.getChildFile (s.file).existsAsFile())
                sounds.push_back (std::move (s));
        }
    }
}

void Library::saveIndex()
{
    juce::Array<juce::var> arr;
    for (auto& s : sounds)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", s.id);
        o->setProperty ("name", s.name);
        o->setProperty ("tag", s.tag);
        o->setProperty ("file", s.file);
        o->setProperty ("date", s.date.toISO8601 (true));
        o->setProperty ("seconds", s.seconds);
        o->setProperty ("bpm", s.bpm);
        o->setProperty ("bpmGuessed", s.bpmGuessed);
        o->setProperty ("fx", s.fx.toVar());
        juce::Array<juce::var> m;
        for (int x : s.markers)
            m.add (x);
        o->setProperty ("markers", m);
        arr.add (juce::var (o));
    }
    auto* top = new juce::DynamicObject();
    top->setProperty ("version", 1);
    top->setProperty ("sounds", arr);
    indexFile.replaceWithText (juce::JSON::toString (juce::var (top)));
    sendChangeMessage();
}

const SoundItem* Library::find (const juce::String& id) const
{
    for (auto& s : sounds)
        if (s.id == id)
            return &s;
    return nullptr;
}

juce::File Library::fileFor (const juce::String& id) const
{
    if (auto* s = find (id))
        return soundsFolder.getChildFile (s->file);
    return {};
}

crate::SharedAudio Library::load (const juce::String& id)
{
    juce::String error;
    auto f = fileFor (id);
    return f.existsAsFile() ? crate::AudioFiles::load (f, formatManager, error) : nullptr;
}

juce::File Library::uniqueFile (const juce::String& name) const
{
    auto base = juce::File::createLegalFileName (name.trim());
    if (base.isEmpty())
        base = "Sound";
    auto f = soundsFolder.getChildFile (base + ".wav");
    for (int i = 2; f.exists(); ++i)
        f = soundsFolder.getChildFile (base + " " + juce::String (i) + ".wav");
    return f;
}

juce::String Library::add (const juce::String& name, const juce::String& tag, const juce::AudioBuffer<float>& audio,
                           const FxKnobs& fx, const std::vector<int>& markers, juce::Time date)
{
    auto f = uniqueFile (name);
    if (! crate::AudioFiles::write (f, audio))
        return {};
    SoundItem s;
    s.id = juce::Uuid().toString();
    s.name = name.trim().isEmpty() ? f.getFileNameWithoutExtension() : name.trim();
    s.tag = tag;
    s.file = f.getFileName();
    s.date = date;
    s.seconds = crate::seconds (audio.getNumSamples());
    s.fx = fx;
    s.markers = markers;
    fillTempoFromName (s);
    sounds.insert (sounds.begin(), s);
    saveIndex();
    return s.id;
}

bool Library::update (const juce::String& id, const juce::String& name, const juce::AudioBuffer<float>& audio,
                      const FxKnobs& fx, const std::vector<int>& markers)
{
    for (auto& s : sounds)
    {
        if (s.id != id)
            continue;
        if (! crate::AudioFiles::write (soundsFolder.getChildFile (s.file), audio))
            return false;
        if (name.trim().isNotEmpty())
            s.name = name.trim();
        s.seconds = crate::seconds (audio.getNumSamples());
        s.fx = fx;
        s.markers = markers;
        saveIndex();
        return true;
    }
    return false;
}

bool Library::rename (const juce::String& id, const juce::String& newName)
{
    for (auto& s : sounds)
    {
        if (s.id != id || newName.trim().isEmpty())
            continue;
        s.name = newName.trim();
        if (s.bpm <= 0 || s.bpmGuessed)
            fillTempoFromName (s);
        saveIndex();
        return true;
    }
    return false;
}

bool Library::remove (const juce::String& id)
{
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        if (sounds[i].id != id)
            continue;
        soundsFolder.getChildFile (sounds[i].file).deleteFile();
        sounds.erase (sounds.begin() + (long) i);
        saveIndex();
        return true;
    }
    return false;
}

juce::String Library::importFile (const juce::File& f, juce::String& error)
{
    auto audio = crate::AudioFiles::load (f, formatManager, error);
    if (audio == nullptr)
    {
        if (error.isEmpty())
            error = "That file couldn't be read.";
        return {};
    }
    auto id = add (f.getFileNameWithoutExtension(), kTagImported, *audio, {}, {});
    if (id.isEmpty())
        error = "The sound couldn't be saved.";
    else
    {
        // A tempo written into the file (ACID) beats one guessed from its name.
        double acid = 0;
        if (std::unique_ptr<juce::AudioFormatReader> reader { formatManager.createReaderFor (f) })
            acid = reader->metadataValues[juce::WavAudioFormat::acidTempo].getDoubleValue();
        if (acid >= 40.0 && acid <= 300.0)
            for (auto& s : sounds)
                if (s.id == id)
                {
                    s.bpm = acid;
                    s.bpmGuessed = false;
                    saveIndex();
                    break;
                }
    }
    return id;
}
}
