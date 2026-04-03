#pragma once
#include <cstdint>

#include "PCH.h"

namespace BowState::detail {

    [[nodiscard]] RE::ButtonEvent* MakeAttackButtonEvent(float value, float heldSecs);
    [[nodiscard]] RE::ButtonEvent* MakeGenericButtonEvent(RE::INPUT_DEVICE dev, const RE::BSFixedString& userEvent,
                                                          std::uint32_t idCode, float value, float heldSecs);
    void EnqueueSyntheticAttack(RE::ButtonEvent* ev);
    void EnqueueSyntheticEvent(RE::ButtonEvent* ev);
    [[nodiscard]] RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);
    void DispatchAttackButtonEvent(RE::ButtonEvent* ev);

}