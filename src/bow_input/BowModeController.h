#pragma once

#include <cstdint>

#include "HotkeyDetector.h"

namespace RE {
    class PlayerCharacter;
    class Actor;
    class ActorEquipManager;
    class InventoryEntryData;
    class ExtraDataList;
}

namespace BowState {
    struct IntegratedBowState;
}

namespace BowInput {
    struct ModeState {
        bool holdMode = true;
        bool smartMode = false;
        bool smartPending = false;
        float smartTimer = 0.0f;
    };

    struct ExitState {
        bool pending = false;
        bool waitForEquip = false;
        float waitEquipTimer = 0.0f;
        float delayTimer = 0.0f;
        float waitEquipMax = 3.0f;
        int delayMs = 0;
    };

    struct AttackHoldState {
        std::atomic_bool active{false};
        std::atomic<float> secs{0.0f};
        bool arrowAttachConfirmed = false;
        std::uint64_t watchdogAtMs = 0;
        std::uint8_t retryCount = 0;
    };

    struct PostExitAttackState {
        bool pending = false;
        std::uint8_t stage = 0;
        std::uint64_t downAtMs = 0;
        std::uint64_t upAtMs = 0;
        std::uint64_t holdStartMs = 0;
        std::uint64_t minHoldMs = 200;
    };

    class BowModeController final : public IHotkeyCallbacks {
    public:
        static BowModeController& Get() noexcept;

        bool hotkeyDown = false;

        std::uint64_t fakeEnableBumperAtMs = 0;

        std::atomic_bool allowUnequip{true};
        std::atomic<std::uint64_t> allowUnequipReenableMs{0};

        std::atomic<std::uint64_t> lastHotkeyPressMs{0};

        std::atomic_bool pendingRestoreAfterSheathe{false};
        std::atomic_bool sheathRequestedByPlayer{false};

        void OnHotkeyAcceptedPressed(RE::PlayerCharacter* player, bool blocked) override;
        void OnHotkeyAcceptedReleased(RE::PlayerCharacter* player, bool blocked) override;

        void UpdateSmartMode(RE::PlayerCharacter* player, float dt);
        [[nodiscard]] bool UpdateExitPending(float dt);
        void PumpAttackHold(float dt);
        void PumpPostExitAttackTap();

        void OnAnimEvent(std::string_view tag, RE::PlayerCharacter* player);

        void OnWeaponSheathe(RE::PlayerCharacter* player);

        void ForceImmediateExit();

        [[nodiscard]] bool IsHotkeyDown() const noexcept;
        [[nodiscard]] bool IsInHoldAutoExitDelay() const noexcept;

        ModeState& Mode() noexcept { return mode_; }
        ExitState& Exit() noexcept { return exit_; }
        AttackHoldState& AttackHold() noexcept { return attackHold_; }
        PostExitAttackState& PostExitAttack() noexcept { return postExitAttack_; }

        const ModeState& Mode() const noexcept { return mode_; }
        const ExitState& Exit() const noexcept { return exit_; }

        void UpdateUnequipGate() noexcept;

        void CompleteExit();

    private:
        BowModeController() = default;

        ModeState mode_;
        ExitState exit_;
        AttackHoldState attackHold_;
        PostExitAttackState postExitAttack_;

        void OnKeyPressed(RE::PlayerCharacter* player);
        void OnKeyReleased();

        static void EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                 BowState::IntegratedBowState& st);
        static void ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                BowState::IntegratedBowState& st);

        void ScheduleExitBowMode(bool waitForEquip, int delayMs);
        static void ScheduleAutoAttackDraw();

        void ResetExitState();

        static bool IsWeaponDrawn(RE::Actor* actor);
        static void SetWeaponDrawn(RE::Actor* actor, bool drawn);
        static RE::ExtraDataList* GetPrimaryExtra(RE::InventoryEntryData const* entry);

        void UpdateExitEquipWait(float dt);
        [[nodiscard]] bool IsExitDelayReady(float dt);

        static void StartAutoAttackDraw();
        static void StopAutoAttackDraw();
    };

}