#pragma once
#include <cstdint>

namespace BowInput::Timing {

    inline constexpr std::uint64_t kFakeEnableBumperDelayMs = 200;

    inline constexpr std::uint64_t kDisableSkipEquipDelayMs = 500;

    inline constexpr std::uint64_t kAutoDrawWatchdogMs = 400;

    inline constexpr std::uint64_t kPostExitAttackDownDelayMs = 100;
    inline constexpr std::uint64_t kPostExitAttackTapMs = 200;
    inline constexpr std::uint64_t kPostExitAttackMinHoldMs = 200;

}