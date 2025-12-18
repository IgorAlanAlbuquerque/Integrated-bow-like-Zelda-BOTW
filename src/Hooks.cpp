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
            auto* player = RE::PlayerCharacter::GetSingleton();
            const bool isPlayer = (player && a_actor == player);

            if (isPlayer) {
                const auto objId = a_object ? a_object->GetFormID() : 0u;
                const auto slotId = a_slot ? a_slot->GetFormID() : 0u;

                spdlog::info(
                    "[IBOW][HOOK][Equip] call mgr={} actor={} obj={} extra={} count={} slot={} "
                    "queue={} force={} sounds={} applyNow={} | state: usingBow={} equipingBow={} hotkeyDown={}",
                    (void*)a_mgr, (void*)a_actor, objId ? fmt::format("0x{:08X}", objId) : std::string("null"),
                    (void*)a_extraData, a_count, slotId ? fmt::format("0x{:08X}", slotId) : std::string("null"),
                    a_queueEquip, a_forceEquip, a_playSounds, a_applyNow, BowState::IsUsingBow(),
                    BowState::IsEquipingBow(), BowInput::IsHotkeyDown());
            }

            if (isPlayer) {
                if (auto weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr) {
                    const auto weapId = weap->GetFormID();
                    const bool isBowLike = IsBowOrCrossbow(weap);

                    const bool equipingBow = BowState::IsEquipingBow();
                    const bool usingBow = BowState::IsUsingBow();
                    const bool hkDown = BowInput::IsHotkeyDown();

                    spdlog::info(
                        "[IBOW][HOOK][Equip][WEAP] weap=0x{:08X} name='{}' bowLike={} | usingBow={} equipingBow={} "
                        "hotkeyDown={}",
                        weapId, weap->GetName() ? weap->GetName() : "", isBowLike, usingBow, equipingBow, hkDown);

                    if (isBowLike && !equipingBow && !usingBow && hkDown) {
                        spdlog::info(
                            "[IBOW][HOOK][Equip][WEAP] -> SetChosenBow (reason: bowlike && !equiping && !using && "
                            "hotkeyDown) "
                            "weap=0x{:08X} extra={}",
                            weapId, (void*)a_extraData);

                        BowState::SetChosenBow(weap, a_extraData);

                        spdlog::info("[IBOW][HOOK][Equip][WEAP] -> return after SetChosenBow");
                        return;
                    }

                    if (usingBow && !isBowLike) {
                        spdlog::info(
                            "[IBOW][HOOK][Equip][WEAP] usingBow=true and equipping non-bow weapon -> call original, "
                            "then SetUsingBow(false). "
                            "weap=0x{:08X}",
                            weapId);

                        if (func) {
                            spdlog::info("[IBOW][HOOK][Equip][WEAP] calling original func(...)");
                            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip,
                                 a_playSounds, a_applyNow);
                            spdlog::info("[IBOW][HOOK][Equip][WEAP] returned from original func(...)");
                        } else {
                            spdlog::warn("[IBOW][HOOK][Equip][WEAP] original func is null! (skipping original call)");
                        }

                        BowState::SetUsingBow(false);
                        spdlog::info("[IBOW][HOOK][Equip][WEAP] -> SetUsingBow(false) done, returning");
                        return;
                    }

                    spdlog::info("[IBOW][HOOK][Equip][WEAP] -> no early return in weapon branch (fallthrough)");
                }

                if (auto ammo = a_object ? a_object->As<RE::TESAmmo>() : nullptr) {
                    const auto ammoId = ammo->GetFormID();

                    const bool equipingBow = BowState::IsEquipingBow();
                    const bool usingBow = BowState::IsUsingBow();
                    const bool hkDown = BowInput::IsHotkeyDown();

                    spdlog::info(
                        "[IBOW][HOOK][Equip][AMMO] ammo=0x{:08X} name='{}' | usingBow={} equipingBow={} hotkeyDown={}",
                        ammoId, ammo->GetName() ? ammo->GetName() : "", usingBow, equipingBow, hkDown);

                    if (hkDown && !usingBow && !equipingBow) {
                        spdlog::info(
                            "[IBOW][HOOK][Equip][AMMO] -> SetPreferredArrow (reason: hotkeyDown && !using && "
                            "!equiping) ammo=0x{:08X}",
                            ammoId);

                        BowState::SetPreferredArrow(ammo);
                        spdlog::info("[IBOW][HOOK][Equip][AMMO] -> return after SetPreferredArrow");
                        return;
                    }

                    spdlog::info("[IBOW][HOOK][Equip][AMMO] -> no early return in ammo branch (fallthrough)");
                }
            }

            if (!func) {
                if (isPlayer) {
                    spdlog::warn("[IBOW][HOOK][Equip] func is null -> return (player call)");
                }
                return;
            }

            if (isPlayer) {
                spdlog::info("[IBOW][HOOK][Equip] fallthrough -> calling original func(...)");
            }

            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds,
                 a_applyNow);

            if (isPlayer) {
                spdlog::info("[IBOW][HOOK][Equip] fallthrough -> returned from original func(...)");
            }
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
            Hook::stl::write_call<PollInputDevicesHook>(REL::RelocationID(67315, 68617),
                                                        REL::VariantOffset(0x7B, 0x7B, 0x81));
        }
    };

    struct PlayerAnimGraphProcessEventHook {
        // vfunc real no vtable é um function pointer com "this" explícito
        using Fn = RE::BSEventNotifyControl (*)(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,
                                                const RE::BSAnimationGraphEvent*,
                                                RE::BSTEventSource<RE::BSAnimationGraphEvent>*);

        static inline Fn _orig{nullptr};

        static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                              const RE::BSAnimationGraphEvent* a_ev,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src) {
            // 1) deixa o jogo processar primeiro (mais seguro contra reentrância)
            const auto ret = _orig ? _orig(a_this, a_ev, a_src) : RE::BSEventNotifyControl::kContinue;

            // 2) reaproveita exatamente sua lógica atual
            if (a_ev) {
                BowInput::HandleAnimEvent(a_ev, a_src);
            }

            return ret;
        }

        static void Install() {
            // [2] = vtable do BSTEventSink<BSAnimationGraphEvent> (conforme sugestão)
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[2]};

            // index 1 = ProcessEvent (0 costuma ser dtor, 1 costuma ser ProcessEvent nesses sinks)
            const std::uintptr_t orig = vtbl.write_vfunc(1, thunk);  // :contentReference[oaicite:4]{index=4}
            _orig = reinterpret_cast<Fn>(orig);

            spdlog::info("[IBOW][HOOK][ANIM] PlayerCharacter::ProcessEvent hooked. orig={}", (void*)orig);
        }
    };
}

namespace Hooks {
    void Install_Hooks() {
        SKSE::AllocTrampoline(64);
        EquipObjectHook::Install();
        PollInputDevicesHook::Install();
        PlayerAnimGraphProcessEventHook::Install();
        BowInput::RegisterInputHandler();
    }
}