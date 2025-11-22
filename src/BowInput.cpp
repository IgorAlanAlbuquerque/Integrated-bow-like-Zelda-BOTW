
#include "BowInput.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "BowConfig.h"
#include "BowState.h"
#include "PCH.h"

namespace {
    bool g_holdMode = true;  // NOSONAR

    int g_bowKeyScanCodes[3] = {0x2F, -1, -1};     // NOSONAR
    bool g_bowKeyDown[3] = {false, false, false};  // NOSONAR

    int g_bowPadButtons[3] = {-1, -1, -1};         // NOSONAR
    bool g_bowPadDown[3] = {false, false, false};  // NOSONAR

    bool g_kbdComboDown = false;  // NOSONAR
    bool g_padComboDown = false;  // NOSONAR
    bool g_hotkeyDown = false;    // NOSONAR

    std::atomic_bool bowDrawed = false;   // NOSONAR
    std::atomic_uint64_t g_exitToken{0};  // NOSONAR

    inline bool IsAutoDrawEnabled() {
        auto const& cfg = IntegratedBow::GetBowConfig();
        return cfg.autoDrawEnabled.load(std::memory_order_relaxed);
    }

    inline float GetSheathedDelayMs() {
        auto const& cfg = IntegratedBow::GetBowConfig();
        float secs = cfg.sheathedDelaySeconds.load(std::memory_order_relaxed);
        if (secs < 0.0f) {
            secs = 0.0f;
        }
        return secs * 1000.0f;
    }

    void StartAutoAttackDraw() {
        using namespace BowState::detail;

        auto* ev = MakeAttackButtonEvent(1.0f, 0.0f);
        DispatchAttackButtonEvent(ev);

        spdlog::info("[IntegratedBow] AutoAttack: DOWN");
    }

    void StopAutoAttackDraw() {
        using namespace BowState::detail;

        auto* ev = MakeAttackButtonEvent(0.0f, 0.1f);
        DispatchAttackButtonEvent(ev);

        spdlog::info("[IntegratedBow] AutoAttack: UP");
    }

    void ScheduleAutoAttackDraw() {
        const bool autoDraw = IsAutoDrawEnabled();
        const bool holdMode = g_holdMode;

        bowDrawed.store(false, std::memory_order_relaxed);
        BowState::SetAutoAttackHeld(false);

        std::thread([autoDraw, holdMode]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1050));
            bowDrawed.store(true, std::memory_order_relaxed);

            if (!autoDraw || !holdMode) {
                return;
            }

            auto* task = SKSE::GetTaskInterface();
            if (!task) {
                return;
            }

            task->AddTask([]() {
                if (auto const& st = BowState::Get(); !st.isUsingBow || BowState::IsAutoAttackHeld()) {
                    return;
                }

                BowState::SetAutoAttackHeld(true);
                StartAutoAttackDraw();
            });
        }).detach();
    }

    inline bool AreAllActiveKeysDown() {
        bool hasActive = false;
        for (int i = 0; i < 3; ++i) {
            if (g_bowKeyScanCodes[i] >= 0) {
                hasActive = true;
                if (!g_bowKeyDown[i]) {
                    return false;
                }
            }
        }
        return hasActive;
    }

    inline bool AreAllActivePadButtonsDown() {
        bool hasActive = false;
        for (int i = 0; i < 3; ++i) {
            if (g_bowPadButtons[i] >= 0) {
                hasActive = true;
                if (!g_bowPadDown[i]) {
                    return false;
                }
            }
        }
        return hasActive;
    }

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

                    auto dev = button->GetDevice();
                    if (dev == RE::INPUT_DEVICE::kKeyboard) {
                        HandleKeyboardButton(button, player);
                    } else if (dev == RE::INPUT_DEVICE::kGamepad) {
                        HandleGamepadButton(button, player);
                    }
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        void UpdateHotkeyState(RE::PlayerCharacter* player, bool newKbdCombo, bool newPadCombo) const {
            g_kbdComboDown = newKbdCombo;
            g_padComboDown = newPadCombo;

            const bool anyNow = g_kbdComboDown || g_padComboDown;

            if (anyNow && !g_hotkeyDown) {
                g_hotkeyDown = true;
                OnKeyPressed(player);
            } else if (!anyNow && g_hotkeyDown) {
                g_hotkeyDown = false;
                OnKeyReleased(player);
            }
        }

        void HandleKeyboardButton(const RE::ButtonEvent* a_event, RE::PlayerCharacter* player) const {
            const auto code = static_cast<int>(a_event->idCode);

            int idx = -1;
            for (int i = 0; i < 3; ++i) {
                if (g_bowKeyScanCodes[i] >= 0 && code == g_bowKeyScanCodes[i]) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                return;
            }

            if (a_event->IsDown()) {
                g_bowKeyDown[idx] = true;
            } else if (a_event->IsUp()) {
                g_bowKeyDown[idx] = false;
            } else {
                return;
            }

            const bool comboK = AreAllActiveKeysDown();

            UpdateHotkeyState(player, comboK, g_padComboDown);
        }

        void HandleGamepadButton(const RE::ButtonEvent* a_event, RE::PlayerCharacter* player) const {
            const auto code = static_cast<int>(a_event->idCode);

            int idx = -1;
            for (int i = 0; i < 3; ++i) {
                if (g_bowPadButtons[i] >= 0 && code == g_bowPadButtons[i]) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                return;
            }

            if (a_event->IsDown()) {
                g_bowPadDown[idx] = true;
            } else if (a_event->IsUp()) {
                g_bowPadDown[idx] = false;
            } else {
                return;
            }

            const bool comboP = AreAllActivePadButtonsDown();

            UpdateHotkeyState(player, g_kbdComboDown, comboP);
        }

        void OnKeyPressed(RE::PlayerCharacter* player) const {
            auto* equipMgr = RE::ActorEquipManager::GetSingleton();

            g_exitToken.fetch_add(1, std::memory_order_acq_rel);
            if (!equipMgr) {
                return;
            }

            auto& st = BowState::Get();
            if (!BowState::EnsureChosenBowInInventory()) {
                return;
            }

            if (RE::TESObjectWEAP const* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
                !bow) {
                return;
            }

            if (g_holdMode) {
                if (!st.isUsingBow) {
                    EnterBowMode(player, equipMgr, st);
                    return;
                }
                if (bowDrawed.load(std::memory_order_relaxed)) {
                    BowState::SetAutoAttackHeld(true);
                    StartAutoAttackDraw();
                } else {
                    ScheduleAutoAttackDraw();
                }
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
            if (!st.isUsingBow) {
                return;
            }

            const bool autoDraw = IsAutoDrawEnabled();
            const float delayMsF = GetSheathedDelayMs();
            const auto delayMs = static_cast<int>(delayMsF + 0.5f);

            if (bowDrawed.load(std::memory_order_relaxed)) {
                if (autoDraw && BowState::IsAutoAttackHeld()) {
                    StopAutoAttackDraw();
                    BowState::SetAutoAttackHeld(false);
                }

                if (delayMs <= 0) {
                    ExitBowMode(player, equipMgr, st);
                    return;
                }

                const auto myToken = g_exitToken.fetch_add(1, std::memory_order_acq_rel) + 1;

                std::thread([delayMs, autoDraw, myToken]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

                    if (g_exitToken.load(std::memory_order_acquire) != myToken) {
                        return;
                    }

                    auto* task = SKSE::GetTaskInterface();
                    if (!task) {
                        return;
                    }

                    task->AddTask([autoDraw, myToken]() {
                        if (g_exitToken.load(std::memory_order_acquire) != myToken) {
                            return;
                        }

                        auto* player = RE::PlayerCharacter::GetSingleton();
                        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
                        if (!player || !equipMgr) {
                            return;
                        }

                        auto& st = BowState::Get();
                        if (!st.isUsingBow) {
                            return;
                        }

                        if (autoDraw && BowState::IsAutoAttackHeld()) {
                            StopAutoAttackDraw();
                            BowState::SetAutoAttackHeld(false);
                        }

                        ExitBowMode(player, equipMgr, st);
                    });
                }).detach();

                return;
            }

            RE::NiPointer<RE::Actor> playerHandle{player};

            const auto myToken = g_exitToken.fetch_add(1, std::memory_order_acq_rel) + 1;

            std::thread([playerHandle, autoDraw, delayMs, myToken]() {
                while (!bowDrawed.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));

                    if (g_exitToken.load(std::memory_order_acquire) != myToken) {
                        return;
                    }
                }

                if (delayMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }

                if (g_exitToken.load(std::memory_order_acquire) != myToken) {
                    return;
                }

                auto* task = SKSE::GetTaskInterface();
                if (!task) {
                    return;
                }

                task->AddTask([autoDraw, myToken]() {
                    if (g_exitToken.load(std::memory_order_acquire) != myToken) {
                        return;
                    }

                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* equipMgr = RE::ActorEquipManager::GetSingleton();
                    if (!player || !equipMgr) {
                        return;
                    }

                    auto& st = BowState::Get();
                    if (!st.isUsingBow) {
                        return;
                    }

                    if (autoDraw && BowState::IsAutoAttackHeld()) {
                        StopAutoAttackDraw();
                        BowState::SetAutoAttackHeld(false);
                    }

                    ExitBowMode(player, equipMgr, st);
                });
            }).detach();
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
            bowDrawed.store(false, std::memory_order_relaxed);
            BowState::SetAutoAttackHeld(false);
            ScheduleAutoAttackDraw();
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

    void SetKeyScanCodes(int k1, int k2, int k3) {
        g_bowKeyScanCodes[0] = k1;
        g_bowKeyScanCodes[1] = k2;
        g_bowKeyScanCodes[2] = k3;
        g_bowKeyDown[0] = g_bowKeyDown[1] = g_bowKeyDown[2] = false;
        g_kbdComboDown = false;
        g_hotkeyDown = false;
    }

    void SetGamepadButtons(int b1, int b2, int b3) {
        g_bowPadButtons[0] = b1;
        g_bowPadButtons[1] = b2;
        g_bowPadButtons[2] = b3;
        g_bowPadDown[0] = g_bowPadDown[1] = g_bowPadDown[2] = false;
        g_padComboDown = false;
        g_hotkeyDown = false;
    }
}
