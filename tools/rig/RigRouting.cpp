#include "RigRouting.h"

#include "BankModel.h"

juce::String RigRouting::controlName (int c)
{
    switch (c)
    {
        case mpcLevel:     return "MPC level";
        case samplerLevel: return "Sampler level";
        case masterLevel:  return "Master level";
        case mpcMute:      return "MPC mute";
        case samplerMute:  return "Sampler mute";
        case play:         return "Play";
        case stop:         return "Stop";
        case metronome:    return "Metronome";
        default:           return {};
    }
}

RigRouting::RigRouting()
{
    for (auto& a : assigned)
        a = -1;
}

void RigRouting::forget (int control)
{
    if (juce::isPositiveAndBelow (control, (int) numControls))
        assigned[(size_t) control] = -1;
}

std::pair<int, int> RigRouting::getAssignment (int control) const
{
    if (! juce::isPositiveAndBelow (control, (int) numControls))
        return { 0, -1 };
    const int a = assigned[(size_t) control].load();
    if (a < 0)
        return { 0, -1 };
    return { a / 128, a % 128 };
}

void RigRouting::setAssignment (int control, int channel, int cc)
{
    if (! juce::isPositiveAndBelow (control, (int) numControls))
        return;
    if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
    {
        assigned[(size_t) control] = -1;
        return;
    }
    // One control per knob: take it off anything else first.
    const int packed = channel * 128 + cc;
    for (auto& a : assigned)
        if (a.load() == packed)
            a = -1;
    assigned[(size_t) control] = packed;
}

int RigRouting::mpcNoteForPad (int padNote) const
{
    const int i = padNote - kFirstPadNote;
    if (! juce::isPositiveAndBelow (i, 16))
        return -1;
    return noteForPad (juce::jlimit (0, 3, padBank.load()) * 16 + i);
}

void RigRouting::handleController (int channel, int cc, int value, Result& result)
{
    const int packed = channel * 128 + cc;

    const int teach = learning.load();
    if (teach >= 0)
    {
        // Buttons send a press and a release; learn on the press so the release doesn't
        // count as a press of the new control.
        if (isFader (teach) || value > 0)
        {
            setAssignment (teach, channel, cc);
            lastValue[(size_t) teach] = value;
            learning = -1;
        }
        return;
    }

    for (int c = 0; c < numControls; ++c)
    {
        if (assigned[(size_t) c].load() != packed)
            continue;
        if (isFader (c))
            result.values[(size_t) c] = (float) value / 127.0f;
        else if (value > 0 && lastValue[(size_t) c] == 0)
            ++result.presses[(size_t) c];
        lastValue[(size_t) c] = value;
    }
}

void RigRouting::route (const juce::MidiBuffer& in, juce::MidiBuffer& toMpc, juce::MidiBuffer& toSampler, Result& result)
{
    result.values.fill (-1.0f);
    result.presses.fill (0);

    const int pads = padChannel.load();
    const auto keys = (Dest) keysTo.load();

    for (const auto metadata : in)
    {
        auto m = metadata.getMessage();
        const int at = metadata.samplePosition;

        if (m.isController())
        {
            // All notes off and the like go to both tools; the rest are knobs and faders.
            if (m.getControllerNumber() >= 120)
            {
                toMpc.addEvent (m, at);
                toSampler.addEvent (m, at);
                continue;
            }
            handleController (m.getChannel(), m.getControllerNumber(), m.getControllerValue(), result);
            continue;
        }

        const bool isNote = m.isNoteOnOrOff() || m.isAftertouch();
        if (isNote && pads > 0 && m.getChannel() == pads)
        {
            const int note = mpcNoteForPad (m.getNoteNumber());
            if (note >= 0)
                m.setNoteNumber (note);
            toMpc.addEvent (m, at);
            continue;
        }

        if (isNote || m.isPitchWheel() || m.isChannelPressure())
        {
            if (keys == Dest::sampler)
                toSampler.addEvent (m, at);
            else if (keys == Dest::mpc)
                toMpc.addEvent (m, at);
        }
    }
}
