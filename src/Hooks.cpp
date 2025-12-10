#include "Hooks.h"

#include <type_traits>

#include "BowInput.h"
#include "BowState.h"
#include "HookUtil.hpp"
#include "PCH.h"

namespace {
    inline bool IsBowOrCrossbow(const RE::TESObjectWEAP* weap) {
        if (!weap) {
            return false;
        }
        return weap->IsBow() || weap->IsCrossbow();
    }

    struct EquipObjectHook {
        using Fn = void(RE::ActorEquipManager*, RE::Actor*, RE::TESBoundObject*, RE::ExtraDataList*, std::uint32_t,
                        const RE::BGSEquipSlot*, bool, bool, bool, bool);
        static inline Fn* func{nullptr};
        static void thunk(  // NOSONAR
            RE::ActorEquipManager* a_mgr, RE::Actor* a_actor, RE::TESBoundObject* a_object,
            RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot, bool a_queueEquip,
            bool a_forceEquip, bool a_playSounds, bool a_applyNow) {
            if (auto const* player = RE::PlayerCharacter::GetSingleton(); player && a_actor == player) {
                if (auto weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr) {
                    const bool isBowLike = IsBowOrCrossbow(weap);
                    if (isBowLike && !BowState::IsEquipingBow() && !BowState::IsUsingBow() &&
                        BowInput::IsHotkeyDown()) {
                        BowState::SetChosenBow(weap, a_extraData);
                        return;
                    }
                    if (BowState::IsUsingBow() && !isBowLike) {
                        if (func) {
                            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip,
                                 a_playSounds, a_applyNow);
                        }
                        BowState::SetUsingBow(false);
                        return;
                    }
                }
            }
            if (!func) {
                spdlog::warn("[IBOW] EquipObjectHook::thunk: func is null, cannot forward call");
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

    struct PollInputDevicesHook {
        using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
        static inline std::uintptr_t func{0};
        static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
            using namespace BowState::detail;
            RE::InputEvent* head = a_events ? *a_events : nullptr;
            head = FlushSyntheticInput(head);
            RE::InputEvent* const arr[2]{head, nullptr};  // NOSONAR - definição padrão
            if (func != 0) {
                auto* original = reinterpret_cast<Fn*>(func);  // NOSONAR - interop
                original(a_dispatcher, arr);
            }
        }

        static void Install() {
            REL::Relocation<std::uintptr_t> target{REL::RelocationID(67315, 68617)};
            Hook::stl::write_call<PollInputDevicesHook>(REL::RelocationID(67315, 68617),
                                                        REL::VariantOffset(0x7B, 0x7B, 0x81));
        }
    };
}

namespace Hooks {
    void Install_Hooks() {
        SKSE::AllocTrampoline(64);
        EquipObjectHook::Install();
        PollInputDevicesHook::Install();
        BowInput::RegisterInputHandler();
    }
}