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

        const bool inCombo = ComboContains(hk.bowCombo, unifiedCode);

        BOW_DEBUG_LOG(
            "[EventFilter] ShouldFilterBow: unifiedCode={} rawIdCode={} userEvent={} value={} heldSecs={} inCombo={} "
            "hotkeyDown={} suppressUntilReleased={} pendingSrc={}",
            unifiedCode, rawIdCode, userEvent.c_str(), value, heldSecs, inCombo, hotkeyDown, rt.suppressUntilReleased,
            static_cast<int>(rt.exclusivePendingSrc));

        if (!inCombo) return false;

        if (auto& rp = GetBowReplayState();
            rp.armed && rp.rawIdCode == rawIdCode && rp.userEvent == userEvent && (value > 0.5f) == rp.valueAboveHalf) {
            BOW_DEBUG_LOG("[EventFilter] ShouldFilterBow: replay passthrough rawIdCode={}", rawIdCode);
            rp.armed = false;
            return false;
        }

        if (hotkeyDown) {
            BOW_DEBUG_LOG("[EventFilter] ShouldFilterBow: filtered because hotkeyDown=true");
            return true;
        }

        if (rt.suppressUntilReleased) {
            BOW_DEBUG_LOG("[EventFilter] ShouldFilterBow: filtered because suppressUntilReleased=true");
            return true;
        }

        if (ComboIsFullyDown(hk.bowCombo, inputs)) {
            if (PendingActive(rt)) {
                GetBowDeferredQueue().retained.emplace_back(rawIdCode, userEvent, value, heldSecs);
                BOW_DEBUG_LOG("[EventFilter] ShouldFilterBow: combo fully down, retained deferred event");
            } else {
                BOW_DEBUG_LOG("[EventFilter] ShouldFilterBow: combo fully down, filtering event");
            }
            return true;
        }

        if (PendingActive(rt)) {
            GetBowDeferredQueue().retained.emplace_back(rawIdCode, userEvent, value, heldSecs);
            BOW_DEBUG_LOG("[EventFilter] ShouldFilterBow: pending active, retained deferred event");
            return true;
        }

        BOW_DEBUG_LOG("[EventFilter] ShouldFilterBow: event not filtered");
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
                    BOW_DEBUG_LOG("[EventFilter] FilterBowEvents: ignored unknown gamepad rawCode={}", rawCode);
                    prev = cur;
                    cur = next;
                    continue;
                }
                unified = kGamepadOffset + idx;
            } else if (dev == RE::INPUT_DEVICE::kMouse) {
                const auto idx = static_cast<int>(rawCode);
                if (idx < 0 || idx >= 10) {
                    BOW_DEBUG_LOG("[EventFilter] FilterBowEvents: ignored invalid mouse rawCode={}", rawCode);
                    prev = cur;
                    cur = next;
                    continue;
                }
                unified = kMouseOffset + idx;
            }

            BOW_DEBUG_LOG("[EventFilter] FilterBowEvents: dev={} rawCode={} unified={} pressed={} value={} held={}",
                          static_cast<int>(dev), rawCode, unified, btn->IsPressed(), btn->Value(), btn->HeldDuration());

            if (unified >= 0 && unified < kMaxCode)
                remove = ShouldFilterBow(unified, rawCode, btn->QUserEvent(), btn->Value(), btn->HeldDuration());

            BOW_DEBUG_LOG("[EventFilter] FilterBowEvents: decision remove={} unified={}", remove, unified);
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