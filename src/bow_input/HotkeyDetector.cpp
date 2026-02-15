#include "HotkeyDetector.h"

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <utility>

namespace BowInput {
    namespace {
        constexpr float kExclusiveConfirmDelaySec = 0.10f;

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

        inline bool ComboContains(const std::array<int, kMaxComboKeys>& combo, int code) {
            return std::ranges::any_of(combo, [&](int v) {
                if (v == -1) return false;
                return InputUtil::NormalizePadCode(v) == code;
            });
        }

        inline bool IsAllowedExtra_Keyboard_MoveOrCamera(int code) {
            switch (code) {
                case kDIK_W:
                case kDIK_A:
                case kDIK_S:
                case kDIK_D:
                    return true;
                default:
                    return false;
            }
        }

        inline bool IsAllowedExtra_Gamepad_MoveOrCamera(int) { return false; }

        bool ComboDown(const std::array<int, kMaxComboKeys>& combo,
                       const std::array<std::atomic_bool, kMaxCode>& down) {
            if (!AnyEnabled(combo)) return false;

            auto enabledOnly = combo | std::views::filter([](int v) { return v != -1; });

            return std::ranges::all_of(enabledOnly, [&](int v) {
                const int code = InputUtil::NormalizePadCode(v);
                if (code < 0 || code >= kMaxCode) return false;
                return down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed);
            });
        }

        template <class AllowedFn>
        bool ComboExclusiveNow(const std::array<int, kMaxComboKeys>& combo,
                               const std::array<std::atomic_bool, kMaxCode>& down, std::span<const int> downList,
                               AllowedFn isAllowedExtra) {
            if (!ComboDown(combo, down)) return false;

            const auto isDisallowedExtraDown = [&](int code) {
                if (code < 0 || code >= kMaxCode) return false;
                if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) return false;
                if (ComboContains(combo, code)) return false;
                if (isAllowedExtra(code)) return false;
                return true;
            };

            return std::ranges::none_of(downList, isDisallowedExtraDown);
        }

        template <class AllowedFn>
        bool ComboExclusiveReleaseOk(const std::array<int, kMaxComboKeys>& combo,
                                     const std::array<std::atomic_bool, kMaxCode>& down, std::span<const int> downList,
                                     AllowedFn isAllowedExtra) {
            const auto isDisallowedExtraDown = [&](int code) {
                if (code < 0 || code >= kMaxCode) return false;
                if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) return false;
                if (ComboContains(combo, code)) return false;
                if (isAllowedExtra(code)) return false;
                return true;
            };

            return std::ranges::none_of(downList, isDisallowedExtraDown);
        }

        inline Snapshot MakeSnapshot(bool prevHotkeyDown, const HotkeyRuntime& rt, const HotkeyConfig& hk,
                                     const InputState& inputs) {
            const bool kbNow = ComboDown(hk.bowKeyScanCodes, inputs.kbDown);
            const bool gpNow = ComboDown(hk.bowPadButtons, inputs.gpDown);

            Snapshot s{};
            s.kbNow = kbNow;
            s.gpNow = gpNow;
            s.rawNow = kbNow || gpNow;
            s.prevAccepted = prevHotkeyDown;
            s.kbPressedEdge = kbNow && !rt.prevRawKbComboDown;
            s.gpPressedEdge = gpNow && !rt.prevRawGpComboDown;
            return s;
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
            if (!rt.suppressUntilReleased) {
                return false;
            }

            if (s.rawNow) {
                ClearPending(rt);
                inOut_hotkeyDown = false;
                return true;
            }

            rt.suppressUntilReleased = false;
            inOut_hotkeyDown = false;
            return true;
        }

        inline bool StillExclusive(PendingSrc pending, bool rawNow, const HotkeyConfig& hk, const InputState& inputs) {
            if (pending == PendingSrc::Kb) {
                const auto kbList = inputs.DownList(RE::INPUT_DEVICE::kKeyboard);
                return rawNow ? ComboExclusiveNow(hk.bowKeyScanCodes, inputs.kbDown, kbList,
                                                  IsAllowedExtra_Keyboard_MoveOrCamera)
                              : ComboExclusiveReleaseOk(hk.bowKeyScanCodes, inputs.kbDown, kbList,
                                                        IsAllowedExtra_Keyboard_MoveOrCamera);
            }

            const auto gpList = inputs.DownList(RE::INPUT_DEVICE::kGamepad);
            return rawNow
                       ? ComboExclusiveNow(hk.bowPadButtons, inputs.gpDown, gpList, IsAllowedExtra_Gamepad_MoveOrCamera)
                       : ComboExclusiveReleaseOk(hk.bowPadButtons, inputs.gpDown, gpList,
                                                 IsAllowedExtra_Gamepad_MoveOrCamera);
        }

        inline void ArmPendingIfEdge(const Snapshot& s, HotkeyRuntime& rt, const HotkeyConfig& hk,
                                     const InputState& inputs) {
            if (s.kbPressedEdge) {
                const auto kbList = inputs.DownList(RE::INPUT_DEVICE::kKeyboard);
                if (ComboExclusiveNow(hk.bowKeyScanCodes, inputs.kbDown, kbList,
                                      IsAllowedExtra_Keyboard_MoveOrCamera)) {
                    rt.exclusivePendingSrc = std::to_underlying(PendingSrc::Kb);
                    rt.exclusivePendingTimer = kExclusiveConfirmDelaySec;
                }
                return;
            }

            if (s.gpPressedEdge) {
                const auto gpList = inputs.DownList(RE::INPUT_DEVICE::kGamepad);
                if (ComboExclusiveNow(hk.bowPadButtons, inputs.gpDown, gpList, IsAllowedExtra_Gamepad_MoveOrCamera)) {
                    rt.exclusivePendingSrc = std::to_underlying(PendingSrc::Gp);
                    rt.exclusivePendingTimer = kExclusiveConfirmDelaySec;
                }
            }
        }

        inline bool ComputeAccepted(const Snapshot& s, HotkeyRuntime& rt, const HotkeyConfig& hk,
                                    const InputState& inputs, bool requireExclusive, float dt) {
            if (!requireExclusive) {
                ClearPending(rt);
                return s.rawNow;
            }

            if (s.prevAccepted) {
                return s.rawNow;
            }

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
        if (!player) {
            return;
        }

        const Snapshot s = MakeSnapshot(inOut_hotkeyDown, rt, hk, inputs);

        if (ApplySuppressGate(rt, s, inOut_hotkeyDown)) {
            CommitEdges(rt, s);
            return;
        }

        const bool acceptedNow = ComputeAccepted(s, rt, hk, inputs, requireExclusive, dt);

        CommitEdges(rt, s);

        const bool prevAccepted = s.prevAccepted;
        inOut_hotkeyDown = acceptedNow;

        if (acceptedNow && !prevAccepted) {
            cb.OnHotkeyAcceptedPressed(player, blocked);
        } else if (!acceptedNow && prevAccepted) {
            cb.OnHotkeyAcceptedReleased(player, blocked);
        }
    }
}