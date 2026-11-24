#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <atomic>

// Kaleidobox Visuals' choices, the same on the Mac and the phone: the five looks, the colours
// (the 12 notes' colours, or a set of six), and how wild and how long the trails are.
namespace crate::visuals
{
inline constexpr int kLooks = 5;
inline const char* lookName (int look)
{
    static const char* names[kLooks] { "Kaleido", "Tunnel", "Liquid", "Bloom", "Trails" };
    return names[juce::jlimit (0, kLooks - 1, look)];
}
inline const char* lookLine (int look)
{
    static const char* lines[kLooks] { "Mirrored shapes folding in 6 to 12 slices; the folds change every 2 bars.",
                                       "Fly through rings and stripes. Bass is the speed, the kick lights the rings.",
                                       "Melting colour that oozes and swirls, warped by the low end.",
                                       "A mandala of petals, one ring per tool, with the sound's spectrum as a halo.",
                                       "Your sound's wave drawn as a ring of light that spirals out forever." };
    return lines[juce::jlimit (0, kLooks - 1, look)];
}
// How much of the last picture stays behind in each look (Trails keeps the most).
inline float lookTrail (int look)
{
    static const float t[kLooks] { 0.55f, 0.45f, 0.7f, 0.5f, 1.0f };
    return t[juce::jlimit (0, kLooks - 1, look)];
}

// The colours. The first three follow the notes playing (each of the 12 notes owns a colour); the
// rest are fixed sets of six (Tools: each tool's own rail colour, brightest while it plays).
enum class Colours { scriabin, circle, yours, tools, acid, ember, ice, count };
inline const char* coloursName (Colours c)
{
    switch (c)
    {
        case Colours::scriabin: return "Scriabin";
        case Colours::circle: return "Circle of fifths";
        case Colours::yours: return "Your colours";
        case Colours::tools: return "Tools";
        case Colours::acid: return "Acid";
        case Colours::ember: return "Ember";
        case Colours::ice: return "Ice";
        default: return "";
    }
}
inline bool followsNotes (Colours c) { return c == Colours::scriabin || c == Colours::circle || c == Colours::yours; }

inline const char* noteName (int pc)
{
    static const char* names[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pc % 12) + 12) % 12];
}

using NoteColours = std::array<juce::Colour, 12>;
// Scriabin's colour keyboard (for Prometheus, 1910): the best-known historic note-to-colour map,
// laid round the circle of fifths. Synesthetes all see different colours; this is one famous set.
inline NoteColours scriabinColours()
{
    return { juce::Colour (0xffff0000), juce::Colour (0xff9000ff), juce::Colour (0xffffff00), juce::Colour (0xffb7468b),
             juce::Colour (0xffc3f2ff), juce::Colour (0xffab0034), juce::Colour (0xff7f8bfd), juce::Colour (0xffff8000),
             juce::Colour (0xffbb75fc), juce::Colour (0xff33cc33), juce::Colour (0xffa9677c), juce::Colour (0xff8ec9ff) };
}
// A rainbow walked round the circle of fifths: notes that sound good together sit side by side.
inline NoteColours circleColours()
{
    NoteColours c;
    for (int pc = 0; pc < 12; ++pc)
        c[(size_t) pc] = juce::Colour::fromHSL ((float) ((pc * 7) % 12) / 12.0f, 0.85f, 0.58f, 1.0f);
    return c;
}
inline std::array<juce::Colour, 6> fixedColours (Colours c)
{
    switch (c)
    {
        case Colours::acid: return { juce::Colour (0xffff2fd0), juce::Colour (0xff7b2cff), juce::Colour (0xff00e5ff), juce::Colour (0xff39ff88), juce::Colour (0xfffff23a), juce::Colour (0xffff6a00) };
        case Colours::ember: return { juce::Colour (0xff2b0a3d), juce::Colour (0xff8a1144), juce::Colour (0xffe8432f), juce::Colour (0xffffb23e), juce::Colour (0xfffff1c9), juce::Colour (0xffd8578e) };
        case Colours::ice: return { juce::Colour (0xff0b1d4a), juce::Colour (0xff1f6fd1), juce::Colour (0xff5fe0ff), juce::Colour (0xffe8fbff), juce::Colour (0xff9b7bff), juce::Colour (0xff3a2a8a) };
        default:   // Tools: DRUMS, PADS, BASS, SYNTH, VOCODER, DRONE, in their rail colours
            return { juce::Colour (0xfff0674f), juce::Colour (0xfff2b632), juce::Colour (0xfff0a24a), juce::Colour (0xff5fb3c4), juce::Colour (0xff82cf6a), juce::Colour (0xffd8578e) };
    }
}
inline const char* toolSlotName (int slot)
{
    static const char* n[6] { "DRUMS", "PADS", "BASS", "SYNTH", "VOCODER", "DRONE" };
    return n[juce::jlimit (0, 5, slot)];
}

// What's picked: shared by every picture open at once (the Mac's window and its second screen),
// written on the message thread, read by the picture. Saved by VisualSettings.
struct VisualState
{
    std::atomic<int> look { 0 };
    std::atomic<bool> drift { false };
    std::atomic<int> colours { (int) Colours::scriabin };
    std::atomic<int> wild { 3 };     // 1..4
    std::atomic<int> trails { 3 };   // 1..4
    juce::SpinLock yoursLock;        // message thread and the picture
    NoteColours yours = scriabinColours();

    Colours getColours() const { return (Colours) juce::jlimit (0, (int) Colours::count - 1, colours.load()); }
    NoteColours noteColours()
    {
        switch (getColours())
        {
            case Colours::circle: return circleColours();
            case Colours::yours: { const juce::SpinLock::ScopedLockType l (yoursLock); return yours; }
            default: return scriabinColours();
        }
    }
    void setYours (int pc, juce::Colour c)
    {
        const juce::SpinLock::ScopedLockType l (yoursLock);
        yours[(size_t) (((pc % 12) + 12) % 12)] = c;
    }
};
} // namespace crate::visuals
