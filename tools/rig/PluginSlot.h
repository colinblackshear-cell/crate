#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Other makers' plugins in the Rig: Audio Units (and VST3) installed on the computer, such as
// Arturia's synths or Cymatics' effects.
//
// PluginHost finds them (a scan, once, kept in a file so the next start is instant) and makes
// them. PluginSlot holds one: an instrument the Bass or Synth track plays instead of its own
// sound, or an effect at the end of a strip's or a loop layer's effects. The audio thread
// never waits for the window: a slot that's being changed is skipped for that block.
class PluginHost : private juce::Timer
{
public:
    PluginHost();
    ~PluginHost() override;

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList list;

    // What's installed, by maker then name.
    std::vector<juce::PluginDescription> instruments() const;
    std::vector<juce::PluginDescription> effects() const;

    // Looks for plugins, a few at a time (the window stays live). `progress` hears what's
    // being looked at and, at the end, done = true.
    void startScan (std::function<void (const juce::String& now, bool done)> progress);
    bool isScanning() const { return scanning; }
    bool hasScanned() const { return list.getNumTypes() > 0 || scannedOnce; }

    static juce::File listFile();

private:
    void timerCallback() override;
    void save();

    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    int formatIndex = 0;
    bool scanning = false, scannedOnce = false;
    std::function<void (const juce::String&, bool)> onProgress;

    JUCE_DECLARE_NON_COPYABLE (PluginHost)
};

class PluginSlot
{
public:
    explicit PluginSlot (bool instrument);
    bool isInstrumentSlot() const { return isInstrument; }
    ~PluginSlot();

    // Message thread.
    void prepare (double sampleRate, int maxBlock);
    void setPlayHead (juce::AudioPlayHead*);
    // Makes the plugin (it can take a moment) and puts it in. `done` hears an error, or "".
    void load (const juce::PluginDescription&, std::function<void (const juce::String& error)> done = {});
    void setInstance (std::unique_ptr<juce::AudioPluginInstance>);
    void clear();
    bool isLoaded() const { return loaded.load(); }
    // Loaded and not switched off: it's what you hear.
    bool isPlaying() const { return loaded.load() && ! bypassed.load(); }
    juce::String getName() const;
    juce::AudioPluginInstance* getInstance() const { return instance.get(); }

    // The plugin's own window.
    void showWindow();
    void closeWindow();
    bool isWindowOpen() const { return window != nullptr; }
    // Its own screen inside a Kaleidobox page instead (the Bass or Synth's Edit view). There's one
    // at a time: showing the window takes it out of the page, and this closes the window. The slot
    // owns it (the page only shows it); it goes when the plugin changes. Null when nothing's loaded.
    juce::Component* getEmbeddedEditor();
    void releaseEmbeddedEditor();
    bool isEmbedded() const { return embedded != nullptr; }

    // Its sounds (the plugin's programs), for the Rig's sound picker.
    int numPrograms() const;
    int currentProgram() const;
    juce::String programName (int) const;
    void setProgram (int);

    // Saved with the show: which plugin and its settings.
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);

    // Audio thread. An instrument replaces `buffer` with what it plays for `midi`; an effect
    // works on `buffer` in place. False (and nothing done) when empty, bypassed or busy.
    bool process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    std::atomic<bool> bypassed { false };
    // The plugin's level in the Rig (0..2, 1 = as it comes), saved with the show.
    std::atomic<float> level { 1.0f };

    // The instrument shell's 8 macro knobs: each turns one of the plugin's settings (its index
    // in getParameters(), or -1 for none). Saved with the show. Message thread.
    static constexpr int kMacros = 8;
    int getMacro (int knob) const;
    void setMacro (int knob, int parameterIndex);
    juce::AudioProcessorParameter* macroParameter (int knob) const;
    // Puts the plugin's most useful settings on the knobs: Arturia's own macros (Brightness,
    // Timbre, Time, Movement) and the usual cutoff, resonance, attack, release... first.
    void autoMapMacros();

    std::function<void()> onChange;   // loaded, cleared (message thread)

private:
    const bool isInstrument;
    juce::SharedResourcePointer<PluginHost> host;
    juce::SpinLock lock;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::atomic<bool> loaded { false };
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer noMidi;
    juce::AudioPlayHead* playHead = nullptr;
    double rate = 44100.0;
    int block = 512;
    std::unique_ptr<juce::DocumentWindow> window;
    std::unique_ptr<juce::AudioProcessorEditor> embedded;
    std::array<int, kMacros> macros;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE (PluginSlot)
};

// Menus for picking a plugin: the installed ones by maker, as submenus.
namespace PluginMenu
{
// Adds `plugins` to `menu` as one submenu per maker, ids from firstId (firstId + index).
void addPlugins (juce::PopupMenu& menu, const std::vector<juce::PluginDescription>& plugins, int firstId);

// The whole menu for an effect slot: None, Open, Bypass, the effects, Scan. `said` hears what
// happened, for the Rig's message line.
void showForEffect (PluginSlot&, juce::Component& target, std::function<void (const juce::String&)> said);

// Scans, telling `said` as it goes, then calls `then`.
void scan (std::function<void (const juce::String&)> said, std::function<void()> then);
}
