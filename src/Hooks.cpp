#include "Hooks.h"

#include "BowInput.h"
#include "BowState.h"
#include "HookUtil.hpp"
#include "PCH.h"

namespace {
    struct EquipObjectHook {
        using Fn = void(RE::ActorEquipManager*, RE::Actor*, RE::TESBoundObject*, RE::ExtraDataList*, std::uint32_t,
                        const RE::BGSEquipSlot*, bool, bool, bool, bool);

        static inline Fn* func{nullptr};

        static void thunk(RE::ActorEquipManager* a_mgr, RE::Actor* a_actor, RE::TESBoundObject* a_object,
                          RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot,
                          bool a_queueEquip, bool a_forceEquip, bool a_playSounds, bool a_applyNow) {
            if (auto const* player = RE::PlayerCharacter::GetSingleton(); player && a_actor == player) {
                if (auto weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr) {
                    if (weap->IsBow() && !BowState::isEquipingBow() && !BowState::IsUsingBow()) {
                        BowState::SetChosenBow(weap);

                        return;
                    }
                    if (BowState::IsUsingBow() && weap->IsBow()) {
                        func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip,
                             a_playSounds, a_applyNow);
                        BowState::SetChosenBow(weap);
                        return;
                    }
                    if (BowState::IsUsingBow() && !weap->IsBow()) {
                        func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip,
                             a_playSounds, a_applyNow);
                        BowState::SetUsingBow(false);
                        return;
                    }
                }
            }

            if (!func) {
                return;
            }

            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds,
                 a_applyNow);
        }

        static void Install() {
            REL::Relocation<std::uintptr_t> target{RE::Offset::ActorEquipManager::EquipObject};
            Hook::stl::write_detour<EquipObjectHook>(RE::Offset::ActorEquipManager::EquipObject);
        }
    };
}

namespace Hooks {
    void Install_Hooks() {
        SKSE::AllocTrampoline(64);

        EquipObjectHook::Install();
        BowInput::RegisterInputHandler();
    }
}