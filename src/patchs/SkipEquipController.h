#pragma once
#include <atomic>
#include <cstdint>

namespace RE {
    class PlayerCharacter;
}

namespace IntegratedBow::SkipEquipController {
    void Enable(RE::PlayerCharacter* pc, int loadDelayMs, bool skip3D);
    void Disable(RE::PlayerCharacter* pc);

    void EnableAndArmDisable(RE::PlayerCharacter* pc, int loadDelayMs, bool skip3D, std::uint64_t delayMs);

    void ArmDisable(std::uint64_t delayMs);

    void Cancel();

    void Tick();
}
