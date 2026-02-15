#pragma once

#include <cstdint>

namespace RE {
    template <class T>
    class BSTEventSink;
    template <class T>
    class BSTEventSource;
    struct InputEvent;
    struct BSAnimationGraphEvent;
}

namespace BowInput {

    void RegisterInputHandler();

    void SetMode(int mode);

    void SetKeyScanCodes(int k1, int k2, int k3);

    void SetGamepadButtons(int b1, int b2, int b3);

    void RequestGamepadCapture();

    int PollCapturedGamepadButton();

    [[nodiscard]] bool IsHotkeyDown();

    [[nodiscard]] bool IsUnequipAllowed() noexcept;

    void BlockUnequipForMs(std::uint64_t ms) noexcept;

    void ForceAllowUnequip() noexcept;

    void HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>* src);

    class BowInputHandler final : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static BowInputHandler* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
                                              RE::BSTEventSource<RE::InputEvent*>*) override;

    private:
        BowInputHandler() = default;

        void ProcessButtonEvents(RE::InputEvent* const* a_events, RE::PlayerCharacter* player) const;
        void ProcessOneButton(const RE::ButtonEvent* button, RE::PlayerCharacter* player) const;
        [[nodiscard]] static float CalculateDeltaTime();
    };

}