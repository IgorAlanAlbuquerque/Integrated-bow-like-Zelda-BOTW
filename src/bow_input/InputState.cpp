#include "InputState.h"

#include <cassert>
#include <ranges>

namespace BowInput {
    InputState& Inputs() noexcept {
        static InputState s;  // NOSONAR
        return s;
    }

    void InputState::Clear() {
        for (auto& v : kbDown) v.store(false, std::memory_order_relaxed);
        for (auto& v : gpDown) v.store(false, std::memory_order_relaxed);

        kbDownList.clear();
        gpDownList.clear();
    }

    namespace {
        inline void DownListAdd(std::vector<int>& list, int code) {
            if (std::ranges::find(list, code) == list.end()) {
                list.push_back(code);
            }
        }

        inline void DownListRemove(std::vector<int>& list, int code) {
            auto it = std::ranges::find(list, code);
            if (it != list.end()) {
                *it = list.back();
                list.pop_back();
            }
        }
    }

    void InputState::OnButton(RE::INPUT_DEVICE dev, int code, bool isPressed, bool isDownEdge, bool isUpEdge) {
        if (code < 0 || code >= kMaxCode) {
            return;
        }

        const auto idx = static_cast<std::size_t>(code);

        if (dev == RE::INPUT_DEVICE::kKeyboard) {
            kbDown[idx].store(isPressed, std::memory_order_relaxed);
            if (isDownEdge) DownListAdd(kbDownList, code);
            if (isUpEdge) DownListRemove(kbDownList, code);

        } else if (dev == RE::INPUT_DEVICE::kGamepad) {
            gpDown[idx].store(isPressed, std::memory_order_relaxed);
            if (isDownEdge) DownListAdd(gpDownList, code);
            if (isUpEdge) DownListRemove(gpDownList, code);
        }
    }

    std::span<const int> InputState::DownList(RE::INPUT_DEVICE dev) const {
        switch (dev) {
            case RE::INPUT_DEVICE::kKeyboard:

            case RE::INPUT_DEVICE::kMouse:
                return std::span<const int>(kbDownList);

            case RE::INPUT_DEVICE::kGamepad:
                return std::span<const int>(gpDownList);

            default:
                assert(false && "InputState::DownList: device não suportado");
                return {};
        }
    }
}