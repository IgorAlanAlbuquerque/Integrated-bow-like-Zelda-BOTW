#pragma once
#include <cstdint>

#include "PCH.h"

namespace BowState::detail {

    // Cria um evento de ataque sintético (mouse esquerdo + "Right Attack/Block")
    [[nodiscard]] RE::ButtonEvent* MakeAttackButtonEvent(float value, float heldSecs);

    // Cria um evento sintético genérico com device, userEvent e idCode arbitrários
    [[nodiscard]] RE::ButtonEvent* MakeGenericButtonEvent(RE::INPUT_DEVICE dev, const RE::BSFixedString& userEvent,
                                                          std::uint32_t idCode, float value, float heldSecs);

    // Enfileira um evento para ser injetado no próximo FlushSyntheticInput
    void EnqueueSyntheticAttack(RE::ButtonEvent* ev);
    void EnqueueSyntheticEvent(RE::ButtonEvent* ev);  // alias semântico

    // Prepend todos os eventos enfileirados ao linked list da engine
    [[nodiscard]] RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);

    // Conveniência — cria e enfileira ataque em um passo
    void DispatchAttackButtonEvent(RE::ButtonEvent* ev);

}