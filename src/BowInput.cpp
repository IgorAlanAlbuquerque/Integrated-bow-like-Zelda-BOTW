
#include "BowInput.h"

#include "BowState.h"
#include "PCH.h"

namespace {
    bool g_holdMode = true;
    std::uint32_t g_bowKeyScanCode = 0x2F;

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
            if (a_event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                return false;
            }

            auto code = a_event->idCode;
            return code == g_bowKeyScanCode;
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

        static void EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                 BowState::IntegratedBowState& st, RE::TESObjectWEAP* bow) {
            auto* rightForm = player->GetEquippedObject(false);
            auto* leftForm = player->GetEquippedObject(true);

            BowState::SetPrevWeapons(rightForm, leftForm);

            equipMgr->EquipObject(player, bow, nullptr, 1, nullptr, true, true, true, false);

            st.isUsingBow = true;
        }

        static void ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                BowState::IntegratedBowState& st) {
            if (st.chosenBow) {
                equipMgr->UnequipObject(player, st.chosenBow, nullptr, 1, nullptr, true, true, false, false, nullptr);
            }

            if (st.prevRight) {
                equipMgr->EquipObject(player, st.prevRight, nullptr, 1, nullptr, true, true, true, false);
            }
            if (st.prevLeft) {
                equipMgr->EquipObject(player, st.prevLeft, nullptr, 1, nullptr, true, true, true, false);
            }

            st.isUsingBow = false;
            st.prevRight = nullptr;
            st.prevLeft = nullptr;
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
}
