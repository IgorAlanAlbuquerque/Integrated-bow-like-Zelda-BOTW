#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

#include "PCH.h"

namespace BowState {

    struct ChosenInstance {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
    };

    struct ExtraEquippedItem {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
    };

    struct IntegratedBowState {
        ChosenInstance chosenBow{};
        ChosenInstance prevRight{};
        ChosenInstance prevLeft{};
        RE::FormID prevRightFormID{0};
        RE::FormID prevLeftFormID{0};

        bool isUsingBow{false};
        bool isEquipingBow{false};
        bool wasCombatPosed{false};
        bool isAutoAttackHeld{false};

        std::atomic_bool waitingAutoAttackAfterEquip{false};
        std::atomic_bool bowEquipped{false};

        std::vector<ExtraEquippedItem> prevExtraEquipped{};
        RE::TESAmmo* prevAmmo{nullptr};

        bool pendingFinalizeExtras{false};
        float pendingFinalizeExtrasTimer{0.0f};
        RE::TESBoundObject* pendingDesiredRight{nullptr};
        RE::TESBoundObject* pendingDesiredLeft{nullptr};
    };

    IntegratedBowState& Get();

    // ── accessors ──────────────────────────────────────────────────────────

    inline bool IsAutoAttackHeld() { return Get().isAutoAttackHeld; }
    inline void SetAutoAttackHeld(bool v) { Get().isAutoAttackHeld = v; }
    inline bool IsEquipingBow() { return Get().isEquipingBow; }
    inline bool HasChosenBow() { return Get().chosenBow.base != nullptr; }
    inline bool IsUsingBow() { return Get().isUsingBow; }
    inline void SetUsingBow(bool v) { Get().isUsingBow = v; }

    inline bool IsBowEquipped() { return Get().bowEquipped.load(std::memory_order_relaxed); }
    inline void SetBowEquipped(bool v) { Get().bowEquipped.store(v, std::memory_order_relaxed); }

    inline bool IsWaitingAutoAfterEquip() { return Get().waitingAutoAttackAfterEquip.load(std::memory_order_relaxed); }
    inline void SetWaitingAutoAfterEquip(bool v) {
        Get().waitingAutoAttackAfterEquip.store(v, std::memory_order_relaxed);
    }

    inline void SetPrevWeapons(RE::TESBoundObject* rightBase, RE::ExtraDataList* rightExtra,
                               RE::TESBoundObject* leftBase, RE::ExtraDataList* leftExtra) {
        auto& st = Get();
        st.prevRight.base = rightBase;
        st.prevRight.extra = rightExtra;
        st.prevRightFormID = rightBase ? rightBase->GetFormID() : 0;
        st.prevLeft.base = leftBase;
        st.prevLeft.extra = leftExtra;
        st.prevLeftFormID = leftBase ? leftBase->GetFormID() : 0;
    }
    inline void ClearPrevWeapons() {
        auto& st = Get();
        st.prevRight.base = nullptr;
        st.prevRight.extra = nullptr;
        st.prevRightFormID = 0;
        st.prevLeft.base = nullptr;
        st.prevLeft.extra = nullptr;
        st.prevLeftFormID = 0;
    }

    inline void SetPrevAmmo(RE::TESAmmo* ammo) { Get().prevAmmo = ammo; }
    inline RE::TESAmmo* GetPrevAmmo() { return Get().prevAmmo; }
    inline void ClearPrevAmmo() { Get().prevAmmo = nullptr; }

    inline void SetPrevExtraEquipped(std::vector<ExtraEquippedItem>&& items) {
        Get().prevExtraEquipped = std::move(items);
    }
    inline const std::vector<ExtraEquippedItem>& GetPrevExtraEquipped() { return Get().prevExtraEquipped; }
    inline void ClearPrevExtraEquipped() { Get().prevExtraEquipped.clear(); }

    void Reset();
}