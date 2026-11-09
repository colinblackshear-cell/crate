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
}

PluginHost::~PluginHost()
{
    stopTimer();
}

juce::File PluginHost::listFile()
{
    return crateDataFolder().getChildFile ("Rig Plugins.xml");
}

std::vector<juce::PluginDescription> PluginHost::instruments() const
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : list.getTypes())
        if (d.isInstrument)
            out.push_back (d);
    std::sort (out.begin(), out.end(), byMakerThenName);
    return out;
}

std::vector<juce::PluginDescription> PluginHost::effects() const
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : list.getTypes())
        if (! d.isInstrument && d.numOutputChannels > 0)
            out.push_back (d);
    std::sort (out.begin(), out.end(), byMakerThenName);
    return out;
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
    startTimer (10);
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
PluginSlot::PluginSlot (bool instrument) : isInstrument (instrument) {}

PluginSlot::~PluginSlot()
{
    *alive = false;
    closeWindow();
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
    host->formats.createPluginInstanceAsync (description, rate, block,
                                            [this, weak = std::weak_ptr<bool> (alive), done] (std::unique_ptr<juce::AudioPluginInstance> made,
                                                                                               const juce::String& error)
                                            {
                                                if (weak.expired())
                                                    return;
                                                if (made != nullptr)
                                                    setInstance (std::move (made));
                                                if (done)
                                                    done (made == nullptr && error.isEmpty() ? juce::String ("It didn't load") : error);
                                            });
}

void PluginSlot::setInstance (std::unique_ptr<juce::AudioPluginInstance> made)
{
    closeWindow();
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
    bypassed = (bool) tree.getProperty ("bypassed", false);
    const auto stateText = tree.getProperty ("state").toString();
    load (description, [this, stateText, weak = std::weak_ptr<bool> (alive)] (const juce::String& error)
          {
              if (weak.expired() || error.isNotEmpty() || instance == nullptr)
                  return;
              juce::MemoryBlock state;
              if (state.fromBase64Encoding (stateText) && ! state.isEmpty())
                  instance->setStateInformation (state.getData(), (int) state.getSize());
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
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        buffer.copyFrom (ch, 0, work, juce::jmin (ch, outs - 1), 0, n);
    return true;
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
