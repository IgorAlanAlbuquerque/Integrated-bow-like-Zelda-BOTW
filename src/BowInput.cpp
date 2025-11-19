
#include "BowInput.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "BowState.h"
#include "PCH.h"

namespace {
    bool g_holdMode = true;                 // NOSONAR
    std::uint32_t g_bowKeyScanCode = 0x2F;  // NOSONAR
    int g_bowGamepadButton = -1;            // NOSONAR
    std::atomic_bool bowDrawed = false;     // NOSONAR

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

        void OnKeyPressed(RE::PlayerCharacter* player) const {
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

        void OnKeyReleased(RE::PlayerCharacter* player) const {
            if (!g_holdMode) {
                return;
            }

            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (!equipMgr) {
                return;
            }

            auto& st = BowState::Get();
            if (st.isUsingBow) {
                if (bowDrawed.load()) {
                    ExitBowMode(player, equipMgr, st);
                    return;
                }

                RE::NiPointer<RE::Actor> playerHandle{player};

                std::thread([playerHandle]() {
                    while (!bowDrawed.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }

                    auto* task = SKSE::GetTaskInterface();
                    if (!task) {
                        return;
                    }

                    task->AddTask([]() {
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
                        if (!player || !equipMgr) {
                            return;
                        }

                        auto& st = BowState::Get();
                        if (!st.isUsingBow) {
                            return;
                        }

                        ExitBowMode(player, equipMgr, st);
                    });
                }).detach();
            }
        }

        static bool IsWeaponDrawn(RE::Actor* actor) {
            if (!actor) {
                return false;
            }

            auto const* state = actor->AsActorState();
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
            bowDrawed = false;
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1050));
                bowDrawed = true;
            }).detach();
        }

        static void ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                BowState::IntegratedBowState& st) {
            if (!player || !equipMgr) {
                return;
            }

            if (!st.wasCombatPosed && !player->IsInCombat()) {
                SetWeaponDrawn(player, false);

                RE::TESBoundObject* prevRight = st.prevRight;
                RE::TESBoundObject* prevLeft = st.prevLeft;
                RE::TESObjectWEAP* chosenBow = st.chosenBow;

                st.isUsingBow = false;
                st.prevRight = nullptr;
                st.prevLeft = nullptr;

                std::thread([prevRight, prevLeft, chosenBow]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(950));

                    auto* task = SKSE::GetTaskInterface();
                    if (!task) {
                        return;
                    }

                    task->AddTask([prevRight, prevLeft, chosenBow]() {
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
                        if (!player || !equipMgr) {
                            return;
                        }

                        if (prevRight) {
                            equipMgr->EquipObject(player, prevRight, nullptr, 1, nullptr, true, true, true, false);
                        }
                        if (prevLeft) {
                            equipMgr->EquipObject(player, prevLeft, nullptr, 1, nullptr, true, true, true, false);
                        }

                        if (!prevRight && !prevLeft && chosenBow) {
                            equipMgr->UnequipObject(player, chosenBow, nullptr, 1, nullptr, true, true, true, false,
                                                    nullptr);
                        }
                    });
                }).detach();

                return;
            }

            if (st.prevRight) {
                equipMgr->EquipObject(player, st.prevRight, nullptr, 1, nullptr, true, true, true, false);
            }
            if (st.prevLeft) {
                equipMgr->EquipObject(player, st.prevLeft, nullptr, 1, nullptr, true, true, true, false);
            }

            if (!st.prevRight && !st.prevLeft && st.chosenBow) {
                equipMgr->UnequipObject(player, st.chosenBow, nullptr, 1, nullptr, true, true, true, false, nullptr);
            }

            st.isUsingBow = false;
            st.prevRight = nullptr;
            st.prevLeft = nullptr;
            bowDrawed = false;
        }
    };
}

namespace BowInput {
    void RegisterInputHandler() {
        if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
            mgr->AddEventSink(IntegratedBowInputHandler::GetSingleton());
        }
    }

    void SetHoldMode(bool hold) { g_holdMode = hold; }

    void SetKeyScanCode(std::uint32_t scanCode) { g_bowKeyScanCode = scanCode; }
    void SetGamepadButton(int button) { g_bowGamepadButton = button; }
}
