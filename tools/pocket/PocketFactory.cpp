#include "PocketFactory.h"

#include "FactoryKits.h"

namespace pocket
{
const std::vector<FactoryKit>& factoryKits()
{
    static const std::vector<FactoryKit> list = []
    {
        std::vector<FactoryKit> out;
        for (auto& k : crate::factory::kits())
            out.push_back ({ k.name, k.what, k.colour });
        return out;
    }();
    return list;
}

std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> makeFactorySounds (int index)
{
    return crate::factory::makeSounds (index);
}

int loadFactoryKit (Library& lib, Kit& kit, int index, int bank)
{
    if (index < 0 || index >= (int) factoryKits().size())
        return 0;
    const auto names = crate::factory::soundNames (index);
    int filled = 0;
    for (int i = 0; i < (int) names.size() && i < 16; ++i)
    {
        juce::String id;
        for (auto& item : lib.items())
            if (item.tag == kTagFactory && item.name == names[(size_t) i])
                id = item.id;
        if (id.isEmpty())
            id = lib.add (names[(size_t) i], kTagFactory, crate::factory::makeSound (index, i), FxKnobs(), {});
        const int pad = bank * 16 + i;
        if (id.isNotEmpty() && kit.assign (pad, id, 0, -1, names[(size_t) i]))
            ++filled;
    }
    kit.name = Kit::mpcName (juce::String (factoryKits()[(size_t) index].name).toUpperCase());
    return filled;
}
}
