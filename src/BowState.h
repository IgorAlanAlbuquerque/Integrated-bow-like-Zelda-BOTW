#pragma once
#include "BowConfig.h"
#include "PCH.h"
#include "RE/E/ExtraTextDisplayData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectREFR.h"

namespace BowState {
    namespace detail {
        static constexpr std::string_view kQualityTags[] = {"fine",     "superior", "exquisite",
                                                            "flawless", "epic",     "legendary"};
        inline bool IsTemperingTag(std::string_view inside) {
            std::string lower;
            lower.reserve(inside.size());
            for (char c : inside) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }

            for (auto q : kQualityTags) {
                if (lower == q) {
                    return true;
                }
            }
            return false;
        }

        inline void StripTemperingSuffixes(std::string& name) {
            for (;;) {
                while (!name.empty() && name.back() == ' ') {
                    name.pop_back();
                }

                if (name.size() < 3 || name.back() != ')') {
                    break;
                }

                auto open = name.rfind('(');
                if (open == std::string::npos || open == 0 || name[open - 1] != ' ') {
                    break;
                }

                if (std::string_view inside(name.data() + open + 1, name.size() - open - 2); !IsTemperingTag(inside)) {
                    break;
                }

                name.erase(open - 1);
            }

            while (!name.empty() && name.back() == ' ') {
                name.pop_back();
            }
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

            for (;;) {
                auto pos = curName.find(chosenTag);
                if (pos == std::string::npos) {
                    break;
                }
                curName.erase(pos, chosenTag.size());
            }

            StripTemperingSuffixes(curName);

            if (!curName.empty()) {
                curName += " (chosen)";
            } else {
                curName = "(chosen)";
            }

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

            const char* cstr = tdd->displayName.c_str();
            if (!cstr || !*cstr) {
                extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
                return;
            }

            std::string curName = cstr;
            constexpr std::string_view tag{" (chosen)"};

            auto pos = curName.rfind(tag);
            if (pos == std::string::npos) {
                return;
            }

            curName.erase(pos, tag.size());

            while (!curName.empty() && curName.back() == ' ') {
                curName.pop_back();
            }

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

        inline void DispatchAttackButtonEvent(RE::ButtonEvent* ev) {
            if (!ev) {
                return;
            }

            auto* pc = RE::PlayerControls::GetSingleton();
            if (!pc) {
                return;
            }

            auto* handler = pc->attackBlockHandler;
            if (!handler) {
                return;
            }

            if (!handler->CanProcess(ev)) {
                return;
            }

            handler->ProcessButton(ev, &pc->data);
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
        bool isAutoAttackHeld{false};
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
        auto const& st = Get();

        if (!st.chosenBow.base || !st.chosenBow.extra) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        auto inventory = player->GetInventory([&](RE::TESBoundObject& obj) { return &obj == st.chosenBow.base; });

        for (auto const& [obj, data] : inventory) {
            if (obj != st.chosenBow.base) {
                continue;
            }

            auto const& entryPtr = data.second;
            RE::InventoryEntryData const* entry = entryPtr.get();
            if (!entry || !entry->extraLists) {
                continue;
            }

            for (auto const* extra : *entry->extraLists) {
                if (extra == st.chosenBow.extra) {
                    return true;
                }
            }
        }

        ClearChosenBow();
        return false;
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
        st.isAutoAttackHeld = false;
    }

}
