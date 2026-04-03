#include "LoadoutRestore.h"

#include <ranges>

#include "Config/Config.h"
#include "Input/InputHandler.h"
#include "PCH.h"
#include "Patchs/SkipEquipController.h"
#include "State/ChosenBow.h"
#include "State/SessionState.h"

namespace BowState {
    namespace {

        constexpr std::uint64_t kSkipReturnFallbackDisableMs = 1000;
        constexpr std::uint64_t kSkipReturnDisableAfterMs = 200;

        struct ScopedSkipEquipReturn {
            bool enabled{false};

            ScopedSkipEquipReturn(bool en, RE::PlayerCharacter* pc) : enabled(en) {
                if (enabled) IntegratedBow::SkipEquipController::EnableAndArmDisable(pc, kSkipReturnFallbackDisableMs);
            }

            ~ScopedSkipEquipReturn() {
                if (enabled) IntegratedBow::SkipEquipController::ArmDisable(kSkipReturnDisableAfterMs);
            }
        };

        bool IsHandReady(RE::PlayerCharacter* player, RE::TESBoundObject* desired, bool leftHand) {
            if (!player) return false;
            auto* cur = player->GetEquippedObject(leftHand);
            auto const* curBase = cur ? cur->As<RE::TESBoundObject>() : nullptr;
            return desired ? curBase == desired : curBase == nullptr;
        }

    }

    void CaptureWornArmorSnapshot(std::vector<ExtraEquippedItem>& out) {
        out.clear();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });
        for (auto const& [obj, data] : inventory) {
            auto* armor = obj->As<RE::TESObjectARMO>();
            if (!armor) continue;
            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists) continue;
            for (auto* extra : *entry->extraLists) {
                if (!extra) continue;
                const bool worn = extra->HasType(RE::ExtraDataType::kWorn);
                const bool wornLeft = extra->HasType(RE::ExtraDataType::kWornLeft);
                if (worn || wornLeft) out.push_back({armor->As<RE::TESBoundObject>(), extra});
            }
        }
    }

    std::vector<ExtraEquippedItem> DiffArmorSnapshot(const std::vector<ExtraEquippedItem>& before,
                                                     const std::vector<ExtraEquippedItem>& after) {
        std::vector<ExtraEquippedItem> removed;
        for (auto const& b : before) {
            const bool stillWorn =
                std::ranges::any_of(after, [&](auto const& a) { return a.base == b.base && a.extra == b.extra; });
            if (!stillWorn) removed.push_back(b);
        }
        return removed;
    }

    void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* equipMgr) {
        if (!actor || !equipMgr) return;
        auto& st = Get();
        for (auto const& item : st.prevExtraEquipped) {
            if (!item.base) continue;
            const bool isArmor = (item.base->GetFormType() == RE::FormType::Armor);
            const bool queue = false;
            const bool force = !isArmor;
            const bool applyNow = isArmor;
            equipMgr->EquipObject(actor, item.base, item.extra, 1, nullptr, queue, force, true, applyNow);
        }
        st.prevExtraEquipped.clear();
    }

    void AppendPrevExtraEquipped(const ExtraEquippedItem& item) { Get().prevExtraEquipped.push_back(item); }

    bool ContainsPrevExtraEquipped(const ExtraEquippedItem& item) {
        return std::ranges::any_of(Get().prevExtraEquipped,
                                   [&](auto const& e) { return e.base == item.base && e.extra == item.extra; });
    }

    void ApplyHiddenItemsPatch(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                               const std::vector<RE::FormID>& hiddenFormIDs) {
        if (!player || !equipMgr || hiddenFormIDs.empty()) return;

        auto inventory = player->GetInventory([](RE::TESBoundObject&) { return true; });
        for (auto const& [obj, data] : inventory) {
            auto* armor = obj->As<RE::TESObjectARMO>();
            if (!armor) continue;
            if (!std::ranges::binary_search(hiddenFormIDs, armor->GetFormID())) continue;
            auto const* entry = data.second.get();
            if (!entry || !entry->extraLists) continue;
            for (auto* extra : *entry->extraLists) {
                if (!extra) continue;
                const bool worn = extra->HasType(RE::ExtraDataType::kWorn);
                const bool wornLeft = extra->HasType(RE::ExtraDataType::kWornLeft);
                if (!worn && !wornLeft) continue;
                ExtraEquippedItem item{armor->As<RE::TESBoundObject>(), extra};
                if (ContainsPrevExtraEquipped(item)) continue;
                equipMgr->UnequipObject(player, item.base, item.extra, 1, nullptr, true, true, true, false, nullptr);
                AppendPrevExtraEquipped(item);
            }
        }
    }

    void RestorePrevWeaponsAndAmmo(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                   IntegratedBowState& st) {
        if (!player || !equipMgr) return;

        auto const& cfg = IntegratedBow::GetBowConfig();

        const auto hasSpell = [&] {
            const auto isSpell = [](RE::TESForm const* obj) {
                if (!obj) return false;
                return obj->Is(RE::FormType::Spell) || obj->Is(RE::FormType::Shout) || obj->Is(RE::FormType::Scroll);
            };
            return isSpell(st.prevRight.base) || isSpell(st.prevLeft.base);
        };

        const bool doSkipReturn = cfg.skipEquipReturnToMeleePatch.load(std::memory_order_relaxed) && !hasSpell();
        ScopedSkipEquipReturn skipGuard(doSkipReturn, player);

        BowInput::ForceAllowUnequip();

        auto* rightBase = st.prevRight.base;
        auto* rightExtra = st.prevRight.extra;

        if (!rightBase && st.prevRightFormID != 0) {
            if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevRightFormID)) {
                rightBase = form;
                rightExtra = FindAnyInstanceExtraForBase(form);
            }
        }
        if (rightBase) {
            if (rightExtra) {
                if (auto* live = ResolveLiveExtra(rightBase, rightExtra))
                    rightExtra = live;
                else
                    rightExtra = FindAnyInstanceExtraForBase(rightBase);
            }
            equipMgr->EquipObject(player, rightBase, rightExtra, 1, nullptr, rightExtra == nullptr, false, true, false);
        }

        auto* leftBase = st.prevLeft.base;
        auto* leftExtra = st.prevLeft.extra;

        if (!leftBase && st.prevLeftFormID != 0) {
            if (auto* form = RE::TESForm::LookupByID<RE::TESBoundObject>(st.prevLeftFormID)) {
                leftBase = form;
                leftExtra = FindAnyInstanceExtraForBase(form);
            }
        }
        if (leftBase) {
            if (leftExtra) {
                if (auto* live = ResolveLiveExtra(leftBase, leftExtra))
                    leftExtra = live;
                else
                    leftExtra = FindAnyInstanceExtraForBase(leftBase);
            }
            equipMgr->EquipObject(player, leftBase, leftExtra, 1, nullptr, leftExtra == nullptr, false, true, false);
        }

        if (!rightBase && !leftBase && st.chosenBow.base)
            equipMgr->UnequipObject(player, st.chosenBow.base, st.chosenBow.extra, 1, nullptr, true, true, true, false,
                                    nullptr);

        if (auto* prevAmmo = st.prevAmmo) {
            equipMgr->EquipObject(player, prevAmmo, nullptr, 1, nullptr, true, false, true, false);
        } else if (auto* preferred = GetPreferredArrow()) {
            equipMgr->UnequipObject(player, preferred, nullptr, 1, nullptr, true, true, true, false, nullptr);
        }

        st.prevAmmo = nullptr;
        st.isUsingBow = false;

        if (st.prevExtraEquipped.empty()) {
            ClearPrevWeapons();
            ClearPrevExtraEquipped();
            return;
        }

        st.pendingFinalizeExtras = true;
        st.pendingFinalizeExtrasTimer = 0.0f;
        st.pendingDesiredRight = st.prevRight.base;
        st.pendingDesiredLeft = st.prevLeft.base;
        ClearPrevWeapons();
    }

    void UpdateDeferredFinalize(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr, float dt) {
        auto& st = Get();
        if (!st.pendingFinalizeExtras) return;

        st.pendingFinalizeExtrasTimer += dt;

        const bool rightOK = IsHandReady(player, st.pendingDesiredRight, false);
        const bool leftOK = IsHandReady(player, st.pendingDesiredLeft, true);
        const bool timedOut = st.pendingFinalizeExtrasTimer >= 2.0f;

        if (!timedOut && !(rightOK && leftOK)) return;

        ReequipPrevExtraEquipped(player, equipMgr);

        st.pendingFinalizeExtras = false;
        st.pendingFinalizeExtrasTimer = 0.0f;
        st.pendingDesiredRight = nullptr;
        st.pendingDesiredLeft = nullptr;
        ClearPrevExtraEquipped();
    }
}