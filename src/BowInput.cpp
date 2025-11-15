
#include "BowInput.h"

#include "BowState.h"
#include "PCH.h"

namespace {
    bool g_holdMode = true;
    std::uint32_t g_bowKeyScanCode = 0x2F;
    int g_bowGamepadButton = -1;

    class IntegratedBowInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        static IntegratedBowInputHandler* GetSingleton() {
            static IntegratedBowInputHandler instance;
            return std::addressof(instance);
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events,
                                              RE::BSTEventSource<RE::InputEvent*>*) override {
            if (!a_events) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            for (auto e = *a_events; e; e = e->next) {
                if (auto button = e->AsButtonEvent()) {
                    if (!button->IsDown() && !button->IsUp()) {
                        continue;
                    }

                    if (!IsOurKey(button)) {
                        continue;
                    }

                    if (button->IsDown()) {
                        OnKeyPressed(player);
                    } else if (button->IsUp()) {
                        OnKeyReleased(player);
                    }
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        bool IsOurKey(const RE::ButtonEvent* a_event) const {
            auto dev = a_event->GetDevice();

            if (dev == RE::INPUT_DEVICE::kKeyboard) {
                auto code = a_event->idCode;
                return code == g_bowKeyScanCode;
            }

            if (dev == RE::INPUT_DEVICE::kGamepad) {
                if (g_bowGamepadButton < 0) return false;
                auto code = static_cast<int>(a_event->idCode);
                return code == g_bowGamepadButton;
            }

            return false;
        }

        void OnKeyPressed(RE::PlayerCharacter* player) {
            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (!equipMgr) {
                return;
            }

            auto& st = BowState::Get();
            auto* bow = st.chosenBow;
            if (!bow) {
                return;
            }

            if (g_holdMode) {
                if (st.isUsingBow) {
                    return;
                }
                EnterBowMode(player, equipMgr, st, bow);
            } else {
                if (st.isUsingBow) {
                    ExitBowMode(player, equipMgr, st);
                } else {
                    EnterBowMode(player, equipMgr, st, bow);
                }
            }
        }

        void OnKeyReleased(RE::PlayerCharacter* player) {
            if (!g_holdMode) {
                return;
            }

            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (!equipMgr) {
                return;
            }

            auto& st = BowState::Get();
            if (st.isUsingBow) {
                ExitBowMode(player, equipMgr, st);
            }
        }

        static bool IsWeaponDrawn(RE::Actor* actor) {
            if (!actor) {
                return false;
            }

            auto* state = actor->AsActorState();
            if (!state) {
                return false;
            }

            return state->IsWeaponDrawn();
        }

        static void SetWeaponDrawn(RE::Actor* actor, bool drawn) {
            if (!actor) return;
            actor->DrawWeaponMagicHands(drawn);
        }

        static void EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                 BowState::IntegratedBowState& st, RE::TESObjectWEAP* bow) {
            if (!player || !equipMgr || !bow) {
                return;
            }
            auto* rightForm = player->GetEquippedObject(false);
            auto* leftForm = player->GetEquippedObject(true);

            BowState::SetPrevWeapons(rightForm, leftForm);

            const bool alreadyDrawn = IsWeaponDrawn(player);
            st.wasCombatPosed = alreadyDrawn;

            st.isEquipingBow = true;
            equipMgr->EquipObject(player, bow, nullptr, 1, nullptr, true, true, true, false);

            if (!alreadyDrawn) {
                SetWeaponDrawn(player, true);
            }

            st.isEquipingBow = false;
            st.isUsingBow = true;
        }

        static void ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                BowState::IntegratedBowState& st) {
            if (!st.wasCombatPosed && !player->IsInCombat()) {
                SetWeaponDrawn(player, false);
            }
            if (st.prevRight) {
                equipMgr->EquipObject(player, st.prevRight, nullptr, 1, nullptr, true, true, true, false);
            }
            if (st.prevLeft) {
                equipMgr->EquipObject(player, st.prevLeft, nullptr, 1, nullptr, true, true, true, false);
            }

            st.isUsingBow = false;
        }
    };
}

namespace BowInput {
    void RegisterInputHandler() {
        spdlog::info("[IntegratedBow] Registering IntegratedBowInputHandler");
        if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
            mgr->AddEventSink(IntegratedBowInputHandler::GetSingleton());
        }
    }

    void SetHoldMode(bool hold) { g_holdMode = hold; }

    void SetKeyScanCode(std::uint32_t scanCode) { g_bowKeyScanCode = scanCode; }
    void SetGamepadButton(int button) { g_bowGamepadButton = button; }
}
