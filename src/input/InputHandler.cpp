#include "InputHandler.h"

#include <atomic>
#include <chrono>
#include <ranges>
#include <string_view>

#include "Config/Config.h"
#include "Input/HotkeyDetector.h"
#include "Input/InputGate.h"
#include "Input/InputState.h"
#include "Input/InputTiming.h"
#include "Input/ModeController.h"
#include "Input/ReplayState.h"
#include "PCH.h"
#include "Patchs/SkipEquipController.h"
#include "State/LoadoutRestore.h"
#include "State/SessionState.h"

using namespace BowInput::Timing;

namespace BowInput {
    namespace {
        inline std::uint64_t NowMs() noexcept {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }

        inline bool HasTransformArchetype(const RE::MagicItem* item) {
            if (!item) return false;
            using ArchetypeID = RE::EffectArchetypes::ArchetypeID;
            return std::ranges::any_of(item->effects, [](const auto* effect) {
                if (!effect || !effect->baseEffect) return false;
                const auto arch = effect->baseEffect->GetArchetype();
                return arch == ArchetypeID::kWerewolf || arch == ArchetypeID::kVampireLord;
            });
        }

        inline const std::vector<RE::SpellItem*>& GetTransformPowers() {
            static std::vector<RE::SpellItem*> s_powers;
            if (!s_powers.empty()) return s_powers;
            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) return s_powers;
            auto const& spells = dh->GetFormArray<RE::SpellItem>();
            s_powers.reserve(spells.size());
            for (auto* spell : spells) {
                if (!spell) continue;
                if (spell->GetSpellType() != RE::MagicSystem::SpellType::kPower) continue;
                if (HasTransformArchetype(spell)) s_powers.push_back(spell);
            }
            return s_powers;
        }

        inline bool IsCurrentTransformPower(RE::Actor* actor) {
            if (!actor) return false;
            for (auto* power : GetTransformPowers())
                if (actor->IsCurrentShout(power)) return true;
            return false;
        }

        struct CaptureState {
            std::atomic_bool requested{false};
            std::atomic_bool modeActive{false};
            std::atomic_int capturedEncoded{-1};
        };

        CaptureState g_capture;                                   // NOSONAR
        HotkeyConfig g_hotkeyConfig{.bowCombo = {0x2F, -1, -1}};  // NOSONAR
        HotkeyRuntime g_hotkeyRuntime;                            // NOSONAR
        constexpr std::pair<int, int> kMouseVKMap[]               // NOSONAR
            = {
                {kMouseOffset + 0, VK_LBUTTON},  {kMouseOffset + 1, VK_RBUTTON},  {kMouseOffset + 2, VK_MBUTTON},
                {kMouseOffset + 3, VK_XBUTTON1}, {kMouseOffset + 4, VK_XBUTTON2},
        };

        void ReconcilePressedKeysWithPhysicalState() {
            static std::uint64_t s_nextRunMs = 0;

            const auto now = NowMs();
            if (now < s_nextRunMs) {
                return;
            }
            s_nextRunMs = now + 150;

            auto& inputs = BowInput::Inputs();
            bool clearedAny = false;

            for (int code = 0; code < BowInput::kMouseOffset; ++code) {
                const auto idx = static_cast<std::size_t>(code);
                if (!inputs.down[idx].load(std::memory_order_relaxed)) continue;

                const UINT vk = MapVirtualKeyA(static_cast<UINT>(code), MAPVK_VSC_TO_VK);
                if (vk == 0) {
                    inputs.down[idx].store(false, std::memory_order_relaxed);
                    clearedAny = true;
                    BOW_DEBUG_LOG(
                        "[InputHandler] ReconcilePressedKeysWithPhysicalState: cleared keyboard code={} because vk=0",
                        code);
                    continue;
                }

                const bool physicallyDown = (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
                if (!physicallyDown) {
                    inputs.down[idx].store(false, std::memory_order_relaxed);
                    clearedAny = true;
                    BOW_DEBUG_LOG(
                        "[InputHandler] ReconcilePressedKeysWithPhysicalState: cleared keyboard code={} vk={}", code,
                        vk);
                }
            }

            for (const auto& [idxInt, vk] : kMouseVKMap) {
                if (idxInt < 0 || idxInt >= BowInput::kMaxCode) continue;
                const auto idx = static_cast<std::size_t>(idxInt);
                if (!inputs.down[idx].load(std::memory_order_relaxed)) continue;

                const bool physicallyDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
                if (!physicallyDown) {
                    inputs.down[idx].store(false, std::memory_order_relaxed);
                    clearedAny = true;
                    BOW_DEBUG_LOG("[InputHandler] ReconcilePressedKeysWithPhysicalState: cleared mouse idx={} vk={}",
                                  idxInt, vk);
                }
            }

            if (clearedAny) {
                auto& rt = BowInput::GetHotkeyRuntimeMut();
                rt.prevRawComboDown = false;
                rt.suppressUntilReleased = false;
                rt.exclusivePendingSrc = 0;
                rt.exclusivePendingTimer = 0.0f;

                auto& ctrl = BowModeController::Get();
                ctrl.hotkeyDown = false;

                CancelBowPending();

                BOW_DEBUG_LOG(
                    "[InputHandler] ReconcilePressedKeysWithPhysicalState: reset runtime after clearing stuck keys");
            }
        }
    }

    const HotkeyConfig& GetHotkeyConfig() noexcept { return g_hotkeyConfig; }
    const HotkeyRuntime& GetHotkeyRuntime() noexcept { return g_hotkeyRuntime; }
    HotkeyRuntime& GetHotkeyRuntimeMut() noexcept { return g_hotkeyRuntime; }

    void ProcessBowLogic(float dt) {
        ReconcilePressedKeysWithPhysicalState();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: no player");
            return;
        }

        auto& ctrl = BowModeController::Get();
        const bool blocked = InputGate::IsInputBlockedByMenus();

        ctrl.UpdateUnequipGate();

        const bool requireExclusive =
            IntegratedBow::GetBowConfig().requireExclusiveHotkeyPatch.load(std::memory_order_relaxed);

        const std::uint8_t pendingBefore = g_hotkeyRuntime.exclusivePendingSrc;
        HotkeyDetector::Tick(player, dt, g_hotkeyConfig, Inputs(), requireExclusive, blocked, ctrl.hotkeyDown,
                             g_hotkeyRuntime, ctrl);

        if (const std::uint8_t pendingAfter = g_hotkeyRuntime.exclusivePendingSrc;
            pendingBefore != 0 && pendingAfter == 0) {
            BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: pending transitioned to zero hotkeyDown={}",
                          ctrl.hotkeyDown);

            if (ctrl.hotkeyDown) {
                BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: ConfirmBowPending");
                ConfirmBowPending();
            } else {
                BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: CancelBowPending");
                CancelBowPending();
            }
        }

        ctrl.UpdateSmartMode(player, dt);

        if (ctrl.UpdateExitPending(dt)) {
            BOW_DEBUG_LOG(
                "[InputHandler] ProcessBowLogic: UpdateExitPending completed exit, enabling suppressUntilReleased");
            g_hotkeyRuntime.suppressUntilReleased = true;
            g_hotkeyRuntime.prevRawComboDown = false;
            g_hotkeyRuntime.exclusivePendingSrc = 0;
            g_hotkeyRuntime.exclusivePendingTimer = 0.0f;
        }

        ctrl.PumpPostExitAttackTap();
        ctrl.PumpAttackHold(dt);

        if (ctrl.fakeEnableBumperAtMs != 0 && NowMs() >= ctrl.fakeEnableBumperAtMs) {
            BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: fake EnableBumper fired");
            ctrl.fakeEnableBumperAtMs = 0;
            if (BowState::IsWaitingAutoAfterEquip() && BowState::IsUsingBow()) {
                ctrl.OnAnimEvent("EnableBumper", player);
            }
        }

        if (ctrl.sheathRestoreAtMs != 0 && NowMs() >= ctrl.sheathRestoreAtMs) {
            BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: sheathRestoreAtMs fired");
            ctrl.sheathRestoreAtMs = 0;
            if (auto* equipMgr = RE::ActorEquipManager::GetSingleton()) {
                auto& st = BowState::Get();
                BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: sheath timer fired isUsingBow={}", st.isUsingBow);
                if (st.isUsingBow) {
                    BOW_DEBUG_LOG("[InputHandler] ProcessBowLogic: restoring previous weapons after sheath delay");
                    BowState::SetBowEquipped(false);
                    BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
                }
            }
        }

        IntegratedBow::SkipEquipController::Tick();

        if (auto* equipMgr = RE::ActorEquipManager::GetSingleton()) {
            BowState::UpdateDeferredFinalize(player, equipMgr, dt);
        }
    }

    void SetMode(int mode) {
        BOW_DEBUG_LOG("[InputHandler] SetMode: requested mode={} before holdMode={} smartMode={} hotkeyDown={}", mode,
                      BowModeController::Get().Mode().holdMode, BowModeController::Get().Mode().smartMode,
                      BowModeController::Get().hotkeyDown);

        auto& ctrl = BowModeController::Get();
        ctrl.hotkeyDown = false;
        ctrl.Mode().smartPending = false;
        ctrl.Mode().smartTimer = 0.0f;
        ctrl.Mode().smartImmediatePressStarted = false;
        ctrl.Mode().smartPromotedToHold = false;

        switch (mode) {
            case 0:
                ctrl.Mode().holdMode = true;
                ctrl.Mode().smartMode = false;
                break;
            case 1:
                ctrl.Mode().holdMode = false;
                ctrl.Mode().smartMode = false;
                break;
            case 2:
            default:
                ctrl.Mode().holdMode = false;
                ctrl.Mode().smartMode = true;
                break;
        }

        BOW_DEBUG_LOG("[InputHandler] SetMode: applied mode={} after holdMode={} smartMode={} hotkeyDown={}", mode,
                      ctrl.Mode().holdMode, ctrl.Mode().smartMode, ctrl.hotkeyDown);
    }

    void SetCombo(int k1, int k2, int k3) {
        BOW_DEBUG_LOG("[InputHandler] SetCombo: before oldCombo=({}, {}, {}) newCombo=({}, {}, {})",
                      g_hotkeyConfig.bowCombo[0], g_hotkeyConfig.bowCombo[1], g_hotkeyConfig.bowCombo[2], k1, k2, k3);

        g_hotkeyConfig.bowCombo = {k1, k2, k3};
        auto& ctrl = BowModeController::Get();
        ctrl.hotkeyDown = false;
        g_hotkeyRuntime.prevRawComboDown = false;
        g_hotkeyRuntime.exclusivePendingSrc = 0;
        g_hotkeyRuntime.exclusivePendingTimer = 0.0f;

        BOW_DEBUG_LOG(
            "[InputHandler] SetCombo: after combo=({}, {}, {}) hotkeyDown={} prevRawComboDown={} pendingSrc={} "
            "pendingTimer={}",
            g_hotkeyConfig.bowCombo[0], g_hotkeyConfig.bowCombo[1], g_hotkeyConfig.bowCombo[2], ctrl.hotkeyDown,
            g_hotkeyRuntime.prevRawComboDown, static_cast<int>(g_hotkeyRuntime.exclusivePendingSrc),
            g_hotkeyRuntime.exclusivePendingTimer);
    }

    void RequestHotkeyCapture() {
        BOW_DEBUG_LOG("[InputHandler] RequestHotkeyCapture: requested");
        g_capture.requested.store(true, std::memory_order_relaxed);
        g_capture.capturedEncoded.store(-1, std::memory_order_relaxed);
    }

    void CancelHotkeyCapture() {
        BOW_DEBUG_LOG("[InputHandler] CancelHotkeyCapture");
        g_capture.requested.store(false, std::memory_order_relaxed);
        g_capture.capturedEncoded.store(-1, std::memory_order_relaxed);
    }

    void SetCaptureModeActive(bool active) {
        BOW_DEBUG_LOG("[InputHandler] SetCaptureModeActive: active={}", active);
        g_capture.modeActive.store(active, std::memory_order_relaxed);

        if (!active) {
            g_capture.requested.store(false, std::memory_order_relaxed);
            g_capture.capturedEncoded.store(-1, std::memory_order_relaxed);
        }
    }

    bool IsCaptureModeActive() { return g_capture.modeActive.load(std::memory_order_relaxed); }

    int PollCapturedHotkey() {
        if (const int v = g_capture.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
            BOW_DEBUG_LOG("[InputHandler] PollCapturedHotkey: capturedEncoded={}", v);
            g_capture.capturedEncoded.store(-1, std::memory_order_relaxed);
            return v;
        }
        return -1;
    }

    bool IsHotkeyDown() { return BowModeController::Get().hotkeyDown; }

    bool IsUnequipAllowed() noexcept { return BowModeController::Get().allowUnequip.load(std::memory_order_relaxed); }

    void BlockUnequipForMs(std::uint64_t ms) noexcept {
        auto& ctrl = BowModeController::Get();
        ctrl.allowUnequip.store(false, std::memory_order_relaxed);
        ctrl.allowUnequipReenableMs.store(NowMs() + ms, std::memory_order_relaxed);
    }

    void ForceAllowUnequip() noexcept {
        auto& ctrl = BowModeController::Get();
        ctrl.allowUnequip.store(true, std::memory_order_relaxed);
        ctrl.allowUnequipReenableMs.store(0, std::memory_order_relaxed);
    }

    void HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
        if (!ev || !ev->holder) return;
        auto* actor = ev->holder->As<RE::Actor>();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!actor || actor != player) return;
        const std::string_view tag{ev->tag.c_str(), ev->tag.size()};
        BowModeController::Get().OnAnimEvent(tag, player);
    }

    void HandleCaptureEvents(RE::InputEvent** a_evns) {
        if (!a_evns) {
            BOW_DEBUG_LOG("[InputHandler] HandleCaptureEvents: null events");
            return;
        }
        if (!g_capture.modeActive.load(std::memory_order_relaxed)) return;
        if (!g_capture.requested.load(std::memory_order_relaxed)) return;

        BOW_DEBUG_LOG("[InputHandler] HandleCaptureEvents: capture requested, scanning events");

        for (auto* e = *a_evns; e; e = e->next) {
            const auto* btn = e->AsButtonEvent();
            if (!btn || !btn->IsDown()) continue;

            const auto dev = btn->GetDevice();
            auto code = static_cast<int>(btn->idCode);

            BOW_DEBUG_LOG("[InputHandler] HandleCaptureEvents: raw button dev={} rawCode={}", static_cast<int>(dev),
                          code);

            if (dev == RE::INPUT_DEVICE::kGamepad) {
                code = InputUtil::GamepadIdToIndex(code);
                if (code < 0) continue;
                code = kGamepadOffset + code;
            } else if (dev == RE::INPUT_DEVICE::kMouse) {
                if (code < 0 || code >= 10) continue;
                code = kMouseOffset + code;
            }

            if (code < 0 || code >= kMaxCode) continue;

            BOW_DEBUG_LOG("[InputHandler] HandleCaptureEvents: captured unifiedCode={}", code);

            g_capture.capturedEncoded.store(code, std::memory_order_relaxed);
            g_capture.requested.store(false, std::memory_order_relaxed);
            return;
        }
    }

    void CancelIfPendingActive() {
        BOW_DEBUG_LOG("[InputHandler] CancelIfPendingActive: try cancel pending");
        if (g_hotkeyRuntime.exclusivePendingSrc != 0) {
            g_hotkeyRuntime.exclusivePendingSrc = 0;
            g_hotkeyRuntime.exclusivePendingTimer = 0.0f;
            CancelBowPending();
            BOW_DEBUG_LOG("[InputHandler] CancelIfPendingActive: pending canceled");
        }
    }
}