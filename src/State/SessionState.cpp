#include "SessionState.h"

namespace BowState {

    IntegratedBowState& Get() {
        static IntegratedBowState s;  // NOSONAR
        return s;
    }

    void Reset() {
        auto& st = Get();
        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;
        st.prevRight.base = nullptr;
        st.prevRight.extra = nullptr;
        st.prevRightFormID = 0;
        st.prevLeft.base = nullptr;
        st.prevLeft.extra = nullptr;
        st.prevLeftFormID = 0;

        st.isUsingBow = false;
        st.isEquipingBow = false;
        st.wasCombatPosed = false;
        st.isAutoAttackHeld = false;

        st.waitingAutoAttackAfterEquip.store(false, std::memory_order_relaxed);
        st.bowEquipped.store(false, std::memory_order_relaxed);

        st.prevAmmo = nullptr;

        st.pendingFinalizeExtras = false;
        st.pendingFinalizeExtrasTimer = 0.0f;
        st.pendingDesiredRight = nullptr;
        st.pendingDesiredLeft = nullptr;

        st.prevExtraEquipped.clear();
    }
}