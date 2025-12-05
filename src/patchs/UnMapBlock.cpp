#include "UnMapBlock.h"

#include "../PCH.h"

UnMapBlock::SavedLeftAttackMapping UnMapBlock::g_savedLeftAttack;

void UnMapBlock::SetNoLeftBlockPatch(bool patch) {
    auto const* controlMap = RE::ControlMap::GetSingleton();
    auto const* userEvents = RE::UserEvents::GetSingleton();
    if (!controlMap || !userEvents) {
        return;
    }

    const auto ctx = RE::ControlMap::InputContextID::kGameplay;
    auto* context = controlMap->controlMap[ctx];
    if (!context) {
        return;
    }

    const auto devIndex = static_cast<std::size_t>(RE::INPUT_DEVICE::kGamepad);
    auto& mappings = context->deviceMappings[devIndex];

    for (auto& m : mappings) {
        if (m.eventID == userEvents->leftAttack) {
            if (patch) {
                if (!g_savedLeftAttack.saved) {
                    g_savedLeftAttack.saved = true;
                    g_savedLeftAttack.inputKey = m.inputKey;
                    g_savedLeftAttack.modifier = m.modifier;
                }

                m.inputKey = static_cast<std::uint16_t>(RE::ControlMap::kInvalid);
                m.modifier = 0;
            } else {
                if (g_savedLeftAttack.saved) {
                    m.inputKey = g_savedLeftAttack.inputKey;
                    m.modifier = g_savedLeftAttack.modifier;
                }
            }
        }
    }
}