#pragma once
#include <atomic>

namespace BowInput {
    inline constexpr int kMaxComboKeys = 3;
    struct HotkeyConfig {
        std::array<int, BowInput::kMaxComboKeys> bowKeyScanCodes{0x2F, -1, -1};
        std::array<int, BowInput::kMaxComboKeys> bowPadButtons{-1, -1, -1};
    };
    struct HotkeyState {
        std::array<bool, BowInput::kMaxComboKeys> bowKeyDown{false, false, false};
        std::array<bool, BowInput::kMaxComboKeys> bowPadDown{false, false, false};
        bool kbdComboDown = false;
        bool padComboDown = false;
        bool hotkeyDown = false;
    };
    struct CaptureState {
        std::atomic_bool captureRequested{false};
        std::atomic_int capturedEncoded{-1};
    };
    struct AttackHoldState {
        std::atomic_bool active{false};
        std::atomic<float> secs{0.0f};
    };
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

    struct GlobalState {
        HotkeyConfig hotkeyConfig;
        HotkeyState hotkey;
        CaptureState capture;
        AttackHoldState attackHold;
        ModeState mode;
        ExitState exit;
        std::atomic_bool pendingRestoreAfterSheathe{false};
        std::atomic_bool sheathRequestedByPlayer{false};
    };

    GlobalState& Globals() noexcept;
    void RegisterInputHandler();
    void RegisterAnimEventSink();

    void SetMode(int mode);
    void SetKeyScanCodes(int k1, int k2, int k3);
    void SetGamepadButtons(int b1, int b2, int b3);
    void RequestGamepadCapture();
    int PollCapturedGamepadButton();
    bool IsHotkeyDown();

    class IntegratedBowInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static IntegratedBowInputHandler* GetSingleton();
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
                                              RE::BSTEventSource<RE::InputEvent*>*) override;

    private:
        static void ScheduleExitBowMode(bool waitForEquip, int delayMs);
        static void ScheduleAutoAttackDraw();
        void UpdateHotkeyState(RE::PlayerCharacter* player, bool newKbdCombo, bool newPadCombo) const;
        void HandleKeyboardButton(const RE::ButtonEvent* a_event, RE::PlayerCharacter* player) const;
        void HandleGamepadButton(const RE::ButtonEvent* a_event, RE::PlayerCharacter* player) const;
        void OnKeyPressed(RE::PlayerCharacter* player) const;
        void OnKeyReleased() const;
        static bool IsWeaponDrawn(RE::Actor* actor);
        static void SetWeaponDrawn(RE::Actor* actor, bool drawn);
        static RE::ExtraDataList* GetPrimaryExtra(RE::InventoryEntryData* entry);
        static void EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                 BowState::IntegratedBowState& st);
        static void ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                BowState::IntegratedBowState& st);
        void HandleNormalMode(RE::PlayerCharacter* player, bool anyNow, bool blocked) const;
        void HandleSmartModePressed(bool blocked) const;
        void HandleSmartModeReleased(RE::PlayerCharacter* player, bool blocked) const;
        void UpdateSmartMode(RE::PlayerCharacter* player, float dt) const;
        void UpdateExitEquipWait(float dt) const;
        bool IsExitDelayReady(float dt) const;
        void CompleteExit() const;
        void UpdateExitPending(float dt) const;
        void ProcessButtonEvent(const RE::ButtonEvent* button, RE::PlayerCharacter* player) const;
        void ProcessInputEvents(RE::InputEvent* const* a_events, RE::PlayerCharacter* player) const;
        float CalculateDeltaTime() const;
        void ResetExitState() const;
    };

    class BowAnimEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static BowAnimEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override;
    };
}
