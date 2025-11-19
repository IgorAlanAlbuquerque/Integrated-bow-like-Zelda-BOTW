
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

            RE::TESObjectWEAP* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
            if (!bow) {
                return;
            }

            if (g_holdMode) {
                if (st.isUsingBow) {
                    return;
                }
                EnterBowMode(player, equipMgr, st);
            } else {
                if (st.isUsingBow) {
                    ExitBowMode(player, equipMgr, st);
                } else {
                    EnterBowMode(player, equipMgr, st);
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

        static RE::ExtraDataList* GetPrimaryExtra(RE::InventoryEntryData* entry) {
            if (!entry || !entry->extraLists) {
                return nullptr;
            }

            for (auto* xList : *entry->extraLists) {
                if (xList) {
                    return xList;
                }
            }
            return nullptr;
        }

        static void EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                 BowState::IntegratedBowState& st) {
            if (!player || !equipMgr) {
                return;
            }

            RE::TESObjectWEAP* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
            RE::ExtraDataList* bowExtra = st.chosenBow.extra;

            auto* rightEntry = player->GetEquippedEntryData(false);
            auto* leftEntry = player->GetEquippedEntryData(true);

            auto* baseR = rightEntry ? const_cast<RE::TESBoundObject*>(rightEntry->GetObject()) : nullptr;
            auto* extraR = GetPrimaryExtra(rightEntry);

            auto* baseL = leftEntry ? const_cast<RE::TESBoundObject*>(leftEntry->GetObject()) : nullptr;
            auto* extraL = GetPrimaryExtra(leftEntry);

            BowState::SetPrevWeapons(baseR, extraR, baseL, extraL);

            const bool alreadyDrawn = IsWeaponDrawn(player);
            st.wasCombatPosed = alreadyDrawn;

            st.isEquipingBow = true;
            st.isUsingBow = false;
            equipMgr->EquipObject(player, bow, bowExtra, 1, nullptr, true, true, true, false);

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

            RE::TESBoundObject* prevRightBase = st.prevRight.base;
            RE::ExtraDataList* prevRightExtra = st.prevRight.extra;

            RE::TESBoundObject* prevLeftBase = st.prevLeft.base;
            RE::ExtraDataList* prevLeftExtra = st.prevLeft.extra;

            RE::TESBoundObject* chosenBowBase = st.chosenBow.base;
            RE::ExtraDataList* chosenBowExtra = st.chosenBow.extra;

            if (!st.wasCombatPosed && !player->IsInCombat()) {
                SetWeaponDrawn(player, false);

                st.isUsingBow = false;
                st.prevRight.base = nullptr;
                st.prevRight.extra = nullptr;
                st.prevLeft.base = nullptr;
                st.prevLeft.extra = nullptr;

                std::thread([prevRightBase, prevRightExtra, prevLeftBase, prevLeftExtra, chosenBowBase,
                             chosenBowExtra]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(950));

                    auto* task = SKSE::GetTaskInterface();
                    if (!task) {
                        return;
                    }

                    task->AddTask(
                        [prevRightBase, prevRightExtra, prevLeftBase, prevLeftExtra, chosenBowBase, chosenBowExtra]() {
                            auto* player = RE::PlayerCharacter::GetSingleton();
                            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
                            if (!player || !equipMgr) {
                                return;
                            }

                            if (prevRightBase) {
                                equipMgr->EquipObject(player, prevRightBase, prevRightExtra, 1, nullptr, true, true,
                                                      true, false);
                            }
                            if (prevLeftBase) {
                                equipMgr->EquipObject(player, prevLeftBase, prevLeftExtra, 1, nullptr, true, true, true,
                                                      false);
                            }

                            if (!prevRightBase && !prevLeftBase && chosenBowBase) {
                                equipMgr->UnequipObject(player, chosenBowBase, chosenBowExtra, 1, nullptr, true, true,
                                                        true, false, nullptr);
                            }
                        });
                }).detach();

                return;
            }

            if (st.prevRight.base) {
                equipMgr->EquipObject(player, st.prevRight.base, st.prevRight.extra, 1, nullptr, true, true, true,
                                      false);
            }
            if (st.prevLeft.base) {
                equipMgr->EquipObject(player, st.prevLeft.base, st.prevLeft.extra, 1, nullptr, true, true, true, false);
            }

            if (!st.prevRight.base && !st.prevLeft.base && st.chosenBow.base) {
                equipMgr->UnequipObject(player, st.chosenBow.base, st.chosenBow.extra, 1, nullptr, true, true, true,
                                        false, nullptr);
            }

            st.isUsingBow = false;
            st.prevRight.base = nullptr;
            st.prevRight.extra = nullptr;
            st.prevLeft.base = nullptr;
            st.prevLeft.extra = nullptr;
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
