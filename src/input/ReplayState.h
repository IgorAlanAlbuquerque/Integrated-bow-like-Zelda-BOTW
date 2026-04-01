#pragma once
#include <cstdint>
#include <vector>

#include "PCH.h"

namespace BowInput {

    struct BowRetainedEvent {
        std::uint32_t rawIdCode{0};
        RE::BSFixedString userEvent{};
        float value{0.0f};
        float heldSecs{0.0f};
    };

    struct BowReplayState {
        bool armed{false};
        std::uint32_t rawIdCode{0};
        RE::BSFixedString userEvent{};
        bool valueAboveHalf{false};
    };

    struct BowDeferredQueue {
        std::vector<BowRetainedEvent> retained;
        std::vector<BowRetainedEvent> deferred;
    };

    BowDeferredQueue& GetBowDeferredQueue() noexcept;
    BowReplayState& GetBowReplayState() noexcept;

    void CancelBowPending() noexcept;
    void ConfirmBowPending() noexcept;
    bool DrainOneBowDeferredEvent() noexcept;
    void ResetBowReplayState() noexcept;
}