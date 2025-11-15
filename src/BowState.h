#pragma once
#include "PCH.h"

namespace BowState {

    struct IntegratedBowState {
        RE::TESObjectWEAP* chosenBow{nullptr};
        RE::TESBoundObject* prevRight{nullptr};
        RE::TESBoundObject* prevLeft{nullptr};
        bool isUsingBow{false};
        bool isEquipingBow{false};
        bool wasCombatPosed{false};
    };

    inline IntegratedBowState& Get() {
        static IntegratedBowState s;
        return s;
    }

    inline void SetChosenBow(RE::TESObjectWEAP* bow) { Get().chosenBow = bow; }

    inline void ClearChosenBow() {
        auto& st = Get();
        st.chosenBow = nullptr;
    }

    inline bool isEquipingBow() { return Get().isEquipingBow; }

    inline bool HasChosenBow() { return Get().chosenBow != nullptr; }

    inline bool IsUsingBow() { return Get().isUsingBow; }

    inline void SetUsingBow(bool value) { Get().isUsingBow = value; }

    inline void SetPrevWeapons(RE::TESForm* right, RE::TESForm* left) {
        auto& st = Get();
        st.prevRight = right ? right->As<RE::TESBoundObject>() : nullptr;
        st.prevLeft = left ? left->As<RE::TESBoundObject>() : nullptr;
    }

    inline void ClearPrevWeapons() {
        auto& st = Get();
        st.prevRight = nullptr;
        st.prevLeft = nullptr;
    }

    inline void Reset() {
        auto& st = Get();
        st.chosenBow = nullptr;
        st.prevRight = nullptr;
        st.prevLeft = nullptr;
        st.isUsingBow = false;
    }

}
