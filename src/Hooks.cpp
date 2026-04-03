#include "Hooks.h"

#include <type_traits>

#include "HookUtil.hpp"
#include "Input/EventFilter.h"
#include "Input/InputGate.h"
#include "Input/InputHandler.h"
#include "Input/ModeController.h"
#include "Input/SyntheticInput.h"
#include "PCH.h"
#include "State/ChosenBow.h"
#include "State/SessionState.h"

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
        static void thunk(RE::ActorEquipManager* a_mgr, RE::Actor* a_actor, RE::TESBoundObject* a_object,
                          RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot,
                          bool a_queueEquip, bool a_forceEquip, bool a_playSounds, bool a_applyNow) {
            auto const* player = RE::PlayerCharacter::GetSingleton();

            if (const bool isPlayer = (player && a_actor == player); isPlayer) {
                if (auto weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr) {
                    const bool isBowLike = IsBowOrCrossbow(weap);
                    const bool usingBow = BowState::IsUsingBow();

                    if (isBowLike && !BowState::IsEquipingBow() && !usingBow && BowInput::IsHotkeyDown()) {
                        BowState::SetChosenBow(weap, a_extraData);

                        return;
                    }

                    if (usingBow && !isBowLike) {
                        if (func) {
                            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip,
                                 a_playSounds, a_applyNow);
                        }
                        BowState::SetUsingBow(false);

                        return;
                    }
                }

                if (auto ammo = a_object ? a_object->As<RE::TESAmmo>() : nullptr) {
                    if (BowInput::IsHotkeyDown() && !BowState::IsUsingBow() && !BowState::IsEquipingBow()) {
                        BowState::SetPreferredArrow(ammo);

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

        static void Install() { Hook::stl::write_detour<EquipObjectHook>(RELOCATION_ID(37938, 38894)); }
    };

    inline std::string FormIDStr(const RE::TESForm* f) {
        return f ? fmt::format("0x{:08X}", f->GetFormID()) : std::string("null");
    }

    struct UnequipObjectHook {
        using Fn = bool(RE::ActorEquipManager*, RE::Actor*, RE::TESBoundObject*, RE::ExtraDataList*, std::uint32_t,
                        const RE::BGSEquipSlot*, bool, bool, bool, bool, const RE::BGSEquipSlot*);
        static inline Fn* func{nullptr};

        static void thunk(RE::ActorEquipManager* a_mgr, RE::Actor* a_actor, RE::TESBoundObject* a_object,
                          RE::ExtraDataList* a_extraData, std::uint32_t a_count, const RE::BGSEquipSlot* a_slot,
                          bool a_queueEquip, bool a_forceEquip, bool a_playSounds, bool a_applyNow,
                          const RE::BGSEquipSlot* a_slotToReplace) {
            bool block = false;
            auto const* player = RE::PlayerCharacter::GetSingleton();

            if (const bool isPlayer = (player && a_actor == player);
                isPlayer && !BowInput::IsUnequipAllowed() && a_object) {
                if (auto const* weap = a_object->As<RE::TESObjectWEAP>(); weap && IsBowOrCrossbow(weap)) {
                    block = true;

                } else if (auto const* ammo = a_object->As<RE::TESAmmo>()) {
                    block = true;
                }
            }

            if (block) {
                return;
            }

            if (!func) {
                return;
            }

            func(a_mgr, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds,
                 a_applyNow, a_slotToReplace);
        }

        static void Install() { Hook::stl::write_detour<UnequipObjectHook>(RELOCATION_ID(37945, 38901)); }
    };

    struct PollInputDevicesHook {
        using Fn = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent* const*);
        static inline std::uintptr_t func{0};

        static float CalcDt() {
            using clock = std::chrono::steady_clock;
            static clock::time_point s_last = clock::now();
            const auto now = clock::now();
            float dt = std::chrono::duration<float>(now - s_last).count();
            s_last = now;
            if (dt < 0.0f || dt > 0.5f) dt = 0.0f;
            return dt;
        }

        static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
            if (!a_events) return;

            auto* player = RE::PlayerCharacter::GetSingleton();
            const float dt = CalcDt();

            static bool s_prevBlocked = false;
            RE::InputEvent* head = *a_events;
            const bool blocked = BowInput::InputGate::IsInputBlockedByMenus();

            if (blocked && !s_prevBlocked) {
                BowInput::CancelIfPendingActive();
            }
            s_prevBlocked = blocked;

            BowInput::UpdateBowInputState(&head);
            BowInput::HandleCaptureEvents(&head);

            if (!blocked) {
                if (player) BowInput::BowModeController::Get().ProcessSpecialEvents(&head, player);
                BowInput::FilterBowEvents(&head);
            }

            BowInput::ProcessBowLogic(dt);
            BowInput::DrainBowDeferredEvents();
            head = BowState::detail::FlushSyntheticInput(head);

            RE::InputEvent* const arr[2]{head, nullptr};
            if (func != 0) reinterpret_cast<Fn*>(func)(a_dispatcher, arr);
        }

        static void Install() { Hook::stl::write_call<PollInputDevicesHook>(RELOCATION_ID(67315, 68617), 0x7B); }
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
            if (a_ev) {
                BowInput::HandleAnimEvent(a_ev, a_src);
            }
            return ret;
        }

        static void Install() {
            REL::Relocation<std::uintptr_t> vtbl{RE::VTABLE_PlayerCharacter[2]};
            const std::uintptr_t orig = vtbl.write_vfunc(1, thunk);
            _orig = reinterpret_cast<Fn>(orig);
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
    }
}