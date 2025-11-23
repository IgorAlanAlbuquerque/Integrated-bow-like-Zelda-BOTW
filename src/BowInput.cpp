
#include "BowInput.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "BowConfig.h"
#include "BowState.h"
#include "PCH.h"

namespace {
    bool g_holdMode = true;  // NOSONAR

    int g_bowKeyScanCodes[BowInput::kMaxComboKeys] = {0x2F, -1, -1};     // NOSONAR
    bool g_bowKeyDown[BowInput::kMaxComboKeys] = {false, false, false};  // NOSONAR

    int g_bowPadButtons[BowInput::kMaxComboKeys] = {-1, -1, -1};         // NOSONAR
    bool g_bowPadDown[BowInput::kMaxComboKeys] = {false, false, false};  // NOSONAR

    bool g_kbdComboDown = false;  // NOSONAR
    bool g_padComboDown = false;  // NOSONAR
    bool g_hotkeyDown = false;    // NOSONAR

    std::atomic_bool bowDrawed = false;   // NOSONAR
    std::atomic_uint64_t g_exitToken{0};  // NOSONAR

    inline bool IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        if (ui->GameIsPaused()) {
            return true;
        }

        static const RE::BSFixedString inventoryMenu{"InventoryMenu"};
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        static const RE::BSFixedString statsMenu{"StatsMenu"};
        static const RE::BSFixedString mapMenu{"MapMenu"};
        static const RE::BSFixedString journalMenu{"Journal Menu"};
        static const RE::BSFixedString favoritesMenu{"FavoritesMenu"};
        static const RE::BSFixedString containerMenu{"ContainerMenu"};
        static const RE::BSFixedString barterMenu{"BarterMenu"};
        static const RE::BSFixedString trainingMenu{"Training Menu"};
        static const RE::BSFixedString craftingMenu{"Crafting Menu"};
        static const RE::BSFixedString giftMenu{"GiftMenu"};
        static const RE::BSFixedString lockpickingMenu{"Lockpicking Menu"};
        static const RE::BSFixedString sleepWaitMenu{"Sleep/Wait Menu"};
        static const RE::BSFixedString loadingMenu{"Loading Menu"};
        static const RE::BSFixedString mainMenu{"Main Menu"};
        static const RE::BSFixedString console{"Console"};

        if (static const RE::BSFixedString mcm{"Mod Configuration Menu"};
            ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(statsMenu) ||
            ui->IsMenuOpen(mapMenu) || ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(favoritesMenu) ||
            ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) || ui->IsMenuOpen(trainingMenu) ||
            ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(giftMenu) || ui->IsMenuOpen(lockpickingMenu) ||
            ui->IsMenuOpen(sleepWaitMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(mainMenu) ||
            ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm)) {
            return true;
        }

        return false;
    }

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
    }

    void StopAutoAttackDraw() {
        using namespace BowState::detail;

        auto* ev = MakeAttackButtonEvent(0.0f, 0.1f);
        DispatchAttackButtonEvent(ev);
    }

    inline bool AreAllActiveKeysDown() {
        bool hasActive = false;
        for (int i = 0; i < BowInput::kMaxComboKeys; ++i) {
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
        for (int i = 0; i < BowInput::kMaxComboKeys; ++i) {
            if (g_bowPadButtons[i] >= 0) {
                hasActive = true;
                if (!g_bowPadDown[i]) {
                    return false;
                }
            }
        }
        return hasActive;
    }
}

void BowInput::IntegratedBowInputHandler::UpdateHotkeyState(RE::PlayerCharacter* player, bool newKbdCombo,
                                                            bool newPadCombo) const {
    g_kbdComboDown = newKbdCombo;
    g_padComboDown = newPadCombo;

    const bool anyNow = g_kbdComboDown || g_padComboDown;

    if (anyNow && !g_hotkeyDown) {
        g_hotkeyDown = true;
        OnKeyPressed(player);
    } else if (!anyNow && g_hotkeyDown) {
        g_hotkeyDown = false;
        OnKeyReleased();
    }
}

void BowInput::IntegratedBowInputHandler::HandleKeyboardButton(const RE::ButtonEvent* a_event,
                                                               RE::PlayerCharacter* player) const {
    const auto code = static_cast<int>(a_event->idCode);

    int idx = -1;
    for (int i = 0; i < kMaxComboKeys; ++i) {
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

void BowInput::IntegratedBowInputHandler::HandleGamepadButton(const RE::ButtonEvent* a_event,
                                                              RE::PlayerCharacter* player) const {
    const auto code = static_cast<int>(a_event->idCode);

    int idx = -1;
    for (int i = 0; i < kMaxComboKeys; ++i) {
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

void BowInput::IntegratedBowInputHandler::OnKeyPressed(RE::PlayerCharacter* player) const {
    auto* equipMgr = RE::ActorEquipManager::GetSingleton();

    g_exitToken.fetch_add(1, std::memory_order_acq_rel);
    if (!equipMgr) {
        return;
    }

    auto& st = BowState::Get();
    if (!BowState::EnsureChosenBowInInventory()) {
        return;
    }

    if (RE::TESObjectWEAP const* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr; !bow) {
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

void BowInput::IntegratedBowInputHandler::OnKeyReleased() const {
    if (!g_holdMode) {
        return;
    }

    if (auto const* equipMgr = RE::ActorEquipManager::GetSingleton(); !equipMgr) {
        return;
    }

    if (auto const& st = BowState::Get(); !st.isUsingBow) {
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

        ScheduleExitBowMode(false, delayMs);
        return;
    }

    ScheduleExitBowMode(true, delayMs);
}

bool BowInput::IntegratedBowInputHandler::IsWeaponDrawn(RE::Actor* actor) {
    if (!actor) {
        return false;
    }

    auto const* state = actor->AsActorState();
    if (!state) {
        return false;
    }

    return state->IsWeaponDrawn();
}

void BowInput::IntegratedBowInputHandler::SetWeaponDrawn(RE::Actor* actor, bool drawn) {
    if (!actor) return;

    actor->DrawWeaponMagicHands(drawn);
}

RE::ExtraDataList* BowInput::IntegratedBowInputHandler::GetPrimaryExtra(RE::InventoryEntryData* entry) {
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

void BowInput::IntegratedBowInputHandler::EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                                       BowState::IntegratedBowState& st) {
    if (!player || !equipMgr) {
        return;
    }

    RE::TESObjectWEAP* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
    RE::ExtraDataList* bowExtra = st.chosenBow.extra;

    auto* rightEntry = player->GetEquippedEntryData(false);
    auto* leftEntry = player->GetEquippedEntryData(true);

    auto* baseR = rightEntry ? rightEntry->GetObject() : nullptr;
    auto* extraR = GetPrimaryExtra(rightEntry);

    auto* baseL = leftEntry ? leftEntry->GetObject() : nullptr;
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

void BowInput::IntegratedBowInputHandler::ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
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

        std::thread([prevRightBase, prevRightExtra, prevLeftBase, prevLeftExtra, chosenBowBase, chosenBowExtra]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(950));

            auto* task = SKSE::GetTaskInterface();
            if (!task) {
                return;
            }

            task->AddTask([prevRightBase, prevRightExtra, prevLeftBase, prevLeftExtra, chosenBowBase,
                           chosenBowExtra]() {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* equipMgr = RE::ActorEquipManager::GetSingleton();
                if (!player || !equipMgr) {
                    return;
                }

                if (prevRightBase) {
                    equipMgr->EquipObject(player, prevRightBase, prevRightExtra, 1, nullptr, true, true, true, false);
                }
                if (prevLeftBase) {
                    equipMgr->EquipObject(player, prevLeftBase, prevLeftExtra, 1, nullptr, true, true, true, false);
                }

                if (!prevRightBase && !prevLeftBase && chosenBowBase) {
                    equipMgr->UnequipObject(player, chosenBowBase, chosenBowExtra, 1, nullptr, true, true, true, false,
                                            nullptr);
                }
            });
        }).detach();

        return;
    }

    if (st.prevRight.base) {
        equipMgr->EquipObject(player, st.prevRight.base, st.prevRight.extra, 1, nullptr, true, true, true, false);
    }
    if (st.prevLeft.base) {
        equipMgr->EquipObject(player, st.prevLeft.base, st.prevLeft.extra, 1, nullptr, true, true, true, false);
    }

    if (!st.prevRight.base && !st.prevLeft.base && st.chosenBow.base) {
        equipMgr->UnequipObject(player, st.chosenBow.base, st.chosenBow.extra, 1, nullptr, true, true, true, false,
                                nullptr);
    }

    st.isUsingBow = false;
    st.prevRight.base = nullptr;
    st.prevRight.extra = nullptr;
    st.prevLeft.base = nullptr;
    st.prevLeft.extra = nullptr;
}

void BowInput::IntegratedBowInputHandler::ScheduleExitBowMode(bool waitBowDraw, int delayMs) {
    const auto myToken = g_exitToken.fetch_add(1, std::memory_order_acq_rel) + 1;

    std::thread([waitBowDraw, delayMs, myToken]() {
        if (waitBowDraw) {
            while (!bowDrawed.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                if (g_exitToken.load(std::memory_order_acquire) != myToken) {
                    return;
                }
            }
        }

        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

            if (g_exitToken.load(std::memory_order_acquire) != myToken) {
                return;
            }
        }

        auto* task = SKSE::GetTaskInterface();
        if (!task) {
            return;
        }

        task->AddTask([myToken]() {
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

            if (IsAutoDrawEnabled() && BowState::IsAutoAttackHeld()) {
                StopAutoAttackDraw();
                BowState::SetAutoAttackHeld(false);
            }

            ExitBowMode(player, equipMgr, st);
        });
    }).detach();
}

RE::BSEventNotifyControl BowInput::IntegratedBowInputHandler::ProcessEvent(RE::InputEvent* const* a_events,
                                                                           RE::BSTEventSource<RE::InputEvent*>*) {
    if (!a_events) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (IsInputBlockedByMenus()) {
        for (int i = 0; i < BowInput::kMaxComboKeys; ++i) {
            g_bowKeyDown[i] = false;
            g_bowPadDown[i] = false;
        }
        g_kbdComboDown = false;
        g_padComboDown = false;
        g_hotkeyDown = false;

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

void BowInput::IntegratedBowInputHandler::ScheduleAutoAttackDraw() {
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

            if (!g_hotkeyDown) {
                return;
            }

            BowState::SetAutoAttackHeld(true);
            StartAutoAttackDraw();
        });
    }).detach();
}

BowInput::IntegratedBowInputHandler* BowInput::IntegratedBowInputHandler::GetSingleton() {
    static IntegratedBowInputHandler instance;
    return std::addressof(instance);
}

void BowInput::RegisterInputHandler() {
    if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
        mgr->AddEventSink(IntegratedBowInputHandler::GetSingleton());
    }
}

void BowInput::SetHoldMode(bool hold) { g_holdMode = hold; }

void BowInput::SetKeyScanCodes(int k1, int k2, int k3) {
    int vals[kMaxComboKeys] = {k1, k2, k3};

    for (int i = 0; i < kMaxComboKeys; ++i) {
        g_bowKeyScanCodes[i] = vals[i];
        g_bowKeyDown[i] = false;
    }

    g_kbdComboDown = false;
    g_hotkeyDown = false;
}

void BowInput::SetGamepadButtons(int b1, int b2, int b3) {
    int vals[kMaxComboKeys] = {b1, b2, b3};

    for (int i = 0; i < kMaxComboKeys; ++i) {
        g_bowPadButtons[i] = vals[i];
        g_bowPadDown[i] = false;
    }

    g_padComboDown = false;
    g_hotkeyDown = false;
}
