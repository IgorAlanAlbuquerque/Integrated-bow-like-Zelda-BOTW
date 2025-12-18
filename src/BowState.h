#pragma once
#include <array>
#include <mutex>
#include <queue>
#include <ranges>

#include "BowConfig.h"
#include "PCH.h"

namespace RE {
    class InputEvent;
    template <class Event>
    class BSTEventSource;
}

namespace BowState {
    namespace detail {
        static constexpr std::array<std::string_view, 6> kQualityTags{"fine",     "superior", "exquisite",
                                                                      "flawless", "epic",     "legendary"};
        constexpr std::uint32_t kAttackMouseIdCode = 0;
        struct SyntheticInputState {
            std::mutex mutex;
            std::queue<RE::ButtonEvent*> pending;
        };

        SyntheticInputState& GetSyntheticInputState();
        bool IsTemperingTag(std::string_view inside);
        void TrimTrailingSpaces(std::string& s);
        void RemoveChosenTagInplace(std::string& s);
        void StripTemperingSuffixes(std::string& name);
        void ApplyChosenTagToInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra);
        void RemoveChosenTagFromInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra);
        RE::BSFixedString GetAttackUserEvent();
        RE::ButtonEvent* MakeAttackButtonEvent(float value, float heldSecs);
        void EnqueueSyntheticAttack(RE::ButtonEvent* ev);
        RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head);
        void DispatchAttackButtonEvent(RE::ButtonEvent* ev);
    }

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
    };

    IntegratedBowState& Get();

    inline bool IsAutoAttackHeld() { return Get().isAutoAttackHeld; }
    inline void SetAutoAttackHeld(bool value) { Get().isAutoAttackHeld = value; }
    inline bool IsEquipingBow() { return Get().isEquipingBow; }
    inline bool HasChosenBow() { return Get().chosenBow.base != nullptr; }
    inline bool IsUsingBow() { return Get().isUsingBow; }
    inline void SetUsingBow(bool value) { Get().isUsingBow = value; }
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
    inline void Reset() {
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
    }
    inline void SetBowEquipped(bool v) {
        auto& st = Get();
        st.bowEquipped.store(v, std::memory_order_relaxed);
    }
    inline bool IsBowEquipped() { return Get().bowEquipped.load(std::memory_order_relaxed); }
    inline void SetWaitingAutoAfterEquip(bool v) {
        auto& st = Get();
        st.waitingAutoAttackAfterEquip.store(v, std::memory_order_relaxed);
    }
    inline bool IsWaitingAutoAfterEquip() { return Get().waitingAutoAttackAfterEquip.load(std::memory_order_relaxed); }
    inline void SetPrevExtraEquipped(std::vector<ExtraEquippedItem>&& items) {
        auto& st = Get();
        st.prevExtraEquipped = std::move(items);
    }
    inline const std::vector<ExtraEquippedItem>& GetPrevExtraEquipped() { return Get().prevExtraEquipped; }
    inline void ClearPrevExtraEquipped() {
        auto& st = Get();
        st.prevExtraEquipped.clear();
    }
    inline void SetPrevAmmo(RE::TESAmmo* ammo) { Get().prevAmmo = ammo; }
    inline RE::TESAmmo* GetPrevAmmo() { return Get().prevAmmo; }
    inline void ClearPrevAmmo() { Get().prevAmmo = nullptr; }

    void LoadChosenBow(RE::TESObjectWEAP* bow);
    void ClearChosenBow();
    bool EnsureChosenBowInInventory();
    void SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra);
    RE::ExtraDataList* FindAnyInstanceExtraForBase(RE::TESBoundObject* base);
    void CaptureWornArmorSnapshot(std::vector<ExtraEquippedItem>& out);
    std::vector<ExtraEquippedItem> DiffArmorSnapshot(const std::vector<ExtraEquippedItem>& before,
                                                     const std::vector<ExtraEquippedItem>& after);
    void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* equipMgr);
    void AppendPrevExtraEquipped(const ExtraEquippedItem& item);
    bool ContainsPrevExtraEquipped(const ExtraEquippedItem& item);
    void ApplyHiddenItemsPatch(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                               const std::vector<RE::FormID>& hiddenFormIDs);
    RE::TESAmmo* GetPreferredArrow();
    void SetPreferredArrow(RE::TESAmmo* ammo);
    void RestorePrevWeaponsAndAmmo(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr, IntegratedBowState& st,
                                   bool clearIsUsingBow);
}
