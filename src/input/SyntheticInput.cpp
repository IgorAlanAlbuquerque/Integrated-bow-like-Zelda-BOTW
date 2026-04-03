#include "SyntheticInput.h"

#include <mutex>
#include <queue>

#include "PCH.h"

namespace BowState::detail {
    namespace {

        constexpr std::uint32_t kAttackMouseIdCode = 0;

        struct SyntheticInputState {
            std::mutex mutex;
            std::queue<RE::ButtonEvent*> pending;
        };

        SyntheticInputState& GetState() noexcept {
            static SyntheticInputState s;  // NOSONAR
            return s;
        }

        RE::BSFixedString GetAttackUserEvent() {
            static RE::BSFixedString ev{"Right Attack/Block"};
            return ev;
        }

    }

    RE::ButtonEvent* MakeAttackButtonEvent(float value, float heldSecs) {
        auto* ev = static_cast<RE::ButtonEvent*>(RE::malloc(sizeof(RE::ButtonEvent)));
        if (!ev) {
            return nullptr;
        }

        std::memset(ev, 0, sizeof(RE::ButtonEvent));

        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ButtonEvent[0]};
        *reinterpret_cast<std::uintptr_t*>(ev) = vtbl.address();

        ev->next = nullptr;
        ev->Init(RE::INPUT_DEVICE::kMouse, kAttackMouseIdCode, value, heldSecs, GetAttackUserEvent());

        return ev;
    }

    RE::ButtonEvent* MakeGenericButtonEvent(RE::INPUT_DEVICE dev, const RE::BSFixedString& userEvent,
                                            std::uint32_t idCode, float value, float heldSecs) {
        auto* ev = static_cast<RE::ButtonEvent*>(RE::malloc(sizeof(RE::ButtonEvent)));
        if (!ev) {
            return nullptr;
        }

        std::memset(ev, 0, sizeof(RE::ButtonEvent));

        REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_ButtonEvent[0]};
        *reinterpret_cast<std::uintptr_t*>(ev) = vtbl.address();

        ev->next = nullptr;
        ev->Init(dev, idCode, value, heldSecs, userEvent);

        return ev;
    }

    void EnqueueSyntheticAttack(RE::ButtonEvent* ev) {
        if (!ev) return;
        auto& st = GetState();
        std::scoped_lock lk(st.mutex);
        st.pending.push(ev);
    }

    void EnqueueSyntheticEvent(RE::ButtonEvent* ev) { EnqueueSyntheticAttack(ev); }

    RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head) {
        auto& st = GetState();

        std::queue<RE::ButtonEvent*> local;
        {
            std::scoped_lock lk(st.mutex);
            local.swap(st.pending);
        }

        if (local.empty()) return head;

        RE::InputEvent* synthHead = nullptr;
        RE::InputEvent* synthTail = nullptr;

        while (!local.empty()) {
            auto* ev = local.front();
            local.pop();
            if (!ev) continue;
            ev->next = nullptr;
            if (!synthHead) {
                synthHead = synthTail = ev;
            } else {
                synthTail->next = ev;
                synthTail = ev;
            }
        }

        if (!head) return synthHead;
        synthTail->next = head;
        return synthHead;
    }

    void DispatchAttackButtonEvent(RE::ButtonEvent* ev) { EnqueueSyntheticAttack(ev); }

}