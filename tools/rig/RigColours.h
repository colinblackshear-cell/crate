#pragma once

#include <juce_graphics/juce_graphics.h>

#include "RigEngine.h"

// Each tool's colour, the same on its tab, its mixer strip and its Stage tile.
namespace RigColours
{
// The design's track colours (drums red, bass orange, synth yellow, vocoder teal, loops green).
inline const juce::Colour mpc { 0xffe85a3a }, sampler { 0xffc58fd6 }, vocoder { 0xff5fb3c4 }, master { 0xfff1e7d1 },
                          bass { 0xfff39b33 }, synth { 0xffefcb45 }, input { 0xffd8c49a }, loops { 0xff9cc27a },
                          song { 0xffffb23e }, drums { 0xffd8452c };   // the drum machine: its step-key red

inline juce::Colour forStrip (int strip)
{
    switch (strip)
    {
        case RigEngine::mpcStrip:     return mpc;
        case RigEngine::samplerStrip: return sampler;
        case RigEngine::vocoderStrip: return vocoder;
        case RigEngine::bassStrip:    return bass;
        case RigEngine::synthStrip:   return synth;
        case RigEngine::inputStrip:   return input;
        case RigEngine::loopsStrip:   return loops;
        case RigEngine::drumsStrip:   return drums;
        default:                      return master;
    }
}
}
