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

juce::String factorySoundFor (Library& lib, const Kit& kit, int pad, juce::String* name)
{
    if (! juce::isPositiveAndBelow (pad, kPads))
        return {};
    for (int index = 0; index < (int) factoryKits().size(); ++index)
    {
        if (kit.name != Kit::mpcName (juce::String (factoryKits()[(size_t) index].name).toUpperCase()))
            continue;
        const auto names = crate::factory::soundNames (index);
        const int slot = pad % 16;
        if (slot >= (int) names.size())
            return {};
        // the bank still plays one of the kit's sounds, so this bank is the one it went on
        bool onBank = false;
        const int first = pad / 16 * 16;
        for (int i = first; i < first + 16 && ! onBank; ++i)
            if (const auto* item = lib.find (kit.pads[(size_t) i].soundId); item != nullptr && item->tag == kTagFactory)
                onBank = std::find (names.begin(), names.end(), item->name) != names.end();
        if (! onBank)
            return {};
        juce::String id;
        for (auto& item : lib.items())
            if (item.tag == kTagFactory && item.name == names[(size_t) slot])
                id = item.id;
        if (id.isEmpty())
            id = lib.add (names[(size_t) slot], kTagFactory, crate::factory::makeSound (index, slot), FxKnobs(), {});
        if (name != nullptr)
            *name = names[(size_t) slot];
        return id;
    }
    return {};
}
}
