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
    enum ToolId { mpcTool, samplerTool, vocoderTool, bassTool, synthTool_, fxTool, numTools = fxTool + 7 };
    juce::AudioProcessor& getTool (int id);
    static juce::String toolName (int id);
    RigTransport& getTransport() { return transport; }
    RigRouting& getRouting() { return routing; }

    // (The vocoder came later, so it's last: saved shows keep their strip order.)
    enum StripId { mpcStrip, samplerStrip, masterStrip, vocoderStrip, bassStrip, synthStrip, inputStrip, numStrips };
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

    // Plays a note into the Rig as if it came from a keyboard (tests, the window).
    void addMidi (const juce::MidiMessage&);

    //==========================================================================
    // Audio. `prepare` and `process` are what the audio device calls; tests call them directly.
    void prepare (double sampleRate, int maximumBlockSize);
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples);
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
    std::array<Strip, numStrips> strips;
    std::array<StripFx, numStrips> fx;
    juce::MidiBuffer noMidi;
    std::array<juce::LinearSmoothedValue<float>, numStrips> gains;

    juce::MidiMessageCollector incoming;
    juce::SpinLock nowLock;
    juce::MidiBuffer nowMidi;          // addMidiNow, under nowLock

    // Audio-thread buffers, sized in prepare().
    juce::AudioBuffer<float> mpcBuffer, samplerBuffer, vocoderBuffer, bassBuffer, synthBuffer, inputBuffer, mixBuffer;
    juce::MidiBuffer midiIn, mpcMidi, samplerMidi, vocoderMidi, bassMidi, synthMidi;
    double sampleRate = 0;
    int maxBlock = 0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigEngine)
};
