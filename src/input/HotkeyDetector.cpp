#include "HotkeyDetector.h"

#include <ranges>
#include <utility>

#include "Input/InputTiming.h"

namespace BowInput {
    namespace {
        constexpr int kDIK_W = 0x11;
        constexpr int kDIK_A = 0x1E;
        constexpr int kDIK_S = 0x1F;
        constexpr int kDIK_D = 0x20;

        enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };

        struct Snapshot {
            bool kbNow{};
            bool gpNow{};
            bool rawNow{};
            bool prevAccepted{};
            bool kbPressedEdge{};
            bool gpPressedEdge{};
        };

        inline bool AnyEnabled(const std::array<int, kMaxComboKeys>& a) {
            return std::ranges::any_of(a, [](int v) { return v != -1; });
        }

        inline bool ComboContains(const std::array<int, kMaxComboKeys>& combo, int unifiedCode) {
            return std::ranges::any_of(combo, [&](int v) { return v == unifiedCode; });
        }

        inline bool IsAllowedExtra(int unifiedCode) {
            if (unifiedCode >= kMouseOffset) return false;
            return unifiedCode == kDIK_W || unifiedCode == kDIK_A || unifiedCode == kDIK_S || unifiedCode == kDIK_D;
        }

        bool ComboDown(const std::array<int, kMaxComboKeys>& combo, const InputState& inputs) {
            if (!AnyEnabled(combo)) return false;
            auto enabled = combo | std::views::filter([](int v) { return v != -1; });
            return std::ranges::all_of(enabled, [&](int v) {
                if (v < 0 || v >= kMaxCode) return false;
                return inputs.down[static_cast<std::size_t>(v)].load(std::memory_order_relaxed);
            });
        }

        bool ComboExclusiveNow(const std::array<int, kMaxComboKeys>& combo, const InputState& inputs) {
            if (!ComboDown(combo, inputs)) return false;
            for (int code = 0; code < kMaxCode; ++code) {
                if (!inputs.down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) continue;
                if (ComboContains(combo, code)) continue;
                if (IsAllowedExtra(code)) continue;
                return false;
            }
            return true;
        }

        bool ComboExclusiveReleaseOk(const std::array<int, kMaxComboKeys>& combo, const InputState& inputs) {
            for (int code = 0; code < kMaxCode; ++code) {
                if (!inputs.down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) continue;
                if (ComboContains(combo, code)) continue;
                if (IsAllowedExtra(code)) continue;
                return false;
            }
            return true;
        }

        inline Snapshot MakeSnapshot(bool prevHotkeyDown, const HotkeyRuntime& rt, const HotkeyConfig& hk,
                                     const InputState& inputs) {
            const bool kbNow = ComboDown(hk.bowKeyScanCodes, inputs);
            const bool gpNow = ComboDown(hk.bowPadButtons, inputs);
            return Snapshot{
                .kbNow = kbNow,
                .gpNow = gpNow,
                .rawNow = kbNow || gpNow,
                .prevAccepted = prevHotkeyDown,
                .kbPressedEdge = kbNow && !rt.prevRawKbComboDown,
                .gpPressedEdge = gpNow && !rt.prevRawGpComboDown,
            };
        }

        inline void CommitEdges(HotkeyRuntime& rt, const Snapshot& s) noexcept {
            rt.prevRawKbComboDown = s.kbNow;
            rt.prevRawGpComboDown = s.gpNow;
        }

        inline void ClearPending(HotkeyRuntime& rt) noexcept {
            rt.exclusivePendingSrc = 0;
            rt.exclusivePendingTimer = 0.0f;
        }

        inline bool ApplySuppressGate(HotkeyRuntime& rt, const Snapshot& s, bool& inOut_hotkeyDown) noexcept {
            if (!rt.suppressUntilReleased) return false;
            ClearPending(rt);
            inOut_hotkeyDown = false;
            if (!s.rawNow) rt.suppressUntilReleased = false;
            return true;
        }

        inline bool StillExclusive(PendingSrc pending, bool rawNow, const HotkeyConfig& hk, const InputState& inputs) {
            const auto& combo = (pending == PendingSrc::Kb) ? hk.bowKeyScanCodes : hk.bowPadButtons;
            return rawNow ? ComboExclusiveNow(combo, inputs) : ComboExclusiveReleaseOk(combo, inputs);
        }

        inline void ArmPendingIfEdge(const Snapshot& s, HotkeyRuntime& rt, const HotkeyConfig& hk,
                                     const InputState& inputs) {
            if (s.kbPressedEdge && ComboExclusiveNow(hk.bowKeyScanCodes, inputs)) {
                rt.exclusivePendingSrc = std::to_underlying(PendingSrc::Kb);
                rt.exclusivePendingTimer = BowInput::Timing::kExclusiveConfirmDelaySec;
                return;
            }
            if (s.gpPressedEdge && ComboExclusiveNow(hk.bowPadButtons, inputs)) {
                rt.exclusivePendingSrc = std::to_underlying(PendingSrc::Gp);
                rt.exclusivePendingTimer = BowInput::Timing::kExclusiveConfirmDelaySec;
            }
        }

        inline bool ComputeAccepted(const Snapshot& s, HotkeyRuntime& rt, const HotkeyConfig& hk,
                                    const InputState& inputs, bool requireExclusive, float dt) {
            if (!requireExclusive) {
                ClearPending(rt);
                return s.rawNow;
            }

            if (s.prevAccepted) return s.rawNow;

            const auto pending = static_cast<PendingSrc>(rt.exclusivePendingSrc);

            if (pending == PendingSrc::None) {
                ArmPendingIfEdge(s, rt, hk, inputs);
                return false;
            }

            if (!StillExclusive(pending, s.rawNow, hk, inputs)) {
                ClearPending(rt);
                return false;
            }

            if (!s.rawNow) {
                ClearPending(rt);
                return true;
            }

            rt.exclusivePendingTimer -= dt;
            if (rt.exclusivePendingTimer <= 0.0f) {
                ClearPending(rt);
                return true;
            }

            return false;
        }
    }

    void HotkeyDetector::Tick(RE::PlayerCharacter* player, float dt, const HotkeyConfig& hk, const InputState& inputs,
                              bool requireExclusive, bool blocked, bool& inOut_hotkeyDown, HotkeyRuntime& rt,
                              IHotkeyCallbacks& cb) {
        if (!player) return;

        const Snapshot s = MakeSnapshot(inOut_hotkeyDown, rt, hk, inputs);

        if (ApplySuppressGate(rt, s, inOut_hotkeyDown)) {
            CommitEdges(rt, s);
            return;
        }

        const bool acceptedNow = ComputeAccepted(s, rt, hk, inputs, requireExclusive, dt);
        CommitEdges(rt, s);

        const bool prevAccepted = s.prevAccepted;
        inOut_hotkeyDown = acceptedNow;

        if (acceptedNow && !prevAccepted)
            cb.OnHotkeyAcceptedPressed(player, blocked);
        else if (!acceptedNow && prevAccepted)
            cb.OnHotkeyAcceptedReleased(player, blocked);
    }
}