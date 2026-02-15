#pragma once
#include <string_view>

namespace BowInput {
    struct InputGate {
        static bool IsInputBlockedByMenus();
        static bool IsAttackEvent(std::string_view ue) noexcept;
    };
}
