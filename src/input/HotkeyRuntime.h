#pragma once
#include "HotkeyDetector.h"

namespace BowInput {
    [[nodiscard]] const HotkeyConfig& GetHotkeyConfig() noexcept;
    [[nodiscard]] const HotkeyRuntime& GetHotkeyRuntime() noexcept;
    [[nodiscard]] HotkeyRuntime& GetHotkeyRuntimeMut() noexcept;
}