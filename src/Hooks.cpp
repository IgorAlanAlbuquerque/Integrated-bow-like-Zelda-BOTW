#include "Hooks.h"

#include "BowInput.h"
#include "BowState.h"
#include "PCH.h"

namespace {
    struct EquipObjectHook {
        using Fn = void(RE::ActorEquipManager*, RE::Actor*, RE::TESBoundObject*, RE::ExtraDataList*, std::uint32_t,
                        const RE::BGSEquipSlot*, bool, bool, bool, bool);

        static inline Fn* func{nullptr};

        static void thunk(RE::ActorEquipManager* a_mgr, RE::Actor* a_actor, RE::TESBoundObject* a_object,
                          RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot,
                          bool a_queueEquip, bool a_forceEquip, bool a_playSounds, bool a_applyNow) {
            auto const* player = RE::PlayerCharacter::GetSingleton();

            if (player && a_actor == player) {
                if (auto weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr) {
                    if (weap->IsBow()) {
                        BowState::SetChosenBow(weap);
                        spdlog::info("[IntegratedBow] Arco selecionado");
                        return;
                    }
                }
            }

            if (!func) {
                spdlog::error("[IntegratedBow] EquipObjectHook::func == nullptr!");
                return;
            }

            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds,
                 a_applyNow);
        }

        static void Install() {
            REL::Relocation<std::uintptr_t> target{RE::Offset::ActorEquipManager::EquipObject};

            spdlog::info("[IntegratedBow] Installing EquipObjectHook at {:#x}", target.address());

            // usa o helper do PCH
            stl::write_thunk_jump<EquipObjectHook>(target.address());

            spdlog::info("[IntegratedBow] EquipObjectHook original func = {}", static_cast<const void*>(func));
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
