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
            auto const* player = RE::PlayerCharacter::GetSingleton();
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

                if (auto weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr) {
                    const bool isBowLike = IsBowOrCrossbow(weap);
                    const bool usingBow = BowState::IsUsingBow();

                    spdlog::info(
                        "[IBOW][HOOK][Equip][WEAP] weap=0x{:08X} name='{}' bowLike={} | usingBow={} equipingBow={} "
                        "hotkeyDown={}",
                        weap->GetFormID(), weap->GetName() ? weap->GetName() : "", isBowLike, usingBow,
                        BowState::IsEquipingBow(), BowInput::IsHotkeyDown());

                    if (isBowLike && !BowState::IsEquipingBow() && !usingBow && BowInput::IsHotkeyDown()) {
                        spdlog::info(
                            "[IBOW][HOOK][Equip][WEAP] -> SetChosenBow (reason: bowlike && !equiping && !using && "
                            "hotkeyDown) "
                            "weap=0x{:08X} extra={}",
                            weap->GetFormID(), (void*)a_extraData);
                        BowState::SetChosenBow(weap, a_extraData);
                        spdlog::info("[IBOW][HOOK][Equip][WEAP] -> return after SetChosenBow");
                        return;
                    }

                    if (usingBow && !isBowLike) {
                        spdlog::info(
                            "[IBOW][HOOK][Equip][WEAP] usingBow=true and equipping non-bow weapon -> call original, "
                            "then SetUsingBow(false). "
                            "weap=0x{:08X}",
                            weap->GetFormID());
                        if (func) {
                            spdlog::info("[IBOW][HOOK][Equip][WEAP] calling original func(...)");
                            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip,
                                 a_playSounds, a_applyNow);
                            spdlog::info("[IBOW][HOOK][Equip][WEAP] returned from original func(...)");
                        }
                        BowState::SetUsingBow(false);
                        spdlog::info("[IBOW][HOOK][Equip][WEAP] -> SetUsingBow(false) done, returning");

                        return;
                    }
                    spdlog::info("[IBOW][HOOK][Equip][WEAP] -> no early return in weapon branch (fallthrough)");
                }

                if (auto ammo = a_object ? a_object->As<RE::TESAmmo>() : nullptr) {
                    if (BowInput::IsHotkeyDown() && !BowState::IsUsingBow() && !BowState::IsEquipingBow()) {
                        spdlog::info(
                            "[IBOW][HOOK][Equip][AMMO] ammo=0x{:08X} name='{}' | usingBow={} equipingBow={} "
                            "hotkeyDown={}",
                            ammo->GetFormID(), ammo->GetName() ? ammo->GetName() : "", BowState::IsUsingBow(),
                            BowState::IsEquipingBow(), BowInput::IsHotkeyDown());
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

        static void Install() { Hook::stl::write_detour<EquipObjectHook>(RE::Offset::ActorEquipManager::EquipObject); }
    };

    inline std::string FormIDStr(const RE::TESForm* f) {
        return f ? fmt::format("0x{:08X}", f->GetFormID()) : std::string("null");
    }

    struct UnequipObjectHook {
        using Fn = bool(RE::ActorEquipManager*, RE::Actor*, RE::TESBoundObject*, RE::ExtraDataList*, std::uint32_t,
                        const RE::BGSEquipSlot*, bool, bool, bool, bool, const RE::BGSEquipSlot*);
        static inline Fn* func{nullptr};

        static void thunk(  // NOSONAR
            RE::ActorEquipManager* a_mgr, RE::Actor* a_actor, RE::TESBoundObject* a_object,
            RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot, bool a_queueEquip,
            bool a_forceEquip, bool a_playSounds, bool a_applyNow, const RE::BGSEquipSlot* a_slotToReplace) {
            bool block = false;
            std::string reason;
            auto const* player = RE::PlayerCharacter::GetSingleton();
            const bool isPlayer = (player && a_actor == player);

            if (isPlayer) {
                const auto objId = a_object ? a_object->GetFormID() : 0u;
                const auto slotId = a_slot ? a_slot->GetFormID() : 0u;
                spdlog::info(
                    "[IBOW][HOOK][Unequip] call mgr={} actor={} obj={} extra={} count={} slot={} "
                    "queue={} force={} sounds={} applyNow={} slotToReplace={} | state: usingBow={} equipingBow={} "
                    "hotkeyDown={} unequipAllowed={}",
                    (void*)a_mgr, (void*)a_actor, objId ? fmt::format("0x{:08X}", objId) : std::string("null"),
                    (void*)a_extraData, a_count, slotId ? fmt::format("0x{:08X}", slotId) : std::string("null"),
                    a_queueEquip, a_forceEquip, a_playSounds, a_applyNow,
                    a_slotToReplace ? fmt::format("0x{:08X}", a_slotToReplace->GetFormID()) : std::string("null"),
                    BowState::IsUsingBow(), BowState::IsEquipingBow(), BowInput::IsHotkeyDown(),
                    BowInput::IsUnequipAllowed());
            }
            if (isPlayer && !BowInput::IsUnequipAllowed() && a_object) {
                if (auto const* weap = a_object->As<RE::TESObjectWEAP>(); weap && IsBowOrCrossbow(weap)) {
                    block = true;
                    reason = "blocked bow/crossbow";
                    spdlog::info("[IBOW][HOOK][Unequip][WEAP] BLOCK weap={} name='{}' extra={}",
                                 FormIDStr(weap).c_str(), weap->GetName() ? weap->GetName() : "", (void*)a_extraData);
                } else if (auto const* ammo = a_object->As<RE::TESAmmo>()) {
                    block = true;
                    reason = "blocked ammo";
                    spdlog::info("[IBOW][HOOK][Unequip][AMMO] BLOCK ammo={} name='{}' extra={}",
                                 FormIDStr(ammo).c_str(), ammo->GetName() ? ammo->GetName() : "", (void*)a_extraData);
                }
            }

            if (block) {
                if (isPlayer) {
                    spdlog::info("[IBOW][HOOK][Unequip] -> return ({})", reason);
                }
                return;
            }

            if (!func) {
                if (isPlayer) {
                    spdlog::warn("[IBOW][HOOK][Unequip] func is null -> return (player call)");
                }
                return;
            }

            if (isPlayer) {
                spdlog::info("[IBOW][HOOK][Unequip] calling original func(...)");
            }

            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds,
                 a_applyNow, a_slotToReplace);
        }

        static void Install() {
            Hook::stl::write_detour<UnequipObjectHook>(RE::Offset::ActorEquipManager::UnequipObject);
        }
    };

    struct PollInputDevicesHook {
        using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
        static inline std::uintptr_t func{0};
        static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
            using namespace BowState::detail;
            RE::InputEvent* headBefore = a_events ? *a_events : nullptr;
            RE::InputEvent* headAfter = FlushSyntheticInput(headBefore);

            const bool interesting = (headAfter != headBefore) || BowState::IsUsingBow() || BowState::IsEquipingBow() ||
                                     BowInput::IsHotkeyDown();

            if (interesting) {
                spdlog::info(
                    "[IBOW][HOOK][PollInput] dispatcher={} func=0x{:X} headBefore={} headAfter={} | state: usingBow={} "
                    "equipingBow={} hotkeyDown={}",
                    (void*)a_dispatcher, func, (void*)headBefore, (void*)headAfter, BowState::IsUsingBow(),
                    BowState::IsEquipingBow(), BowInput::IsHotkeyDown());
            }
            RE::InputEvent* const arr[2]{headAfter, nullptr};  // NOSONAR - definição padrão
            if (func != 0) {
                auto* original = reinterpret_cast<Fn*>(func);  // NOSONAR
                if (interesting) {
                    spdlog::info("[IBOW][HOOK][PollInput] calling original(...)");
                }
                original(a_dispatcher, arr);
                if (interesting) {
                    spdlog::info("[IBOW][HOOK][PollInput] returned from original(...)");
                }
            } else if (interesting) {
                spdlog::warn("[IBOW][HOOK][PollInput] func is 0 -> skipping original");
            }
        }

        static void Install() {
            Hook::stl::write_call<PollInputDevicesHook>(REL::RelocationID(67315, 68617),
                                                        REL::VariantOffset(0x7B, 0x7B, 0x81));
        }
    };

    struct PlayerAnimGraphProcessEventHook {
        using Fn = RE::BSEventNotifyControl (*)(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,
                                                const RE::BSAnimationGraphEvent*,
                                                RE::BSTEventSource<RE::BSAnimationGraphEvent>*);

        static inline Fn _orig{nullptr};

        static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                              const RE::BSAnimationGraphEvent* a_ev,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src) {
            const auto ret = _orig ? _orig(a_this, a_ev, a_src) : RE::BSEventNotifyControl::kContinue;

            const bool active = BowState::IsUsingBow() || BowState::IsEquipingBow() || BowInput::IsHotkeyDown();
            if (a_ev && active) {
                std::string_view tag{a_ev->tag.c_str(), a_ev->tag.size()};
                std::string_view payload{a_ev->payload.c_str(), a_ev->payload.size()};

                using Under = std::underlying_type_t<RE::BSEventNotifyControl>;
                spdlog::info(
                    "[IBOW][HOOK][AnimGraph] this={} src={} tag='{}' payload='{}' ret={} | state: usingBow={} "
                    "equipingBow={} hotkeyDown={}",
                    (void*)a_this, (void*)a_src, tag, payload, static_cast<Under>(ret), BowState::IsUsingBow(),
                    BowState::IsEquipingBow(), BowInput::IsHotkeyDown());
            }

            if (a_ev) {
                BowInput::HandleAnimEvent(a_ev, a_src);
            }

            return ret;
        }

        static void Install() {
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[2]};

            const std::uintptr_t orig = vtbl.write_vfunc(1, thunk);
            _orig = reinterpret_cast<Fn>(orig);  // NOSONAR - interop
        }
    };
}

namespace Hooks {
    void Install_Hooks() {
        SKSE::AllocTrampoline(64);
        EquipObjectHook::Install();
        UnequipObjectHook::Install();
        PollInputDevicesHook::Install();
        PlayerAnimGraphProcessEventHook::Install();
        BowInput::RegisterInputHandler();
    }
}