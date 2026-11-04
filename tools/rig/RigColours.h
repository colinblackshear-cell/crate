#pragma once

#include <juce_graphics/juce_graphics.h>

#include "RigEngine.h"

// Each tool's colour, the same on its tab, its mixer strip and its Stage tile.
namespace RigColours
{
inline const juce::Colour mpc { 0xffff8a3d }, sampler { 0xff8fd3c8 }, vocoder { 0xffb48cff }, master { 0xffeeeeee },
                          bass { 0xff4fc3f7 }, synth { 0xfff06292 }, input { 0xffffd54f }, loops { 0xff81c784 },
                          song { 0xffffb74d };

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
        default:                      return master;
    }
}
}
