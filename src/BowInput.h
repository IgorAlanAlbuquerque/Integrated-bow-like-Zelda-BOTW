#pragma once
#include <atomic>

namespace BowInput {
    inline constexpr int kMaxComboKeys = 3;
    struct GlobalState {
        bool g_holdMode = true;
        std::array<int, BowInput::kMaxComboKeys> g_bowKeyScanCodes = {0x2F, -1, -1};
        std::array<bool, BowInput::kMaxComboKeys> g_bowKeyDown = {false, false, false};
        std::array<int, BowInput::kMaxComboKeys> g_bowPadButtons = {-1, -1, -1};
        std::array<bool, BowInput::kMaxComboKeys> g_bowPadDown = {false, false, false};
        bool g_kbdComboDown = false;
        bool g_padComboDown = false;
        bool g_hotkeyDown = false;
        std::atomic_uint64_t g_exitToken{0};
        std::atomic_bool g_captureRequested{false};
        std::atomic_int g_capturedEncoded{-1};
        std::atomic_bool g_attackHoldActive{false};
        std::atomic<float> g_attackHoldSecs{0.0f};
        std::atomic_bool g_pendingRestoreAfterSheathe{false};
    };

    GlobalState& Globals() noexcept;
    void RegisterInputHandler();
    void RegisterAnimEventSink();

    void SetHoldMode(bool hold);
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
    };

    class BowAnimEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static BowAnimEventSink* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) override;
    };
}
