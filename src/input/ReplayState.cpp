#include "ReplayState.h"

#include "BowState.h"

namespace BowInput {

    BowDeferredQueue& GetBowDeferredQueue() noexcept {
        static BowDeferredQueue s;  // NOSONAR
        return s;
    }

    BowReplayState& GetBowReplayState() noexcept {
        static BowReplayState s;  // NOSONAR
        return s;
    }

    void CancelBowPending() noexcept {
        auto& q = GetBowDeferredQueue();
        for (auto& ev : q.retained) q.deferred.push_back(ev);
        q.retained.clear();
    }

    void ConfirmBowPending() noexcept {
        auto& q = GetBowDeferredQueue();
        auto& rp = GetBowReplayState();
        q.retained.clear();
        q.deferred.clear();
        rp.armed = false;
    }

    bool DrainOneBowDeferredEvent() noexcept {
        auto& q = GetBowDeferredQueue();
        auto& rp = GetBowReplayState();

        if (q.deferred.empty()) return false;

        const auto ev = q.deferred.front();
        q.deferred.erase(q.deferred.begin());

        rp.armed = true;
        rp.rawIdCode = ev.rawIdCode;
        rp.userEvent = ev.userEvent;
        rp.valueAboveHalf = ev.value > 0.5f;

        auto* synth = BowState::detail::MakeGenericButtonEvent(RE::INPUT_DEVICE::kMouse, ev.userEvent, ev.rawIdCode,
                                                               ev.value, ev.heldSecs);
        BowState::detail::EnqueueSyntheticEvent(synth);

        return true;
    }

    void ResetBowReplayState() noexcept {
        auto& q = GetBowDeferredQueue();
        auto& rp = GetBowReplayState();

        q.retained.clear();
        q.deferred.clear();
        rp = BowReplayState{};
    }
}