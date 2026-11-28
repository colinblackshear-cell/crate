#include "RigDocFlow.h"

namespace
{
    juce::String named (const juce::String& message, const juce::String& name)
    {
        return message.replace ("{name}", name);
    }
}

RigDocFlow::RigDocFlow (RigEngine& e, rigdirty::Asker& a, Host& h) : engine (e), asker (&a), host (h) {}

RigDocFlow::~RigDocFlow() = default;

//==============================================================================
RigDocFlow::Wording RigDocFlow::newSongWords()
{
    return { "Start a new song?", "Kaleidobox clears every tool, loop and song part for a new song. Save {name} first?",
             "Save, then new song", "New song, don't save" };
}

RigDocFlow::Wording RigDocFlow::lessonWords()
{
    return { "Make a beat", "The lesson starts a new song, so {name} stays as it is. Save it first?",
             "Save, then the lesson", "Lesson, don't save" };
}

RigDocFlow::Wording RigDocFlow::startWords (const juce::String& what)
{
    return { what, "This starts a new song. Save {name} first?", "Save, then start", "Start, don't save" };
}

RigDocFlow::Wording RigDocFlow::openWords (const juce::File& file)
{
    return { "Open " + file.getFileNameWithoutExtension() + "?", "Save {name} first?", "Save, then open", "Open, don't save" };
}

RigDocFlow::Wording RigDocFlow::importWords()
{
    return { "Import a song from Logic",
             "The Logic song comes in as a new song (its stems as loops, its markers as sections). Save {name} first?",
             "Save, then import", "Import, don't save" };
}

RigDocFlow::Wording RigDocFlow::quitWords()
{
    return { "Quit Kaleidobox?", "Save {name} before quitting? If you don't save, your changes are lost.", "Save", "Don't Save" };
}

juce::String RigDocFlow::savedMessage (RigEngine& engine, const juce::File& file, const rigshow::Result& result)
{
    int loops = 0;
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (const auto st = engine.getLooper().getState (i); st == RigLooper::State::playing || st == RigLooper::State::overdubbing)
            ++loops;
    return "Saved " + file.getFileNameWithoutExtension() + ": the show" + (loops > 0 ? " and " + juce::String (loops) + (loops == 1 ? " loop" : " loops") : juce::String())
         + ", in " + file.getParentDirectory().getFullPathName() + ". Cmd+S saves it again."
         + (result.warning.isNotEmpty() ? " " + result.warning : juce::String());
}

juce::File RigDocFlow::importStart()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
}

juce::String RigDocFlow::showName() const
{
    const auto f = host.getCurrentShow();
    return f != juce::File() ? f.getFileNameWithoutExtension() : juce::String ("this song");
}

//==============================================================================
bool RigDocFlow::begin (std::unique_ptr<Request> r)
{
    ++requestsForTests;
    if (current != nullptr)
    {
        ++stats.ignored;
        host.say ("Kaleidobox is still on what you asked for last (a question or a file window is open): finish that first.");
        return false;
    }
    r->serial = ++serial;
    r->startShow = host.getCurrentShow();
    current = std::move (r);
    return true;
}

void RigDocFlow::end()
{
    current.reset();
    ++serial;
}

void RigDocFlow::abandon()
{
    if (current != nullptr)
        end();
}

template <typename Arg>
std::function<void (Arg)> RigDocFlow::guard (std::function<void (Arg)> fn)
{
    std::weak_ptr<int> alive = life;
    const auto s = serial;
    return [this, alive, s, fn] (Arg a)
    {
        if (alive.expired())
            return;   // the flow (the window) is gone
        if (current == nullptr || current->serial != s)
        {
            ++stats.stale;
            return;
        }
        if (host.getCurrentShow() != current->startShow)
        {
            ++stats.stale;   // another show was opened meanwhile
            end();
            return;
        }
        fn (a);
    };
}

//==============================================================================
bool RigDocFlow::newSong (const Wording& words, Then then, bool clearWhenBlank)
{
    auto r = std::make_unique<Request>();
    r->kind = Kind::blank;
    r->words = words;
    r->then = std::move (then);
    r->clearWhenBlank = clearWhenBlank;
    if (! begin (std::move (r)))
        return false;
    decide();
    return true;
}

bool RigDocFlow::openFile (const juce::File& file, Then then)
{
    auto r = std::make_unique<Request>();
    r->kind = Kind::open;
    r->file = file;
    r->words = openWords (file);
    r->then = std::move (then);
    if (! begin (std::move (r)))
        return false;
    if (read (*current))
        decide();
    else
        end();
    return true;
}

bool RigDocFlow::openChooser (Then then)
{
    auto r = std::make_unique<Request>();
    r->kind = Kind::open;
    r->then = std::move (then);
    if (! begin (std::move (r)))
        return false;
    asker->chooseOpenFile (host.showsFolder(), guard<const juce::File&> ([this] (const juce::File& file)
    {
        if (file == juce::File())
            return end();   // cancelled: nothing happened
        current->file = file;
        current->words = openWords (file);
        if (! read (*current))
            return end();
        decide();
    }));
    return true;
}

bool RigDocFlow::importFromLogic (Then then)
{
    auto r = std::make_unique<Request>();
    r->kind = Kind::import;
    r->words = importWords();
    r->then = std::move (then);
    if (! begin (std::move (r)))
        return false;
    asker->chooseFolder ("Pick the folder with the song's stems (File > Export > All Tracks as Audio Files) and its MIDI file", importStart(),
                         guard<const juce::File&> ([this] (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return end();   // cancelled: nothing happened
        current->file = folder;
        if (! read (*current))
            return end();
        decide();
    }));
    return true;
}

bool RigDocFlow::save()
{
    auto r = std::make_unique<Request>();
    r->kind = Kind::saveOnly;
    if (! begin (std::move (r)))
        return false;
    saveFirst();
    return true;
}

bool RigDocFlow::quit (Then then)
{
    auto r = std::make_unique<Request>();
    r->kind = Kind::quit;
    r->words = quitWords();
    r->then = std::move (then);
    if (! begin (std::move (r)))
        return false;
    decide();
    return true;
}

bool RigDocFlow::saveAs()
{
    auto r = std::make_unique<Request>();
    r->kind = Kind::saveOnly;
    r->saveAsOnly = true;
    if (! begin (std::move (r)))
        return false;
    saveFirst();
    return true;
}

//==============================================================================
// Step 2: what replaces the show, read before anything changes.
bool RigDocFlow::read (Request& r)
{
    if (r.kind == Kind::open)
    {
        if (auto xml = juce::parseXML (r.file))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.hasType (RigEngine::kShowType))
            {
                r.tree = tree;
                return true;
            }
        }
        host.say ("Couldn't open " + r.file.getFileNameWithoutExtension() + ": the file isn't a Kaleidobox song.");
        return false;
    }
    if (r.kind == Kind::import)
    {
        // (The tempo a new song has, for stems with no MIDI tempo: the song comes in on a new one.)
        auto song = std::make_unique<rigimport::Song> (rigimport::read (r.file, engine.blankBpm()));
        if (song->layers.empty())
        {
            host.say (song->notes.joinIntoString (". ") + ".");
            return false;
        }
        r.song = std::move (song);
        return true;
    }
    return true;
}

// Step 3: why the show you're on needs saving before it's replaced (empty: it doesn't).
juce::String RigDocFlow::whyProtect (const Request& r)
{
    const bool untitled = host.getCurrentShow() == juce::File();
    if (untitled)
        return engine.isBlank() ? juce::String() : juce::String ("it isn't saved as a song");
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (const auto st = engine.getLooper().getState (i);
            st == RigLooper::State::armed || st == RigLooper::State::recording || st == RigLooper::State::overdubbing)
            return "a loop is still recording";
    // Quitting with its file gone (moved, deleted): what's open is the only copy.
    if (r.kind == Kind::quit && ! host.getCurrentShow().existsAsFile())
        return "its file isn't there any more";
    auto& dirty = engine.getDirty();
    if (dirty.isEnabled())
    {
        if (cachedStateForTests)
            return dirty.getState() == RigDirty::State::clean ? juce::String() : juce::String ("it has changed");
        if (! dirty.waitUntilResolved (verifyTimeoutMs))
        {
            ++stats.unresolved;
            return "Kaleidobox couldn't check it in time";
        }
        return dirty.getState() == RigDirty::State::clean ? juce::String() : juce::String ("it has changed");
    }
    // The model is off: nothing can say it's unchanged, apart from a save just now that worked.
    return r.justSaved ? juce::String() : juce::String ("it may have changed");
}

void RigDocFlow::decide()
{
    auto& r = *current;
    const auto why = whyProtect (r);
    if (why.isEmpty())
        return replaceNow();
    if (r.rounds >= maxRounds)
    {
        ++stats.roundsExceeded;
        host.say (showName() + " keeps changing (" + why + (r.kind == Kind::quit ? "), so Kaleidobox didn't quit." : "), so it wasn't replaced.")
                  + " Stop what's recording, save it, then try again.");
        return end();
    }
    ++r.rounds;
    rigdirty::Question q;
    q.title = r.words.title;
    q.message = named (r.words.message, showName());
    if (r.justSaved)
        q.message = showName() + " changed after it was saved (" + why + "). " + q.message;
    q.saveLabel = r.words.save;
    q.dontSaveLabel = r.words.dontSave;
    q.cancelLabel = "Cancel";
    ++stats.questions;
    asker->ask (q, guard<rigdirty::Choice> ([this] (rigdirty::Choice c) { answered (c); }));
}

void RigDocFlow::answered (rigdirty::Choice c)
{
    switch (c)
    {
        case rigdirty::Choice::save:     return saveFirst();
        case rigdirty::Choice::dontSave: return replaceNow (true);
        case rigdirty::Choice::cancel:
        default:                         return end();   // nothing changes; playback goes on
    }
}

// Step 4: Save. To the show's own file, or Save As (never a name made up for you).
void RigDocFlow::saveFirst()
{
    auto& r = *current;
    const auto file = host.getCurrentShow();
    const bool there = file != juce::File() && file.getParentDirectory().isDirectory();
    // (Its file isn't as Kaleidobox left it: the Save As window, on that file, asks before replacing it.)
    const bool confirm = there && ! r.saveAsOnly && host.confirmBeforeSaving (file);
    if (! r.saveAsOnly && there && ! confirm)
        return saveTo (file);

    const auto folder = host.showsFolder();
    folder.createDirectory();
    const auto suggested = confirm ? file : file != juce::File() ? folder.getChildFile (file.getFileName()) : folder.getChildFile ("My show.crateshow");
    asker->chooseSaveFile (suggested, guard<const juce::File&> ([this] (const juce::File& picked)
    {
        if (picked == juce::File())
        {
            ++stats.saveAsCancelled;
            if (current->kind != Kind::saveOnly)
                host.say (showName() + " wasn't saved, so it's still open.");
            return end();
        }
        const auto target = picked.withFileExtension ("crateshow");
        if (host.reserved (target))
        {
            ++stats.failedSaves;
            host.say ("\"" + target.getFileNameWithoutExtension() + "\" is the one Kaleidobox keeps for you, so nothing was saved"
                      + (current->kind != Kind::saveOnly ? " or replaced" : "") + ". Pick another name.");
            return end();
        }
        // The window asked before replacing what you picked, not this one (the name had no
        // .crateshow): never replace a show it didn't ask about.
        if (target != picked && target.exists())
        {
            ++stats.failedSaves;
            host.say (target.getFileName() + " is there already, so nothing was saved" + (current->kind != Kind::saveOnly ? " or replaced" : "")
                      + ". Pick another name.");
            return end();
        }
        saveTo (target);
    }));
}

void RigDocFlow::saveTo (const juce::File& file)
{
    auto& r = *current;
    host.documentSaving (file);
    const auto result = engine.saveShowFile (file, rigshow::SaveKind::document);
    if (! result.ok)
    {
        ++stats.failedSaves;
        host.say ("Couldn't save " + file.getFileName() + ": " + result.error
                  + (r.kind == Kind::quit ? " Kaleidobox is still open." : r.kind != Kind::saveOnly ? " Nothing was replaced." : ""));
        asker->showError ("Couldn't save " + file.getFileNameWithoutExtension(), result.error);
        return end();
    }
    ++stats.saves;
    host.documentSaved (file, result);
    host.setCurrentShow (file);
    r.startShow = file;
    host.say (savedMessage (engine, file, result));
    if (r.kind == Kind::saveOnly)
        return end();
    r.justSaved = true;
    r.savedName = file.getFileNameWithoutExtension();
    // Saved over the show it's opening: open what's in that file now.
    if (r.kind == Kind::open && r.file == file && ! read (r))
        return end();
    decide();   // again: did anything change since?
}

// Step 5: the show is replaced, here and now.
void RigDocFlow::replaceNow (bool discarding)
{
    auto r = std::move (current);
    ++serial;   // (anything still on its way for it is stale)
    Outcome out;
    out.saved = r->savedName;
    out.discarded = discarding;
    auto& transport = engine.getTransport();
    switch (r->kind)
    {
        case Kind::blank:
            if (! r->clearWhenBlank && host.getCurrentShow() == juce::File() && engine.isBlank())
                break;   // nothing made yet: left as it is
            transport.stop();
            engine.allNotesOff();
            engine.newShow();
            host.setCurrentShow (juce::File());
            out.cleared = true;
            host.replaced (Host::Replaced::blank);
            break;
        case Kind::open:
            engine.loadShow (r->tree);
            engine.readLoopAudio (r->file);
            host.setCurrentShow (host.reserved (r->file) ? juce::File() : r->file);   // (the Last show opens untitled)
            out.file = r->file;
            host.replaced (Host::Replaced::opened);
            if (engine.getLoopAudioProblem().isNotEmpty())
                host.say (engine.getLoopAudioProblem());
            break;
        case Kind::import:
            transport.stop();
            engine.allNotesOff();
            engine.newShow();
            host.setCurrentShow (juce::File());
            out.cleared = true;
            out.said = rigimport::apply (engine, std::move (*r->song));
            host.replaced (Host::Replaced::imported);
            break;
        case Kind::quit:
            ++stats.quits;   // nothing replaced: the app quits now
            if (r->then)
                r->then (out);
            return;
        case Kind::saveOnly:
        default:
            return;
    }
    ++stats.replacements;
    if (r->then)
        r->then (out);
}
