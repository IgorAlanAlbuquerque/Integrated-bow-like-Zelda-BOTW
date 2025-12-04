#pragma once

namespace UnMapBlock {
    struct SavedLeftAttackMapping {
        bool saved = false;
        std::uint16_t inputKey = 0;
        std::uint16_t modifier = 0;
    };

    extern SavedLeftAttackMapping g_savedLeftAttack;  // NOSONAR - local state

    void SetNoLeftBlockPatch(bool patch);
}
