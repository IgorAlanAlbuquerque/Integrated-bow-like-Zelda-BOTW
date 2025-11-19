#pragma once
#include "BowConfig.h"
#include "PCH.h"
#include "RE/E/ExtraTextDisplayData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectREFR.h"

namespace BowState {
    namespace detail {

        inline void ApplyChosenTagToInstance(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
            if (!base || !extra) {
                return;
            }

            const char* cstr = extra->GetDisplayName(base);
            if (!cstr || !*cstr) {
                return;
            }

            constexpr std::string_view tag{" (chosen)"};

            std::string curName{cstr};
            if (curName.ends_with(tag)) {
                return;
            }

            curName += tag;

            auto* tdd = extra->GetExtraTextDisplayData();
            if (!tdd) {
                tdd = new RE::ExtraTextDisplayData(base, 1.0f);
                if (!tdd) {
                    spdlog::warn("[IntegratedBow] failed to create ExtraTextDisplayData for chosen bow");
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

            const char* cstr = tdd->GetDisplayName(base, 1.0f);
            if (!cstr || !*cstr) {
                return;
            }

            std::string curName = cstr;
            constexpr std::string_view tag{" (chosen)"};

            if (!curName.ends_with(tag)) {
                return;
            }

            curName.erase(curName.size() - tag.size());

            tdd->SetName(curName.c_str());
        }

    }
    struct ChosenInstance {
        RE::TESBoundObject* base{nullptr};
        RE::ExtraDataList* extra{nullptr};
    };

    struct IntegratedBowState {
        ChosenInstance chosenBow{};
        ChosenInstance prevRight{};
        ChosenInstance prevLeft{};
        bool isUsingBow{false};
        bool isEquipingBow{false};
        bool wasCombatPosed{false};
    };

    inline IntegratedBowState& Get() {
        static IntegratedBowState s;
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

        auto inventory = player->GetInventory([&](RE::TESBoundObject& obj) { return &obj == base; });

        constexpr const char* kChosenTag = " (chosen)";
        constexpr std::size_t kTagLen = 9;

        for (auto& [obj, data] : inventory) {
            if (obj != base) {
                continue;
            }

            auto& entryPtr = data.second;
            RE::InventoryEntryData* entry = entryPtr.get();

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
                    spdlog::info("[IntegratedBow] LoadChosenBow: found chosen instance in inventory: {}", dispName);
                    return;
                }
            }
        }
    }

    inline void ClearChosenBow() {
        auto& st = Get();
        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;
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

        st.prevLeft.base = leftBase;
        st.prevLeft.extra = leftExtra;
    }

    inline void ClearPrevWeapons() {
        auto& st = Get();

        st.prevRight.base = nullptr;
        st.prevRight.extra = nullptr;

        st.prevLeft.base = nullptr;
        st.prevLeft.extra = nullptr;
    }

    inline void Reset() {
        auto& st = Get();

        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;

        st.prevRight.base = nullptr;
        st.prevRight.extra = nullptr;

        st.prevLeft.base = nullptr;
        st.prevLeft.extra = nullptr;

        st.isUsingBow = false;
        st.isEquipingBow = false;
        st.wasCombatPosed = false;
    }

}
