#include "BowModeController.h"

#include <chrono>
#include <string_view>

#include "../PCH.h"
#include "../config/BowConfig.h"
#include "../patchs/HiddenItemsPatch.h"
#include "../patchs/SkipEquipController.h"
#include "BowState.h"
#include "InputGate.h"

using namespace std::literals;

namespace BowInput {
    namespace {
        constexpr float kSmartClickThreshold = 0.18f;
        constexpr std::uint64_t kFakeEnableBumperDelayMs = 150;
        constexpr std::uint64_t kDisableSkipEquipDelayMs = 500;
        constexpr std::uint64_t kPostExitAttackDownDelayMs = 1000;
        constexpr std::uint64_t kPostExitAttackTapMs = 2000;

        inline std::uint64_t NowMs() noexcept {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }

        inline bool IsAutoDrawEnabled() {
            return IntegratedBow::GetBowConfig().autoDrawEnabled.load(std::memory_order_relaxed);
        }

        inline float GetSheathedDelayMs() {
            float secs = IntegratedBow::GetBowConfig().sheathedDelaySeconds.load(std::memory_order_relaxed);
            if (secs < 0.0f) secs = 0.0f;
            return secs * 1000.0f;
        }
    }

    BowModeController& BowModeController::Get() noexcept {
        static BowModeController s;  // NOSONAR
        return s;
    }

    bool BowModeController::IsHotkeyDown() const noexcept { return hotkeyDown; }

    bool BowModeController::IsInHoldAutoExitDelay() const noexcept {
        if (!mode_.holdMode) return false;
        if (!exit_.pending) return false;
        if (exit_.delayMs <= 0) return false;
        if (hotkeyDown) return false;
        if (!IsAutoDrawEnabled()) return false;

        const auto target = static_cast<float>(exit_.delayMs);
        return exit_.delayTimer < target;
    }

    void BowModeController::UpdateUnequipGate() noexcept {
        if (allowUnequip.load(std::memory_order_relaxed)) return;

        const std::uint64_t until = allowUnequipReenableMs.load(std::memory_order_relaxed);
        if (until != 0 && NowMs() >= until) {
            allowUnequip.store(true, std::memory_order_relaxed);
        }
    }

    void BowModeController::OnHotkeyAcceptedPressed(RE::PlayerCharacter* player, bool blocked) {
        if (mode_.smartMode) {
            hotkeyDown = true;
            if (!blocked) {
                mode_.smartPending = true;
                mode_.smartTimer = 0.0f;
            } else {
                mode_.smartPending = false;
                mode_.smartTimer = 0.0f;
            }
        } else {
            hotkeyDown = true;
            if (!blocked) OnKeyPressed(player);
        }
    }

    void BowModeController::OnHotkeyAcceptedReleased(RE::PlayerCharacter* player, bool blocked) {
        if (mode_.smartMode) {
            hotkeyDown = false;
            if (!blocked) {
                if (mode_.smartPending) {
                    mode_.smartPending = false;
                    mode_.smartTimer = 0.0f;
                    mode_.holdMode = false;
                    OnKeyPressed(player);
                }
                OnKeyReleased();
            } else {
                mode_.smartPending = false;
                mode_.smartTimer = 0.0f;
            }
        } else {
            hotkeyDown = false;
            if (!blocked) OnKeyReleased();
        }
    }

    void BowModeController::UpdateSmartMode(RE::PlayerCharacter* player, float dt) {
        if (!mode_.smartMode || !mode_.smartPending || !hotkeyDown) return;

        mode_.smartTimer += dt;

        if (mode_.smartTimer >= kSmartClickThreshold) {
            mode_.smartPending = false;
            mode_.smartTimer = 0.0f;
            mode_.holdMode = true;

            if (!InputGate::IsInputBlockedByMenus()) {
                OnKeyPressed(player);
            }
        }
    }

    bool BowModeController::UpdateExitPending(float dt) {
        if (!exit_.pending) return false;

        if (exit_.waitForEquip && !BowState::IsBowEquipped()) {
            UpdateExitEquipWait(dt);
            return false;
        }

        if (IsExitDelayReady(dt)) {
            CompleteExit();
            return true;
        }

        return false;
    }

    void BowModeController::PumpAttackHold(float dt) {
        if (!attackHold_.active.load(std::memory_order_relaxed)) return;

        if (!BowState::IsAutoAttackHeld()) {
            attackHold_.active.store(false, std::memory_order_relaxed);
            attackHold_.secs.store(0.0f, std::memory_order_relaxed);
            return;
        }

        float cur = attackHold_.secs.load(std::memory_order_relaxed);
        cur += dt;
        attackHold_.secs.store(cur, std::memory_order_relaxed);

        auto* ev = BowState::detail::MakeAttackButtonEvent(1.0f, cur);
        BowState::detail::DispatchAttackButtonEvent(ev);
    }

    void BowModeController::PumpPostExitAttackTap() {
        if (!postExitAttack_.pending) return;
        const std::uint64_t now = NowMs();
        if (postExitAttack_.stage == 0) {
            if (postExitAttack_.downAtMs != 0 && now >= postExitAttack_.downAtMs) {
                auto* evPress = BowState::detail::MakeAttackButtonEvent(1.0f, 0.0f);
                BowState::detail::DispatchAttackButtonEvent(evPress);
                postExitAttack_.holdStartMs = now;
                postExitAttack_.stage = 1;
            }
            return;
        }

        if (postExitAttack_.stage == 1) {
            std::uint64_t heldDuration = now - postExitAttack_.holdStartMs;
            auto* evHold = BowState::detail::MakeAttackButtonEvent(1.0f, heldDuration / 1000.0f);
            BowState::detail::DispatchAttackButtonEvent(evHold);
            if (heldDuration >= postExitAttack_.minHoldMs) {
                postExitAttack_.stage = 2;
            }
            return;
        }

        if (postExitAttack_.stage == 2 && postExitAttack_.upAtMs != 0 && now >= postExitAttack_.upAtMs) {
            auto* evRelease = BowState::detail::MakeAttackButtonEvent(0.0f, 0.1f);
            BowState::detail::DispatchAttackButtonEvent(evRelease);
            postExitAttack_ = {};
        }
    }

    void BowModeController::OnAnimEvent(std::string_view tag, RE::PlayerCharacter* player) {
        if (tag == "weaponSwing"sv) {
            g_attackSwingDetected.store(true, std::memory_order_relaxed);
        }
        if (tag == "EnableBumper"sv) {
            BowState::SetBowEquipped(true);

            const bool waiting = BowState::IsWaitingAutoAfterEquip();
            const bool usingBow = BowState::IsUsingBow();
            const bool autoDraw = IsAutoDrawEnabled();
            const bool hkDown = hotkeyDown;
            const bool holdMode = mode_.holdMode;

            if (waiting && usingBow && holdMode && autoDraw && hkDown) {
                BowState::SetWaitingAutoAfterEquip(false);
                if (!BowState::IsAutoAttackHeld()) {
                    BowState::SetAutoAttackHeld(true);
                    StartAutoAttackDraw();
                }
            }

        } else if (tag == "WeaponSheathe"sv) {
            OnWeaponSheathe(player);

        } else if (tag == "bowReset"sv) {
            const bool autoDraw = IsAutoDrawEnabled();
            const bool autoHeld = BowState::IsAutoAttackHeld();
            const bool hkDown = hotkeyDown;

            if (autoDraw && autoHeld && hkDown) {
                StopAutoAttackDraw();
                BowState::SetAutoAttackHeld(true);
                StartAutoAttackDraw();
            }
        }
    }

    void BowModeController::OnWeaponSheathe(RE::PlayerCharacter* player) {
        const bool pendingRestore = pendingRestoreAfterSheathe.load(std::memory_order_relaxed);
        const bool sheathReq = sheathRequestedByPlayer.load(std::memory_order_relaxed);

        if (!pendingRestore && !sheathReq) return;
        if (!player) return;

        auto& st = BowState::Get();
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();

        if (!equipMgr) {
            pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
            st.isUsingBow = false;
            BowState::ClearPrevWeapons();
            BowState::ClearPrevExtraEquipped();
            BowState::ClearPrevAmmo();
            BowState::SetBowEquipped(false);
            return;
        }

        if (pendingRestore) {
            pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
            BowState::SetBowEquipped(false);
            BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
            return;
        }

        sheathRequestedByPlayer.store(false, std::memory_order_relaxed);

        if (sheathReq && st.isUsingBow) {
            BowState::SetBowEquipped(false);
            BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
        }
    }

    void BowModeController::ForceImmediateExit() {
        auto& st = BowState::Get();

        st.chosenBow.extra = nullptr;
        st.prevRight.base = nullptr;
        st.prevRight.extra = nullptr;
        st.prevLeft.base = nullptr;
        st.prevLeft.extra = nullptr;
        st.prevRightFormID = 0;
        st.prevLeftFormID = 0;
        st.wasCombatPosed = false;
        st.isEquipingBow = false;
        st.isUsingBow = false;

        BowState::ClearPrevExtraEquipped();
        BowState::ClearPrevAmmo();
        BowState::SetAutoAttackHeld(false);
        BowState::SetWaitingAutoAfterEquip(false);
        BowState::SetBowEquipped(false);

        pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
        sheathRequestedByPlayer.store(false, std::memory_order_relaxed);

        fakeEnableBumperAtMs = 0;
        attackHold_.active.store(false, std::memory_order_relaxed);
        attackHold_.secs.store(0.0f, std::memory_order_relaxed);

        exit_.pending = false;
        exit_.waitForEquip = false;
        exit_.waitEquipTimer = 0.0f;
        exit_.delayTimer = 0.0f;
        exit_.delayMs = 0;

        hotkeyDown = false;
        mode_.smartPending = false;
        mode_.smartTimer = 0.0f;
    }

    void BowModeController::OnKeyPressed(RE::PlayerCharacter* player) {
        lastHotkeyPressMs.store(NowMs(), std::memory_order_relaxed);

        auto* equipMgr = RE::ActorEquipManager::GetSingleton();

        if (mode_.holdMode && exit_.pending) {
            ResetExitState();
        }

        if (!equipMgr) return;

        auto& st = BowState::Get();

        if (!BowState::EnsureChosenBowInInventory()) return;

        if (const RE::TESObjectWEAP* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
            !bow) {
            return;
        }

        if (mode_.holdMode) {
            if (!st.isUsingBow) {
                EnterBowMode(player, equipMgr, st);
                return;
            }
            if (IsAutoDrawEnabled()) {
                if (BowState::IsBowEquipped()) {
                    BowState::SetAutoAttackHeld(true);
                    StartAutoAttackDraw();
                } else {
                    ScheduleAutoAttackDraw();
                }
            }
        } else {
            if (st.isUsingBow) {
                ScheduleExitBowMode(!BowState::IsBowEquipped(), 0);
            } else {
                EnterBowMode(player, equipMgr, st);
            }
        }
    }

    void BowModeController::OnKeyReleased() {
        if (!mode_.holdMode) return;
        if (!RE::ActorEquipManager::GetSingleton()) return;
        if (!BowState::Get().isUsingBow) return;

        const bool autoDraw = IsAutoDrawEnabled();
        const auto delayMs = static_cast<int>(GetSheathedDelayMs() + 0.5f);

        if (autoDraw && BowState::IsAutoAttackHeld()) {
            StopAutoAttackDraw();
            BowState::SetAutoAttackHeld(false);
        }

        ScheduleExitBowMode(!BowState::IsBowEquipped(), delayMs);
    }

    void BowModeController::EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                         BowState::IntegratedBowState& st) {
        auto& ctrl = Get();
        ctrl.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);
        ctrl.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);

        if (!player || !equipMgr) return;

        BowState::SetPrevAmmo(player->GetCurrentAmmo());

        std::vector<BowState::ExtraEquippedItem> wornBefore;
        BowState::CaptureWornArmorSnapshot(wornBefore);

        auto* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
        auto* bowExtra = st.chosenBow.extra;
        if (!bow) return;

        auto* rightEntry = player->GetEquippedEntryData(false);
        auto* leftEntry = player->GetEquippedEntryData(true);

        BowState::SetPrevWeapons(rightEntry ? rightEntry->GetObject() : nullptr, GetPrimaryExtra(rightEntry),
                                 leftEntry ? leftEntry->GetObject() : nullptr, GetPrimaryExtra(leftEntry));

        const bool alreadyDrawn = IsWeaponDrawn(player);
        st.wasCombatPosed = alreadyDrawn;
        st.isEquipingBow = true;
        st.isUsingBow = false;

        auto const& cfg = IntegratedBow::GetBowConfig();

        if (cfg.skipEquipBowAnimationPatch.load(std::memory_order_relaxed)) {
            IntegratedBow::SkipEquipController::EnableAndArmDisable(player, 0, false, kDisableSkipEquipDelayMs);
        }

        equipMgr->EquipObject(player, bow, bowExtra, 1, nullptr, true, false, true, false);

        st.isUsingBow = true;
        st.isEquipingBow = false;

        std::vector<BowState::ExtraEquippedItem> wornAfter;
        BowState::CaptureWornArmorSnapshot(wornAfter);
        BowState::SetPrevExtraEquipped(BowState::DiffArmorSnapshot(wornBefore, wornAfter));

        if (HiddenItemsPatch::IsEnabled()) {
            BowState::ApplyHiddenItemsPatch(player, equipMgr, HiddenItemsPatch::GetHiddenFormIDs());
        }

        if (auto* preferred = BowState::GetPreferredArrow()) {
            auto inv = player->GetInventory([preferred](RE::TESBoundObject const& obj) { return &obj == preferred; });
            if (!inv.empty()) {
                equipMgr->EquipObject(player, preferred, nullptr, 1, nullptr, true, false, true, false);
            }
        }

        if (!alreadyDrawn) SetWeaponDrawn(player, true);

        BowState::SetAutoAttackHeld(false);

        const bool shouldWaitAuto = ctrl.mode_.holdMode && IsAutoDrawEnabled() && ctrl.hotkeyDown;
        BowState::SetWaitingAutoAfterEquip(shouldWaitAuto);

        ctrl.allowUnequip.store(false, std::memory_order_relaxed);
        ctrl.allowUnequipReenableMs.store(NowMs() + 2000, std::memory_order_relaxed);

        if (shouldWaitAuto && cfg.skipEquipBowAnimationPatch.load(std::memory_order_relaxed) && alreadyDrawn) {
            ctrl.fakeEnableBumperAtMs = NowMs() + kFakeEnableBumperDelayMs;
        } else {
            ctrl.fakeEnableBumperAtMs = 0;
        }
    }

    void BowModeController::ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                        BowState::IntegratedBowState& st) {
        auto& ctrl = Get();

        if (!player || !equipMgr) return;

        ctrl.fakeEnableBumperAtMs = 0;

        if (!st.wasCombatPosed && !player->IsInCombat()) {
            SetWeaponDrawn(player, false);
            ctrl.pendingRestoreAfterSheathe.store(true, std::memory_order_relaxed);
            st.isUsingBow = false;
            return;
        }

        BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
    }

    void BowModeController::ScheduleExitBowMode(bool waitForEquip, int delayMs) {
        exit_.pending = true;
        exit_.waitForEquip = waitForEquip;
        exit_.waitEquipTimer = 0.0f;
        exit_.delayTimer = 0.0f;
        exit_.delayMs = delayMs;
    }

    void BowModeController::ScheduleAutoAttackDraw() {
        if (!RE::PlayerCharacter::GetSingleton()) return;
        if (!BowState::Get().isUsingBow) return;
        BowState::SetWaitingAutoAfterEquip(true);
    }

    void BowModeController::UpdateExitEquipWait(float dt) {
        if (!exit_.waitForEquip || BowState::IsBowEquipped()) return;

        exit_.waitEquipTimer += dt;

        if (exit_.waitEquipTimer >= exit_.waitEquipMax) {
            exit_.waitForEquip = false;
            exit_.waitEquipTimer = 0.0f;
        }
    }

    bool BowModeController::IsExitDelayReady(float dt) {
        if (exit_.delayMs <= 0) return true;

        exit_.delayTimer += dt * 1000.0f;
        return exit_.delayTimer >= static_cast<float>(exit_.delayMs);
    }

    void BowModeController::CompleteExit() {
        if (auto* equipMgr = RE::ActorEquipManager::GetSingleton()) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                auto& bowSt = BowState::Get();
                ExitBowMode(player, equipMgr, bowSt);
            }
        }

        ResetExitState();
    }

    void BowModeController::ResetExitState() {
        exit_.pending = false;
        exit_.waitForEquip = false;
        exit_.waitEquipTimer = 0.0f;
        exit_.delayTimer = 0.0f;
        exit_.delayMs = 0;

        mode_.smartPending = false;
        mode_.smartTimer = 0.0f;

        hotkeyDown = false;
    }

    void BowModeController::StartAutoAttackDraw() {
        auto& ctrl = Get();
        ctrl.attackHold_.active.store(true, std::memory_order_relaxed);
        ctrl.attackHold_.secs.store(0.0f, std::memory_order_relaxed);
        ctrl.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);

        auto* ev = BowState::detail::MakeAttackButtonEvent(1.0f, 0.0f);
        BowState::detail::DispatchAttackButtonEvent(ev);
    }

    void BowModeController::StopAutoAttackDraw() {
        auto& ctrl = Get();

        float held = ctrl.attackHold_.secs.load(std::memory_order_relaxed);
        if (held <= 0.0f) held = 0.1f;

        auto* ev = BowState::detail::MakeAttackButtonEvent(0.0f, held);
        BowState::detail::DispatchAttackButtonEvent(ev);

        ctrl.attackHold_.active.store(false, std::memory_order_relaxed);
        ctrl.attackHold_.secs.store(0.0f, std::memory_order_relaxed);
    }

    bool BowModeController::IsWeaponDrawn(RE::Actor* actor) {
        if (!actor) return false;
        auto const* state = actor->AsActorState();
        return state && state->IsWeaponDrawn();
    }

    void BowModeController::SetWeaponDrawn(RE::Actor* actor, bool drawn) {
        if (actor) actor->DrawWeaponMagicHands(drawn);
    }

    RE::ExtraDataList* BowModeController::GetPrimaryExtra(RE::InventoryEntryData const* entry) {
        if (!entry || !entry->extraLists) return nullptr;
        for (auto* xList : *entry->extraLists) {
            if (xList) return xList;
        }
        return nullptr;
    }
}