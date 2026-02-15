#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include "RE/I/InputEvent.h"

namespace BowInput {
    constexpr int kMaxCode = 65536;

    namespace InputUtil {

        [[nodiscard]] inline int NormalizePadCode(int v) noexcept {
            if (v >= 0) return v;
            if (v == -1) return -1;
            return -v - 1;
        }

        [[nodiscard]] inline int EncodeCapture(RE::INPUT_DEVICE dev, int code) noexcept {
            return (dev == RE::INPUT_DEVICE::kGamepad) ? (-code - 1) : code;
        }
    }

    struct InputState {
        std::array<std::atomic_bool, kMaxCode> kbDown{};
        std::array<std::atomic_bool, kMaxCode> gpDown{};

        std::vector<int> kbDownList;
        std::vector<int> gpDownList;

        InputState() {
            kbDownList.reserve(32);
            gpDownList.reserve(32);
        }

        void Clear();

        void OnButton(RE::INPUT_DEVICE dev, int code, bool isPressed, bool isDownEdge, bool isUpEdge);

        [[nodiscard]] std::span<const int> DownList(RE::INPUT_DEVICE dev) const;
    };

    InputState& Inputs() noexcept;
}