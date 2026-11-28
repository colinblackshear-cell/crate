#include "RigLoopLab.h"

#include "RigTransport.h"

namespace Warp = crate::Warp;

namespace
{
constexpr int kMaxUndo = 64;

// The plan leaves the recording exactly as it is at `bpm`: no markers that move anything, the
// loop's own length, every setting at its default, and the tempo the lab opened it at.
bool isIdentity (const RigLoopLab::Layer& l, double bpm)
{
    const auto& p = l.plan;
    return p.isStraight() && p.settings == Warp::Settings {}
           && juce::exactlyEqual (p.sourceBeats, l.originalBeats) && juce::exactlyEqual (p.targetBeats, l.originalBeats)
           && std::abs (bpm - l.originalBpm) < 0.01;
}

std::vector<double> hitSeconds (const RigLoopLab::Layer& l, double sampleRate)
{
    std::vector<double> s;
    if (l.originalBeats <= 0 || sampleRate <= 0)
        return s;
    const double length = l.original.getNumSamples() / sampleRate;
    for (double h : l.hits)
        s.push_back (h / l.originalBeats * length);
    return s;
}
}

RigLoopLab::RigLoopLab (RigLooper& l, RigTransport& t) : looper (l), transport (t) {}

RigLoopLab::~RigLoopLab()
{
    *alive = false;
    pool.removeAllJobs (true, 10000);
}

bool RigLoopLab::stillOurs (int layer) const
{
    const auto& l = layers[(size_t) layer];
    return l.open && looper.getLayer (layer).version.load() == l.playingVersion
           && looper.getState (layer) == RigLooper::State::playing;
}

bool RigLoopLab::isOpen (int layer) const
{
    return juce::isPositiveAndBelow (layer, RigLooper::kLayers) && stillOurs (layer);
}

bool RigLoopLab::open (int layer)
{
    if (! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return false;
    if (stillOurs (layer))
        return true;
    auto& l = layers[(size_t) layer];
    if (looper.getState (layer) != RigLooper::State::playing)
        return false;
    int version = -1;
    auto audio = looper.copyLayerAudio (layer, &version);
    const double beats = looper.getLayer (layer).lengthBeats.load();
    if (audio.getNumSamples() == 0 || beats <= 0 || version < 0)
        return false;

    const auto keep = l;   // the knobs you set last time carry over to a new loop
    l = {};
    l.sensitivity = keep.sensitivity;
    l.grid = keep.grid;
    l.strength = keep.strength;
    l.follow = keep.follow;
    l.serial = l.appliedSerial = keep.serial;
    l.original = std::move (audio);
    l.originalBeats = beats;
    l.plan.sourceBeats = l.plan.targetBeats = beats;
    l.hits = Warp::findHits (l.original, beats, l.sensitivity);
    l.soundsLike = Warp::soundsLikeBpm (hitSeconds (l, looper.getSampleRate()), transport.getBpm());
    l.playingVersion = version;
    l.renderedBpm = l.originalBpm = transport.getBpm();
    l.open = true;
    historySites[(size_t) layer].touch();
    return true;
}

void RigLoopLab::edit (int layer, const Warp::Plan& plan)
{
    if (! stillOurs (layer))
        return;
    auto& l = layers[(size_t) layer];
    l.undo.push_back (l.plan);
    if ((int) l.undo.size() > kMaxUndo)
        l.undo.erase (l.undo.begin());
    l.plan = plan;
    Warp::tidy (l.plan);
    historySites[(size_t) layer].touch();
    render (layer);
}

void RigLoopLab::setPlan (int layer, const Warp::Plan& p) { edit (layer, p); }

void RigLoopLab::setSettings (int layer, const Warp::Settings& s)
{
    if (! stillOurs (layer))
        return;
    auto p = layers[(size_t) layer].plan;
    p.settings = s;
    edit (layer, p);
}

void RigLoopLab::setSensitivity (int layer, float sensitivity)
{
    if (! stillOurs (layer))
        return;
    auto& l = layers[(size_t) layer];
    l.sensitivity = juce::jlimit (0.0f, 1.0f, sensitivity);
    l.hits = Warp::findHits (l.original, l.originalBeats, l.sensitivity);
    l.soundsLike = Warp::soundsLikeBpm (hitSeconds (l, looper.getSampleRate()), transport.getBpm());
    if (l.snapOn)
    {
        auto p = l.plan;
        Warp::snap (p, l.hits, l.grid, l.strength);
        edit (layer, p);
    }
    else if (onChange)
        onChange (layer);
}

void RigLoopLab::setSnap (int layer, bool on, double gridBeats, float strength)
{
    if (! stillOurs (layer))
        return;
    auto& l = layers[(size_t) layer];
    l.snapOn = on;
    l.grid = gridBeats;
    l.strength = juce::jlimit (0.0f, 1.0f, strength);
    auto p = l.plan;
    Warp::snap (p, l.hits, gridBeats, on ? l.strength : 0.0f);
    edit (layer, p);
}

void RigLoopLab::fitTo (int layer, double targetBeats)
{
    if (! stillOurs (layer) || targetBeats <= 0)
        return;
    auto p = layers[(size_t) layer].plan;
    Warp::setLength (p, targetBeats);
    auto& l = layers[(size_t) layer];
    if (l.snapOn)
        Warp::snap (p, l.hits, l.grid, l.strength);
    edit (layer, p);
}

double RigLoopLab::fitToBeat (int layer)
{
    if (! open (layer))
        return -1.0;
    auto& l = layers[(size_t) layer];
    const double songBpm = transport.getBpm();
    auto p = l.plan;
    p.settings.style = Warp::Style::voice;
    // How long the take lasts, in seconds: its beats at the tempo it plays at as it is.
    const double seconds = l.originalBeats * 60.0 / juce::jmax (1.0, l.originalBpm);
    // The same hits and tempo the phone reads (sensitivity 0.5), whatever the lab's knob is at.
    const auto hits = Warp::findHits (l.original, l.originalBeats, 0.5f);
    double soundsLike = 0.0;
    if (hits.size() >= 4)
    {
        std::vector<double> s;
        for (double h : hits)
            s.push_back (h / l.originalBeats * seconds);
        soundsLike = Warp::soundsLikeBpm (s, songBpm);
    }
    const double sungBeats = soundsLike > 0.0 ? seconds * soundsLike / 60.0 : l.originalBeats;
    Warp::setLength (p, juce::jmax (4.0, std::round (sungBeats / 4.0) * 4.0));
    Warp::snap (p, hits, 0.5, 0.85f);
    l.hits = hits;
    l.sensitivity = 0.5f;
    l.snapOn = true;
    l.grid = 0.5;
    l.strength = 0.85f;
    edit (layer, p);
    return soundsLike;
}

void RigLoopLab::moveMarker (int layer, int index, double targetBeat)
{
    if (! stillOurs (layer))
        return;
    auto p = layers[(size_t) layer].plan;
    Warp::moveMarker (p, index, targetBeat);
    edit (layer, p);
}

int RigLoopLab::addMarker (int layer, double targetBeat)
{
    if (! stillOurs (layer))
        return -1;
    auto p = layers[(size_t) layer].plan;
    const int index = Warp::addMarker (p, targetBeat);
    if (index >= 0)
        edit (layer, p);
    return index;
}

void RigLoopLab::removeMarker (int layer, int index)
{
    if (! stillOurs (layer))
        return;
    auto p = layers[(size_t) layer].plan;
    Warp::removeMarker (p, index);
    edit (layer, p);
}

void RigLoopLab::resetWarp (int layer)
{
    if (! stillOurs (layer))
        return;
    auto& l = layers[(size_t) layer];
    l.snapOn = false;
    Warp::Plan p;
    p.sourceBeats = p.targetBeats = l.originalBeats;
    edit (layer, p);
}

void RigLoopLab::setFollow (int layer, bool on)
{
    layers[(size_t) layer].follow = on;
    if (onChange)
        onChange (layer);
}

bool RigLoopLab::canUndo (int layer) const
{
    return stillOurs (layer) && ! layers[(size_t) layer].undo.empty();
}

void RigLoopLab::undo (int layer)
{
    if (! canUndo (layer))
        return;
    auto& l = layers[(size_t) layer];
    l.plan = l.undo.back();
    l.undo.pop_back();
    historySites[(size_t) layer].touch();
    l.snapOn = std::any_of (l.plan.markers.begin(), l.plan.markers.end(), [] (const Warp::Marker& m) { return ! m.manual; });
    render (layer);
}

void RigLoopLab::setOriginal (int layer, bool playOriginal)
{
    if (! stillOurs (layer))
        return;
    auto& l = layers[(size_t) layer];
    if (playOriginal == l.playingOriginal)
        return;
    if (playOriginal)
    {
        l.playingVersion = looper.setLayerAudio (layer, l.original, l.originalBeats);
        l.playingOriginal = true;
        if (onChange)
            onChange (layer);
        return;
    }
    l.playingOriginal = false;
    if (l.rendered.getNumSamples() > 0 && std::abs (l.renderedBpm - transport.getBpm()) < 0.01)
    {
        l.playingVersion = looper.setLayerAudio (layer, l.rendered, l.plan.targetBeats);
        if (onChange)
            onChange (layer);
    }
    else
        render (layer);
}

int RigLoopLab::print (int layer)
{
    if (! stillOurs (layer))
        return -1;
    const auto& l = layers[(size_t) layer];
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (i != layer && looper.getState (i) == RigLooper::State::empty)
        {
            auto audio = l.playingOriginal || l.rendered.getNumSamples() == 0 ? l.original : l.rendered;
            const double beats = l.playingOriginal || l.rendered.getNumSamples() == 0 ? l.originalBeats : l.plan.targetBeats;
            looper.setLayerAudio (i, std::move (audio), beats);
            return i;
        }
    return -1;
}

bool RigLoopLab::isRendering (int layer) const
{
    const auto& l = layers[(size_t) layer];
    return l.serial != l.appliedSerial;
}

void RigLoopLab::render (int layer)
{
    auto& l = layers[(size_t) layer];
    const int serial = ++l.serial;
    const double bpm = juce::jmax (20.0, transport.getBpm());
    const double sampleRate = looper.getSampleRate();
    const int length = juce::jmax (1, (int) std::round (l.plan.targetBeats * 60.0 / bpm * sampleRate));
    if (onChange)
        onChange (layer);

    // Nothing changed at all (no warp, same length, no transpose, formant or style, the tempo it
    // was opened at): the recording itself, as ORIGINAL plays it. Rendering would give it back
    // only when its length is exactly targetBeats at this tempo; a loop a few samples longer or
    // shorter would come back stretched to fit.
    if (isIdentity (l, bpm))
    {
        apply (layer, serial, l.original, bpm);
        return;
    }
    if (renderInline)
    {
        apply (layer, serial, Warp::render (l.original, l.plan, length, sampleRate), bpm);
        return;
    }
    auto original = std::make_shared<juce::AudioBuffer<float>> (l.original);
    const auto plan = l.plan;
    std::weak_ptr<bool> token = alive;
    pool.addJob ([this, token, layer, serial, original, plan, length, sampleRate, bpm]
    {
        auto out = std::make_shared<juce::AudioBuffer<float>> (Warp::render (*original, plan, length, sampleRate));
        juce::MessageManager::callAsync ([this, token, layer, serial, out, bpm]
        {
            if (auto t = token.lock(); t != nullptr && *t)
                apply (layer, serial, std::move (*out), bpm);
        });
    });
}

void RigLoopLab::apply (int layer, int serial, juce::AudioBuffer<float> audio, double bpm)
{
    auto& l = layers[(size_t) layer];
    if (serial != l.serial)
        return;   // a newer render is on its way
    l.appliedSerial = serial;
    if (! stillOurs (layer) || audio.getNumSamples() == 0)
    {
        if (onChange)
            onChange (layer);
        return;
    }
    l.rendered = std::move (audio);
    l.renderedBpm = bpm;
    if (! l.playingOriginal)
        l.playingVersion = looper.setLayerAudio (layer, l.rendered, l.plan.targetBeats);
    if (onChange)
        onChange (layer);
}

void RigLoopLab::tick()
{
    const double bpm = transport.getBpm();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        auto& l = layers[(size_t) i];
        if (! l.open)
            continue;
        if (! stillOurs (i))
        {
            if (! isRendering (i) && looper.getState (i) != RigLooper::State::playing)
                l.open = false;   // cleared or recording again: the lab lets go
            continue;
        }
        if (l.follow && ! l.playingOriginal && ! isRendering (i) && std::abs (bpm - l.renderedBpm) > 0.01)
            render (i);
    }
}
