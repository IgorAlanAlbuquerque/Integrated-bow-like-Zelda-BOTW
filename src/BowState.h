#pragma once
#include <array>
#include <mutex>
#include <queue>

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
        inline bool IsTemperingTag(std::string_view inside) {
            for (auto q : kQualityTags) {
                if (inside.size() != q.size()) {
                    continue;
                }

                bool match = true;
                for (std::size_t i = 0; i < q.size(); ++i) {
                    const auto c = static_cast<char>(std::tolower(static_cast<unsigned char>(inside[i])));
                    if (c != q[i]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    return true;
                }
            }

            return false;
        }

        inline void TrimTrailingSpaces(std::string& s) {
            while (!s.empty() && s.back() == ' ') {
                s.pop_back();
            }
        }

        inline void RemoveChosenTagInplace(std::string& s) {
            constexpr std::string_view chosenTag{" (chosen)"};

            for (;;) {
                auto pos = s.find(chosenTag);
                if (pos == std::string::npos) {
                    break;
                }
                s.erase(pos, chosenTag.size());
            }

            TrimTrailingSpaces(s);
        }

        inline void StripTemperingSuffixes(std::string& name) {
            TrimTrailingSpaces(name);

            for (;;) {
                bool canStrip = true;
                std::size_t open = std::string::npos;

                if (name.size() < 3 || name.back() != ')') {
                    canStrip = false;
                }

                if (canStrip) {
                    open = name.rfind('(');
                    if (open == std::string::npos || open == 0 || name[open - 1] != ' ') {
                        canStrip = false;
                    }
                }

                if (canStrip) {
                    const std::string_view inside(name.data() + open + 1, name.size() - open - 2);
                    if (!IsTemperingTag(inside)) {
                        canStrip = false;
                    }
                }

                if (!canStrip) {
                    break;
                }

                name.erase(open - 1);
                TrimTrailingSpaces(name);
            }

            TrimTrailingSpaces(name);
        }

        inline void ApplyChosenTagToInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
            if (!base || !extra) {
                return;
            }

            auto* tdd = extra->GetExtraTextDisplayData();
            const char* cstr = nullptr;

            if (tdd) {
                cstr = tdd->displayName.c_str();
            }

            if (!cstr || !*cstr) {
                cstr = extra->GetDisplayName(base);
            }

            if (!cstr || !*cstr) {
                cstr = base->GetName();
            }
            if (!cstr || !*cstr) {
                return;
            }

            constexpr std::string_view chosenTag{" (chosen)"};
            std::string curName{cstr};

            RemoveChosenTagInplace(curName);
            StripTemperingSuffixes(curName);

            if (!curName.empty()) {
                curName += chosenTag;
            } else {
                curName = "(chosen)";
            }

            if (!tdd) {
                tdd = new RE::ExtraTextDisplayData(base, 1.0f);  // NOSONAR Lifetime é gerenciado pelo engine.
                if (!tdd) {
                    return;
                }
                extra->Add(tdd);
            }

            tdd->SetName(curName.c_str());
        }

        inline void RemoveChosenTagFromInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
            if (!base || !extra) {
                return;
            }

            auto* tdd = extra->GetExtraTextDisplayData();
            if (!tdd) {
                return;
            }

            const char* cstr = tdd->displayName.c_str();
            if (!cstr || !*cstr) {
                extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
                return;
            }

            std::string curName = cstr;
            RemoveChosenTagInplace(curName);
            StripTemperingSuffixes(curName);

            if (curName.empty()) {
                extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
            } else {
                tdd->SetName(curName.c_str());
            }
        }

        inline RE::BSFixedString GetAttackUserEvent() {
            static RE::BSFixedString ev{"Right Attack/Block"};
            return ev;
        }

        constexpr std::uint32_t kAttackMouseIdCode = 0;

        inline RE::ButtonEvent* MakeAttackButtonEvent(float value, float heldSecs) {
            return RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, GetAttackUserEvent(), kAttackMouseIdCode, value,
                                           heldSecs);
        }

        struct SyntheticInputState {
            std::mutex mutex;
            std::queue<RE::ButtonEvent*> pending;
        };

        inline SyntheticInputState& GetSyntheticInputState() {
            static SyntheticInputState s;  // NOSONAR
            return s;
        }

        inline void EnqueueSyntheticAttack(RE::ButtonEvent* ev) {
            if (!ev) {
                return;
            }

            auto& st = GetSyntheticInputState();
            {
                std::scoped_lock lk(st.mutex);
                st.pending.push(ev);
            }
        }

        inline RE::InputEvent* FlushSyntheticInput(RE::InputEvent* head) {
            auto& st = GetSyntheticInputState();

            std::queue<RE::ButtonEvent*> local;
            {
                std::scoped_lock lk(st.mutex);
                local.swap(st.pending);
            }

            if (local.empty()) {
                return head;
            }

            RE::InputEvent* synthHead = nullptr;
            RE::InputEvent* synthTail = nullptr;

            while (!local.empty()) {
                auto* ev = local.front();
                local.pop();
                if (!ev) {
                    continue;
                }

                ev->next = nullptr;

                if (!synthHead) {
                    synthHead = ev;
                    synthTail = ev;
                } else {
                    synthTail->next = ev;
                    synthTail = ev;
                }
            }

            if (!head) {
                return synthHead;
            }

            synthTail->next = head;
            return synthHead;
        }

        inline void DispatchAttackButtonEvent(RE::ButtonEvent* ev) { EnqueueSyntheticAttack(ev); }
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

    inline IntegratedBowState& Get() {
        static IntegratedBowState s;  // NOSONAR
        return s;
    }

    inline void SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra) {
        auto& cfg = IntegratedBow::GetBowConfig();
        auto& st = Get();

        if (RE::TESBoundObject* base = bow ? bow->As<RE::TESBoundObject>() : nullptr;
            st.chosenBow.base == base && st.chosenBow.extra == extra) {
            detail::RemoveChosenTagFromInstance(st.chosenBow.base, st.chosenBow.extra);
            st.chosenBow.base = nullptr;
            st.chosenBow.extra = nullptr;
            cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
        } else {
            if (st.chosenBow.base && st.chosenBow.extra) {
                BowState::detail::RemoveChosenTagFromInstance(st.chosenBow.base, st.chosenBow.extra);
            }
            st.chosenBow.base = base;
            st.chosenBow.extra = extra;
            detail::ApplyChosenTagToInstance(st.chosenBow.base, st.chosenBow.extra);

            if (bow) {
                cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);

            } else {
                cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
            }
        }

        cfg.Save();
    }

    inline void LoadChosenBow(RE::TESObjectWEAP* bow) {
        auto& st = Get();

        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;

        if (!bow) {
            return;
        }

        RE::TESBoundObject* base = bow->As<RE::TESBoundObject>();
        if (!base) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        auto inventory = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

        constexpr const char* kChosenTag = " (chosen)";
        constexpr std::size_t kTagLen = 9;

        for (auto const& [obj, data] : inventory) {
            if (obj != base) {
                continue;
            }

            auto& entryPtr = data.second;
            RE::InventoryEntryData const* entry = entryPtr.get();

            if (!entry || !entry->extraLists) {
                continue;
            }

            for (auto* extra : *entry->extraLists) {
                if (!extra) {
                    continue;
                }

                const char* dispName = extra->GetDisplayName(obj);
                if (!dispName || !*dispName) {
                    continue;
                }

                const std::size_t len = std::strlen(dispName);
                if (len < kTagLen) {
                    continue;
                }

                if (std::memcmp(dispName + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                    st.chosenBow.base = base;
                    st.chosenBow.extra = extra;

                    return;
                }
            }
        }
    }

    inline void ClearChosenBow() {
        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();

        if (st.chosenBow.base && st.chosenBow.extra) {
            detail::RemoveChosenTagFromInstance(st.chosenBow.base, st.chosenBow.extra);
        }

        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;

        cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
        cfg.Save();
    }

    inline bool IsAutoAttackHeld() { return Get().isAutoAttackHeld; }

    inline void SetAutoAttackHeld(bool value) { Get().isAutoAttackHeld = value; }

    inline bool EnsureChosenBowInInventory() {
        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        if (!st.chosenBow.base || !st.chosenBow.extra) {
            if (const auto formId = cfg.chosenBowFormID.load(std::memory_order_relaxed); formId != 0) {
                if (auto* bowForm = RE::TESForm::LookupByID<RE::TESObjectWEAP>(formId)) {
                    LoadChosenBow(bowForm);
                }
            }

            if (!st.chosenBow.base || !st.chosenBow.extra) {
                return false;
            }
        }

        auto const* const chosenBase = st.chosenBow.base;
        if (!chosenBase) {
            return false;
        }

        auto inventory = player->GetInventory([chosenBase](RE::TESBoundObject& obj) { return &obj == chosenBase; });

        constexpr const char* kChosenTag = " (chosen)";
        constexpr std::size_t kTagLen = 9;

        RE::ExtraDataList const* foundExact = nullptr;
        RE::ExtraDataList* foundTagged = nullptr;
        RE::ExtraDataList* foundAny = nullptr;

        for (auto const& [obj, data] : inventory) {
            if (obj != chosenBase) {
                continue;
            }

            auto const& entryPtr = data.second;
            RE::InventoryEntryData const* entry = entryPtr.get();
            if (!entry || !entry->extraLists) {
                continue;
            }

            for (auto* extra : *entry->extraLists) {
                if (!extra) {
                    continue;
                }

                if (extra == st.chosenBow.extra) {
                    foundExact = extra;
                    break;
                }

                if (!foundAny) {
                    foundAny = extra;
                }

                const char* dispName = extra->GetDisplayName(obj);
                if (!dispName || !*dispName) {
                    continue;
                }

                const std::size_t len = std::strlen(dispName);
                if (len < kTagLen) {
                    continue;
                }

                if (std::memcmp(dispName + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                    foundTagged = extra;
                }
            }

            if (foundExact) {
                break;
            }
        }

        if (foundExact) {
            return true;
        }

        if (foundTagged) {
            st.chosenBow.extra = foundTagged;

            return true;
        }

        if (foundAny) {
            st.chosenBow.extra = foundAny;

            BowState::detail::ApplyChosenTagToInstance(st.chosenBow.base, st.chosenBow.extra);

            if (auto const* bow = st.chosenBow.base->As<RE::TESObjectWEAP>()) {
                cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);

            } else {
                cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
            }
            cfg.Save();

            return true;
        }

        ClearChosenBow();
        return false;
    }

    inline RE::ExtraDataList* FindAnyInstanceExtraForBase(RE::TESBoundObject* base) {
        if (!base) {
            return nullptr;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return nullptr;
        }

        auto inventory = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

        for (auto const& [obj, data] : inventory) {
            if (obj != base) {
                continue;
            }

            auto const& entryPtr = data.second;
            auto const* entry = entryPtr.get();
            if (!entry || !entry->extraLists) {
                continue;
            }

            for (auto* extra : *entry->extraLists) {
                if (extra) {
                    return extra;
                }
            }
        }

        return nullptr;
    }

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

    inline void CaptureWornArmorSnapshot(std::vector<ExtraEquippedItem>& out) {
        out.clear();

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });

        for (auto const& [obj, data] : inventory) {
            auto* armor = obj->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            auto const& entryPtr = data.second;
            auto const* entry = entryPtr.get();
            if (!entry || !entry->extraLists) {
                continue;
            }

            for (auto* extra : *entry->extraLists) {
                if (!extra) {
                    continue;
                }

                if (extra->HasType(RE::ExtraDataType::kWorn) || extra->HasType(RE::ExtraDataType::kWornLeft)) {
                    out.push_back(ExtraEquippedItem{armor->As<RE::TESBoundObject>(), extra});
                }
            }
        }
    }

    inline std::vector<ExtraEquippedItem> DiffArmorSnapshot(const std::vector<ExtraEquippedItem>& before,
                                                            const std::vector<ExtraEquippedItem>& after) {
        std::vector<ExtraEquippedItem> removed;
        for (auto const& b : before) {
            bool stillWorn = false;
            for (auto const& a : after) {
                if (a.base == b.base && a.extra == b.extra) {
                    stillWorn = true;
                    break;
                }
            }
            if (!stillWorn) {
                removed.push_back(b);
            }
        }

        return removed;
    }

    inline void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* equipMgr) {
        if (!actor || !equipMgr) {
            return;
        }

        auto& st = Get();

        for (auto const& item : st.prevExtraEquipped) {
            if (!item.base) {
                continue;
            }

            equipMgr->EquipObject(actor, item.base, item.extra, 1, nullptr, true, true, true, false);
        }

        st.prevExtraEquipped.clear();
    }

    inline void AppendPrevExtraEquipped(const ExtraEquippedItem& item) {
        auto& st = Get();
        st.prevExtraEquipped.push_back(item);
    }

    inline bool ContainsPrevExtraEquipped(const ExtraEquippedItem& item) {
        auto const& st = Get();
        for (auto const& e : st.prevExtraEquipped) {
            if (e.base == item.base && e.extra == item.extra) {
                return true;
            }
        }
        return false;
    }

    inline void ApplyHiddenItemsPatch(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                      const std::vector<RE::FormID>& hiddenFormIDs) {
        if (!player || !equipMgr || hiddenFormIDs.empty()) {
            return;
        }

        auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });

        for (auto const& [obj, data] : inventory) {
            auto* armor = obj->As<RE::TESObjectARMO>();
            if (!armor) {
                continue;
            }

            if (auto const formId = armor->GetFormID();
                !std::binary_search(hiddenFormIDs.begin(), hiddenFormIDs.end(), formId)) {
                continue;
            }

            auto const& entryPtr = data.second;
            auto const* entry = entryPtr.get();
            if (!entry || !entry->extraLists) {
                continue;
            }

            for (auto* extra : *entry->extraLists) {
                if (!extra) {
                    continue;
                }

                if (!extra->HasType(RE::ExtraDataType::kWorn) && !extra->HasType(RE::ExtraDataType::kWornLeft)) {
                    continue;
                }

                ExtraEquippedItem item{armor->As<RE::TESBoundObject>(), extra};
                if (ContainsPrevExtraEquipped(item)) {
                    continue;
                }

                equipMgr->UnequipObject(player, item.base, item.extra, 1, nullptr, true, true, true, false, nullptr);

                AppendPrevExtraEquipped(item);
            }
        }
    }

    inline RE::TESAmmo* GetPreferredArrow() {
        auto const& cfg = IntegratedBow::GetBowConfig();
        const auto formId = cfg.preferredArrowFormID.load(std::memory_order_relaxed);
        if (formId == 0) {
            return nullptr;
        }
        return RE::TESForm::LookupByID<RE::TESAmmo>(formId);
    }

    inline void SetPreferredArrow(RE::TESAmmo* ammo) {
        auto& cfg = IntegratedBow::GetBowConfig();

        if (ammo) {
            cfg.preferredArrowFormID.store(ammo->GetFormID(), std::memory_order_relaxed);
        } else {
            cfg.preferredArrowFormID.store(0u, std::memory_order_relaxed);
        }

        cfg.Save();
    }

    inline void SetPrevAmmo(RE::TESAmmo* ammo) { Get().prevAmmo = ammo; }

    inline RE::TESAmmo* GetPrevAmmo() { return Get().prevAmmo; }

    inline void ClearPrevAmmo() { Get().prevAmmo = nullptr; }

    inline void RestorePrevWeaponsAndAmmo(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                          IntegratedBowState& st, bool clearIsUsingBow) {
        if (!player || !equipMgr) {
            return;
        }

        ReequipPrevExtraEquipped(player, equipMgr);

        // ----- RIGHT HAND -----
        RE::TESBoundObject* rightBase = st.prevRight.base;
        RE::ExtraDataList* rightExtra = st.prevRight.extra;

        if (!rightBase && st.prevRightFormID != 0) {
            if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevRightFormID)) {
                rightBase = form;
                rightExtra = FindAnyInstanceExtraForBase(form);
            }
        }

        if (rightBase) {
            equipMgr->EquipObject(player, rightBase, rightExtra, 1, nullptr, true, true, true, false);
        }

        // ----- LEFT HAND -----
        RE::TESBoundObject* leftBase = st.prevLeft.base;
        RE::ExtraDataList* leftExtra = st.prevLeft.extra;

        if (!leftBase && st.prevLeftFormID != 0) {
            if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevLeftFormID)) {
                leftBase = form;
                leftExtra = FindAnyInstanceExtraForBase(form);
            }
        }

        if (leftBase) {
            equipMgr->EquipObject(player, leftBase, leftExtra, 1, nullptr, true, true, true, false);
        }

        if (!rightBase && !leftBase && st.chosenBow.base) {
            equipMgr->UnequipObject(player, st.chosenBow.base, st.chosenBow.extra, 1, nullptr, true, true, true, false,
                                    nullptr);
        }

        if (auto* prevAmmo = st.prevAmmo) {
            equipMgr->EquipObject(player, prevAmmo, nullptr, 1, nullptr, true, true, true, false);
        } else {
            if (auto* preferred = GetPreferredArrow()) {
                equipMgr->UnequipObject(player, preferred, nullptr, 1, nullptr, true, true, true, false, nullptr);
            }
        }
        st.prevAmmo = nullptr;

        if (clearIsUsingBow) {
            st.isUsingBow = false;
        }

        ClearPrevWeapons();
        ClearPrevExtraEquipped();
    }
}
