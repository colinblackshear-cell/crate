#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

// Warp: a recorded loop's timing reshaped, Loop Lab's WARP page (Mac and phone share it).
//
// A loop is `sourceBeats` long as recorded. Warp markers pin a moment of the recording (a beat
// of the source, usually a hit) to a beat of the warped loop (`targetBeats` long). Between two
// markers the sound is stretched or squeezed in a straight line, so pulling a marker onto a
// beat line moves that hit onto the beat and everything around it follows smoothly. The ends
// are always pinned (0 to 0, sourceBeats to targetBeats), so the loop still goes round on the
// bar: FIT TO n BARS is just a new targetBeats.
//
// How the time change sounds (Style):
//   voice    keeps the pitch (a phase vocoder; transpose and formant work here too)
//   beats    keeps every hit exactly as played: the pieces between markers are moved, not
//            stretched (a gap is left silent, an overlap cut with a short fade)
//   texture  grains: smeary and dreamy, good for pads, drones and slowed-down voices
//   tape     pitch follows speed, like a tape machine sped up or slowed down
// Transpose and formant are applied in every style (in tape they come on top of its own pitch).
//
// Loops are round: rendering reads past the end from the start, so a warped loop joins itself
// with no click. Everything here works on copies and returns new buffers (any thread).
namespace crate::Warp
{
    enum class Style { voice, beats, texture, tape };
    juce::String styleName (Style);   // "VOICE"

    struct Marker
    {
        double source = 0;   // beats into the recording
        double target = 0;   // beats into the warped loop
        bool manual = false; // dragged by hand (SNAP leaves it where it is)
    };

    struct Settings
    {
        Style style = Style::voice;
        float transpose = 0;   // semitones, -12..12 (fine tuning as fractions)
        float formant = 0;     // semitones, -12..12: the voice's character, kept apart from its pitch
        bool operator== (const Settings& o) const { return style == o.style && transpose == o.transpose && formant == o.formant; }
    };

    struct Plan
    {
        double sourceBeats = 0, targetBeats = 0;
        std::vector<Marker> markers;   // in order, strictly inside both ends
        Settings settings;

        bool isValid() const { return sourceBeats > 0 && targetBeats > 0; }
        // True when rendering would give the recording back unchanged (apart from its length).
        bool isStraight() const;
        // Where the warped loop's `targetBeat` plays from in the recording (and back).
        double sourceAt (double targetBeat) const;
        double targetAt (double sourceBeat) const;
        juce::var toVar() const;
        static Plan fromVar (const juce::var&);
    };

    // Puts markers in order and drops any that would make time run backwards or sit on an end
    // (closer than 1/64 beat). A manual marker wins over an automatic one it clashes with.
    void tidy (Plan&);

    // FIT TO n BARS: the loop lasts `targetBeats` from now on; markers keep their place in it.
    void setLength (Plan&, double targetBeats);

    // The hits in the recording, in beats of it (sensitivity 0..1).
    std::vector<double> findHits (const juce::AudioBuffer<float>&, double sourceBeats, float sensitivity);

    // SNAP TO BEAT: an automatic marker on every hit, pulled `strength` (0..1) of the way from
    // where it plays now to the nearest `gridBeats` line. Markers dragged by hand stay. With
    // strength 0 (or no grid) the automatic markers go and only the hand-placed ones are left.
    void snap (Plan&, const std::vector<double>& hits, double gridBeats, float strength);

    // Drags marker `index` to `targetBeat` (it becomes manual); returns where it landed (it
    // can't pass its neighbours).
    double moveMarker (Plan&, int index, double targetBeat);
    // Puts a manual marker on the source beat that plays at `targetBeat` now; returns its index.
    int addMarker (Plan&, double targetBeat);
    void removeMarker (Plan&, int index);

    // The tempo the recording sounds like it's at, from the spacing of its hits, picked near
    // `songBpm` (doubled or halved to get there). 0 when it can't tell (too few hits or no beat).
    double soundsLikeBpm (const std::vector<double>& hitSeconds, double songBpm);

    // The warped loop, exactly `lengthSamples` long (targetBeats at the song's tempo).
    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& source, const Plan&, int lengthSamples, double sampleRate);
}
