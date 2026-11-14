#include "RigPlugin.h"
#include "RigView.h"

namespace
{
    // Kaleidobox Vocoder, MPC, Drums, Bass and Synth: this box with one tool in it (a StripId);
    // -1 is the whole box.
#if defined (RIG_PLUGIN_TOOL)
    constexpr int kOnlyTool = RIG_PLUGIN_TOOL;
#else
    constexpr int kOnlyTool = -1;
#endif
}

juce::AudioProcessor::BusesProperties RigPlugin::buses (int onlyTool)
{
#if defined (RIG_PLUGIN_INPUT)
    // Kaleidobox Input (an AU MIDI-controlled effect): the track's audio comes in on the main
    // input, or the Side Chain's on the second one. One stereo output.
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withInput ("Side chain", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Rig", juce::AudioChannelSet::stereo(), true);
#else
    auto b = BusesProperties()
                 .withInput ("Input", juce::AudioChannelSet::stereo(), false)
                 .withInput ("Side chain", juce::AudioChannelSet::stereo(), false)
                 .withOutput ("Rig", juce::AudioChannelSet::stereo(), true);
    // A one-tool plugin is its own track already: no outputs per tool.
    if (onlyTool < 0)
        for (int s = 0; s < RigEngine::numStems; ++s)
            b = b.withOutput (RigEngine::stemName (s), juce::AudioChannelSet::stereo(), false);
    return b;
#endif
}

RigPlugin::RigPlugin (int tool) : AudioProcessor (buses (tool)), onlyTool (tool)
{
    // The tools inside are the Rig's, not plugins of their own: they behave as they do in the
    // Rig app (they'd think they were in Logic otherwise).
    juce::AudioProcessor::setTypeOfNextNewPlugin (wrapperType_Undefined);
    engine = std::make_unique<RigEngine>();
    engine->micOn = true;   // Logic's Side Chain decides
    engine->setOnlyTool (onlyTool);
}

const juce::String RigPlugin::getName() const
{
    return onlyTool < 0 ? juce::String ("Kaleidobox") : "Kaleidobox " + RigEngine::onlyToolName (onlyTool);
}

RigPlugin::~RigPlugin() = default;

bool RigPlugin::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.outputBuses.isEmpty() || layouts.outputBuses[0] != juce::AudioChannelSet::stereo())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
        if (! layouts.outputBuses[i].isDisabled() && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void RigPlugin::prepareToPlay (double sampleRate, int maximumBlockSize)
{
    mic.setSize (2, juce::jmax (16, maximumBlockSize));
    engine->prepare (sampleRate, maximumBlockSize);
}

void RigPlugin::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // The mic first: the input and output share channels. Logic's Side Chain can arrive on
    // either input, so whatever comes in on both is the mic.
    int micChannels = 0;
    if (n <= mic.getNumSamples())
    {
        for (int b = 0; b < getBusCount (true); ++b)
        {
            if (! getBus (true, b)->isEnabled())
                continue;
            const auto in = getBusBuffer (buffer, true, b);
            if (in.getNumChannels() == 0)
                continue;
            if (micChannels == 0)
                mic.clear (0, n);
            micChannels = 2;
            for (int ch = 0; ch < 2; ++ch)
                mic.addFrom (ch, 0, in, juce::jmin (ch, in.getNumChannels() - 1), 0, n);
        }
    }

    if (auto* head = getPlayHead())
        engine->getTransport().setHostPosition (head->getPosition());
    else
        engine->getTransport().setHostPosition ({});

    // The tools' own outputs, where Logic has turned them on.
    for (int s = 0; s < RigEngine::numStems; ++s)
    {
        float* const* out = nullptr;
        if (auto* bus = getBus (false, s + 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() == 2)
        {
            auto stem = getBusBuffer (buffer, false, s + 1);
            stem.clear();
            auto& ptrs = stemChannels[(size_t) s];
            ptrs = { stem.getWritePointer (0), stem.getWritePointer (1) };
            out = ptrs.data();
        }
        engine->setStemOutput (s, out);
        engine->setStemOn (s, out != nullptr);
    }

    auto main = getBusBuffer (buffer, false, 0);
    engine->setHostMidi (&midi);
    engine->process (mic.getArrayOfReadPointers(), micChannels, main.getArrayOfWritePointers(), main.getNumChannels(), n);
    engine->setHostMidi (nullptr);
    for (int s = 0; s < RigEngine::numStems; ++s)
        engine->setStemOutput (s, nullptr);
    midi.clear();
}

namespace
{
    class RigPluginEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit RigPluginEditor (RigPlugin& p) : AudioProcessorEditor (p), view (p.getEngine(), nullptr)
        {
            addAndMakeVisible (view);
            setResizable (true, true);
            setResizeLimits (1000, 700, 4000, 3000);
            setSize (1440, 960);
        }

        void resized() override { view.setBounds (getLocalBounds()); }

    private:
        RigView view;
    };
}

juce::AudioProcessorEditor* RigPlugin::createEditor()
{
    return new RigPluginEditor (*this);
}

// The loops are too big for Logic's song file, so they go in WAVs in a folder of their own
// for each song (the song keeps the folder's name).
juce::File RigPlugin::loopsFile() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("Crate").getChildFile ("Rig").getChildFile ("In Logic")
        .getChildFile (songId + ".crateshow");
}

void RigPlugin::getStateInformation (juce::MemoryBlock& data)
{
    if (songId.isEmpty())
        songId = juce::Uuid().toString();
    auto show = engine->saveShow();
    show.setProperty ("logicSong", songId, nullptr);
    if (auto xml = show.createXml())
        copyXmlToBinary (*xml, data);
    loopsFile().getParentDirectory().createDirectory();
    engine->writeLoopAudio (loopsFile());
}

void RigPlugin::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.hasType (RigEngine::kShowType))
        {
            setOldToolState (data, size);
            return;
        }
        engine->loadShow (tree);
        songId = tree.getProperty ("logicSong").toString();
        if (songId.isNotEmpty())
            engine->readLoopAudio (loopsFile());
    }
    else
    {
        setOldToolState (data, size);
    }
}

// Songs made with the first Kaleidobox Bass, Synth and Drums plugins (the tools on their own,
// before they were the box's) saved just the tool: it goes on the tool here, so they still sound
// as they did.
void RigPlugin::setOldToolState (const void* data, int size)
{
    auto& e = *engine;
    juce::AudioProcessor* tool = nullptr;
    switch (onlyTool)
    {
        case RigEngine::bassStrip:  tool = &e.getBass(); break;
        case RigEngine::synthStrip: tool = &e.getSynth(); break;
        case RigEngine::drumsStrip: tool = &e.getDrums(); break;
        default: break;
    }
    if (tool != nullptr && size > 0)
        tool->setStateInformation (data, size);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RigPlugin (kOnlyTool);
}
