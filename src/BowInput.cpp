
#include "BowInput.h"

#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>

#include "BowConfig.h"
#include "BowState.h"
#include "PCH.h"

using namespace std::literals;

namespace {
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
        auto& ist = BowInput::Globals();
        using namespace BowState::detail;
        ist.g_attackHoldActive.store(true, std::memory_order_relaxed);
        ist.g_attackHoldSecs.store(0.0f, std::memory_order_relaxed);

        auto* ev = MakeAttackButtonEvent(1.0f, 0.0f);
        DispatchAttackButtonEvent(ev);
    }

    void StopAutoAttackDraw() {
        auto& ist = BowInput::Globals();
        using namespace BowState::detail;

        float held = ist.g_attackHoldSecs.load(std::memory_order_relaxed);
        if (held <= 0.0f) {
            held = 0.1f;
        }

        auto* ev = MakeAttackButtonEvent(0.0f, held);
        DispatchAttackButtonEvent(ev);

        ist.g_attackHoldActive.store(false, std::memory_order_relaxed);
        ist.g_attackHoldSecs.store(0.0f, std::memory_order_relaxed);
    }

    inline bool AreAllActiveKeysDown() {
        auto const& ist = BowInput::Globals();
        bool hasActive = false;
        for (int i = 0; i < BowInput::kMaxComboKeys; ++i) {
            if (ist.g_bowKeyScanCodes[i] >= 0) {
                hasActive = true;
                if (!ist.g_bowKeyDown[i]) {
                    return false;
                }
            }
        }
        return hasActive;
    }

    inline bool AreAllActivePadButtonsDown() {
        auto const& ist = BowInput::Globals();
        bool hasActive = false;
        for (int i = 0; i < BowInput::kMaxComboKeys; ++i) {
            if (ist.g_bowPadButtons[i] >= 0) {
                hasActive = true;
                if (!ist.g_bowPadDown[i]) {
                    return false;
                }
            }
        }
        return hasActive;
    }

    void PumpAttackHold(float dt) {
        auto& ist = BowInput::Globals();
        if (!ist.g_attackHoldActive.load(std::memory_order_relaxed)) {
            return;
        }

        if (!BowState::IsAutoAttackHeld()) {
            ist.g_attackHoldActive.store(false, std::memory_order_relaxed);
            ist.g_attackHoldSecs.store(0.0f, std::memory_order_relaxed);
            return;
        }

        float cur = ist.g_attackHoldSecs.load(std::memory_order_relaxed);
        cur += dt;
        ist.g_attackHoldSecs.store(cur, std::memory_order_relaxed);

        using namespace BowState::detail;

        auto* ev = MakeAttackButtonEvent(1.0f, cur);
        DispatchAttackButtonEvent(ev);
    }

    inline void weaponSheatheHelper() {
        auto& ist = BowInput::Globals();
        auto* player = RE::PlayerCharacter::GetSingleton();
        BowState::SetBowEquipped(false);

        if (ist.g_pendingRestoreAfterSheathe.load(std::memory_order_relaxed)) {
            ist.g_pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);

            auto* equipMgr = RE::ActorEquipManager::GetSingleton();
            if (player && equipMgr) {
                auto& st = BowState::Get();

                if (st.prevRight.base) {
                    equipMgr->EquipObject(player, st.prevRight.base, st.prevRight.extra, 1, nullptr, true, true, true,
                                          false);
                }
                if (st.prevLeft.base) {
                    equipMgr->EquipObject(player, st.prevLeft.base, st.prevLeft.extra, 1, nullptr, true, true, true,
                                          false);
                }

                if (!st.prevRight.base && !st.prevLeft.base && st.chosenBow.base) {
                    equipMgr->UnequipObject(player, st.chosenBow.base, st.chosenBow.extra, 1, nullptr, true, true, true,
                                            false, nullptr);
                }

                st.prevRight.base = nullptr;
                st.prevRight.extra = nullptr;
                st.prevLeft.base = nullptr;
                st.prevLeft.extra = nullptr;
            }
        }
    }
}

BowInput::GlobalState& BowInput::Globals() noexcept {
    static GlobalState s;  // NOSONAR
    return s;
}

void BowInput::IntegratedBowInputHandler::UpdateHotkeyState(RE::PlayerCharacter* player, bool newKbdCombo,
                                                            bool newPadCombo) const {
    auto& st = BowInput::Globals();
    st.g_kbdComboDown = newKbdCombo;
    st.g_padComboDown = newPadCombo;

    const bool anyNow = st.g_kbdComboDown || st.g_padComboDown;

    const bool blocked = IsInputBlockedByMenus();

    if (anyNow && !st.g_hotkeyDown) {
        st.g_hotkeyDown = true;

        if (!blocked) {
            OnKeyPressed(player);
        }
    } else if (!anyNow && st.g_hotkeyDown) {
        st.g_hotkeyDown = false;

        if (!blocked) {
            OnKeyReleased();
        }
    }
}

void BowInput::IntegratedBowInputHandler::HandleKeyboardButton(const RE::ButtonEvent* a_event,
                                                               RE::PlayerCharacter* player) const {
    const auto code = static_cast<int>(a_event->idCode);
    auto& st = BowInput::Globals();

    if (st.g_captureRequested.load(std::memory_order_relaxed)) {
        if (a_event->IsDown()) {
            int encoded = code;
            st.g_capturedEncoded.store(encoded, std::memory_order_relaxed);
            st.g_captureRequested.store(false, std::memory_order_relaxed);
        }
        return;
    }

    int idx = -1;
    for (int i = 0; i < kMaxComboKeys; ++i) {
        if (st.g_bowKeyScanCodes[i] >= 0 && code == st.g_bowKeyScanCodes[i]) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return;
    }

    if (a_event->IsDown()) {
        st.g_bowKeyDown[idx] = true;
    } else if (a_event->IsUp()) {
        st.g_bowKeyDown[idx] = false;
    } else {
        return;
    }

    const bool comboK = AreAllActiveKeysDown();

    UpdateHotkeyState(player, comboK, st.g_padComboDown);
}

void BowInput::IntegratedBowInputHandler::HandleGamepadButton(const RE::ButtonEvent* a_event,
                                                              RE::PlayerCharacter* player) const {
    const auto code = static_cast<int>(a_event->idCode);
    auto& st = BowInput::Globals();

    if (st.g_captureRequested.load(std::memory_order_relaxed)) {
        if (a_event->IsDown()) {
            int encoded = -(code + 1);
            st.g_capturedEncoded.store(encoded, std::memory_order_relaxed);
            st.g_captureRequested.store(false, std::memory_order_relaxed);
        }
        return;
    }

    int idx = -1;
    for (int i = 0; i < kMaxComboKeys; ++i) {
        if (st.g_bowPadButtons[i] >= 0 && code == st.g_bowPadButtons[i]) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return;
    }

    if (a_event->IsDown()) {
        st.g_bowPadDown[idx] = true;
    } else if (a_event->IsUp()) {
        st.g_bowPadDown[idx] = false;
    } else {
        return;
    }

    const bool comboP = AreAllActivePadButtonsDown();

    UpdateHotkeyState(player, st.g_kbdComboDown, comboP);
}

void BowInput::IntegratedBowInputHandler::OnKeyPressed(RE::PlayerCharacter* player) const {
    auto* equipMgr = RE::ActorEquipManager::GetSingleton();
    auto& ist = BowInput::Globals();

    ist.g_exitToken.fetch_add(1, std::memory_order_acq_rel);
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

    if (ist.g_holdMode) {
        if (!st.isUsingBow) {
            EnterBowMode(player, equipMgr, st);
            return;
        }

        if (IsAutoDrawEnabled()) {
            if (BowState::IsBowEquipped()) {
                BowState::SetAutoAttackHeld(true);
                StartAutoAttackDraw();
            } else {
                ScheduleAutoAttackDraw();
            }
        }
    } else {
        if (st.isUsingBow) {
            ScheduleExitBowMode(!BowState::IsBowEquipped(), 0);
        } else {
            EnterBowMode(player, equipMgr, st);
        }
    }
}

void BowInput::IntegratedBowInputHandler::OnKeyReleased() const {
    if (auto const& ist = BowInput::Globals(); !ist.g_holdMode) {
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

    if (autoDraw && BowState::IsAutoAttackHeld()) {
        StopAutoAttackDraw();
        BowState::SetAutoAttackHeld(false);
    }

    ScheduleExitBowMode(!BowState::IsBowEquipped(), delayMs);
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
    auto const& ist = BowInput::Globals();
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
    st.isUsingBow = true;
    st.isEquipingBow = false;

    if (!alreadyDrawn) {
        SetWeaponDrawn(player, true);
    }

    BowState::SetAutoAttackHeld(false);
    if (ist.g_holdMode && IsAutoDrawEnabled() && BowInput::IsHotkeyDown()) {
        BowState::SetWaitingAutoAfterEquip(true);
    } else {
        BowState::SetWaitingAutoAfterEquip(false);
    }
}

void BowInput::IntegratedBowInputHandler::ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                                      BowState::IntegratedBowState& st) {
    auto& ist = BowInput::Globals();
    if (!player || !equipMgr) {
        return;
    }

    if (!st.wasCombatPosed && !player->IsInCombat()) {
        SetWeaponDrawn(player, false);

        ist.g_pendingRestoreAfterSheathe.store(true, std::memory_order_relaxed);

        st.isUsingBow = false;

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

void BowInput::IntegratedBowInputHandler::ScheduleExitBowMode(bool waitForEquip, int delayMs) {
    const auto myToken = BowInput::Globals().g_exitToken.fetch_add(1, std::memory_order_acq_rel) + 1;

    std::thread([waitForEquip, delayMs, myToken]() {
        auto const& ist = BowInput::Globals();

        if (waitForEquip) {
            using clock = std::chrono::steady_clock;
            auto start = clock::now();

            while (!BowState::IsBowEquipped()) {
                if (ist.g_exitToken.load(std::memory_order_acquire) != myToken) {
                    return;
                }

                auto now = clock::now();

                if (auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                    elapsed > 3000) {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            if (ist.g_exitToken.load(std::memory_order_acquire) != myToken) {
                return;
            }
        }

        auto* task = SKSE::GetTaskInterface();
        if (!task) {
            return;
        }

        task->AddTask([myToken]() {
            if (auto const& istInner = BowInput::Globals();
                istInner.g_exitToken.load(std::memory_order_acquire) != myToken) {
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

    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    const auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (dt < 0.0f || dt > 0.5f) {
        dt = 0.0f;
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

    PumpAttackHold(dt);

    return RE::BSEventNotifyControl::kContinue;
}

void BowInput::IntegratedBowInputHandler::ScheduleAutoAttackDraw() {
    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !player) {
        return;
    }

    if (auto const& st = BowState::Get(); !st.isUsingBow) {
        return;
    }

    BowState::SetWaitingAutoAfterEquip(true);
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

void BowInput::SetHoldMode(bool hold) {
    auto& st = BowInput::Globals();
    st.g_holdMode = hold;
}

void BowInput::SetKeyScanCodes(int k1, int k2, int k3) {
    auto& st = BowInput::Globals();
    std::array<int, kMaxComboKeys> vals{k1, k2, k3};

    for (int i = 0; i < kMaxComboKeys; ++i) {
        st.g_bowKeyScanCodes[i] = vals[i];
        st.g_bowKeyDown[i] = false;
    }

    st.g_kbdComboDown = false;
    st.g_hotkeyDown = false;
}

void BowInput::SetGamepadButtons(int b1, int b2, int b3) {
    auto& st = BowInput::Globals();
    std::array<int, kMaxComboKeys> vals = {b1, b2, b3};

    for (int i = 0; i < kMaxComboKeys; ++i) {
        st.g_bowPadButtons[i] = vals[i];
        st.g_bowPadDown[i] = false;
    }

    st.g_padComboDown = false;
    st.g_hotkeyDown = false;
}

void BowInput::RequestGamepadCapture() {
    auto& st = BowInput::Globals();
    st.g_captureRequested.store(true, std::memory_order_relaxed);
    st.g_capturedEncoded.store(-1, std::memory_order_relaxed);
}

int BowInput::PollCapturedGamepadButton() {
    auto& st = BowInput::Globals();
    if (int v = st.g_capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        st.g_capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }
    return -1;
}

bool BowInput::IsHotkeyDown() {
    auto const& st = BowInput::Globals();
    return st.g_hotkeyDown;
}

BowInput::BowAnimEventSink* BowInput::BowAnimEventSink::GetSingleton() {
    static BowAnimEventSink instance;
    return std::addressof(instance);
}

RE::BSEventNotifyControl BowInput::BowAnimEventSink::ProcessEvent(const RE::BSAnimationGraphEvent* ev,
                                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!ev || !ev->holder) {
        return RE::BSEventNotifyControl::kContinue;
    }
    auto* actor = ev->holder->As<RE::Actor>();
    auto const& ist = BowInput::Globals();

    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !actor || actor != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    std::string_view tag{ev->tag.c_str(), ev->tag.size()};
    std::string_view payload{ev->payload.c_str(), ev->payload.size()};

    if (tag == "EnableBumper"sv) {
        BowState::SetBowEquipped(true);
        const bool waiting = BowState::IsWaitingAutoAfterEquip();
        const bool usingBow = BowState::IsUsingBow();
        const bool autoDraw = IsAutoDrawEnabled();
        const bool hotkeyDown = BowInput::IsHotkeyDown();

        if (waiting && usingBow && ist.g_holdMode && autoDraw && hotkeyDown) {
            BowState::SetWaitingAutoAfterEquip(false);

            if (!BowState::IsAutoAttackHeld()) {
                BowState::SetAutoAttackHeld(true);
                StartAutoAttackDraw();
            }
        }
    }

    else if (tag == "WeaponSheathe"sv)
        weaponSheatheHelper();

    else if (tag == "bowReset"sv) {
        const bool autoDraw = IsAutoDrawEnabled();
        const bool autoHeld = BowState::IsAutoAttackHeld();
        const bool hotkeyDown = BowInput::IsHotkeyDown();

        if (autoDraw && autoHeld && hotkeyDown) {
            StopAutoAttackDraw();

            BowState::SetAutoAttackHeld(true);

            StartAutoAttackDraw();
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

void BowInput::RegisterAnimEventSink() {
    auto const* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    player->AddAnimationGraphEventSink(BowAnimEventSink::GetSingleton());
}