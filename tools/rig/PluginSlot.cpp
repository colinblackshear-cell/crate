#include "PluginSlot.h"

namespace
{
bool byMakerThenName (const juce::PluginDescription& a, const juce::PluginDescription& b)
{
    const int maker = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
    return maker != 0 ? maker < 0 : a.name.compareIgnoreCase (b.name) < 0;
}

juce::File crateDataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Crate");
}

// The plugin's own window, in a plain frame.
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& p, std::function<void()> closed)
        : DocumentWindow (p.getName(), juce::Colour (0xff1e1a16), DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        if (auto* editor = p.createEditorIfNeeded())
            setContentOwned (editor, true);
        else
            setContentOwned (new juce::GenericAudioProcessorEditor (p), true);
        setResizable (getContentComponent() != nullptr && dynamic_cast<juce::GenericAudioProcessorEditor*> (getContentComponent()) != nullptr, false);
        centreWithSize (juce::jmax (300, getWidth()), juce::jmax (200, getHeight()));
        setVisible (true);
        toFront (true);
    }

    void closeButtonPressed() override
    {
        if (onClosed)
            onClosed();   // deletes this
    }

private:
    std::function<void()> onClosed;
};
}

//==============================================================================
PluginHost::PluginHost()
{
#if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat (new juce::AudioUnitPluginFormat());
#endif
#if JUCE_PLUGINHOST_VST3
    formats.addFormat (new juce::VST3PluginFormat());
#endif
    if (auto xml = juce::XmlDocument::parse (listFile()))
    {
        list.recreateFromXml (*xml);
        scannedOnce = true;
    }
    // Still written down from last time: those were opening when Kaleidobox stopped.
    if (openingFile().existsAsFile())
    {
        frozeWhileOpening.addLines (openingFile().loadFileAsString());
        frozeWhileOpening.removeEmptyStrings();
        openingFile().deleteFile();
    }
}

PluginHost::~PluginHost()
{
    *alive = false;
    stopTimer();
    helperScan.reset();
}

juce::File PluginHost::listFile()
{
    return crateDataFolder().getChildFile ("Rig Plugins.xml");
}

namespace
{
// Each plugin once: where it's installed as an Audio Unit and as a VST3 (as Arturia's are), the
// Audio Unit (the one Logic uses too). Shows saved with the VST3 still open it.
std::vector<juce::PluginDescription> onePerPlugin (const juce::Array<juce::PluginDescription>& types,
                                                   std::function<bool (const juce::PluginDescription&)> wanted)
{
    auto key = [] (const juce::PluginDescription& d) { return (d.manufacturerName + "\n" + d.name).toLowerCase(); };
    juce::StringArray audioUnits;
    for (const auto& d : types)
        if (wanted (d) && d.pluginFormatName == "AudioUnit")
            audioUnits.add (key (d));
    std::vector<juce::PluginDescription> out;
    for (const auto& d : types)
        if (wanted (d) && ! (d.pluginFormatName == "VST3" && audioUnits.contains (key (d))))
            out.push_back (d);
    std::sort (out.begin(), out.end(), byMakerThenName);
    return out;
}
}

std::vector<juce::PluginDescription> PluginHost::instruments() const
{
    return onePerPlugin (list.getTypes(), [] (const juce::PluginDescription& d) { return d.isInstrument; });
}

std::vector<juce::PluginDescription> PluginHost::effects() const
{
    return onePerPlugin (list.getTypes(), [] (const juce::PluginDescription& d) { return ! d.isInstrument && d.numOutputChannels > 0; });
}

//==============================================================================
// Looking at plugins in the helper.
struct PluginHost::Outcome
{
    Looked kind = Looked::crashed;
    size_t job = 0;
    juce::OwnedArray<juce::PluginDescription> types;
};

class PluginHost::HelperScan : public juce::Thread
{
public:
    HelperScan (PluginHost& h, std::vector<Job> j) : juce::Thread ("Kaleidobox plugin scan"), host (h), jobs (std::move (j))
    {
        for (const auto& job : jobs)
            formatNames.add (host.formats.getFormat (job.format)->getName());
    }
    ~HelperScan() override { stopThread (5000); }

    void run() override
    {
        for (size_t i = 0; i < jobs.size() && ! threadShouldExit(); ++i)
        {
            say (jobs[i].name);
            auto outcome = std::make_shared<Outcome>();
            outcome->job = i;
            outcome->kind = lookInHelper (formatNames[(int) i], jobs[i].id, outcome->types, [this] { return threadShouldExit(); });
            if (outcome->kind == Looked::cancelled)
                return;
            juce::MessageManager::callAsync ([&h = host, weak = std::weak_ptr<bool> (host.alive), outcome]
                                             {
                                                 if (! weak.expired())
                                                     h.finished (*outcome);
                                             });
        }
        if (! threadShouldExit())
            juce::MessageManager::callAsync ([&h = host, weak = std::weak_ptr<bool> (host.alive)]
                                             {
                                                 if (! weak.expired())
                                                     h.allDone();
                                             });
    }

private:
    void say (const juce::String& name)
    {
        juce::MessageManager::callAsync ([&h = host, weak = std::weak_ptr<bool> (host.alive), name]
                                         {
                                             if (! weak.expired() && h.onProgress)
                                                 h.onProgress (name, false);
                                         });
    }

    PluginHost& host;
    const std::vector<Job> jobs;
    juce::StringArray formatNames;
};

PluginHost::Looked PluginHost::lookInHelper (const juce::String& formatName, const juce::String& id,
                                            juce::OwnedArray<juce::PluginDescription>& found, std::function<bool()> cancelled)
{
    const auto out = juce::File::createTempFile (".kaleidobox-scan.xml");
    juce::ChildProcess helper;
    if (! helper.start (juce::StringArray { scanHelper.getFullPathName(), kScanArgument, formatName, out.getFullPathName(), id }, 0))
        return Looked::crashed;
    const auto started = juce::Time::getMillisecondCounter();
    while (helper.isRunning())
    {
        const bool stop = cancelled != nullptr && cancelled();
        if (stop || juce::Time::getMillisecondCounter() - started > (juce::uint32) scanTimeoutMs)
        {
            helper.kill();
            out.deleteFile();
            return stop ? Looked::cancelled : Looked::froze;
        }
        juce::Thread::sleep (50);
    }
    // What it found is only believed when the file is whole (a plugin that crashes on the way
    // out, after it was looked at, still counts as looked at).
    auto xml = juce::XmlDocument::parse (out);
    out.deleteFile();
    if (xml == nullptr || ! xml->hasTagName ("KaleidoboxScan"))
        return Looked::crashed;
    for (auto* e : xml->getChildIterator())
    {
        auto d = std::make_unique<juce::PluginDescription>();
        if (d->loadFromXml (*e))
            found.add (d.release());
    }
    return Looked::found;
}

bool PluginHost::isScanHelperLaunch (const juce::StringArray& commandLine)
{
    return commandLine.contains (kScanArgument);
}

bool PluginHost::runScanHelper (const juce::StringArray& commandLine)
{
    const int at = commandLine.indexOf (kScanArgument);
    if (at < 0 || at + 3 >= commandLine.size())
        return false;
   #if JUCE_MAC
    juce::Process::setDockIconVisible (false);
   #endif
    // Made here, on the message thread, and looked at from a thread of its own while this
    // thread keeps running: plugins are opened on the message thread, and some Audio Units
    // need it free while they open. Nothing is cleaned up: when the file is written, the helper
    // simply ends (a plugin that crashes while it's closed can't spoil what was found).
    struct Helper : juce::Thread
    {
        Helper (juce::String f, juce::File o, juce::String i) : juce::Thread ("Kaleidobox plugin scan helper"), formatName (f), out (o), id (i)
        {
           #if JUCE_PLUGINHOST_AU && JUCE_MAC
            formats.addFormat (new juce::AudioUnitPluginFormat());
           #endif
           #if JUCE_PLUGINHOST_VST3
            formats.addFormat (new juce::VST3PluginFormat());
           #endif
        }
        void run() override
        {
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* format : formats.getFormats())
                if (format->getName() == formatName)
                    format->findAllTypesForFile (found, id);
            juce::XmlElement xml ("KaleidoboxScan");
            for (auto* d : found)
                if (auto e = d->createXml())
                    xml.addChildElement (e.release());
            const bool written = xml.writeTo (out);
            std::_Exit (written ? 0 : 2);
        }
        juce::AudioPluginFormatManager formats;
        const juce::String formatName;
        const juce::File out;
        const juce::String id;
    };
    auto* helper = new Helper (commandLine[at + 1], juce::File (commandLine[at + 2]), commandLine[at + 3]);
    helper->startThread();
    return true;
}

void PluginHost::finished (const Outcome& outcome)
{
    if (outcome.job >= jobs.size())
        return;
    const auto& job = jobs[outcome.job];
    if (outcome.kind == Looked::found)
    {
        list.removeFromBlacklist (job.id);
        for (auto* d : outcome.types)
            list.addType (*d);
    }
    else if (outcome.kind != Looked::cancelled)
    {
        // Left out of the menus; tried again at the next scan.
        list.addToBlacklist (job.id);
        skippedThisScan.add (job.name + (outcome.kind == Looked::froze ? " (it froze)" : " (it crashed)"));
    }
}

void PluginHost::allDone()
{
    helperScan.reset();
    jobs.clear();
    scanning = false;
    scannedOnce = true;
    save();
    if (onProgress)
    {
        auto text = "Found " + juce::String (list.getNumTypes()) + " plugins";
        if (! skippedThisScan.isEmpty())
            text << ". Skipped " << skippedThisScan.joinIntoString (", ") << ": Kaleidobox stayed safe, and tries "
                 << (skippedThisScan.size() == 1 ? "it" : "them") << " again next scan";
        onProgress (text, true);
    }
}

juce::File PluginHost::openingFile()
{
    return crateDataFolder().getChildFile ("Rig Plugin Opening.txt");
}

void PluginHost::opening (const juce::PluginDescription& d, bool starting)
{
    const auto id = d.createIdentifierString();
    if (starting)
        openingNow.add (id);
    else
        openingNow.removeString (id);   // (one of them, when the same plugin opens twice at once)
    if (starting)
        frozeWhileOpening.removeString (id);   // picked again by hand: it gets another go
    crateDataFolder().createDirectory();
    if (openingNow.isEmpty())
        openingFile().deleteFile();
    else
        openingFile().replaceWithText (openingNow.joinIntoString ("\n"));
}

bool PluginHost::frozeLastTime (const juce::PluginDescription& d) const
{
    return frozeWhileOpening.contains (d.createIdentifierString());
}

juce::StringArray PluginHost::takeLeftClosed()
{
    auto names = closedAtOpen;
    closedAtOpen.clear();
    return names;
}

void PluginHost::startScan (std::function<void (const juce::String&, bool)> progress)
{
    onProgress = std::move (progress);
    if (formats.getNumFormats() == 0)
    {
        scannedOnce = true;
        if (onProgress)
            onProgress ("No plugin formats on this computer", true);
        return;
    }
    scanning = true;
    formatIndex = 0;
    scanner.reset();
    skippedThisScan.clear();
    if (scanHelper == juce::File())
    {
        startTimer (10);   // in here, a few at a time
        return;
    }
    // In the helper: what to look at (what's new or changed, and what was skipped before), then
    // one at a time on a thread of its own, so the window never waits.
    jobs.clear();
    for (int f = 0; f < formats.getNumFormats(); ++f)
    {
        auto* format = formats.getFormat (f);
        for (const auto& id : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
            if (list.getBlacklistedFiles().contains (id) || ! list.isListingUpToDate (id, *format))
            {
                auto name = format->getNameOfPluginFromIdentifier (id);
                if (name.isEmpty() || name == id)
                    name = juce::File::createFileWithoutCheckingPath (id).getFileNameWithoutExtension();
                jobs.push_back ({ f, id, name.isNotEmpty() ? name : id });
            }
    }
    helperScan = std::make_unique<HelperScan> (*this, jobs);
    helperScan->startThread();
}

void PluginHost::timerCallback()
{
    if (scanner == nullptr)
    {
        if (formatIndex >= formats.getNumFormats())
        {
            stopTimer();
            scanning = false;
            scannedOnce = true;
            save();
            if (onProgress)
                onProgress ("Found " + juce::String (list.getNumTypes()) + " plugins", true);
            return;
        }
        auto* format = formats.getFormat (formatIndex);
        // The file being looked at is written down first, so a plugin that crashes the scan is
        // skipped the next time.
        scanner = std::make_unique<juce::PluginDirectoryScanner> (list, *format, format->getDefaultLocationsToSearch(), true,
                                                                  crateDataFolder().getChildFile ("Rig Plugin Scan.txt"), true);
        crateDataFolder().createDirectory();
    }
    juce::String name;
    const auto next = scanner->getNextPluginFileThatWillBeScanned();
    const bool more = scanner->scanNextFile (true, name);
    if (onProgress)
        onProgress (juce::File::createFileWithoutCheckingPath (next).getFileNameWithoutExtension().isNotEmpty()
                        ? juce::File::createFileWithoutCheckingPath (next).getFileNameWithoutExtension() : next,
                    false);
    if (! more)
    {
        scanner.reset();
        ++formatIndex;
    }
}

void PluginHost::save()
{
    crateDataFolder().createDirectory();
    if (auto xml = list.createXml())
        xml->writeTo (listFile());
}

//==============================================================================
PluginSlot::PluginSlot (bool instrument) : isInstrument (instrument) { macros.fill (-1); }

PluginSlot::~PluginSlot()
{
    *alive = false;
    closeWindow();
    releaseEmbeddedEditor();
}

void PluginSlot::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    block = juce::jmax (16, maxBlock);
    const juce::SpinLock::ScopedLockType sl (lock);
    if (instance != nullptr)
    {
        instance->releaseResources();
        instance->prepareToPlay (rate, block);
        scratch.setSize (juce::jmax (2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels()), block, false, true, true);
    }
}

void PluginSlot::setPlayHead (juce::AudioPlayHead* p)
{
    playHead = p;
    if (instance != nullptr)
        instance->setPlayHead (p);
}

void PluginSlot::load (const juce::PluginDescription& description, std::function<void (const juce::String&)> done)
{
    // Written down while it opens: if it freezes or crashes Kaleidobox now, the next launch knows.
    host->opening (description, true);
    host->formats.createPluginInstanceAsync (description, rate, block,
                                            [this, weak = std::weak_ptr<bool> (alive), done, description] (std::unique_ptr<juce::AudioPluginInstance> made,
                                                                                                            const juce::String& error)
                                            {
                                                if (weak.expired())
                                                {
                                                    juce::SharedResourcePointer<PluginHost>()->opening (description, false);
                                                    return;
                                                }
                                                host->opening (description, false);
                                                if (made != nullptr)
                                                    setInstance (std::move (made));
                                                if (done)
                                                    done (made == nullptr && error.isEmpty() ? juce::String ("It didn't load") : error);
                                            });
}

void PluginSlot::setInstance (std::unique_ptr<juce::AudioPluginInstance> made)
{
    leftClosed = {};
    closeWindow();
    releaseEmbeddedEditor();
    if (made != nullptr)
    {
        // Stereo where it can be; whatever it has where it can't.
        made->enableAllBuses();
        made->setPlayHead (playHead);
        made->prepareToPlay (rate, block);
    }
    juce::AudioBuffer<float> newScratch (made != nullptr ? juce::jmax (2, made->getTotalNumInputChannels(), made->getTotalNumOutputChannels()) : 2, block);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (instance, made);
        std::swap (scratch, newScratch);
        loaded = instance != nullptr;
    }
    if (made != nullptr)
        made->releaseResources();
    made.reset();   // the old one, outside the lock
    // A new plugin gets its knobs mapped (a show's own mapping comes back after, in fromTree).
    macros.fill (-1);
    if (instance != nullptr)
        autoMapMacros();
    if (onChange)
        onChange();
}

void PluginSlot::clear()
{
    setInstance (nullptr);
}

juce::String PluginSlot::getName() const
{
    return instance != nullptr ? instance->getName() : juce::String();
}

void PluginSlot::showWindow()
{
    if (instance == nullptr)
        return;
    if (window != nullptr)
    {
        window->toFront (true);
        return;
    }
    // Its editor can only be in one place: out of the page, into the window.
    releaseEmbeddedEditor();
    window = std::make_unique<PluginWindow> (*instance, [this] { juce::MessageManager::callAsync ([this, weak = std::weak_ptr<bool> (alive)]
                                                                                                  {
                                                                                                      if (! weak.expired())
                                                                                                          closeWindow();
                                                                                                  }); });
}

void PluginSlot::closeWindow()
{
    window.reset();
}

juce::Component* PluginSlot::getEmbeddedEditor()
{
    if (instance == nullptr)
        return nullptr;
    if (embedded == nullptr)
    {
        closeWindow();
        if (auto* editor = instance->hasEditor() ? instance->createEditorIfNeeded() : nullptr)
            embedded.reset (editor);
        else
            embedded = std::make_unique<juce::GenericAudioProcessorEditor> (*instance);
        if (auto* generic = dynamic_cast<juce::GenericAudioProcessorEditor*> (embedded.get()))
            generic->setSize (juce::jmax (generic->getWidth(), 520), juce::jmax (generic->getHeight(), 300));
    }
    return embedded.get();
}

void PluginSlot::releaseEmbeddedEditor()
{
    embedded.reset();
}

int PluginSlot::numPrograms() const { return instance != nullptr ? instance->getNumPrograms() : 0; }
int PluginSlot::currentProgram() const { return instance != nullptr ? instance->getCurrentProgram() : 0; }
juce::String PluginSlot::programName (int i) const { return instance != nullptr ? instance->getProgramName (i) : juce::String(); }

void PluginSlot::setProgram (int i)
{
    if (instance != nullptr && juce::isPositiveAndBelow (i, instance->getNumPrograms()))
        instance->setCurrentProgram (i);
}

juce::ValueTree PluginSlot::toTree() const
{
    if (instance == nullptr && leftClosed.isValid())
        return leftClosed.createCopy();   // as the show had it, settings and all
    juce::ValueTree tree ("Plugin");
    if (instance == nullptr)
        return tree;
    const auto description = instance->getPluginDescription();
    tree.setProperty ("id", description.createIdentifierString(), nullptr);
    tree.setProperty ("name", description.name, nullptr);
    if (auto xml = description.createXml())
        tree.setProperty ("description", xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);
    juce::MemoryBlock state;
    instance->getStateInformation (state);
    tree.setProperty ("state", state.toBase64Encoding(), nullptr);
    tree.setProperty ("bypassed", bypassed.load(), nullptr);
    tree.setProperty ("level", level.load(), nullptr);
    juce::StringArray knobs;
    for (int m : macros)
        knobs.add (juce::String (m));
    tree.setProperty ("macros", knobs.joinIntoString (" "), nullptr);
    return tree;
}

void PluginSlot::fromTree (const juce::ValueTree& tree)
{
    if (! tree.isValid() || ! tree.hasProperty ("description"))
    {
        clear();
        return;
    }
    juce::PluginDescription description;
    if (auto xml = juce::XmlDocument::parse (tree.getProperty ("description").toString()); xml == nullptr || ! description.loadFromXml (*xml))
        return;
    if (host->frozeLastTime (description))
    {
        // It froze or crashed Kaleidobox last time it opened: left closed (the show keeps it, and
        // picking it again by hand opens it).
        clear();
        leftClosed = tree.createCopy();
        host->leftClosed (description.name);
        return;
    }
    bypassed = (bool) tree.getProperty ("bypassed", false);
    level = juce::jlimit (0.0f, 2.0f, (float) tree.getProperty ("level", 1.0f));
    const auto stateText = tree.getProperty ("state").toString();
    const auto knobs = tree.getProperty ("macros").toString();
    load (description, [this, stateText, knobs, weak = std::weak_ptr<bool> (alive)] (const juce::String& error)
          {
              if (weak.expired() || error.isNotEmpty() || instance == nullptr)
                  return;
              juce::MemoryBlock state;
              if (state.fromBase64Encoding (stateText) && ! state.isEmpty())
                  instance->setStateInformation (state.getData(), (int) state.getSize());
              if (knobs.isNotEmpty())
              {
                  const auto list = juce::StringArray::fromTokens (knobs, " ", {});
                  for (int i = 0; i < kMacros; ++i)
                      setMacro (i, i < list.size() ? list[i].getIntValue() : -1);
                  if (onChange)
                      onChange();
              }
          });
}

bool PluginSlot::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (! loaded.load() || bypassed.load())
        return false;
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || instance == nullptr)
        return false;
    const int n = buffer.getNumSamples();
    if (n > scratch.getNumSamples())
        return false;
    juce::AudioBuffer<float> work (scratch.getArrayOfWritePointers(), scratch.getNumChannels(), n);
    work.clear();
    const int ins = instance->getTotalNumInputChannels(), outs = instance->getTotalNumOutputChannels();
    if (! isInstrument)
        for (int ch = 0; ch < juce::jmin (ins, work.getNumChannels()); ++ch)
            work.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, n);
    instance->processBlock (work, isInstrument ? midi : noMidi);
    if (outs <= 0)
        return false;
    const float gain = level.load();
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        buffer.copyFrom (ch, 0, work, juce::jmin (ch, outs - 1), 0, n);
        if (gain != 1.0f)
            buffer.applyGain (ch, 0, n, gain);
    }
    return true;
}

int PluginSlot::getMacro (int knob) const
{
    return juce::isPositiveAndBelow (knob, kMacros) ? macros[(size_t) knob] : -1;
}

void PluginSlot::setMacro (int knob, int parameterIndex)
{
    if (! juce::isPositiveAndBelow (knob, kMacros))
        return;
    const int count = instance != nullptr ? instance->getParameters().size() : 0;
    macros[(size_t) knob] = juce::isPositiveAndBelow (parameterIndex, count) ? parameterIndex : -1;
}

juce::AudioProcessorParameter* PluginSlot::macroParameter (int knob) const
{
    const int index = getMacro (knob);
    if (instance == nullptr || index < 0)
        return nullptr;
    const auto& list = instance->getParameters();
    return juce::isPositiveAndBelow (index, list.size()) ? list[index] : nullptr;
}

void PluginSlot::autoMapMacros()
{
    macros.fill (-1);
    if (instance == nullptr)
        return;
    const auto& list = instance->getParameters();
    // Words to look for, best first. Arturia's V Collection and Pigments name their four
    // macros Brightness, Timbre, Time and Movement.
    static const char* const wanted[] = { "brightness", "timbre", "time", "movement", "cutoff", "resonance", "attack", "release",
                                          "decay", "sustain", "drive", "mix", "macro 1", "macro 2", "macro 3", "macro 4" };
    std::vector<int> picked;
    auto usable = [&] (int i)
    {
        auto* p = list[i];
        return p != nullptr && p->isAutomatable() && ! p->isMetaParameter()
               && std::find (picked.begin(), picked.end(), i) == picked.end();
    };
    for (auto* word : wanted)
    {
        if ((int) picked.size() >= kMacros)
            break;
        for (int i = 0; i < list.size(); ++i)
            if (usable (i))
            {
                const auto name = list[i]->getName (64).toLowerCase();
                if (name == word || name.startsWith (juce::String (word) + " ") || name.endsWith (" " + juce::String (word))
                    || (juce::String (word).length() > 4 && name.contains (word)))
                {
                    picked.push_back (i);
                    break;
                }
            }
    }
    // Then simply the first ones it has.
    for (int i = 0; i < list.size() && (int) picked.size() < kMacros; ++i)
        if (usable (i) && list[i]->getName (64).isNotEmpty())
            picked.push_back (i);
    for (size_t k = 0; k < picked.size() && k < (size_t) kMacros; ++k)
        macros[k] = picked[k];
}

//==============================================================================
void PluginMenu::addPlugins (juce::PopupMenu& menu, const std::vector<juce::PluginDescription>& plugins, int firstId)
{
    juce::String maker;
    juce::PopupMenu sub;
    auto flush = [&]
    {
        if (sub.getNumItems() > 0)
            menu.addSubMenu (maker.isEmpty() ? juce::String ("Other") : maker, sub);
        sub = {};
    };
    for (int i = 0; i < (int) plugins.size(); ++i)
    {
        const auto& d = plugins[(size_t) i];
        if (d.manufacturerName != maker)
        {
            flush();
            maker = d.manufacturerName;
        }
        sub.addItem (firstId + i, d.name);
    }
    flush();
}

void PluginMenu::scan (std::function<void (const juce::String&)> said, std::function<void()> then)
{
    juce::SharedResourcePointer<PluginHost> host;
    if (host->isScanning())
        return;
    host->startScan ([said, then] (const juce::String& now, bool done)
                     {
                         if (said)
                             said (done ? now + ". Pick one from the menu." : "Looking for plugins: " + now);
                         if (done && then)
                             then();
                     });
}

void PluginMenu::showForEffect (PluginSlot& slot, juce::Component& target, std::function<void (const juce::String&)> said)
{
    juce::SharedResourcePointer<PluginHost> host;
    const auto plugins = host->effects();
    juce::PopupMenu menu;
    menu.addItem (1, "None", true, ! slot.isLoaded());
    if (slot.isLoaded())
    {
        menu.addItem (2, "Open " + slot.getName() + "'s window");
        menu.addItem (3, "Bypass " + slot.getName(), true, slot.bypassed.load());
    }
    menu.addSeparator();
    if (plugins.empty())
        menu.addItem (-1, host->hasScanned() ? "No effect plugins found" : "Your plugins show here after a scan", false, false);
    else
        addPlugins (menu, plugins, 100);
    menu.addSeparator();
    menu.addItem (4, host->hasScanned() ? "Scan for plugins again" : "Scan for plugins (Quake, Valhalla...)");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                        [&slot, plugins, said, safe = juce::Component::SafePointer<juce::Component> (&target)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1)
                                slot.clear();
                            else if (result == 2)
                                slot.showWindow();
                            else if (result == 3)
                                slot.bypassed = ! slot.bypassed.load();
                            else if (result == 4)
                                scan (said, {});
                            else if (result >= 100)
                            {
                                const auto d = plugins[(size_t) (result - 100)];
                                if (said)
                                    said ("Loading " + d.name + "...");
                                slot.load (d, [&slot, said, name = d.name] (const juce::String& error)
                                           {
                                               if (said)
                                                   said (error.isEmpty() ? name + " is on. Its window is open." : name + " didn't load: " + error);
                                               if (error.isEmpty())
                                                   slot.showWindow();
                                           });
                            }
                        });
}
