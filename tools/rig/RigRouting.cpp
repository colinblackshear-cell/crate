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
        case vocoderLevel: return "Vocoder level";
        case vocoderMute:  return "Vocoder mute";
        case vocoderOn:    return "Vocoder on/off";
        case bassLevel:    return "Bass level";
        case synthLevel:   return "Synth level";
        case bassMute:     return "Bass mute";
        case synthMute:    return "Synth mute";
        case inputLevel:   return "Input level";
        case inputMute:    return "Input mute";
        case inputHear:    return "Input on/off";
        case loopsLevel:   return "Loops level";
        case loopsMute:    return "Loops mute";
        case nextScene:    return "Next scene";
        case previousScene: return "Previous scene";
        default:
            if (c >= loopRecord1 && c <= loopRecord8)
                return "Loop " + juce::String (c - loopRecord1 + 1) + " record";
            if (c >= loopMute1 && c <= loopMute8)
                return "Loop " + juce::String (c - loopMute1 + 1) + " mute";
            return {};
    }
}

juce::String RigRouting::destName (int d)
{
    switch ((Dest) d)
    {
        case Dest::off:     return "Nothing";
        case Dest::mpc:     return "MPC pads";
        case Dest::sampler: return "Sampler";
        case Dest::vocoder: return "Vocoder";
        case Dest::bass:    return "Bass";
        case Dest::synth:   return "Synth";
        default:            return {};
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

std::pair<int, int> RigRouting::takeCaughtKnob()
{
    const int c = caught.exchange (-1);
    if (c < 0)
        return { 0, -1 };
    return { c / 128, c % 128 };
}

void RigRouting::addParameterKnob (int channel, int cc, int tool, int parameter)
{
    if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
        return;
    const int packed = channel * 128 + cc;
    // One knob, one setting: a knob learned again moves to the new setting.
    for (auto& k : knobs)
        if (k.packed.load() == packed)
            k.packed = -1;
    for (auto& k : knobs)
        if (k.packed.load() < 0)
        {
            k.tool = tool;
            k.parameter = parameter;
            k.packed = packed;
            return;
        }
}

void RigRouting::removeParameterKnob (int index)
{
    int n = 0;
    for (auto& k : knobs)
        if (k.packed.load() >= 0 && n++ == index)
        {
            k.packed = -1;
            return;
        }
}

void RigRouting::clearParameterKnobs()
{
    for (auto& k : knobs)
        k.packed = -1;
}

std::vector<RigRouting::ParameterKnob> RigRouting::getParameterKnobs() const
{
    std::vector<ParameterKnob> list;
    for (const auto& k : knobs)
        if (const int p = k.packed.load(); p >= 0)
            list.push_back ({ p / 128, p % 128, k.tool.load(), k.parameter.load() });
    return list;
}

void RigRouting::handleController (int channel, int cc, int value, Result& result)
{
    const int packed = channel * 128 + cc;

    if (catching.load())
    {
        caught = packed;
        catching = false;
        return;
    }

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

    for (const auto& k : knobs)
        if (k.packed.load() == packed && result.numParamMoves < (int) result.paramMoves.size())
        {
            // The latest value per knob wins within a block.
            int slot = result.numParamMoves;
            for (int i = 0; i < result.numParamMoves; ++i)
                if (result.paramMoves[(size_t) i].tool == k.tool.load() && result.paramMoves[(size_t) i].parameter == k.parameter.load())
                    slot = i;
            result.paramMoves[(size_t) slot] = { k.tool.load(), k.parameter.load(), (float) value / 127.0f };
            if (slot == result.numParamMoves)
                ++result.numParamMoves;
        }
}

void RigRouting::route (const juce::MidiBuffer& in, std::array<juce::MidiBuffer*, kNumDests>& to, Result& result)
{
    result.values.fill (-1.0f);
    result.presses.fill (0);
    result.numParamMoves = 0;

    const int pads = padChannel.load();
    const int upper = keysTo.load(), lower = lowerKeysTo.load();
    const int split = splitNote.load();
    auto send = [&] (int dest, const juce::MidiMessage& m, int at)
    {
        if (dest > 0 && dest < kNumDests && to[(size_t) dest] != nullptr)
            to[(size_t) dest]->addEvent (m, at);
    };

    for (const auto metadata : in)
    {
        auto m = metadata.getMessage();
        const int at = metadata.samplePosition;

        if (m.isController())
        {
            // All notes off and the like go to every tool; the rest are knobs and faders.
            if (m.getControllerNumber() >= 120)
            {
                for (int d = 1; d < kNumDests; ++d)
                    send (d, m, at);
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
            send ((int) Dest::mpc, m, at);
            continue;
        }

        if (isNote)
        {
            auto& where = sounding[(size_t) juce::jlimit (0, 15, m.getChannel() - 1)][(size_t) m.getNoteNumber()];
            int dest = split > 0 && m.getNoteNumber() < split ? lower : upper;
            if (m.isNoteOn())
                where = (juce::uint8) juce::jmax (0, dest);
            else if (m.isNoteOff())
            {
                // A key let go goes where it was played, so nothing's left hanging.
                if (where > 0)
                    dest = where;
                where = 0;
            }
            else if (where > 0)
                dest = where;   // aftertouch follows the key
            send (dest, m, at);
        }
        else if (m.isPitchWheel() || m.isChannelPressure())
        {
            send (upper, m, at);
            if (split > 0 && lower != upper)
                send (lower, m, at);
        }
    }
}
