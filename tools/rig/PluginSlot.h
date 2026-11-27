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

    // The standalone app looks at each plugin in a helper: itself, started again with
    // kScanArgument, so a plugin that freezes or crashes while it's being looked at takes only
    // the helper down (it's skipped, and tried again at the next scan), never the app. Main.cpp
    // sets scanHelper; without one (inside Logic, tests) the plugins are looked at in here.
    static inline juce::File scanHelper;
    static constexpr const char* kScanArgument = "--kaleidobox-scan-plugin";
    static inline int scanTimeoutMs = 60000;   // per plugin; Arturia's first open can be slow
    // True when this launch is the helper (Main.cpp: no window, no audio, a second copy is fine).
    static bool isScanHelperLaunch (const juce::StringArray& commandLine);
    // The helper's whole job: looks at one plugin file, writes what it found, exits. False when
    // the command line isn't the helper's.
    static bool runScanHelper (const juce::StringArray& commandLine);

    // Looks at one plugin in the helper and waits for it (not on the message thread): what it
    // found, or that it froze (it's stopped after scanTimeoutMs) or crashed.
    enum class Looked { found, froze, crashed, cancelled };
    static Looked lookInHelper (const juce::String& formatName, const juce::String& fileOrIdentifier,
                                juce::OwnedArray<juce::PluginDescription>& found, std::function<bool()> cancelled = {});

    // A plugin that was being opened when Kaleidobox last stopped (it froze or crashed while it
    // opened): a show doesn't open it again by itself. Message thread.
    void opening (const juce::PluginDescription&, bool starting);
    bool frozeLastTime (const juce::PluginDescription&) const;
    // The plugins a show left closed for that reason, for the launch to say so. Takes them.
    juce::StringArray takeLeftClosed();
    void leftClosed (const juce::String& name) { closedAtOpen.addIfNotAlreadyThere (name); }

private:
    struct Job { int format; juce::String id, name; };
    struct Outcome;
    class HelperScan;
    void timerCallback() override;
    void save();
    void finished (const Outcome&);
    void allDone();
    static juce::File openingFile();

    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    std::unique_ptr<HelperScan> helperScan;
    std::vector<Job> jobs;
    juce::StringArray skippedThisScan, frozeWhileOpening, openingNow, closedAtOpen;
    int formatIndex = 0;
    bool scanning = false, scannedOnce = false;
    std::function<void (const juce::String&, bool)> onProgress;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

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
    // Only the newest request lands: a later load, setInstance, clear() or fromTree drops a load
    // still on its way (its plugin is deleted when it comes, here on the message thread, and its
    // `done` isn't called). Until a plugin picked here is in (or if it doesn't load), the slot is
    // what it was.
    void load (const juce::PluginDescription&, std::function<void (const juce::String& error)> done = {});
    void setInstance (std::unique_ptr<juce::AudioPluginInstance>);
    // Takes the plugin out (Remove): the show no longer has one here.
    void clear();
    bool isLoaded() const { return loaded.load(); }
    // A plugin to save: the one loaded, or a show's own that isn't in: still loading, not
    // installed here, didn't open, or left closed after a freeze. Saved as the show had it (with
    // the slot's level and bypass as they are now, if you changed them since).
    bool hasPluginToSave() const { return loaded.load() || (kept.isValid() && ! oldSavingForTests); }
    // A plugin picked by hand that's still on its way (its name; empty when there's none).
    juce::String getNameLoading() const { return loadingByHand; }
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
    // What a save writes for this slot, in `tree`: the plugin as it is now, the show's own tree
    // (exactly, but for a level or bypass you changed meanwhile) while its plugin isn't in, or
    // nothing (an invalid tree) when there's no plugin.
    // False, with why in `problem`, when the plugin's can't be written whole (it gave no
    // settings where it had some, or they couldn't be read): the save must fail then. `tree` is
    // still the most there is (for a host, which can't be told no).
    bool treeForSave (juce::ValueTree& tree, juce::String& problem) const;

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
    // A show's own plugin (restored from its tree, never one picked by hand) going live: called
    // with false just before the slot reports it, and true just after (its state and knobs in).
    // Only for the load that lands (the newest request). Message thread.
    std::function<void (bool landed)> showPluginLanding;
    // The slot's level and bypass are still what the show's tree gave them (not changed while
    // its plugin loaded). Message thread.
    bool levelAndBypassAsShowHad() const { return juce::exactlyEqual (level.load(), restoredLevel) && bypassed.load() == restoredBypassed; }

    // Tests only, all off: each brings back one of the old faults, so the tests show they catch it.
    static inline bool oldLoadResultForTests = false;   // a load that worked says "It didn't load" (so a show's settings aren't put back)
    static inline bool oldSavingForTests = false;       // only a loaded plugin is saved
    static inline bool noGenerationForTests = false;    // a load on its way lands whatever was asked for after it
    static inline bool keptLevelOverlayOffForTests = false;   // a level or bypass changed while the show's plugin isn't in isn't saved

private:
    void startLoad (const juce::PluginDescription&, const juce::ValueTree& restore, bool byHand, std::function<void (const juce::String&)> done);
    bool liveTree (juce::ValueTree& tree, juce::String& problem) const;
    juce::ValueTree keptForSave() const;
    const bool isInstrument;
    juce::SharedResourcePointer<PluginHost> host;
    juce::SpinLock lock;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::atomic<bool> loaded { false };
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer noMidi;
    juce::ValueTree kept;   // a show's plugin that isn't in (loading, missing, didn't open, froze last time): saved as it is
    juce::uint64 generation = 0;   // the newest request: only its load lands (message thread)
    juce::Array<juce::PluginDescription> stillOpening;   // loads on their way (for the "opening" note)
    juce::String loadingByHand;
    float restoredLevel = 1.0f;     // what fromTree set level and bypass to
    bool restoredBypassed = false;
    mutable bool hasHadState = false;   // this plugin had settings (from the show, or saved): none now is a fault
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
