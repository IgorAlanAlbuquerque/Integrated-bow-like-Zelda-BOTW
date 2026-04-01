#pragma once
#include <array>
#include <atomic>

#include "PCH.h"

namespace BowInput {
    constexpr int kMaxCode = 400;
    constexpr int kMouseOffset = 256;
    constexpr int kGamepadOffset = 266;

    namespace InputUtil {
        [[nodiscard]] inline int GamepadIdToIndex(int idCode) noexcept {
            using Key = RE::BSWin32GamepadDevice::Key;
            switch (static_cast<Key>(idCode)) {
                case Key::kUp:
                    return 0;
                case Key::kDown:
                    return 1;
                case Key::kLeft:
                    return 2;
                case Key::kRight:
                    return 3;
                case Key::kStart:
                    return 4;
                case Key::kBack:
                    return 5;
                case Key::kLeftThumb:
                    return 6;
                case Key::kRightThumb:
                    return 7;
                case Key::kLeftShoulder:
                    return 8;
                case Key::kRightShoulder:
                    return 9;
                case Key::kA:
                    return 10;
                case Key::kB:
                    return 11;
                case Key::kX:
                    return 12;
                case Key::kY:
                    return 13;
                case Key::kLeftTrigger:
                    return 14;
                case Key::kRightTrigger:
                    return 15;
                default:
                    return -1;
            }
        }

        [[nodiscard]] inline int ToUnifiedIndex(RE::INPUT_DEVICE dev, int code) noexcept {
            if (dev == RE::INPUT_DEVICE::kGamepad) {
                const int idx = GamepadIdToIndex(code);
                if (idx < 0) return -1;
                return kGamepadOffset + idx;
            }
            if (dev == RE::INPUT_DEVICE::kMouse) {
                if (code < 0 || code >= 10) return -1;
                return kMouseOffset + code;
            }
            return code;
        }
    }

    struct InputState {
        std::array<std::atomic_bool, kMaxCode> down{};

        void Clear();
        void OnButton(RE::INPUT_DEVICE dev, int code, bool isPressed);
    };

    InputState& Inputs() noexcept;
}