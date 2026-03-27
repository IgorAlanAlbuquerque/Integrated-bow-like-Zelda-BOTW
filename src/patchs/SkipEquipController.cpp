#include "SkipEquipController.h"

#include <chrono>
#include <cstdint>

#include "RE/B/BSFixedString.h"
#include "RE/P/PlayerCharacter.h"

namespace {
    struct State {
        std::uint64_t disableAtMs{0};
        std::uint64_t token{0};
    };

    State& GetState() {
        static State s;  // NOSONAR
        return s;
    }

    std::atomic<std::uint64_t> g_skipToken{0};  // NOSONAR

    std::uint64_t NowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    constexpr const char* kVarInstaAnim = "InstantEquipAnim";

    inline void SetSkipVars(RE::PlayerCharacter* pc, bool enable) {
        if (!pc) return;

        (void)pc->SetGraphVariableBool(kVarInstaAnim, enable);
    }

}

namespace IntegratedBow::SkipEquipController {
    void Enable(RE::PlayerCharacter* pc) { SetSkipVars(pc, true); }
    void Disable(RE::PlayerCharacter* pc) { SetSkipVars(pc, false); }
    void EnableAndArmDisable(RE::PlayerCharacter* pc, std::uint64_t delayMs) {
        const auto token = g_skipToken.fetch_add(1, std::memory_order_relaxed) + 1;
        auto& st = GetState();
        st.token = token;
        st.disableAtMs = NowMs() + delayMs;

        Enable(pc);
    }

    void ArmDisable(std::uint64_t delayMs) {
        auto& st = GetState();
        if (st.token == 0) {
            return;
        }
        st.disableAtMs = NowMs() + delayMs;
    }

    void Cancel() {
        auto& st = GetState();
        st.disableAtMs = 0;
        st.token = 0;
        g_skipToken.fetch_add(1, std::memory_order_relaxed);
    }

    void Tick() {
        auto& st = GetState();
        if (st.disableAtMs == 0) {
            return;
        }

        if (const auto now = NowMs(); now < st.disableAtMs) {
            return;
        }

        const auto token = st.token;
        st.disableAtMs = 0;
        st.token = 0;

        if (g_skipToken.load(std::memory_order_relaxed) != token) {
            return;
        }

        auto* pc = RE::PlayerCharacter::GetSingleton();
        Disable(pc);
    }
}
