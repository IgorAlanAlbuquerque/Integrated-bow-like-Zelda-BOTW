#pragma once

namespace BowInput {
    inline constexpr int kMaxComboKeys = 3;
    void RegisterInputHandler();

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
        static void ScheduleExitBowMode(bool waitBowDraw, int delayMs);
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
}
