#include "UnMapBlock.h"

#include "../PCH.h"

namespace UnMapBlock {
    SavedLeftAttackMapping g_savedLeftAttack;
}

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

                spdlog::info("[IntegratedBow] No-left-block patch ENABLED (gamepad leftAttack unmapped).");
            } else {
                if (g_savedLeftAttack.saved) {
                    m.inputKey = g_savedLeftAttack.inputKey;
                    m.modifier = g_savedLeftAttack.modifier;

                    spdlog::info("[IntegratedBow] No-left-block patch DISABLED (gamepad leftAttack restored).");
                }
            }
        }
    }
}