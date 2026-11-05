#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorProcessor.h"
#include "MpcProcessor.h"
#include "PluginProcessor.h"   // the vocoder
#include "BassProcessor.h"
#include "SynthProcessor.h"
#include "RigRouting.h"
#include "RigTransport.h"
#include "StripFx.h"
#include "RigLooper.h"
#include "RigScenes.h"
#include "RigRecorder.h"
#include "RigCapture.h"

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
    EditorProcessor& getSampler() { return sampler; }
    VocoderProcessor& getVocoder() { return vocoder; }
    BassProcessor& getBass() { return bass; }
    SynthProcessor& getSynth() { return synthTool; }

    // Every tool, by the number knob Learn and shows use for it.
    // Each mixer strip's effects rack is a tool too (fxTool + the strip's StripId), so its knobs
    // can be learned and it's saved with the show.
    enum ToolId { mpcTool, samplerTool, vocoderTool, bassTool, synthTool_, fxTool, numTools = fxTool + 8 };
    juce::AudioProcessor& getTool (int id);
    static juce::String toolName (int id);
    RigTransport& getTransport() { return transport; }
    RigLooper& getLooper() { return looper; }
    RigScenes& getScenes() { return scenes; }

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
    // Plays a 44.1 kHz sound once to the speakers (not recorded), to hear it; null stops it.
    void preview (std::shared_ptr<const juce::AudioBuffer<float>>);
    bool isPreviewing() const { return previewPlaying.load(); }
    int captureDest = RigCapture::toSampler;

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
    RigRouting& getRouting() { return routing; }

    // (The vocoder came later, so it's last: saved shows keep their strip order.)
    enum StripId { mpcStrip, samplerStrip, masterStrip, vocoderStrip, bassStrip, synthStrip, inputStrip, loopsStrip, numStrips };
    static_assert (numTools == fxTool + numStrips);
    struct Strip
    {
        std::atomic<float> level { 0.8f };   // fader position 0..1
        std::atomic<bool> mute { false };
        std::atomic<float> peak { 0.0f };    // since the window last looked
    };
    Strip& getStrip (int id) { return strips[(size_t) id]; }
    // The strip's effects (filter, drive, crush, delay, reverb, repeat), before its fader.
    StripFx& getFx (int strip) { return fx[(size_t) strip]; }
    static juce::String stripName (int id);
    // Fader position to gain: 0.8 is unity, the top is about +6 dB.
    static float faderGain (float level);

    // The mic, heard: while it's off, the vocoder and the Input strip don't hear your mic or
    // interface (so laptop speakers can't feed back). Sampling and looping from the mic still
    // work. Off when the app starts; on in Logic (its Side Chain is the switch there).
    std::atomic<bool> micOn { false };

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

    // Plays a note into the Rig as if it came from a keyboard (tests, the window).
    void addMidi (const juce::MidiMessage&);

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
    enum Stem { mpcStem, vocoderStem, bassStem, synthStem, inputStem, samplerStem, loopsStem, numStems };
    static juce::String stemName (int);
    void setStemOutput (int stem, float* const* stereo) { stemOut[(size_t) stem] = stereo; }
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
    // A show: both tools' sounds and settings, the mixer, tempo, routing and learned controls.
    juce::ValueTree saveShow() const;
    void loadShow (const juce::ValueTree&);
    static constexpr const char* kShowType = "CrateRigShow";

    // Which tools are in their own windows (on a second screen, say) and where, kept by the
    // window and saved with the show (message thread).
    juce::ValueTree screens { "Screens" };

    // The loops' sound is too big for the show file, so it goes in WAV files in a folder next
    // to it ("My show loops"). Call after writing the show file, and after loading it.
    static juce::File loopsFolder (const juce::File& showFile);
    bool writeLoopAudio (const juce::File& showFile) const;
    void readLoopAudio (const juce::File& showFile);

private:
    void processChunk (const float* const* inputs, int numInputs, int offset, int numSamples);
    void applyControls (const RigRouting::Result&);

    void applyStrip (int id, juce::AudioBuffer<float>&);

    MpcProcessor mpc;
    EditorProcessor sampler;
    VocoderProcessor vocoder;
    BassProcessor bass;
    SynthProcessor synthTool;
    RigTransport transport;
    RigRouting routing;
    RigLooper looper;
    RigScenes scenes;
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
    void applyScene (const RigScene::State&);   // audio thread, at the bar
    std::array<double, RigLooper::kLayers> loadedLoopBeats {};
    std::array<Strip, numStrips> strips;
    std::array<StripFx, numStrips> fx;
    juce::MidiBuffer noMidi;
    std::array<juce::LinearSmoothedValue<float>, numStrips> gains;

    const juce::MidiBuffer* hostMidi = nullptr;
    std::atomic<bool> notesOffRequested { false };
    std::array<float* const*, numStems> stemOut {};

    juce::MidiMessageCollector incoming;
    juce::SpinLock nowLock;
    juce::MidiBuffer nowMidi;          // addMidiNow, under nowLock

    // Audio-thread buffers, sized in prepare().
    juce::AudioBuffer<float> mpcBuffer, samplerBuffer, vocoderBuffer, bassBuffer, synthBuffer, inputBuffer, rawInputBuffer, loopsBuffer, mixBuffer, clickBuffer;
    juce::MidiBuffer midiIn, mpcMidi, samplerMidi, vocoderMidi, bassMidi, synthMidi;
    double sampleRate = 0;
    int maxBlock = 0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigEngine)
};
