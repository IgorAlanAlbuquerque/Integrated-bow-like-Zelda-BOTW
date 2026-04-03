#include "EventFilter.h"

#include <ranges>

#include "Config/Config.h"
#include "Input/HotkeyDetector.h"
#include "Input/InputHandler.h"
#include "Input/InputState.h"
#include "Input/InputTiming.h"
#include "Input/ModeController.h"
#include "Input/ReplayState.h"
#include "PCH.h"

using namespace BowInput;
using namespace BowInput::Timing;

namespace {
    inline bool ComboContains(const std::array<int, kMaxComboKeys>& combo, int unifiedCode) {
        return std::ranges::any_of(combo, [&](int v) { return v == unifiedCode; });
    }

    inline bool ComboIsFullyDown(const std::array<int, kMaxComboKeys>& combo, const InputState& inputs) {
        auto enabled = combo | std::views::filter([](int v) { return v != -1; });
        if (std::ranges::empty(enabled)) return false;
        return std::ranges::all_of(enabled, [&](int v) {
            if (v < 0 || v >= kMaxCode) return false;
            return inputs.down[static_cast<std::size_t>(v)].load(std::memory_order_relaxed);
        });
    }

    inline bool PendingActive(const HotkeyRuntime& rt) { return rt.exclusivePendingSrc != 0; }

    bool ShouldFilterBow(int unifiedCode, std::uint32_t rawIdCode, const RE::BSFixedString& userEvent, float value,
                         float heldSecs) {
        const auto& hk = GetHotkeyConfig();
        const auto& rt = GetHotkeyRuntime();
        const bool hotkeyDown = BowModeController::Get().hotkeyDown;
        const auto& inputs = Inputs();

        if (!ComboContains(hk.bowCombo, unifiedCode)) return false;

        auto& rp = GetBowReplayState();
        if (rp.armed && rp.rawIdCode == rawIdCode && rp.userEvent == userEvent && (value > 0.5f) == rp.valueAboveHalf) {
            rp.armed = false;
            return false;
        }

        if (hotkeyDown) return true;
        if (rt.suppressUntilReleased) return true;

        if (ComboIsFullyDown(hk.bowCombo, inputs)) {
            if (PendingActive(rt)) GetBowDeferredQueue().retained.push_back({rawIdCode, userEvent, value, heldSecs});
            return true;
        }

        if (PendingActive(rt)) {
            GetBowDeferredQueue().retained.push_back({rawIdCode, userEvent, value, heldSecs});
            return true;
        }

        return false;
    }
}

void BowInput::UpdateBowInputState(RE::InputEvent** a_evns) {
    if (!a_evns) return;
    auto& inputs = Inputs();
    for (auto* e = *a_evns; e; e = e->next) {
        const auto* btn = e->AsButtonEvent();
        if (!btn) continue;
        inputs.OnButton(btn->GetDevice(), static_cast<int>(btn->idCode), btn->IsPressed());
    }
}

void BowInput::FilterBowEvents(RE::InputEvent** a_evns) {
    if (!a_evns) return;

    RE::InputEvent* prev = nullptr;
    RE::InputEvent* cur = *a_evns;

    while (cur) {
        RE::InputEvent* next = cur->next;
        bool remove = false;

        if (const auto* btn = cur->AsButtonEvent()) {
            const auto dev = btn->GetDevice();
            const std::uint32_t rawCode = btn->idCode;
            auto unified = static_cast<int>(rawCode);

            if (dev == RE::INPUT_DEVICE::kGamepad) {
                const int idx = InputUtil::GamepadIdToIndex(static_cast<int>(rawCode));
                if (idx < 0) {
                    prev = cur;
                    cur = next;
                    continue;
                }
                unified = kGamepadOffset + idx;
            } else if (dev == RE::INPUT_DEVICE::kMouse) {
                const int idx = static_cast<int>(rawCode);
                if (idx < 0 || idx >= 10) {
                    prev = cur;
                    cur = next;
                    continue;
                }
                unified = kMouseOffset + idx;
            }

            if (unified >= 0 && unified < kMaxCode)
                remove = ShouldFilterBow(unified, rawCode, btn->QUserEvent(), btn->Value(), btn->HeldDuration());
        }

        if (remove) {
            if (prev)
                prev->next = next;
            else
                *a_evns = next;
        } else {
            prev = cur;
        }
        cur = next;
    }
}

void BowInput::DrainBowDeferredEvents() { DrainOneBowDeferredEvent(); }