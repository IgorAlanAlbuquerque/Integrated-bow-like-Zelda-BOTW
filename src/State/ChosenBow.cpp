#include "ChosenBow.h"

#include <cstring>
#include <string>
#include <string_view>

#include "PCH.h"
#include "State/SessionState.h"
#include "Config/Config.h"

namespace BowState {
    namespace {

        // ── tag helpers ───────────────────────────────────────────────────

        constexpr std::array<std::string_view, 6> kQualityTags{"fine",     "superior", "exquisite",
                                                               "flawless", "epic",     "legendary"};

        constexpr const char* kChosenTag = " (chosen)";
        constexpr std::size_t kTagLen = 9;

        void TrimTrailingSpaces(std::string& s) {
            while (!s.empty() && s.back() == ' ') s.pop_back();
        }

        bool IsTemperingTag(std::string_view inside) {
            for (auto q : kQualityTags) {
                if (inside.size() != q.size()) continue;
                bool match = true;
                for (std::size_t i = 0; i < q.size(); ++i) {
                    if (static_cast<char>(std::tolower(static_cast<unsigned char>(inside[i]))) != q[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
            return false;
        }

        void RemoveChosenTagInplace(std::string& s) {
            constexpr std::string_view tag{" (chosen)"};
            for (;;) {
                auto pos = s.find(tag);
                if (pos == std::string::npos) break;
                s.erase(pos, tag.size());
            }
            TrimTrailingSpaces(s);
        }

        void StripTemperingSuffixes(std::string& name) {
            TrimTrailingSpaces(name);
            for (;;) {
                if (name.size() < 3 || name.back() != ')') break;
                const auto open = name.rfind('(');
                if (open == std::string::npos || open == 0 || name[open - 1] != ' ') break;
                const std::string_view inside{name.data() + open + 1, name.size() - open - 2};
                if (!IsTemperingTag(inside)) break;
                name.erase(open - 1);
                TrimTrailingSpaces(name);
            }
            TrimTrailingSpaces(name);
        }

        void ApplyChosenTag(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
            if (!base || !extra) return;
            if (IntegratedBow::GetBowConfig().noChosenTag) return;

            auto* tdd = extra->GetExtraTextDisplayData();
            const char* cstr = tdd ? tdd->displayName.c_str() : nullptr;
            if (!cstr || !*cstr) cstr = extra->GetDisplayName(base);
            if (!cstr || !*cstr) cstr = base->GetName();
            if (!cstr || !*cstr) return;

            std::string name{cstr};
            RemoveChosenTagInplace(name);
            StripTemperingSuffixes(name);
            name += !name.empty() ? " (chosen)" : "(chosen)";

            if (!tdd) {
                tdd = new RE::ExtraTextDisplayData(base, 1.0f);  // NOSONAR
                extra->Add(tdd);
            }
            tdd->SetName(name.c_str());
        }

        void RemoveChosenTag(RE::TESBoundObject* base, RE::ExtraDataList* extra) {
            if (!base || !extra) return;
            auto* tdd = extra->GetExtraTextDisplayData();
            if (!tdd) return;
            const char* cstr = tdd->displayName.c_str();
            if (!cstr || !*cstr) {
                extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
                return;
            }
            std::string name{cstr};
            RemoveChosenTagInplace(name);
            StripTemperingSuffixes(name);
            if (name.empty())
                extra->RemoveByType(RE::ExtraDataType::kTextDisplayData);
            else
                tdd->SetName(name.c_str());
        }

        void RemoveTagFromAnyTaggedInstance(RE::TESBoundObject* base) {
            if (!base) return;
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;

            auto inventory = player->GetInventory();
            for (auto const& [obj, data] : inventory) {
                if (obj != base) continue;
                auto const* entry = data.second.get();
                if (!entry || !entry->extraLists) return;
                for (auto* x : *entry->extraLists) {
                    if (!x) continue;
                    const char* disp = x->GetDisplayName(base);
                    if (!disp) continue;
                    const std::size_t len = std::strlen(disp);
                    if (len >= kTagLen && std::memcmp(disp + (len - kTagLen), kChosenTag, kTagLen) == 0)
                        RemoveChosenTag(base, x);
                }
                return;
            }
        }

    }  // namespace

    // ── utilitários públicos ───────────────────────────────────────────────

    RE::ExtraDataList* FindAnyInstanceExtraForBase(RE::TESBoundObject* base) {
        if (!base) return nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto inventory = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

        for (auto const& [obj, data] : inventory) {
            if (obj != base) continue;
            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists) continue;
            for (auto* extra : *entry->extraLists)
                if (extra) return extra;
        }
        return nullptr;
    }

    RE::ExtraDataList* ResolveLiveExtra(RE::TESBoundObject* base, RE::ExtraDataList* candidate) {
        if (!base || !candidate) return nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;

        auto inventory = player->GetInventory();
        for (auto const& [obj, data] : inventory) {
            if (obj != base) continue;
            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists) return nullptr;
            for (auto* x : *entry->extraLists)
                if (x == candidate) return x;
            return nullptr;
        }
        return nullptr;
    }

    // ── arco escolhido ─────────────────────────────────────────────────────

    void LoadChosenBow(RE::TESObjectWEAP* bow) {
        auto& st = Get();
        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;
        if (!bow) return;

        auto* base = bow->As<RE::TESBoundObject>();
        if (!base) return;
        st.chosenBow.base = base;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto inventory = player->GetInventory([base](RE::TESBoundObject& obj) { return &obj == base; });

        for (auto const& [obj, data] : inventory) {
            if (obj != base) continue;
            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists) continue;
            for (auto* extra : *entry->extraLists) {
                if (!extra) continue;
                const char* disp = extra->GetDisplayName(obj);
                if (!disp || !*disp) continue;
                const std::size_t len = std::strlen(disp);
                if (len >= kTagLen && std::memcmp(disp + (len - kTagLen), kChosenTag, kTagLen) == 0) {
                    st.chosenBow.extra = extra;
                    return;
                }
            }
        }
    }

    void ClearChosenBow() {
        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();

        if (st.chosenBow.base && st.chosenBow.extra) RemoveChosenTag(st.chosenBow.base, st.chosenBow.extra);

        st.chosenBow.base = nullptr;
        st.chosenBow.extra = nullptr;
        cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
        cfg.Save();
    }

    bool EnsureChosenBowInInventory() {
        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;

        if (!st.chosenBow.base) {
            if (const auto id = cfg.chosenBowFormID.load(std::memory_order_relaxed); id != 0)
                if (auto* form = RE::TESForm::LookupByID<RE::TESObjectWEAP>(id)) LoadChosenBow(form);
            if (!st.chosenBow.base) return false;
        }

        auto const* chosenBase = st.chosenBow.base;
        auto inventory = player->GetInventory([chosenBase](RE::TESBoundObject& obj) { return &obj == chosenBase; });

        bool anyFound = false;
        RE::ExtraDataList* foundAnyExtra = nullptr;
        RE::ExtraDataList* foundExact = nullptr;
        RE::ExtraDataList* foundTagged = nullptr;

        for (auto const& [obj, data] : inventory) {
            if (obj != chosenBase) continue;
            if (data.first > 0) anyFound = true;

            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists || entry->extraLists->empty()) continue;

            for (auto* extra : *entry->extraLists) {
                if (!extra) continue;
                if (!foundAnyExtra) foundAnyExtra = extra;

                const char* disp = extra->GetDisplayName(obj);
                if (!disp || !*disp) continue;

                if (st.chosenBow.extra && extra == st.chosenBow.extra) {
                    foundExact = extra;
                    break;
                }
                const std::size_t len = std::strlen(disp);
                if (len >= kTagLen && std::memcmp(disp + (len - kTagLen), kChosenTag, kTagLen) == 0)
                    foundTagged = extra;
            }
            if (foundExact) break;
        }

        if (foundExact) return true;

        if (foundTagged) {
            st.chosenBow.extra = foundTagged;
            return true;
        }

        if (foundAnyExtra) {
            st.chosenBow.extra = foundAnyExtra;
            ApplyChosenTag(st.chosenBow.base, st.chosenBow.extra);
            const auto id = st.chosenBow.base->As<RE::TESObjectWEAP>()
                                ? st.chosenBow.base->As<RE::TESObjectWEAP>()->GetFormID()
                                : 0u;
            cfg.chosenBowFormID.store(id, std::memory_order_relaxed);
            cfg.Save();
            return true;
        }

        if (anyFound) {
            st.chosenBow.extra = nullptr;
            if (auto const* bow = st.chosenBow.base->As<RE::TESObjectWEAP>()) {
                cfg.chosenBowFormID.store(bow->GetFormID(), std::memory_order_relaxed);
                cfg.Save();
            }
            return true;
        }

        ClearChosenBow();
        return false;
    }

    void SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra) {
        auto& st = Get();
        auto& cfg = IntegratedBow::GetBowConfig();

        const auto newBase = bow ? bow->As<RE::TESBoundObject>() : nullptr;

        // Toggle: selecionar o mesmo arco deseleciona
        if (st.chosenBow.base == newBase && st.chosenBow.extra == extra) {
            if (auto* live = ResolveLiveExtra(st.chosenBow.base, st.chosenBow.extra))
                RemoveChosenTag(st.chosenBow.base, live);
            else
                RemoveTagFromAnyTaggedInstance(st.chosenBow.base);
            st.chosenBow.base = nullptr;
            st.chosenBow.extra = nullptr;
            cfg.chosenBowFormID.store(0u, std::memory_order_relaxed);
            cfg.Save();
            return;
        }

        // Remover tag do arco anterior
        if (st.chosenBow.base && st.chosenBow.extra) {
            if (auto* live = ResolveLiveExtra(st.chosenBow.base, st.chosenBow.extra))
                RemoveChosenTag(st.chosenBow.base, live);
            else
                RemoveTagFromAnyTaggedInstance(st.chosenBow.base);
        }

        st.chosenBow.base = newBase;
        st.chosenBow.extra = extra;
        cfg.chosenBowFormID.store(bow ? bow->GetFormID() : 0u, std::memory_order_relaxed);
        cfg.Save();

        if (!st.chosenBow.extra) EnsureChosenBowInInventory();
        ApplyChosenTag(st.chosenBow.base, st.chosenBow.extra);
    }

    // ── flecha preferida ───────────────────────────────────────────────────

    RE::TESAmmo* GetPreferredArrow() {
        const auto id = IntegratedBow::GetBowConfig().preferredArrowFormID.load(std::memory_order_relaxed);
        return id ? RE::TESForm::LookupByID<RE::TESAmmo>(id) : nullptr;
    }

    void SetPreferredArrow(RE::TESAmmo* ammo) {
        auto& cfg = IntegratedBow::GetBowConfig();
        cfg.preferredArrowFormID.store(ammo ? ammo->GetFormID() : 0u, std::memory_order_relaxed);
        cfg.Save();
    }
}