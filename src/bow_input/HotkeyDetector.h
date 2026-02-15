#pragma once

#include <cstdint>

#include "InputState.h"

namespace RE {
    class PlayerCharacter;
}

namespace BowInput {
    inline constexpr int kMaxComboKeys = 3;

    struct HotkeyConfig {
        std::array<int, kMaxComboKeys> bowKeyScanCodes{};
        std::array<int, kMaxComboKeys> bowPadButtons{};
    };

    struct HotkeyRuntime {
        bool prevRawKbComboDown{false};
        bool prevRawGpComboDown{false};

        bool suppressUntilReleased{false};

        std::uint8_t exclusivePendingSrc{0};
        float exclusivePendingTimer{0.0f};
    };

    struct IHotkeyCallbacks {
        virtual void OnHotkeyAcceptedPressed(RE::PlayerCharacter* player, bool blocked) = 0;
        virtual void OnHotkeyAcceptedReleased(RE::PlayerCharacter* player, bool blocked) = 0;
        virtual ~IHotkeyCallbacks() = default;
    };

    struct HotkeyDetector {
        static void Tick(RE::PlayerCharacter* player, float dt, const HotkeyConfig& hk, const InputState& inputs,
                         bool requireExclusive, bool blocked, bool& inOut_hotkeyDown, HotkeyRuntime& rt,
                         IHotkeyCallbacks& cb);
    };
}