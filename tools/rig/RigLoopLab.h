#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include "core/Warp.h"
#include "RigLooper.h"

#include <array>
#include <memory>

class RigTransport;

// Loop Lab's engine side on the Mac (the window is LoopLab): a looper layer opened in the lab
// keeps the recording as it was (the original) and a warp plan (core/Warp.h), and plays the
// recording warped by the plan. Nothing is destroyed: ORIGINAL plays the recording again, Undo
// steps back through the plans, Print puts the warped loop on the next empty layer.
//
// Rendering runs on a worker thread; the loop being reshaped keeps playing the last version
// until the new one is ready, then swaps in at once (setLayerAudio). If anything else changes
// the layer meanwhile (an overdub, a clear, a trim, a loop bank), the lab lets go of it: the next
// open starts from what the layer holds then.
//
// FOLLOW SONG TEMPO (on by default): when the tempo changes, an opened layer is rendered again
// at the new tempo from its original, so it keeps its pitch (other layers speed up or slow down
// like tape, as before).
//
// Message thread only (apart from the worker's own copy of what it renders).
class RigLoopLab
{
public:
    RigLoopLab (RigLooper&, RigTransport&);
    ~RigLoopLab();

    struct Layer
    {
        bool open = false;
        juce::AudioBuffer<float> original;
        double originalBeats = 0;
        crate::Warp::Plan plan;
        std::vector<double> hits;        // in beats of the original
        float sensitivity = 0.6f;
        bool snapOn = false;
        double grid = 0.25;              // beats: SNAP TO BEAT's grid
        float strength = 0.75f;
        bool follow = true;
        bool playingOriginal = false;
        double soundsLike = 0;           // the tempo it sounds like (0: can't tell)

        // What the lab last put on the layer (its version), and at what tempo it was rendered.
        int playingVersion = -1;
        double renderedBpm = 0;
        juce::AudioBuffer<float> rendered;   // the last warped loop (empty until the first edit)
        std::vector<crate::Warp::Plan> undo;
        int serial = 0, appliedSerial = 0;   // renders asked for / last one put on the layer
    };

    // Opens a layer (false if it has no loop, or it's recording). A layer the lab still holds
    // keeps its plan; one that changed since starts fresh from what it holds now.
    bool open (int layer);
    bool isOpen (int layer) const;
    const Layer& get (int layer) const { return layers[(size_t) layer]; }

    // The edits. Each is a step Undo can take back, and plays as soon as it's rendered.
    void setPlan (int layer, const crate::Warp::Plan&);
    void setSettings (int layer, const crate::Warp::Settings&);
    void setSensitivity (int layer, float);                         // finds the hits again (and re-snaps)
    void setSnap (int layer, bool on, double gridBeats, float strength);
    void fitTo (int layer, double targetBeats);
    void moveMarker (int layer, int index, double targetBeat);
    int addMarker (int layer, double targetBeat);
    void removeMarker (int layer, int index);
    void resetWarp (int layer);                                     // the recording as it was
    void setFollow (int layer, bool on);
    bool canUndo (int layer) const;
    void undo (int layer);
    // ORIGINAL: play the recording as it was (true) or the warped loop again (false).
    void setOriginal (int layer, bool playOriginal);
    // Puts the warped loop on the next empty layer: returns it (-1 when none is empty).
    int print (int layer);

    bool isRendering (int layer) const;
    // The timer (the engine's, ~10 a second): follows tempo changes, lets go of changed layers.
    void tick();
    std::function<void (int layer)> onChange;

    // Tests: render on the calling thread instead of the worker.
    bool renderInline = false;

private:
    void edit (int layer, const crate::Warp::Plan&);
    void render (int layer);
    void apply (int layer, int serial, juce::AudioBuffer<float> audio, double bpm);
    bool stillOurs (int layer) const;

    RigLooper& looper;
    RigTransport& transport;
    std::array<Layer, RigLooper::kLayers> layers;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Loop Lab") };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE (RigLoopLab)
};
