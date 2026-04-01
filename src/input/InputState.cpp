#include "InputState.h"

namespace BowInput {
    InputState& Inputs() noexcept {
        static InputState s;  // NOSONAR
        return s;
    }

    void InputState::Clear() {
        for (auto& v : down) v.store(false, std::memory_order_relaxed);
    }

    void InputState::OnButton(RE::INPUT_DEVICE dev, int code, bool isPressed) {
        const int idx = InputUtil::ToUnifiedIndex(dev, code);
        if (idx < 0 || idx >= kMaxCode) return;
        down[static_cast<std::size_t>(idx)].store(isPressed, std::memory_order_relaxed);
    }
}