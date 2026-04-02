#pragma once
#include <cstdint>

#include "Input/HotkeyDetector.h"
#include "PCH.h"

namespace BowInput {

    void ProcessBowLogic(float dt);

    void SetMode(int mode);
    void SetKeyScanCodes(int k1, int k2, int k3);
    void SetGamepadButtons(int b1, int b2, int b3);

    void RequestGamepadCapture();
    [[nodiscard]] int PollCapturedGamepadButton();

    [[nodiscard]] bool IsHotkeyDown();
    [[nodiscard]] bool IsUnequipAllowed() noexcept;
    void BlockUnequipForMs(std::uint64_t ms) noexcept;
    void ForceAllowUnequip() noexcept;

    void HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>* src);
    void HandleCaptureEvents(RE::InputEvent** a_evns);
    void CancelIfPendingActive();

    [[nodiscard]] const HotkeyConfig& GetHotkeyConfig() noexcept;
    [[nodiscard]] const HotkeyRuntime& GetHotkeyRuntime() noexcept;
    [[nodiscard]] HotkeyRuntime& GetHotkeyRuntimeMut() noexcept;
}