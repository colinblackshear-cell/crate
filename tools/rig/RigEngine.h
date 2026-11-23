#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorProcessor.h"
#include "MpcProcessor.h"
#include "PluginProcessor.h"   // the vocoder
#include "BassProcessor.h"
#include "SynthProcessor.h"
#include "DrumProcessor.h"
#include "DroneProcessor.h"
#include "RigRouting.h"
#include "RigTransport.h"
#include "StripFx.h"
#include "PerformFx.h"
#include "RigMacros.h"
#include "RigLooper.h"
#include "RigScenes.h"
#include "RigTimeline.h"
#include "RigLiveSong.h"
#include "RigRecorder.h"
#include "RigCapture.h"
#include "core/SoundSet.h"
#include "RigQuantize.h"
#include "RigChords.h"
#include "RigTakes.h"
#include "RigAutomation.h"
#include "RigShowFiles.h"
#include "RigSounds.h"
#include "RigDirty.h"
#include "core/SafetyLimiter.h"
#include "core/control/Controllers.h"

#include <array>

// The Rig's sound: the tools it plays live, run side by side in one program on one clock.
//
// Your audio interface's input is the vocoder's voice. The sampler records from the mic, the
// vocoder, or the Rig's mix (pick with setSampleSource), so you can sample what you're
// playing straight into it. Each tool plays into its own channel strip (level, mute, meter),
// and the strips are summed to the output. MIDI from every connected keyboard is split between the tools by RigRouting.
//
// The tools are the same code as their own apps and Logic plugins: the Rig is their host, and
// they follow its clock the way they follow Logic's.
class RigEngine : public juce::AudioIODeviceCallback,
                  public juce::MidiInputCallback
{
public:
    RigEngine();
    ~RigEngine() override;

    MpcProcessor& getMpc() { return mpc; }
    // Puts chopped slices (from the Sampler) on the MPC's pads in a new kit, from A01, and makes
    // it the kit that plays, so the drum pads play the slices straight away. Message thread.
    // Returns what to tell the user.
    juce::String sendSlicesToMpc (std::vector<crate::SharedAudio> slices, const juce::String& name);
    // The factory kits (the same five as Kaleidobox Pocket's, made in code: FactoryKits.h). Puts kit
    // `index` on the MPC's pads A01-A16 of kit `kit` (-1 = a new kit) and makes it the kit that plays.
    // A new Rig starts with the Street kit on its first kit, as the phone does. Returns what to say.
    juce::String loadFactoryKit (int index, int kit = -1);
    // The factory kit MPC kit `kit` came from (by its name, "Street kit"), or -1.
    int factoryKitOf (int kit);
    // The factory sound that pad `pad` (0..63) of the current kit had, when the kit is a factory kit
    // and that pad is one of its 16 ("Street kick"), or {}. hasFactoryPad: it's playing something else.
    juce::String factoryPadName (int pad);
    bool padDiffersFromFactory (int pad);
    // Puts that factory sound back on the pad (its settings back to the kit's). Returns what to say.
    juce::String restoreFactoryPad (int pad);
    // Puts all 16 of the current factory kit's sounds back, on the same kit. Returns what to say.
    juce::String resetFactoryKit();
    // Sets (core/SoundSet.h): bank 0..3 of the current kit (-1: the whole kit) as a set named `name`.
    SoundSet padsAsSet (int bank, const juce::String& name);
    // A whole-kit set goes on a new kit (yours stay); a one-bank set onto bank `bank` of the current kit
    // (its 16 pads are replaced). Returns what to say.
    juce::String loadSet (const SoundSet&, int bank);
    // Set by the app and the Logic plugin before they make their Rig: a new Rig (and New song) has
    // the Street kit on the pads. Off for the tests, whose Rigs start with empty pads.
    static inline std::atomic<bool> startWithFactoryKit { false };
    EditorProcessor& getSampler() { return sampler; }
    VocoderProcessor& getVocoder() { return vocoder; }
    BassProcessor& getBass() { return bass; }
    SynthProcessor& getSynth() { return synthTool; }
    // The analog drum machine: its own sounds, patterns and track, beside the MPC.
    DrumProcessor& getDrums() { return drums; }
    // The Drone box: a tanpura (and reeds) on the song's note, and strings that ring to what's
    // fed into them. It isn't on the clock: it sounds whenever it's on.
    DroneProcessor& getDrone() { return drone; }
    // Plucks one of its strings (0-3) from the screen, on or off (an off drone wakes for it).
    void pluckDrone (int string) { drone.pluck (string); dronePlucked = true; }
    // Why singing into the drone's strings makes no sound, in a few words ("The mic is off"), or
    // empty when they hear the mic. (Only for "Mic / line in": the strings hear your mic straight
    // from the interface, whether or not the Input strip is on, but only while the mic is on.)
    juce::String whyDroneDeaf() const;

    // Every tool, by the number knob Learn and shows use for it.
    // Each mixer strip's effects rack is a tool too (fxTool + the strip's StripId), so its knobs
    // can be learned and it's saved with the show. The Drone came after the racks, so it's after
    // them (shows keep their numbers).
    enum ToolId { mpcTool, samplerTool, vocoderTool, bassTool, synthTool_, drumsTool, fxTool, droneTool = fxTool + 10, numTools = droneTool + 1 };
    juce::AudioProcessor& getTool (int id);
    // True for a strip's effects rack (fxTool + a StripId).
    static bool isFxTool (int id) { return id >= fxTool && id < droneTool; }
    static juce::String toolName (int id);
    RigTransport& getTransport() { return transport; }
    RigLooper& getLooper() { return looper; }
    // Quantize, for every tool and loop (its settings are saved with the show and kept by New song).
    RigQuantizer& getQuantizer() { return quantizer; }
    RigScenes& getScenes() { return scenes; }
    // The chord builder: your own chords, onto any instrument (saved with the show; kept by New song).
    RigChords& getChords() { return chords; }
    // Automation: knob moves on any tool or its effects, recorded over the beat and played back
    // (saved with the show; New song clears it). Rec moves arms it from the next bar, starting
    // the clock if it's stopped (over the song, its lanes play Once along the song).
    RigAutomation& getAutomation() { return automation; }
    RigMacros& getMacros() { return macros; }
    void pressRecMoves();
    // The setting you last touched on screen (a tool's id and its index, as knob Learn names
    // them), for Move in the Automation panel. Message thread; -1 for none yet.
    int lastTouchedTool = -1, lastTouchedParameter = -1;
    // The song on a timeline (lanes of blocks over the bar ruler). While it's on and the clock
    // plays, it brings loops and tools in and out, with their fades.
    RigTimeline& getTimeline() { return timeline; }
    // Plays the timeline from `bar` (message thread): turns it on, song mode off, and unmutes
    // the loops and tools it uses (it brings them in and out itself).
    void playTimeline (double bar);
    // The Song screen's sections (rigsong): plays section `i` (from the next bar while
    // playing), with the song on. And the Play/Stop at the top: in Song mode (the timeline on)
    // Play starts the song from `songStartBar`; in Jam it just runs the clock.
    // Puts a tool on what a Song box plays (the MPC's sequence, a pattern, the synth's arp).
    // Message thread.
    void applyClip (int lane, int clip);
    void launchSection (int i);
    void playOrStop();
    double songStartBar = 0;
    // True while the song decides what plays (song mode on, with sections): tracks it uses come
    // and go with its boxes, so a track you switch on can stay quiet. jam() hands control back:
    // the song waits, and whatever you switch on plays (message thread).
    bool songInCharge() const { return timeline.on.load() && ! timeline.isEmpty(); }
    void jam() { timeline.on = false; timeline.cycle = false; controlsMoved = true; }
    // Rec live (the Song screen): the song played into, loops and tools brought in and out.
    RigLiveSong& getLiveSong() { return liveSong; }
    // The click counts in until this beat while Rec live waits for its first bar (-1: none).
    std::atomic<double> songCountInUntil { -1.0 };

    // Recording into the song (Rec into): `source` (a RigLooper::Source) onto loop `layer`, over
    // section `section`, from its top; with `cycle` the section plays over and over and every
    // pass is a take (RigTakes). Gets the song there: from the section's top (starting the clock
    // if it's stopped), or, cycling inside that section already, from its next time round.
    // Message thread. False (with why) when it can't.
    RigTakes& getTakes() { return takes; }
    bool recordInto (int section, int layer, int source, bool cycle, juce::String* why = nullptr);
    // The section plays over and over (Cycle on the Rec into bar), or not.
    void cycleSection (int section, bool on);
    // The timer's part: new takes onto their layer (and into their section of the song).
    // Returns how many came in.
    int serviceTakes();
    // Puts take `take` of a layer's takes on the layer (and in the song). False if there's none.
    bool useTake (int layer, int take);
    // The strip a looper source records from (RigLooper::Source to StripId), -1 for none.
    static int stripForSource (int source);
    // The first loop that's free for a new layer: empty, and not in the song. -1 when all are used.
    int freeLayerForSong() const;

    // Notes that stay notes (RigNotes): a take from a tool played by notes is a clip of notes
    // that plays the tool in its section. Message thread: change them, then notesChanged().
    // A RigLooper::Source that records notes (the Bass, the Synth, the Sampler, the MPC, the
    // Drum machine).
    static bool isNotesSource (int source);
    std::vector<rignotes::Clip>& getNoteClips() { return noteClips; }
    // The clip of `strip` that starts at `startBar` (-1 for none).
    int noteClipAt (int strip, double startBar) const;
    void notesChanged();
    // Sends the playing take of a notes layer to a free loop, as sound. The loop, or -1.
    int takeToLoop (int notesLayer);

    // Record the set (message thread): every strip and the mix as WAVs, and what's played on
    // the keys and pads as MIDI, into `folder`; it starts at the next bar. Call stopRecording()
    // to end it, and serviceRecorder() often (it returns true when a take has been saved).
    RigRecorder& getRecorder() { return recorder; }
    bool startRecording (const juce::File& folder);
    void stopRecording() { recorder.stop(); }
    bool serviceRecorder() { return recorder.service(); }
    static juce::File recordingsFolder();

    // Record one tool into another (message thread): `source` (a RigCapture::Source) into
    // captureDest, from the next bar, for `bars` (0: until stopCapture). serviceCapture() hands
    // a finished recording to where it goes and says what happened (empty while nothing's done).
    RigCapture& getCapture() { return capture; }
    bool startCapture (int source, int bars) { return capture.start (source, bars); }
    void stopCapture() { capture.stop(); }
    juce::String serviceCapture();
    int captureSource = RigCapture::mic, captureBars = 0;

    // Sounds from anywhere (message thread). 44.1 kHz audio onto the next empty MPC pad of the
    // bank the keyboard's pads play (returns its name, "A3", or empty if every pad is full), or
    // into the Sampler. `beats` > 0 sets the pad's length for the MPC's keep in time.
    juce::String putOnMpcPad (juce::AudioBuffer<float>, const juce::String& name, double beats);
    void putInSampler (juce::AudioBuffer<float>, const juce::String& name);
    // Onto the first empty loop layer, `beats` long (0: guessed from its length at the Rig's
    // tempo), stretched to the Rig's tempo, and named. Returns the layer (-1: every layer is full).
    int putOnLoopLayer (crate::SharedAudio, const juce::String& name, double beats);
    // Plays a 44.1 kHz sound once to the speakers (not recorded), to hear it; null stops it.
    void preview (std::shared_ptr<const juce::AudioBuffer<float>>);
    bool isPreviewing() const { return previewPlaying.load(); }
    int captureDest = RigCapture::toSampler;
    // Where the next recording goes instead of captureDest (the Sampler screen's Sample a tool); -1: captureDest.
    int captureDestOnce = -1;
    // The last recording made "for Logic" (a WAV to drag onto a Logic track), and where they go:
    // ~/Music/Crate/Rig/To Logic. Message thread.
    juce::File lastLogicFile;
    static juce::File logicFolder();

    // Scenes (message thread): what's playing now, as a scene; and the regular check that
    // moves song mode on and gets the MPC and bass ready for a scene about to start. The
    // window calls tickScenes often (tests call it directly).
    RigScene::State captureScene();
    void tickScenes();
    double getSampleRate() const { return sampleRate; }

    // The MPC, played live (message thread). A sequence picked while the clock runs comes in
    // at the next bar. A song plays its sequences in order on the Rig's clock: started while
    // playing it comes in at the next bar; stopped, it starts the clock from the top.
    void playMpcSequence (int index);
    bool playMpcSong (int song);
    // The MPC's beat on or off, like the other instruments' patterns: on, its sequence plays
    // whenever the Rig plays (on the Rig's clock); off, the Rig's Play leaves it quiet and a beat
    // playing stops at once. Its pads play either way. Saved with the show and in song parts.
    bool isMpcBeatOn() const { return mpc.getSequencer().followHost.load(); }
    void setMpcBeatOn (bool on);
    // Plays the MPC's pads into the looper (message thread): the first empty layer records the
    // MPC (pads, and its beat if that's on) from the next bar, for `bars` bars (0: until this is
    // pressed again, then to the end of that bar). Pressed while that loop records, it ends it.
    // Says what happened.
    juce::String loopMpcPads (int bars);
    // The layer recording (or waiting to record) the pads, or -1.
    int getPadLoopLayer() const;
    RigRouting& getRouting() { return routing; }
    // Your MIDI controllers (Settings > Audio & MIDI > MIDI controllers): what each one is, so a
    // Launchpad, an MPD or any keyboard plays the Rig the way a Launchkey does. They belong to
    // this Mac (their own file), not to the show.
    crate::controllers::Setup& getControllers() { return controllers; }

    // (The vocoder came later, so it's last: saved shows keep their strip order. The drone came
    // last of all.)
    enum StripId { mpcStrip, samplerStrip, masterStrip, vocoderStrip, bassStrip, synthStrip, inputStrip, loopsStrip, drumsStrip, droneStrip, numStrips };
    static_assert (droneTool == fxTool + numStrips && numTools == droneTool + 1, "every strip's rack, then the drone");
    struct Strip
    {
        std::atomic<float> level { 0.8f };   // fader position 0..1
        std::atomic<bool> mute { false };
        std::atomic<float> peak { 0.0f };    // since the window last looked
        // The mixer's: pan (-1 left .. 1 right, after the fader), solo (while any strip is
        // soloed, only soloed strips are heard), and two sends after the fader and pan, to
        // the shared reverb (A) and the shared echo (B). The master has none of these.
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> solo { false };
        std::atomic<float> sendA { 0.0f }, sendB { 0.0f };
        // In Logic's Multi Output: plays out of the tool's own output (its own Logic track)
        // instead of the main mix. Off: it stays in the main mix even though Logic has turned
        // every output on (it does that whether or not you've made tracks for them).
        std::atomic<bool> ownOutput { false };
    };
    Strip& getStrip (int id) { return strips[(size_t) id]; }
    // The strip's effects (filter, drive, crush, delay, reverb, repeat), before its fader.
    StripFx& getFx (int strip) { return fx[(size_t) strip]; }
    // Each looper layer's own effects, which the loop plays through (the Loops strip comes after).
    StripFx& getLoopFx (int layer) { return loopFx[(size_t) layer]; }
    static juce::String stripName (int id);
    // The strip's name on screen (the drum machine is "Drums", the input "Line in"); stripName
    // stays as saved shows and exported files have it.
    static juce::String stripLabel (int);
    // True while any strip is soloed (the master can't be).
    bool anySolo() const;
    // What a strip plays at now: its mute, or a solo elsewhere, silences it.
    bool isSilenced (int strip) const;

    // The mixer's two shared effects, which every strip's sends feed (A: reverb, B: echo in
    // time with the tempo). Each returns into the mix before the master, with its own level.
    struct Return
    {
        std::atomic<float> level { 0.8f };   // fader position, as a strip's
        std::atomic<bool> mute { false };
        std::atomic<float> peak { 0.0f };
        // A: room size 0..1. B: feedback 0..0.9.
        std::atomic<float> amount { 0.5f };
        // A: brightness (1 - damping) 0..1. B: the echo time, an index into echoTimes().
        std::atomic<float> tone { 0.5f };
        std::atomic<int> time { 1 };
    };
    enum ReturnId { reverbReturn, echoReturn, numReturns };
    Return& getReturn (int id) { return returns[(size_t) juce::jlimit (0, (int) numReturns - 1, id)]; }
    static juce::String returnName (int id);
    // The echo's times, in beats, and their names ("1/8 dotted").
    static const std::vector<std::pair<juce::String, double>>& echoTimes();

    // Fader position to gain: 0.8 is unity, the top is about +6 dB.
    static float faderGain (float level);

    // The mic, heard: while it's off, the vocoder and the Input strip don't hear your mic or
    // interface (so laptop speakers can't feed back). Sampling and looping from the mic still
    // work. Off when the app starts; on in Logic (its Side Chain is the switch there).
    std::atomic<bool> micOn { false };
    // The loudest the mic / audio input came in since the window last looked, whether or not
    // anything is listening to it (so you can see the mic works before you switch it on).
    std::atomic<float> micPeak { 0.0f };

    // The vocoder runs only while it's on (it uses the most processor time).
    std::atomic<bool> vocoderOn { true };

    // The Input strip: an instrument or mic from your audio interface (a guitar, a second
    // mic, a drum machine), through its own effects to the speakers. Off until you turn it on,
    // so a mic doesn't feed back. It listens to inputs 1 and 2, or to just one of them.
    std::atomic<bool> inputOn { false };
    enum class InputJack { both, first, second };
    std::atomic<int> inputJack { (int) InputJack::both };
    static juce::String inputJackName (int);

    // What the sampler hears when you record in it.
    enum class SampleSource { mic, vocoder, mix };
    std::atomic<int> sampleSource { (int) SampleSource::mic };
    static juce::String sampleSourceName (int);

    // Something on the audio thread changed a strip or the transport (a learned fader or
    // button), so the window should refresh. Reset by the window.
    std::atomic<bool> controlsMoved { false };

    // Silences every tool's notes at the next block (stuck notes, a latched arp). Stop does it.
    void allNotesOff() { notesOffRequested = true; }

    // An instrument plugin of your own (an Arturia synth...) that the Bass or Synth track plays
    // instead of its own sound: its steps, arp and keys all play the plugin. Empty: its own sound.
    PluginSlot& getBassPlugin() { return bassPlugin; }
    PluginSlot& getSynthPlugin() { return synthPlugin; }
    // The FX pad on the whole mix (before the master fader, so it's in the recorded set).
    PerformFx& getPerformFx() { return performFx; }

    // Plays a note into the Rig as if it came from a keyboard (tests, the window).
    void addMidi (const juce::MidiMessage&);

    // Recording timing. What you sing or play into the mic or line in reaches Kaleidobox late by
    // the interface's round trip (what you hear comes out late, what you sing comes in late), so
    // loops and takes from them are moved that much earlier to land on the beat. The app knows
    // its interface's round trip; the offset (Settings > Click & mic > Timing) adds to it, for
    // what it can't know (in Logic, Logic's own buffers). Saved on this Mac, apart for the app and
    // for Logic.
    void setTimingOffsetMs (float ms, bool save = true);
    float getTimingOffsetMs() const { return timingOffsetMs; }
    int getDeviceRoundTrip() const { return deviceRoundTrip.load(); }   // samples (0 in Logic)
    int getLiveDelaySamples() const;
    static juce::File timingFile();

    //==========================================================================
    // Audio. `prepare` and `process` are what the audio device calls; tests call them directly.
    void prepare (double sampleRate, int maximumBlockSize);
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples);
    // As a plugin: the host's MIDI for the next process() call (sample positions within it),
    // or null. Audio thread.
    void setHostMidi (const juce::MidiBuffer* m) { hostMidi = m; }
    // As a plugin: a tool's own stereo output (Logic's multi-output tracks). A tool sent to
    // its own output plays there, after its strip, and no longer in the main mix. Null puts it
    // back in the mix. Audio thread, before process().
    // (The drum machine came last, so its output is last.)
    enum Stem { mpcStem, vocoderStem, bassStem, synthStem, inputStem, samplerStem, loopsStem, drumsStem, numStems };
    static juce::String stemName (int);
    void setStemOutput (int stem, float* const* stereo) { stemOut[(size_t) stem] = stereo; }
    // The mixer strip a stem comes from, and whether Logic gave this stem an output.
    static int stripOfStem (int stem);
    static int stemOfStrip (int strip);   // -1: none (the master)
    // (The plugin says, each block, which outputs Logic has on; the window reads it.)
    bool hasStemOutput (int stem) const { return stemOn[(size_t) stem].load(); }
    void setStemOn (int stem, bool on) { stemOn[(size_t) stem] = on; }
    // Plays queued MIDI at the start of the next block, without timestamps (tests).
    void addMidiNow (const juce::MidiMessage&);

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    // The last time any MIDI came in (Time::getMillisecondCounter), for the window's MIDI light.
    std::atomic<juce::uint32> lastMidiTime { 0 };

    //==========================================================================
    //==========================================================================
    // One tool only: Kaleidobox Vocoder, MPC, Drums, Bass and Synth are this box with just one
    // tool in it, each its own plugin on its own Logic track (its own MIDI, automation, Freeze
    // and Bounce). Only that tool runs (the MPC with its Sampler, which make one unit), and every
    // note from Logic plays it. A StripId (mpcStrip, vocoderStrip, bassStrip, synthStrip,
    // drumsStrip), or -1 (the default): the whole box. Message thread; call it again after
    // loadShow (a show sets the keys and pads).
    void setOnlyTool (int strip);
    int getOnlyTool() const { return onlyTool.load(); }
    // Whether the strip's tool runs (always, in the whole box; the master always).
    bool runsStrip (int strip) const;
    // The tool's name as a plugin ("Vocoder", "MPC", "Drums", "Bass", "Synth").
    static juce::String onlyToolName (int strip);

    // One tool's sound, to carry between boxes (Get from box / Send to box, and tool sound
    // files): the tool's own settings (the MPC with its Sampler and kits), its strip's effects,
    // fader, pan and sends, and the plugin in its slot (an Arturia synth on the Bass or Synth).
    static constexpr const char* kToolSoundType = "KaleidoboxToolSound";
    juce::ValueTree saveToolSound (int strip) const;
    // From a saved show (or a whole box's show tree). Invalid if the show doesn't have that tool.
    static juce::ValueTree toolSoundFromShow (const juce::ValueTree& show, int strip);
    // Puts a tool sound on its tool here, leaving every other tool as it is. False if it isn't one.
    bool loadToolSound (const juce::ValueTree&);
    // Where tool sound files go (~/Music/Crate/Rig/Tool sounds), and their extension.
    static juce::File toolSoundsFolder();
    static constexpr const char* kToolSoundExtension = ".kbtool";

    // The whole boxes open in this program (the Kaleidobox plugins in this Logic song, or the
    // app), newest last, so a one-tool plugin can get its sound from one or send it back.
    // Message thread.
    static juce::Array<RigEngine*> openBoxes();

    // A show: both tools' sounds and settings, the mixer, tempo, routing and learned controls.
    juce::ValueTree saveShow() const;
    void loadShow (const juce::ValueTree&);
    static constexpr const char* kShowType = "CrateRigShow";
    // A new song: every tool, loop, part and setting as a new Rig has them (the windows stay
    // where they are). isBlank: nothing's been changed since (or since the Rig started).
    void newShow();
    bool isBlank() const;

    // Which tools are in their own windows (on a second screen, say) and where, kept by the
    // window and saved with the show (message thread).
    juce::ValueTree screens { "Screens" };

    //==========================================================================
    // Loop banks (message thread): a named set of all eight looper layers (their loops, lengths,
    // sources, levels, mutes and effects), so you can keep a batch of loops, start a fresh one
    // and come back to it. The layers you see are always the current bank's; switching banks
    // keeps what's on them in the bank you leave. While the clock plays, the new bank's loops
    // come in at the next bar, from their tops. Song parts can switch banks too
    // (RigScene::State::loopBank). Saved with the show; their loops are WAVs in the show's
    // loops folder ("Bank 2/Loop 1.wav"...).
    struct LoopBank
    {
        struct Layer
        {
            std::shared_ptr<const juce::AudioBuffer<float>> audio;   // null: an empty layer
            double beats = 0;
            int source = RigLooper::mix, bars = 0;
            float level = 0.8f;
            bool mute = false;
            juce::MemoryBlock fx;   // StripFx state (empty: no effects)
            int version = -1;       // the looper layer's version when it was kept
        };
        juce::String name;
        std::array<Layer, RigLooper::kLayers> layers;
        int loopCount() const;
    };
    static constexpr int kMaxLoopBanks = 64;
    // The banks, with the current one's layers as they are now on the looper.
    const std::vector<LoopBank>& getLoopBanks();
    // The banks as they're kept, without keeping the layers first (nothing is copied or changed):
    // the current bank's entry is as it was last kept, so its sound is the looper's layers, not
    // this. For watching (the dirty model); message thread.
    const std::vector<LoopBank>& peekLoopBanks() const { return loopBanks; }
    int getNumLoopBanks() const { return juce::jmax (1, (int) loopBanks.size()); }
    // Always one: before you make a second, the layers are "Bank 1".
    int getCurrentLoopBank() const { return juce::jmax (0, currentLoopBank); }
    juce::String getLoopBankName (int bank) const;
    // Each returns what to tell the user. They refuse while a layer is recording or overdubbing
    // (finish it first), and a switch waiting for its bar is replaced by a newer one.
    // Keeps the layers in the current bank, and names it.
    juce::String saveLoopBank (const juce::String& name);
    // Keeps the layers in the current bank, then starts a new empty one (layers keep their sources
    // and lengths for recording; levels, mutes and effects start fresh).
    juce::String newLoopBank (const juce::String& name = {});
    juce::String switchLoopBank (int bank);
    juce::String duplicateLoopBank (int bank);
    juce::String renameLoopBank (int bank, const juce::String& name);
    // Not the only one. Deleting the current bank switches to the one before it.
    juce::String deleteLoopBank (int bank);
    // A bank switch waiting for its bar (-1 for none).
    int getQueuedLoopBank() const { return queuedLoopBank; }
    // Clear all (start fresh): every layer of the bank on screen empty now, with its effects,
    // name, level and mute back as a new layer has them (one that's recording stops and is
    // dropped). Sources and lengths stay, ready to record the same way; other banks keep theirs.
    // undoClearAllLoops brings back everything as it was, while the layers are still empty.
    juce::String clearAllLoops();
    juce::String undoClearAllLoops();
    bool canUndoClearAllLoops() const;
    // The timer's part: puts each layer's effects on as its new loop comes in.
    void serviceLoopBanks();

    // The loops' sound is too big for the show file, so it goes in WAV files in a folder next
    // to it ("My show loops"; see RigShowFiles.h for how a save keeps the last one safe).
    static juce::File loopsFolder (const juce::File& showFile);
    // Saves the show and its loops' sound to `showFile` as one revision: the WAVs are written and
    // checked first, then the show file (which names them) replaces the old one. If anything
    // fails, the last saved show and its sound are left as they were and the result says why.
    // Message thread. A document save (Save, Save As) is what the show is compared with to tell
    // whether it has changed since; a copy (the last show for the next launch) isn't.
    rigshow::Result saveShowFile (const juce::File& showFile, rigshow::SaveKind kind = rigshow::SaveKind::copy);
    // The plugin's state (the host keeps it): the show, naming the loops' revision saved for it
    // under `loopsShowFile`. An invalid tree (with why in `result`) when the loops couldn't be
    // saved; the caller then gives the host its last saved state instead.
    juce::ValueTree saveShowForHost (const juce::File& loopsShowFile, rigshow::Result& result);
    // Why the last readLoopAudio couldn't find the show's loops (empty when it did).
    juce::String getLoopAudioProblem() const { return loopAudioProblem; }
    // A save's warning or error for the window to show (the plugin saves when the host asks).
    // Any thread; takes it.
    void noteShowFilesMessage (const juce::String&, bool error);
    juce::String takeShowFilesMessage (bool& error);
    // A loop's name ("Drums", a stem from Logic), shown on the Song screen. It goes with the sound:
    // once the layer records or gets another loop, it has none. Saved with the show. Message thread.
    void setLoopName (int layer, const juce::String&);
    juce::String getLoopName (int layer) const;
    // After loadShow: brings in the loops' sound saved with that show (its revision).
    void readLoopAudio (const juce::File& showFile);

    // Whether the show has changed since it was opened, made new or saved (RigDirty.h). Off
    // unless the app switches it on.
    RigDirty& getDirty() { return dirty; }

private:
    juce::ValueTree blankShow;   // a new Rig's show, for newShow
    void processChunk (const float* const* inputs, int numInputs, int offset, int numSamples);
    void applyControls (const RigRouting::Result&);

    void applyStrip (int id, juce::AudioBuffer<float>&);

    MpcProcessor mpc;
    int padLoopLayer = -1;
    void mpcCommand (MpcProcessor::HostCommand);
    EditorProcessor sampler;
    VocoderProcessor vocoder;
    BassProcessor bass;
    SynthProcessor synthTool;
    DrumProcessor drums;
    DroneProcessor drone;
    juce::MemoryBlock droneDefaults;   // a new drone's settings, for shows from before it
    std::atomic<bool> dronePlucked { false };
    RigTransport transport;
    RigRouting routing;
    crate::controllers::Setup controllers { crate::controllers::Setup::defaultFile() };
    RigLooper looper;
    // Gives the looper's recording layers more room as they go, and finishes loop bank switches
    // (message thread).
    struct LoopRoom : juce::Timer
    {
        explicit LoopRoom (RigEngine& e) : engine (e) {}
        void timerCallback() override
        {
            engine.looper.maintain();
            engine.serviceLoopBanks();
            engine.quantizer.service();
            if (engine.automation.service())
                engine.dirty.noteChange();
            engine.serviceTakes();
            engine.dirty.service();
        }
        RigEngine& engine;
    } loopRoom { *this };
    RigQuantizer quantizer { *this };
    RigChords chords;
    RigAutomation automation;
    RigTakes takes;
    RigNotePlayer notePlayer;
    std::vector<rignotes::Clip> noteClips;
    juce::MidiBuffer takeMidi;   // the notes played into the tool a take records (audio thread)
    std::vector<LoopBank> loopBanks;
    int currentLoopBank = -1;
    // A switch waiting for its bar: each layer's effects, put on when its loop comes in.
    std::array<juce::MemoryBlock, RigLooper::kLayers> pendingBankFx;
    std::array<bool, RigLooper::kLayers> bankFxPending {};
    // What Clear all took away, for its undo (with the loops' names).
    std::unique_ptr<LoopBank> clearedLoops;
    std::array<juce::String, RigLooper::kLayers> clearedLoopNames;
    void applyBankFx (int layer);
    void putLoopBankOnLayers (int bank, double beat, const RigScene::State* scene);
    RigScenes scenes;
    RigTimeline timeline;
    RigLiveSong liveSong { *this };
    // Message thread: what each box asks a tool to play, just before it starts.
    void tickSong();
    std::array<double, RigTimeline::kLanes> songClipAt = [] { std::array<double, RigTimeline::kLanes> a; a.fill (-2.0); return a; }();
    int songSection = -2;
    // Audio thread: the timeline's level for each strip over this block, and where each loop
    // was last started from (so a block starts its loop once).
    std::array<float, numStrips> arrangeFrom {}, arrangeTo {};
    std::array<double, RigLooper::kLayers> arrangeRestart {};
    void arrange (int numSamples);
    int samplesToTimelineEvent (int numSamples) const;
    RigRecorder recorder;
    RigCapture capture;
    // The preview: the window holds the sounds (so they're never freed here); the audio thread
    // plays the newest by its pointer.
    juce::SpinLock previewLock;
    const juce::AudioBuffer<float>* previewNext = nullptr;   // under previewLock
    int previewVersion = 0, previewSeen = 0;
    const juce::AudioBuffer<float>* previewSound = nullptr;  // audio thread
    double previewPos = 0;
    std::atomic<bool> previewPlaying { false };
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> previewHeld;   // message thread
    void applyScene (const RigScene::State&, double beat);   // audio thread, at the bar (`beat`)
    int queuedLoopBank = -1;
    bool loopBankBusy (juce::String& why) const;
    void storeLoopBank();   // the layers as they are now, into the current bank
    std::array<double, RigLooper::kLayers> loadedLoopBeats {};
    std::array<Strip, numStrips> strips;
    std::array<juce::LinearSmoothedValue<float>, numStrips> pans, sendGainsA, sendGainsB;
    std::array<Return, numReturns> returns;
    std::array<juce::LinearSmoothedValue<float>, numReturns> returnGains;
    juce::Reverb sendReverb;
    juce::AudioBuffer<float> sendBufferA, sendBufferB, echoLine;
    int echoWrite = 0;
    float echoTone[2] {};
    bool soloActive = false;   // audio thread, this block
    void processReturns (juce::AudioBuffer<float>& mix, int numSamples);
    std::array<StripFx, numStrips> fx;
    std::array<StripFx, RigLooper::kLayers> loopFx;
    PluginSlot bassPlugin { true }, synthPlugin { true };
    // The level trim of the Bass's, Synth's and Vocoder's picked sounds (the previous block's, to ramp from).
    RigSounds::Trim bassTrim, synthTrim, vocoderTrim;
    float bassTrimWas = 1.0f, synthTrimWas = 1.0f, vocoderTrimWas = 1.0f;
    // The safety limiters: the master mix (before it's recorded), the final output, and each
    // tool's own output in Logic.
    crate::SafetyLimiter masterLimiter, outputLimiter;
    std::array<crate::SafetyLimiter, numStems> stemLimiters;
    PerformFx performFx;
    RigMacros macros;
    juce::MidiBuffer bassTap, synthTap;
    juce::MidiBuffer noMidi;
    std::array<juce::LinearSmoothedValue<float>, numStrips> gains;
    // The vocoder and Input switches fade (so they never click), and keys let go while the
    // vocoder is off reach it when it's back on (so they can't come back as stuck notes).
    juce::LinearSmoothedValue<float> vocoderFade, inputFade, micFade, droneMicFade;
    juce::MidiBuffer vocoderMissed, midiScratch;

    const juce::MidiBuffer* hostMidi = nullptr;
    std::atomic<int> onlyTool { -1 };
    std::array<juce::String, RigLooper::kLayers> loopNames, loadedLoopNames;
    std::vector<RigTakes::Set> loadedTakeSets;
    juce::String loadedAudioRevision, loopAudioProblem;   // from loadShow / readLoopAudio
    rigshow::Committed committedAudio;                   // the loops' sound saved for this show
    std::vector<rigshow::Asset> loopAssets() const;
    juce::CriticalSection messageLock;
    juce::String showFilesMessage;
    bool showFilesMessageIsError = false;
    float timingOffsetMs = 0;
    std::atomic<int> deviceRoundTrip { 0 };
    void applyTiming();   // from loadShow, until readLoopAudio brings their sound
    std::array<int, RigLooper::kLayers> loopNameVersions {};
    std::atomic<bool> notesOffRequested { false };
    std::array<float* const*, numStems> stemOut {};
    std::array<std::atomic<bool>, numStems> stemOn {};

    juce::MidiMessageCollector incoming;
    juce::SpinLock nowLock;
    juce::MidiBuffer nowMidi;          // addMidiNow, under nowLock

    // Audio-thread buffers, sized in prepare().
    juce::AudioBuffer<float> mpcBuffer, samplerBuffer, vocoderBuffer, bassBuffer, synthBuffer, drumsBuffer, inputBuffer, rawInputBuffer, loopsBuffer, mixBuffer, clickBuffer,
        droneBuffer, droneFeedBuffer;
    // (droneMidi: what the keys play, for the drone's Follow keys; empty while that's off.)
    juce::MidiBuffer midiIn, mpcMidi, samplerMidi, vocoderMidi, bassMidi, synthMidi, drumsMidi, droneMidi;
    double sampleRate = 0;
    int maxBlock = 0;
    bool prepared = false;

    // Last, so it's made after the tools (it listens to their settings) and goes first.
    RigDirty dirty { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigEngine)
};
