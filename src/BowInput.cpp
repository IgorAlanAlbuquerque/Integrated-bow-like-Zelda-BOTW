
#include "BowInput.h"

#include <atomic>
#include <chrono>
#include <ranges>
#include <string_view>
#include <thread>

#include "BowConfig.h"
#include "BowState.h"
#include "PCH.h"
#include "patchs/HiddenItemsPatch.h"

using namespace std::literals;

namespace {
    constexpr float kSmartClickThreshold = 0.18f;

    inline std::uint64_t NowMs() noexcept {
        using clock = std::chrono::steady_clock;
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
    }

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

        spdlog::info(
            "[IBOW][INPUT][AUTO] StartAutoAttackDraw enter | attackHold.active={} secs={:.4f} sheathReq={} autoHeld={}",
            ist.attackHold.active.load(std::memory_order_relaxed), ist.attackHold.secs.load(std::memory_order_relaxed),
            ist.sheathRequestedByPlayer.load(std::memory_order_relaxed), BowState::IsAutoAttackHeld());

        ist.attackHold.active.store(true, std::memory_order_relaxed);
        ist.attackHold.secs.store(0.0f, std::memory_order_relaxed);
        ist.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);

        spdlog::info("[IBOW][INPUT][AUTO] StartAutoAttackDraw set | attackHold.active=true secs=0 sheathReq=false");

        auto* ev = MakeAttackButtonEvent(1.0f, 0.0f);
        spdlog::info("[IBOW][INPUT][AUTO] DispatchAttackButtonEvent DOWN value=1 held=0 ev={}", (void*)ev);
        DispatchAttackButtonEvent(ev);

        spdlog::info("[IBOW][INPUT][AUTO] StartAutoAttackDraw exit");
    }

    void StopAutoAttackDraw() {
        auto& ist = BowInput::Globals();
        using namespace BowState::detail;

        float held = ist.attackHold.secs.load(std::memory_order_relaxed);
        const bool wasActive = ist.attackHold.active.load(std::memory_order_relaxed);

        spdlog::info(
            "[IBOW][INPUT][AUTO] StopAutoAttackDraw enter | active={} heldSecs={:.4f} sheathReq={} autoHeld={}",
            wasActive, held, ist.sheathRequestedByPlayer.load(std::memory_order_relaxed), BowState::IsAutoAttackHeld());

        if (held <= 0.0f) {
            spdlog::info("[IBOW][INPUT][AUTO] StopAutoAttackDraw held<=0 -> clamp to 0.1");
            held = 0.1f;
        }

        auto* ev = MakeAttackButtonEvent(0.0f, held);
        spdlog::info("[IBOW][INPUT][AUTO] DispatchAttackButtonEvent UP value=0 held={:.4f} ev={}", held, (void*)ev);
        DispatchAttackButtonEvent(ev);

        ist.attackHold.active.store(false, std::memory_order_relaxed);
        ist.attackHold.secs.store(0.0f, std::memory_order_relaxed);

        spdlog::info("[IBOW][INPUT][AUTO] StopAutoAttackDraw exit | active=false secs=0");
    }

    inline bool AreAllActiveKeysDown() {
        auto const& ist = BowInput::Globals();
        bool hasActive = false;
        for (int i = 0; i < BowInput::kMaxComboKeys; ++i) {
            if (ist.hotkeyConfig.bowKeyScanCodes[i] >= 0) {
                hasActive = true;
                if (!ist.hotkey.bowKeyDown[i]) {
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
            if (ist.hotkeyConfig.bowPadButtons[i] >= 0) {
                hasActive = true;
                if (!ist.hotkey.bowPadDown[i]) {
                    return false;
                }
            }
        }
        return hasActive;
    }

    void PumpAttackHold(float dt) {
        auto& ist = BowInput::Globals();
        if (!ist.attackHold.active.load(std::memory_order_relaxed)) {
            return;
        }

        if (!BowState::IsAutoAttackHeld()) {
            ist.attackHold.active.store(false, std::memory_order_relaxed);
            ist.attackHold.secs.store(0.0f, std::memory_order_relaxed);
            return;
        }

        float cur = ist.attackHold.secs.load(std::memory_order_relaxed);
        cur += dt;
        ist.attackHold.secs.store(cur, std::memory_order_relaxed);

        using namespace BowState::detail;

        auto* ev = MakeAttackButtonEvent(1.0f, cur);
        DispatchAttackButtonEvent(ev);
    }

    inline void weaponSheatheHelper() {
        auto& ist = BowInput::Globals();
        auto* player = RE::PlayerCharacter::GetSingleton();

        spdlog::info(
            "[IBOW][INPUT][SHEATHE] enter | player={} pendingRestoreAfterSheathe={} sheathReqByPlayer={} "
            "bowEquipped={} usingBow={}",
            (void*)player, ist.pendingRestoreAfterSheathe.load(std::memory_order_relaxed),
            ist.sheathRequestedByPlayer.load(std::memory_order_relaxed), BowState::IsBowEquipped(),
            BowState::Get().isUsingBow);
        auto& st = BowState::Get();

        const bool pendingRestore = ist.pendingRestoreAfterSheathe.load(std::memory_order_relaxed);
        const bool sheathReqPeek = ist.sheathRequestedByPlayer.load(std::memory_order_relaxed);

        if (!pendingRestore && !sheathReqPeek) {
            return;
        }

        if (!player) {
            spdlog::info("[IBOW][INPUT][SHEATHE] player=null -> return");
            return;
        }

        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        if (!equipMgr) {
            spdlog::info("[IBOW][INPUT][SHEATHE] equipMgr=null -> hard reset state | usingBow was={}", st.isUsingBow);
            ist.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
            st.isUsingBow = false;
            BowState::ClearPrevWeapons();
            BowState::ClearPrevExtraEquipped();
            BowState::ClearPrevAmmo();
            BowState::SetBowEquipped(false);
            spdlog::info("[IBOW][INPUT][SHEATHE] equipMgr=null -> return (state cleared)");
            return;
        }

        spdlog::info(
            "[IBOW][INPUT][SHEATHE] equipMgr ok | pendingRestoreAfterSheathe={} usingBow={} prevR=0x{:08X} "
            "prevL=0x{:08X} prevAmmo={}",
            pendingRestore, st.isUsingBow, st.prevRightFormID, st.prevLeftFormID, (void*)st.prevAmmo);

        if (pendingRestore) {
            spdlog::info(
                "[IBOW][INPUT][SHEATHE] pendingRestoreAfterSheathe=true -> restoring prev weapons/ammo (clear flag "
                "first)");
            ist.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
            BowState::SetBowEquipped(false);
            spdlog::info("[IBOW][INPUT][SHEATHE] calling RestorePrevWeaponsAndAmmo(clearIsUsingBow=false)");
            BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
            spdlog::info("[IBOW][INPUT][SHEATHE] RestorePrevWeaponsAndAmmo done -> return");
            return;
        }

        ist.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);
        spdlog::info("[IBOW][INPUT][SHEATHE] sheathRequestedByPlayer.exchange(false) -> {}", sheathReqPeek);
        if (sheathReqPeek && st.isUsingBow) {
            BowState::SetBowEquipped(false);
            spdlog::info("[IBOW][INPUT][SHEATHE] calling RestorePrevWeaponsAndAmmo(clearIsUsingBow=true)");
            BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
            spdlog::info("[IBOW][INPUT][SHEATHE] RestorePrevWeaponsAndAmmo done (clearIsUsingBow=true)");

            return;
        }
        spdlog::info("[IBOW][INPUT][SHEATHE] no pending restore and no sheath request -> exit");
    }

    inline bool HasTransformArchetype(const RE::MagicItem* item) {
        if (!item) {
            return false;
        }

        using ArchetypeID = RE::EffectArchetypes::ArchetypeID;

        for (auto& effect : item->effects) {
            if (!effect) {
                continue;
            }

            auto const* mgef = effect->baseEffect;
            if (!mgef) {
                continue;
            }

            const auto archetype = mgef->GetArchetype();

            switch (archetype) {
                case ArchetypeID::kWerewolf:
                case ArchetypeID::kVampireLord:
                    return true;
                default:
                    break;
            }
        }

        return false;
    }

    inline const std::vector<RE::SpellItem*>& GetTransformPowers() {
        static std::vector<RE::SpellItem*> s_powers;
        if (!s_powers.empty()) {
            return s_powers;
        }

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            return s_powers;
        }

        auto const& spells = dh->GetFormArray<RE::SpellItem>();

        s_powers.reserve(spells.size());

        for (auto* spell : spells) {
            if (!spell) {
                continue;
            }

            if (spell->GetSpellType() != RE::MagicSystem::SpellType::kPower) {
                continue;
            }

            if (HasTransformArchetype(spell)) {
                s_powers.push_back(spell);
            }
        }

        return s_powers;
    }

    inline bool IsCurrentTransformPower(RE::Actor* actor) {
        if (!actor) {
            return false;
        }

        auto& powers = GetTransformPowers();
        for (auto* power : powers) {
            if (actor->IsCurrentShout(power)) {
                return true;
            }
        }

        return false;
    }

    inline void UpdateUnequipGate() noexcept {
        auto& ist = BowInput::Globals();
        if (ist.allowUnequip.load(std::memory_order_relaxed)) {
            return;
        }
        const std::uint64_t until = ist.allowUnequipReenableMs.load(std::memory_order_relaxed);
        if (until != 0 && NowMs() >= until) {
            ist.allowUnequip.store(true, std::memory_order_relaxed);
        }
    }
}

BowInput::GlobalState& BowInput::Globals() noexcept {
    static GlobalState s;  // NOSONAR
    return s;
}

void BowInput::IntegratedBowInputHandler::HandleNormalMode(RE::PlayerCharacter* player, bool anyNow,
                                                           bool blocked) const {
    auto& st = BowInput::Globals();
    spdlog::info("[IBOW][INPUT][MODE][NORMAL] enter | player={} anyNow={} blocked={} hotkeyDown(before)={}",
                 (void*)player, anyNow, blocked, st.hotkey.hotkeyDown);

    if (anyNow && !st.hotkey.hotkeyDown) {
        st.hotkey.hotkeyDown = true;
        spdlog::info("[IBOW][INPUT][MODE][NORMAL] edge: press | hotkeyDown=true");

        if (!blocked) {
            spdlog::info("[IBOW][INPUT][MODE][NORMAL] calling OnKeyPressed");
            OnKeyPressed(player);
        }
    } else if (!anyNow && st.hotkey.hotkeyDown) {
        st.hotkey.hotkeyDown = false;
        spdlog::info("[IBOW][INPUT][MODE][NORMAL] edge: release | hotkeyDown=false");

        if (!blocked) {
            spdlog::info("[IBOW][INPUT][MODE][NORMAL] calling OnKeyReleased");
            OnKeyReleased();
        }
    }
}

void BowInput::IntegratedBowInputHandler::HandleSmartModePressed(bool blocked) const {
    auto& st = BowInput::Globals();
    spdlog::info(
        "[IBOW][INPUT][MODE][SMART] Pressed enter | blocked={} hotkeyDown(before)={} smartPending(before)={} "
        "smartTimer(before)={:.4f}",
        blocked, st.hotkey.hotkeyDown, st.mode.smartPending, st.mode.smartTimer);

    st.hotkey.hotkeyDown = true;

    if (!blocked) {
        st.mode.smartPending = true;
        st.mode.smartTimer = 0.0f;
        spdlog::info("[IBOW][INPUT][MODE][SMART] Pressed -> hotkeyDown=true smartPending=true smartTimer=0");
    } else {
        st.mode.smartPending = false;
        st.mode.smartTimer = 0.0f;
        spdlog::info("[IBOW][INPUT][MODE][SMART] Pressed (blocked) -> hotkeyDown=true smartPending=false smartTimer=0");
    }
}

void BowInput::IntegratedBowInputHandler::HandleSmartModeReleased(RE::PlayerCharacter* player, bool blocked) const {
    auto& st = BowInput::Globals();

    spdlog::info(
        "[IBOW][INPUT][MODE][SMART] Released enter | player={} blocked={} hotkeyDown(before)={} smartPending={} "
        "smartTimer={:.4f} holdMode={}",
        (void*)player, blocked, st.hotkey.hotkeyDown, st.mode.smartPending, st.mode.smartTimer, st.mode.holdMode);

    st.hotkey.hotkeyDown = false;

    if (!blocked) {
        if (st.mode.smartPending) {
            spdlog::info(
                "[IBOW][INPUT][MODE][SMART] Released: smartPending=true -> treat as 'tap' (forcing holdMode=false, "
                "calling OnKeyPressed)");
            st.mode.smartPending = false;
            st.mode.smartTimer = 0.0f;
            st.mode.holdMode = false;

            OnKeyPressed(player);
        }

        spdlog::info("[IBOW][INPUT][MODE][SMART] calling OnKeyReleased");
        OnKeyReleased();
    } else {
        spdlog::info("[IBOW][INPUT][MODE][SMART] Released blocked=true -> clearing smartPending/timer only");
        st.mode.smartPending = false;
        st.mode.smartTimer = 0.0f;
    }

    spdlog::info(
        "[IBOW][INPUT][MODE][SMART] Released exit | hotkeyDown=false smartPending={} smartTimer={:.4f} holdMode={}",
        st.mode.smartPending, st.mode.smartTimer, st.mode.holdMode);
}

void BowInput::IntegratedBowInputHandler::UpdateHotkeyState(RE::PlayerCharacter* player, bool newKbdCombo,
                                                            bool newPadCombo) const {
    auto& st = BowInput::Globals();

    const bool prevK = st.hotkey.kbdComboDown;
    const bool prevP = st.hotkey.padComboDown;
    const bool prevAny = prevK || prevP;

    st.hotkey.kbdComboDown = newKbdCombo;
    st.hotkey.padComboDown = newPadCombo;

    const bool anyNow = st.hotkey.kbdComboDown || st.hotkey.padComboDown;
    const bool blocked = IsInputBlockedByMenus();

    spdlog::info(
        "[IBOW][INPUT][HOTKEY] UpdateHotkeyState player={} prev(kbd={},pad={},any={}) -> new(kbd={},pad={},any={}) "
        "blocked={} smartMode={} holdMode={} hotkeyDown={}",
        (void*)player, prevK, prevP, prevAny, st.hotkey.kbdComboDown, st.hotkey.padComboDown, anyNow, blocked,
        st.mode.smartMode, st.mode.holdMode, st.hotkey.hotkeyDown);

    if (!st.mode.smartMode) {
        spdlog::info("[IBOW][INPUT][HOTKEY] UpdateHotkeyState -> HandleNormalMode(anyNow={}, blocked={})", anyNow,
                     blocked);
        HandleNormalMode(player, anyNow, blocked);
        return;
    }

    if (anyNow && !prevAny) {
        spdlog::info(
            "[IBOW][INPUT][HOTKEY] SmartMode edge: PRESS (anyNow=true, prevAny=false) -> "
            "HandleSmartModePressed(blocked={})",
            blocked);
        HandleSmartModePressed(blocked);
    } else if (!anyNow && prevAny) {
        spdlog::info(
            "[IBOW][INPUT][HOTKEY] SmartMode edge: RELEASE (anyNow=false, prevAny=true) -> "
            "HandleSmartModeReleased(blocked={})",
            blocked);
        HandleSmartModeReleased(player, blocked);
    }
}

void BowInput::IntegratedBowInputHandler::HandleKeyboardButton(const RE::ButtonEvent* a_event,
                                                               RE::PlayerCharacter* player) const {
    const auto code = static_cast<int>(a_event->idCode);
    auto& st = BowInput::Globals();

    if (const bool captureReq = st.capture.captureRequested.load(std::memory_order_relaxed); captureReq) {
        spdlog::info("[IBOW][INPUT][KBD] CAPTURE mode event code={} down={} up={} value={:.3f} held={:.4f}", code,
                     a_event->IsDown(), a_event->IsUp(), a_event->Value(), a_event->HeldDuration());
        if (a_event->IsDown()) {
            const int encoded = code;
            st.capture.capturedEncoded.store(encoded, std::memory_order_relaxed);
            st.capture.captureRequested.store(false, std::memory_order_relaxed);
            spdlog::info("[IBOW][INPUT][KBD] CAPTURE stored encoded={} (code={}) captureRequested=false", encoded,
                         code);
        }
        return;
    }

    int idx = -1;
    int expected = -1;
    for (int i = 0; i < kMaxComboKeys; ++i) {
        expected = st.hotkeyConfig.bowKeyScanCodes[i];
        if (expected >= 0 && code == expected) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        return;
    }

    spdlog::info(
        "[IBOW][INPUT][KBD] hotkey event matched idx={} code={} down={} up={} value={:.3f} held={:.4f} prevDown={}",
        idx, code, a_event->IsDown(), a_event->IsUp(), a_event->Value(), a_event->HeldDuration(),
        st.hotkey.bowKeyDown[idx]);

    if (a_event->IsDown()) {
        st.hotkey.bowKeyDown[idx] = true;
        spdlog::info("[IBOW][INPUT][KBD] idx={} -> bowKeyDown=true", idx);

    } else if (a_event->IsUp()) {
        st.hotkey.bowKeyDown[idx] = false;
        spdlog::info("[IBOW][INPUT][KBD] idx={} -> bowKeyDown=false", idx);

    } else {
        spdlog::info("[IBOW][INPUT][KBD] idx={} ignored (neither down nor up)", idx);
        return;
    }

    const bool comboK = AreAllActiveKeysDown();
    spdlog::info("[IBOW][INPUT][KBD] comboK={} (after update). Calling UpdateHotkeyState(kbdCombo={}, padCombo={})",
                 comboK, comboK, st.hotkey.padComboDown);
    UpdateHotkeyState(player, comboK, st.hotkey.padComboDown);
}

void BowInput::IntegratedBowInputHandler::ResetExitState() const {
    auto& st = BowInput::Globals();

    spdlog::info(
        "[IBOW][INPUT][EXIT] ResetExitState enter pending={} waitForEquip={} waitEquipTimer={:.4f} delayTimer={:.4f} "
        "delayMs={}",
        st.exit.pending, st.exit.waitForEquip, st.exit.waitEquipTimer, st.exit.delayTimer, st.exit.delayMs);
    st.exit.pending = false;
    st.exit.waitForEquip = false;
    st.exit.waitEquipTimer = 0.0f;
    st.exit.delayTimer = 0.0f;
    st.exit.delayMs = 0;

    spdlog::info(
        "[IBOW][INPUT][EXIT] ResetExitState exit pending=false waitForEquip=false waitEquipTimer=0 delayTimer=0 "
        "delayMs=0");
}

void BowInput::IntegratedBowInputHandler::HandleGamepadButton(const RE::ButtonEvent* a_event,
                                                              RE::PlayerCharacter* player) const {
    const auto code = static_cast<int>(a_event->idCode);
    auto& st = BowInput::Globals();

    if (const bool captureReq = st.capture.captureRequested.load(std::memory_order_relaxed); captureReq) {
        spdlog::info("[IBOW][INPUT][PAD] CAPTURE mode event code={} down={} up={} value={:.3f} held={:.4f}", code,
                     a_event->IsDown(), a_event->IsUp(), a_event->Value(), a_event->HeldDuration());
        if (a_event->IsDown()) {
            const int encoded = -(code + 1);
            st.capture.capturedEncoded.store(encoded, std::memory_order_relaxed);
            st.capture.captureRequested.store(false, std::memory_order_relaxed);
            spdlog::info("[IBOW][INPUT][PAD] CAPTURE stored encoded={} (code={}) captureRequested=false", encoded,
                         code);
        }
        return;
    }

    int idx = -1;
    int expected = -1;
    for (int i = 0; i < kMaxComboKeys; ++i) {
        expected = st.hotkeyConfig.bowPadButtons[i];
        if (expected >= 0 && code == expected) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        return;
    }

    spdlog::info(
        "[IBOW][INPUT][PAD] hotkey event matched idx={} code={} down={} up={} value={:.3f} held={:.4f} prevDown={}",
        idx, code, a_event->IsDown(), a_event->IsUp(), a_event->Value(), a_event->HeldDuration(),
        st.hotkey.bowPadDown[idx]);

    if (a_event->IsDown()) {
        st.hotkey.bowPadDown[idx] = true;
        spdlog::info("[IBOW][INPUT][PAD] idx={} -> bowPadDown=true", idx);

    } else if (a_event->IsUp()) {
        st.hotkey.bowPadDown[idx] = false;
        spdlog::info("[IBOW][INPUT][PAD] idx={} -> bowPadDown=false", idx);

    } else {
        spdlog::info("[IBOW][INPUT][PAD] idx={} ignored (neither down nor up)", idx);
        return;
    }

    const bool comboP = AreAllActivePadButtonsDown();
    spdlog::info("[IBOW][INPUT][PAD] comboP={} (after update). Calling UpdateHotkeyState(kbdCombo={}, padCombo={})",
                 comboP, st.hotkey.kbdComboDown, comboP);

    UpdateHotkeyState(player, st.hotkey.kbdComboDown, comboP);
}

void BowInput::IntegratedBowInputHandler::OnKeyPressed(RE::PlayerCharacter* player) const {
    BowInput::Globals().lastHotkeyPressMs.store(NowMs(), std::memory_order_relaxed);
    auto* equipMgr = RE::ActorEquipManager::GetSingleton();
    auto const& ist = BowInput::Globals();

    spdlog::info(
        "[IBOW][INPUT][PRESS] enter player={} equipMgr={} holdMode={} smartMode={} exit.pending={} "
        "exit.waitForEquip={} bowEquipped={} autoHeld={} sheathReqByPlayer={}",
        (void*)player, (void*)equipMgr, ist.mode.holdMode, ist.mode.smartMode, ist.exit.pending, ist.exit.waitForEquip,
        BowState::IsBowEquipped(), BowState::IsAutoAttackHeld(),
        ist.sheathRequestedByPlayer.load(std::memory_order_relaxed));

    if (ist.mode.holdMode && ist.exit.pending) {
        spdlog::info("[IBOW][INPUT][PRESS] holdMode && exit.pending -> ResetExitState()");
        ResetExitState();
    }

    if (!equipMgr) {
        spdlog::info("[IBOW][INPUT][PRESS] equipMgr=null -> return");
        return;
    }

    auto& st = BowState::Get();
    spdlog::info(
        "[IBOW][INPUT][PRESS] state before ensure: isUsingBow={} isEquipingBow={} chosenBase={} chosenExtra={} "
        "prevR=0x{:08X} prevL=0x{:08X} prevAmmo={}",
        st.isUsingBow, st.isEquipingBow, (void*)st.chosenBow.base, (void*)st.chosenBow.extra, st.prevRightFormID,
        st.prevLeftFormID, (void*)st.prevAmmo);

    if (!BowState::EnsureChosenBowInInventory()) {
        spdlog::info("[IBOW][INPUT][PRESS] EnsureChosenBowInInventory failed -> return");
        return;
    }

    if (RE::TESObjectWEAP const* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr; !bow) {
        spdlog::info("[IBOW][INPUT][PRESS] chosen bow is null after ensure -> return");
        return;
    }

    if (ist.mode.holdMode) {
        spdlog::info("[IBOW][INPUT][PRESS] holdMode path | isUsingBow={} bowEquipped={} autoDrawEnabled={}",
                     st.isUsingBow, BowState::IsBowEquipped(), IsAutoDrawEnabled());
        if (!st.isUsingBow) {
            spdlog::info("[IBOW][INPUT][PRESS] holdMode: !isUsingBow -> EnterBowMode()");
            EnterBowMode(player, equipMgr, st);
            spdlog::info("[IBOW][INPUT][PRESS] holdMode: EnterBowMode returned -> return");

            return;
        }

        if (IsAutoDrawEnabled()) {
            if (BowState::IsBowEquipped()) {
                spdlog::info(
                    "[IBOW][INPUT][PRESS] holdMode: autoDraw ON and bowEquipped=true -> SetAutoAttackHeld(true) + "
                    "StartAutoAttackDraw()");
                BowState::SetAutoAttackHeld(true);
                StartAutoAttackDraw();
            } else {
                spdlog::info(
                    "[IBOW][INPUT][PRESS] holdMode: autoDraw ON but bowEquipped=false -> ScheduleAutoAttackDraw()");

                ScheduleAutoAttackDraw();
            }
        }
    } else {
        spdlog::info("[IBOW][INPUT][PRESS] toggle path | isUsingBow={} bowEquipped={}", st.isUsingBow,
                     BowState::IsBowEquipped());
        if (st.isUsingBow) {
            const bool waitForEquip = !BowState::IsBowEquipped();
            spdlog::info(
                "[IBOW][INPUT][PRESS] toggle: isUsingBow=true -> ScheduleExitBowMode(waitForEquip={}, delayMs=0)",
                waitForEquip);
            ScheduleExitBowMode(waitForEquip, 0);
        } else {
            spdlog::info("[IBOW][INPUT][PRESS] toggle: isUsingBow=false -> EnterBowMode()");
            EnterBowMode(player, equipMgr, st);
        }
    }
    spdlog::info("[IBOW][INPUT][PRESS] exit");
}

void BowInput::IntegratedBowInputHandler::OnKeyReleased() const {
    if (auto const& ist = BowInput::Globals(); !ist.mode.holdMode) {
        spdlog::info("[IBOW][INPUT][RELEASE] not holdMode -> return");
        return;
    }

    if (auto const* equipMgr = RE::ActorEquipManager::GetSingleton(); !equipMgr) {
        spdlog::info("[IBOW][INPUT][RELEASE] equipMgr=null -> return");
        return;
    }

    if (auto const& st = BowState::Get(); !st.isUsingBow) {
        spdlog::info("[IBOW][INPUT][RELEASE] isUsingBow=false -> return");
        return;
    }

    const bool autoDraw = IsAutoDrawEnabled();
    const float delayMsF = GetSheathedDelayMs();
    const auto delayMs = static_cast<int>(delayMsF + 0.5f);

    spdlog::info(
        "[IBOW][INPUT][RELEASE] computed delayMsF={:.3f} -> delayMs={} | autoDraw={} autoHeld={} bowEquipped={}",
        delayMsF, delayMs, autoDraw, BowState::IsAutoAttackHeld(), BowState::IsBowEquipped());

    if (autoDraw && BowState::IsAutoAttackHeld()) {
        spdlog::info("[IBOW][INPUT][RELEASE] autoDraw && autoHeld -> StopAutoAttackDraw() + SetAutoAttackHeld(false)");
        StopAutoAttackDraw();
        BowState::SetAutoAttackHeld(false);
    }

    const bool waitForEquip = !BowState::IsBowEquipped();
    spdlog::info("[IBOW][INPUT][RELEASE] ScheduleExitBowMode(waitForEquip={}, delayMs={})", waitForEquip, delayMs);

    ScheduleExitBowMode(waitForEquip, delayMs);
    spdlog::info("[IBOW][INPUT][RELEASE] exit");
}

bool BowInput::IntegratedBowInputHandler::IsWeaponDrawn(RE::Actor* actor) {
    spdlog::info("[IBOW][INPUT][WEAPON] IsWeaponDrawn enter actor={}", (void*)actor);
    if (!actor) {
        spdlog::info("[IBOW][INPUT][WEAPON] actor=null -> false");
        return false;
    }

    auto const* state = actor->AsActorState();
    spdlog::info("[IBOW][INPUT][WEAPON] actor->AsActorState() state={}", (void*)state);

    if (!state) {
        spdlog::info("[IBOW][INPUT][WEAPON] state=null -> false");
        return false;
    }

    const bool drawn = state->IsWeaponDrawn();
    spdlog::info("[IBOW][INPUT][WEAPON] IsWeaponDrawn result={}", drawn);

    return drawn;
}

void BowInput::IntegratedBowInputHandler::SetWeaponDrawn(RE::Actor* actor, bool drawn) {
    spdlog::info("[IBOW][INPUT][WEAPON] SetWeaponDrawn enter actor={} drawn={}", (void*)actor, drawn);
    if (!actor) {
        spdlog::info("[IBOW][INPUT][WEAPON] actor=null -> return");
        return;
    }

    actor->DrawWeaponMagicHands(drawn);
    spdlog::info("[IBOW][INPUT][WEAPON] DrawWeaponMagicHands({}) called", drawn);
}

RE::ExtraDataList* BowInput::IntegratedBowInputHandler::GetPrimaryExtra(RE::InventoryEntryData* entry) {
    spdlog::info("[IBOW][INPUT][EXTRA] GetPrimaryExtra enter entry={}", (void*)entry);
    if (!entry) {
        spdlog::info("[IBOW][INPUT][EXTRA] entry=null -> return null");
        return nullptr;
    }
    if (!entry->extraLists) {
        spdlog::info("[IBOW][INPUT][EXTRA] entry->extraLists=null -> return null");
        return nullptr;
    }

    for (auto* xList : *entry->extraLists) {
        if (xList) {
            return xList;
        }
    }

    spdlog::info("[IBOW][INPUT][EXTRA] GetPrimaryExtra no non-null xList -> return null");
    return nullptr;
}

void BowInput::IntegratedBowInputHandler::EnterBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                                       BowState::IntegratedBowState& st) {
    auto& ist = BowInput::Globals();
    ist.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);
    ist.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
    spdlog::info(
        "[IBOW][INPUT][ENTER] EnterBowMode enter player={} equipMgr={} holdMode={} autoDraw={} hotkeyDown={} "
        "waitingAutoAfterEquip={} bowEquipped={}",
        (void*)player, (void*)equipMgr, ist.mode.holdMode, IsAutoDrawEnabled(), BowInput::IsHotkeyDown(),
        BowState::IsWaitingAutoAfterEquip(), BowState::IsBowEquipped());

    if (!player || !equipMgr) {
        spdlog::info("[IBOW][INPUT][ENTER] player/equipMgr null -> return (player={} equipMgr={})", (void*)player,
                     (void*)equipMgr);
        return;
    }

    RE::TESAmmo* currentAmmo = player->GetCurrentAmmo();
    spdlog::info("[IBOW][INPUT][ENTER] currentAmmo ptr={} formID=0x{:08X} name='{}'", (void*)currentAmmo,
                 currentAmmo ? currentAmmo->GetFormID() : 0u,
                 (currentAmmo && currentAmmo->GetName()) ? currentAmmo->GetName() : "");

    BowState::SetPrevAmmo(currentAmmo);

    std::vector<BowState::ExtraEquippedItem> wornBefore;
    BowState::CaptureWornArmorSnapshot(wornBefore);
    spdlog::info("[IBOW][INPUT][ENTER] wornBefore snapshot size={}", wornBefore.size());

    RE::TESObjectWEAP* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
    RE::ExtraDataList* bowExtra = st.chosenBow.extra;

    spdlog::info("[IBOW][INPUT][ENTER] chosenBow base={} extra={} bow={} bowFormID=0x{:08X} bowName='{}'",
                 (void*)st.chosenBow.base, (void*)bowExtra, (void*)bow, bow ? bow->GetFormID() : 0u,
                 (bow && bow->GetName()) ? bow->GetName() : "");

    if (!bow) {
        spdlog::info("[IBOW][INPUT][ENTER] bow=null -> return");
        return;
    }

    auto* rightEntry = player->GetEquippedEntryData(false);
    auto* leftEntry = player->GetEquippedEntryData(true);

    auto* baseR = rightEntry ? rightEntry->GetObject() : nullptr;
    auto* extraR = GetPrimaryExtra(rightEntry);

    auto* baseL = leftEntry ? leftEntry->GetObject() : nullptr;
    auto* extraL = GetPrimaryExtra(leftEntry);

    spdlog::info(
        "[IBOW][INPUT][ENTER] equipped BEFORE | rightEntry={} baseR={} extraR={} formID=0x{:08X} name='{}' | "
        "leftEntry={} baseL={} extraL={} formID=0x{:08X} name='{}'",
        (void*)rightEntry, (void*)baseR, (void*)extraR, baseR ? baseR->GetFormID() : 0u,
        (baseR && baseR->GetName()) ? baseR->GetName() : "", (void*)leftEntry, (void*)baseL, (void*)extraL,
        baseL ? baseL->GetFormID() : 0u, (baseL && baseL->GetName()) ? baseL->GetName() : "");

    BowState::SetPrevWeapons(baseR, extraR, baseL, extraL);

    const bool alreadyDrawn = IsWeaponDrawn(player);
    st.wasCombatPosed = alreadyDrawn;

    spdlog::info("[IBOW][INPUT][ENTER] alreadyDrawn={} -> st.wasCombatPosed={}", alreadyDrawn, st.wasCombatPosed);

    spdlog::info("[IBOW][INPUT][ENTER] EquipObject(bow) begin | st.isEquipingBow={} st.isUsingBow={}", st.isEquipingBow,
                 st.isUsingBow);

    st.isEquipingBow = true;
    st.isUsingBow = false;

    spdlog::info("[IBOW][INPUT][ENTER] EquipObject player={} bow={} bowExtra={} formID=0x{:08X}", (void*)player,
                 (void*)bow, (void*)bowExtra, bow->GetFormID());

    equipMgr->EquipObject(player, bow, bowExtra, 1, nullptr, true, true, true, false);

    st.isUsingBow = true;
    st.isEquipingBow = false;

    spdlog::info("[IBOW][INPUT][ENTER] EquipObject(bow) done | st.isEquipingBow={} st.isUsingBow={}", st.isEquipingBow,
                 st.isUsingBow);

    std::vector<BowState::ExtraEquippedItem> wornAfter;
    BowState::CaptureWornArmorSnapshot(wornAfter);
    spdlog::info("[IBOW][INPUT][ENTER] wornAfter snapshot size={}", wornAfter.size());

    auto removed = BowState::DiffArmorSnapshot(wornBefore, wornAfter);
    spdlog::info("[IBOW][INPUT][ENTER] removed armor snapshot diff size={}", removed.size());

    BowState::SetPrevExtraEquipped(std::move(removed));

    if (const bool hiddenEnabled = HiddenItemsPatch::IsEnabled(); hiddenEnabled) {
        spdlog::info("[IBOW][INPUT][ENTER] HiddenItemsPatch enabled={}", hiddenEnabled);
        auto const& ids = HiddenItemsPatch::GetHiddenFormIDs();
        spdlog::info("[IBOW][INPUT][ENTER] HiddenItemsPatch hiddenFormIDs count={}", ids.size());

        BowState::ApplyHiddenItemsPatch(player, equipMgr, ids);
        spdlog::info("[IBOW][INPUT][ENTER] HiddenItemsPatch applied");
    }

    if (auto* preferred = BowState::GetPreferredArrow()) {
        spdlog::info("[IBOW][INPUT][ENTER] preferredArrow ptr={} formID=0x{:08X} name='{}'", (void*)preferred,
                     preferred->GetFormID(), preferred->GetName() ? preferred->GetName() : "");

        auto inv = player->GetInventory([preferred](RE::TESBoundObject& obj) { return &obj == preferred; });
        spdlog::info("[IBOW][INPUT][ENTER] preferredArrow inventory match count={}", inv.size());

        if (!inv.empty()) {
            spdlog::info("[IBOW][INPUT][ENTER] equipping preferredArrow");
            equipMgr->EquipObject(player, preferred, nullptr, 1, nullptr, true, true, true, false);
        }
    }

    if (!alreadyDrawn) {
        spdlog::info("[IBOW][INPUT][ENTER] player was not drawn -> SetWeaponDrawn(true)");
        SetWeaponDrawn(player, true);
    }

    BowState::SetAutoAttackHeld(false);

    const bool shouldWaitAuto = (ist.mode.holdMode && IsAutoDrawEnabled() && BowInput::IsHotkeyDown());

    BowState::SetWaitingAutoAfterEquip(shouldWaitAuto);
    ist.allowUnequip.store(false, std::memory_order_relaxed);
    ist.allowUnequipReenableMs.store(NowMs() + 2000, std::memory_order_relaxed);
    spdlog::info("[IBOW][INPUT][ENTER] SetWaitingAutoAfterEquip({}) | holdMode={} autoDraw={} hotkeyDown={}",
                 shouldWaitAuto, ist.mode.holdMode, IsAutoDrawEnabled(), BowInput::IsHotkeyDown());

    spdlog::info("[IBOW][INPUT][ENTER] EnterBowMode exit");
}

void BowInput::IntegratedBowInputHandler::ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                                      BowState::IntegratedBowState& st) {
    auto& ist = BowInput::Globals();

    spdlog::info(
        "[IBOW][INPUT][EXIT] ExitBowMode enter player={} equipMgr={} wasCombatPosed={} inCombat={} usingBow={} "
        "bowEquipped={} pendingRestoreAfterSheathe={}",
        (void*)player, (void*)equipMgr, st.wasCombatPosed, player ? player->IsInCombat() : false, st.isUsingBow,
        BowState::IsBowEquipped(), ist.pendingRestoreAfterSheathe.load(std::memory_order_relaxed));

    if (!player || !equipMgr) {
        spdlog::info("[IBOW][INPUT][EXIT] player/equipMgr null -> return (player={} equipMgr={})", (void*)player,
                     (void*)equipMgr);
        return;
    }

    if (!st.wasCombatPosed && !player->IsInCombat()) {
        spdlog::info(
            "[IBOW][INPUT][EXIT] not combat posed and not in combat -> SetWeaponDrawn(false) + "
            "pendingRestoreAfterSheathe=true + usingBow=false");
        SetWeaponDrawn(player, false);
        ist.pendingRestoreAfterSheathe.store(true, std::memory_order_relaxed);
        st.isUsingBow = false;
        spdlog::info("[IBOW][INPUT][EXIT] ExitBowMode return early (pendingRestoreAfterSheathe=true)");
        return;
    }
    spdlog::info("[IBOW][INPUT][EXIT] restoring prev weapons/ammo now (clearIsUsingBow=true)");
    BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
    spdlog::info("[IBOW][INPUT][EXIT] ExitBowMode exit after RestorePrevWeaponsAndAmmo");
}

void BowInput::IntegratedBowInputHandler::ScheduleExitBowMode(bool waitForEquip, int delayMs) {
    auto& st = BowInput::Globals();

    spdlog::info(
        "[IBOW][INPUT][EXIT_SCHED] ScheduleExitBowMode enter waitForEquip={} delayMs={} | prev pending={} "
        "waitForEquip={} delayMs={} waitEquipTimer={:.4f} delayTimer={:.4f}",
        waitForEquip, delayMs, st.exit.pending, st.exit.waitForEquip, st.exit.delayMs, st.exit.waitEquipTimer,
        st.exit.delayTimer);

    st.exit.pending = true;
    st.exit.waitForEquip = waitForEquip;
    st.exit.waitEquipTimer = 0.0f;
    st.exit.delayTimer = 0.0f;
    st.exit.delayMs = delayMs;

    spdlog::info(
        "[IBOW][INPUT][EXIT_SCHED] ScheduleExitBowMode set | pending=true waitForEquip={} delayMs={} timers reset",
        st.exit.waitForEquip, st.exit.delayMs);
}

void BowInput::IntegratedBowInputHandler::UpdateSmartMode(RE::PlayerCharacter* player, float dt) const {
    auto& st = BowInput::Globals();

    if (!st.mode.smartMode || !st.mode.smartPending || !st.hotkey.hotkeyDown) {
        return;
    }

    spdlog::info(
        "[IBOW][INPUT][SMART] tick enter player={} dt={:.4f} smartTimer(before)={:.4f} threshold={:.4f} hotkeyDown={} "
        "smartPending={} holdMode={}",
        (void*)player, dt, st.mode.smartTimer, kSmartClickThreshold, st.hotkey.hotkeyDown, st.mode.smartPending,
        st.mode.holdMode);

    st.mode.smartTimer += dt;
    spdlog::info("[IBOW][INPUT][SMART] tick after add smartTimer={:.4f}", st.mode.smartTimer);

    if (st.mode.smartTimer >= kSmartClickThreshold) {
        spdlog::info(
            "[IBOW][INPUT][SMART] threshold reached -> commit HOLD mode (smartPending=false, timer=0, holdMode=true)");

        st.mode.smartPending = false;
        st.mode.smartTimer = 0.0f;
        st.mode.holdMode = true;

        const bool blocked = IsInputBlockedByMenus();
        spdlog::info("[IBOW][INPUT][SMART] blockedByMenus={} -> {}", blocked,
                     blocked ? "skip OnKeyPressed" : "call OnKeyPressed");

        if (!blocked) {
            OnKeyPressed(player);
        }
    }
}

void BowInput::IntegratedBowInputHandler::UpdateExitEquipWait(float dt) const {
    auto& st = BowInput::Globals();

    if (const bool bowEquipped = BowState::IsBowEquipped(); !st.exit.waitForEquip || bowEquipped) {
        return;
    }

    spdlog::info(
        "[IBOW][INPUT][EXIT_WAIT] tick enter dt={:.4f} waitEquipTimer(before)={:.4f} waitEquipMax={:.4f} "
        "bowEquipped={}",
        dt, st.exit.waitEquipTimer, st.exit.waitEquipMax, BowState::IsBowEquipped());

    st.exit.waitEquipTimer += dt;

    spdlog::info("[IBOW][INPUT][EXIT_WAIT] tick after add waitEquipTimer={:.4f}", st.exit.waitEquipTimer);

    if (st.exit.waitEquipTimer >= st.exit.waitEquipMax) {
        spdlog::info("[IBOW][INPUT][EXIT_WAIT] timeout reached -> waitForEquip=false, timer reset");

        st.exit.waitForEquip = false;
        st.exit.waitEquipTimer = 0.0f;
    }
}

bool BowInput::IntegratedBowInputHandler::IsExitDelayReady(float dt) const {
    auto& st = BowInput::Globals();

    if (st.exit.delayMs <= 0) {
        spdlog::info("[IBOW][INPUT][EXIT_DELAY] ready immediately (delayMs<=0) delayMs={}", st.exit.delayMs);

        return true;
    }

    st.exit.delayTimer += dt * 1000.0f;

    const auto target = static_cast<float>(st.exit.delayMs);
    const bool ready = st.exit.delayTimer >= target;

    spdlog::info("[IBOW][INPUT][EXIT_DELAY] tick dt={:.4f}s timer {:.2f} ms target={:.2f} ready={}", dt,
                 st.exit.delayTimer, target, ready);

    return ready;
}

void BowInput::IntegratedBowInputHandler::CompleteExit() const {
    spdlog::info("[IBOW][INPUT][EXIT] CompleteExit enter");
    if (auto* equipMgr = RE::ActorEquipManager::GetSingleton(); equipMgr) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        spdlog::info("[IBOW][INPUT][EXIT] equipMgr={} player={}", (void*)equipMgr, (void*)player);

        if (player) {
            auto& bowSt = BowState::Get();
            spdlog::info("[IBOW][INPUT][EXIT] calling ExitBowMode | usingBow={} wasCombatPosed={} bowEquipped={}",
                         bowSt.isUsingBow, bowSt.wasCombatPosed, BowState::IsBowEquipped());

            ExitBowMode(player, equipMgr, bowSt);
            spdlog::info("[IBOW][INPUT][EXIT] ExitBowMode returned");
        }
    }

    spdlog::info("[IBOW][INPUT][EXIT] ResetExitState()");
    ResetExitState();
    spdlog::info("[IBOW][INPUT][EXIT] CompleteExit exit");
}

void BowInput::IntegratedBowInputHandler::UpdateExitPending(float dt) const {
    auto const& st = BowInput::Globals();
    if (!st.exit.pending) {
        return;
    }

    const bool bowEquipped = BowState::IsBowEquipped();

    spdlog::info(
        "[IBOW][INPUT][EXIT] UpdateExitPending tick dt={:.4f} pending=true waitForEquip={} bowEquipped={} delayMs={} "
        "delayTimer={:.2f} waitEquipTimer={:.4f}",
        dt, st.exit.waitForEquip, bowEquipped, st.exit.delayMs, st.exit.delayTimer, st.exit.waitEquipTimer);

    if (st.exit.waitForEquip && !bowEquipped) {
        spdlog::info("[IBOW][INPUT][EXIT] branch: waiting for equip...");

        UpdateExitEquipWait(dt);
    } else if (IsExitDelayReady(dt)) {
        spdlog::info("[IBOW][INPUT][EXIT] branch: delay ready -> CompleteExit()");

        CompleteExit();
    }
}

void BowInput::IntegratedBowInputHandler::ProcessButtonEvent(const RE::ButtonEvent* button,
                                                             RE::PlayerCharacter* player) const {
    if (!button->IsDown() && !button->IsUp()) {
        return;
    }

    auto& ist = BowInput::Globals();
    const auto& ue = button->QUserEvent();
    const auto dev = button->GetDevice();

    spdlog::info(
        "[IBOW][INPUT][BTN] event player={} dev={} ue='{}' down={} up={} value={:.3f} held={:.4f} sheathReqByPlayer={}",
        (void*)player, static_cast<int>(dev), ue, button->IsDown(), button->IsUp(), button->Value(),
        button->HeldDuration(), ist.sheathRequestedByPlayer.load(std::memory_order_relaxed));

    if (ue == "Shout"sv && button->IsDown() && player && IsCurrentTransformPower(player)) {
        IntegratedBowInputHandler::ForceImmediateExit();
    }

    if (ue == "Ready Weapon"sv && button->IsDown()) {
        auto const& st = BowState::Get();
        const std::uint64_t now = NowMs();
        const std::uint64_t lastHotkey = ist.lastHotkeyPressMs.load(std::memory_order_relaxed);
        const bool nearHotkey = (lastHotkey != 0) && (now - lastHotkey) < 250;
        if (dev != RE::INPUT_DEVICE::kKeyboard && dev != RE::INPUT_DEVICE::kGamepad) {
            return;
        }
        if (st.isUsingBow && !ist.hotkey.hotkeyDown && !nearHotkey && BowState::IsBowEquipped() && !st.isEquipingBow) {
            ist.sheathRequestedByPlayer.store(true, std::memory_order_relaxed);
            spdlog::info("[IBOW][INPUT][BTN] Ready Weapon DOWN -> sheathRequestedByPlayer=true");
        }
    }

    if (dev == RE::INPUT_DEVICE::kKeyboard) {
        HandleKeyboardButton(button, player);
    } else if (dev == RE::INPUT_DEVICE::kGamepad) {
        HandleGamepadButton(button, player);
    }
}

void BowInput::IntegratedBowInputHandler::ProcessInputEvents(RE::InputEvent* const* a_events,
                                                             RE::PlayerCharacter* player) const {
    if (!a_events) {
        spdlog::info("[IBOW][INPUT][PROC_EVENTS] a_events=null -> return");
        return;
    }

    for (auto e = *a_events; e; e = e->next) {
        if (auto button = e->AsButtonEvent()) {
            ProcessButtonEvent(button, player);
        }
    }

    spdlog::info("[IBOW][INPUT][PROC_EVENTS] processed");
}

float BowInput::IntegratedBowInputHandler::CalculateDeltaTime() const {
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();

    const auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    if (dt < 0.0f || dt > 0.5f) {
        spdlog::info("[IBOW][INPUT][DT] abnormal dt={:.6f}s -> clamped to 0", dt);

        dt = 0.0f;
    }

    return dt;
}

RE::BSEventNotifyControl BowInput::IntegratedBowInputHandler::ProcessEvent(RE::InputEvent* const* a_events,
                                                                           RE::BSTEventSource<RE::InputEvent*>*) {
    if (!a_events) {
        spdlog::info("[IBOW][INPUT][SINK] ProcessEvent a_events=null -> Continue");
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::info("[IBOW][INPUT][SINK] ProcessEvent player=null -> Continue");

        return RE::BSEventNotifyControl::kContinue;
    }
    auto& ist = BowInput::Globals();
    float dt = CalculateDeltaTime();

    spdlog::info(
        "[IBOW][INPUT][SINK] tick enter dt={:.6f} player={} | smartMode={} smartPending={} smartTimer={:.4f} "
        "hotkeyDown={} holdMode={} | exit.pending={} exit.waitForEquip={} exit.delayMs={} exit.delayTimer={:.2f} "
        "waitEquipTimer={:.4f} | attackHold.active={} heldSecs={:.4f}",
        dt, (void*)player, ist.mode.smartMode, ist.mode.smartPending, ist.mode.smartTimer, ist.hotkey.hotkeyDown,
        ist.mode.holdMode, ist.exit.pending, ist.exit.waitForEquip, ist.exit.delayMs, ist.exit.delayTimer,
        ist.exit.waitEquipTimer, ist.attackHold.active.load(std::memory_order_relaxed),
        ist.attackHold.secs.load(std::memory_order_relaxed));

    UpdateUnequipGate();
    UpdateSmartMode(player, dt);
    UpdateExitPending(dt);
    ProcessInputEvents(a_events, player);
    PumpAttackHold(dt);

    spdlog::info(
        "[IBOW][INPUT][SINK] tick exit | smartPending={} holdMode={} exit.pending={} attackHold.active={} "
        "heldSecs={:.4f}",
        ist.mode.smartPending, ist.mode.holdMode, ist.exit.pending,
        ist.attackHold.active.load(std::memory_order_relaxed), ist.attackHold.secs.load(std::memory_order_relaxed));

    return RE::BSEventNotifyControl::kContinue;
}

void BowInput::IntegratedBowInputHandler::ScheduleAutoAttackDraw() {
    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !player) {
        spdlog::info("[IBOW][INPUT][AUTO_SCHED] player=null -> return");
        return;
    }

    if (auto const& bowSt = BowState::Get(); !bowSt.isUsingBow) {
        spdlog::info("[IBOW][INPUT][AUTO_SCHED] isUsingBow=false -> return");
        return;
    }

    spdlog::info("[IBOW][INPUT][AUTO_SCHED] SetWaitingAutoAfterEquip(true) | bowEquipped={} waitingWas={}",
                 BowState::IsBowEquipped(), BowState::IsWaitingAutoAfterEquip());

    BowState::SetWaitingAutoAfterEquip(true);
    spdlog::info("[IBOW][INPUT][AUTO_SCHED] waitingNow={}", BowState::IsWaitingAutoAfterEquip());
}

BowInput::IntegratedBowInputHandler* BowInput::IntegratedBowInputHandler::GetSingleton() {
    static IntegratedBowInputHandler instance;
    auto* ptr = std::addressof(instance);
    spdlog::info("[IBOW][INPUT][SINGLETON] GetSingleton -> {}", (void*)ptr);

    return ptr;
}

void BowInput::RegisterInputHandler() {
    if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
        auto* sink = IntegratedBowInputHandler::GetSingleton();
        spdlog::info("[IBOW][INPUT][REGISTER] BSInputDeviceManager={} AddEventSink sink={}", (void*)mgr, (void*)sink);

        mgr->AddEventSink(sink);
        spdlog::info("[IBOW][INPUT][REGISTER] AddEventSink done");
    }
}

void BowInput::SetMode(int mode) {
    auto& st = BowInput::Globals();

    spdlog::info(
        "[IBOW][INPUT][MODE] SetMode enter mode={} | BEFORE holdMode={} smartMode={} smartPending={} smartTimer={:.4f} "
        "hotkeyDown={}",
        mode, st.mode.holdMode, st.mode.smartMode, st.mode.smartPending, st.mode.smartTimer, st.hotkey.hotkeyDown);

    st.hotkey.hotkeyDown = false;
    st.mode.smartPending = false;
    st.mode.smartTimer = 0.0f;

    switch (mode) {
        case 0:
            st.mode.holdMode = true;
            st.mode.smartMode = false;
            break;
        case 1:
            st.mode.holdMode = false;
            st.mode.smartMode = false;
            break;
        case 2:
        default:
            st.mode.holdMode = false;
            st.mode.smartMode = true;
            break;
    }

    spdlog::info(
        "[IBOW][INPUT][MODE] SetMode exit | AFTER holdMode={} smartMode={} smartPending={} smartTimer={:.4f} "
        "hotkeyDown={}",
        st.mode.holdMode, st.mode.smartMode, st.mode.smartPending, st.mode.smartTimer, st.hotkey.hotkeyDown);
}

void BowInput::SetKeyScanCodes(int k1, int k2, int k3) {
    auto& st = BowInput::Globals();
    std::array<int, kMaxComboKeys> vals{k1, k2, k3};

    spdlog::info(
        "[IBOW][INPUT][CFG] SetKeyScanCodes enter k1={} k2={} k3={} | prev=[{}, {}, {}] kbdComboDown={} hotkeyDown={}",
        k1, k2, k3, st.hotkeyConfig.bowKeyScanCodes[0], st.hotkeyConfig.bowKeyScanCodes[1],
        st.hotkeyConfig.bowKeyScanCodes[2], st.hotkey.kbdComboDown, st.hotkey.hotkeyDown);

    for (int i = 0; i < kMaxComboKeys; ++i) {
        st.hotkeyConfig.bowKeyScanCodes[i] = vals[i];
        st.hotkey.bowKeyDown[i] = false;
        spdlog::info("[IBOW][INPUT][CFG] SetKeyScanCodes slot[{}] scanCode={} -> bowKeyDown=false", i, vals[i]);
    }

    st.hotkey.kbdComboDown = false;
    st.hotkey.hotkeyDown = false;
    spdlog::info("[IBOW][INPUT][CFG] SetKeyScanCodes exit | new=[{}, {}, {}] kbdComboDown=false hotkeyDown=false",
                 st.hotkeyConfig.bowKeyScanCodes[0], st.hotkeyConfig.bowKeyScanCodes[1],
                 st.hotkeyConfig.bowKeyScanCodes[2]);
}

void BowInput::SetGamepadButtons(int b1, int b2, int b3) {
    auto& st = BowInput::Globals();
    std::array<int, kMaxComboKeys> vals{b1, b2, b3};

    spdlog::info(
        "[IBOW][INPUT][CFG] SetGamepadButtons enter b1={} b2={} b3={} | prev=[{}, {}, {}] padComboDown={} "
        "hotkeyDown={}",
        b1, b2, b3, st.hotkeyConfig.bowPadButtons[0], st.hotkeyConfig.bowPadButtons[1],
        st.hotkeyConfig.bowPadButtons[2], st.hotkey.padComboDown, st.hotkey.hotkeyDown);

    for (int i = 0; i < kMaxComboKeys; ++i) {
        st.hotkeyConfig.bowPadButtons[i] = vals[i];
        st.hotkey.bowPadDown[i] = false;
        spdlog::info("[IBOW][INPUT][CFG] SetGamepadButtons slot[{}] button={} -> bowPadDown=false", i, vals[i]);
    }

    st.hotkey.padComboDown = false;
    st.hotkey.hotkeyDown = false;
    spdlog::info("[IBOW][INPUT][CFG] SetGamepadButtons exit | new=[{}, {}, {}] padComboDown=false hotkeyDown=false",
                 st.hotkeyConfig.bowPadButtons[0], st.hotkeyConfig.bowPadButtons[1], st.hotkeyConfig.bowPadButtons[2]);
}

void BowInput::RequestGamepadCapture() {
    auto& st = BowInput::Globals();
    spdlog::info("[IBOW][INPUT][CAPTURE] RequestGamepadCapture enter | captureRequested={} capturedEncoded={}",
                 st.capture.captureRequested.load(std::memory_order_relaxed),
                 st.capture.capturedEncoded.load(std::memory_order_relaxed));

    st.capture.captureRequested.store(true, std::memory_order_relaxed);
    st.capture.capturedEncoded.store(-1, std::memory_order_relaxed);
    spdlog::info("[IBOW][INPUT][CAPTURE] RequestGamepadCapture exit | captureRequested=true capturedEncoded=-1");
}

int BowInput::PollCapturedGamepadButton() {
    auto& st = BowInput::Globals();

    if (const int v = st.capture.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        spdlog::info("[IBOW][INPUT][CAPTURE] PollCapturedGamepadButton got capturedEncoded={} -> resetting to -1", v);

        st.capture.capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }

    return -1;
}

bool BowInput::IsHotkeyDown() {
    auto const& st = BowInput::Globals();
    const bool v = st.hotkey.hotkeyDown;

    if (static bool last = false; v != last) {
        spdlog::info("[IBOW][INPUT][HOTKEY] IsHotkeyDown changed {} -> {}", last, v);

        last = v;
    }

    return v;
}

void BowInput::HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!ev || !ev->holder) {
        return;
    }

    auto* actor = ev->holder->As<RE::Actor>();
    auto const& ist = BowInput::Globals();
    auto const* player = RE::PlayerCharacter::GetSingleton();

    if (!actor || actor != player) {
        return;
    }

    std::string_view tag{ev->tag.c_str(), ev->tag.size()};
    std::string_view payload{ev->payload.c_str(), ev->payload.size()};

    spdlog::info(
        "[IBOW][ANIM] event player={} tag='{}' payload='{}' | usingBow={} equipingBow={} bowEquipped={} "
        "waitingAutoAfterEquip={} autoHeld={} holdMode={} hotkeyDown={}",
        (void*)player, tag, payload, BowState::IsUsingBow(), BowState::IsEquipingBow(), BowState::IsBowEquipped(),
        BowState::IsWaitingAutoAfterEquip(), BowState::IsAutoAttackHeld(), ist.mode.holdMode, BowInput::IsHotkeyDown());

    if (tag == "EnableBumper"sv) {
        const bool prevEquipped = BowState::IsBowEquipped();
        BowState::SetBowEquipped(true);

        const bool waiting = BowState::IsWaitingAutoAfterEquip();
        const bool usingBow = BowState::IsUsingBow();
        const bool autoDraw = IsAutoDrawEnabled();
        const bool hotkeyDown = BowInput::IsHotkeyDown();
        const bool holdMode = ist.mode.holdMode;

        spdlog::info(
            "[IBOW][ANIM][EnableBumper] bowEquipped {}->{} | waiting={} usingBow={} holdMode={} autoDraw={} "
            "hotkeyDown={} autoHeld={}",
            prevEquipped, BowState::IsBowEquipped(), waiting, usingBow, holdMode, autoDraw, hotkeyDown,
            BowState::IsAutoAttackHeld());

        if (waiting && usingBow && holdMode && autoDraw && hotkeyDown) {
            spdlog::info(
                "[IBOW][ANIM][EnableBumper] conditions met -> waitingAutoAfterEquip=false; ensure auto attack draw");

            BowState::SetWaitingAutoAfterEquip(false);

            if (!BowState::IsAutoAttackHeld()) {
                spdlog::info(
                    "[IBOW][ANIM][EnableBumper] autoHeld=false -> SetAutoAttackHeld(true) + StartAutoAttackDraw()");

                BowState::SetAutoAttackHeld(true);
                StartAutoAttackDraw();
            }
        }
    }

    else if (tag == "WeaponSheathe"sv) {
        spdlog::info("[IBOW][ANIM][WeaponSheathe] calling weaponSheatheHelper()");

        weaponSheatheHelper();
        spdlog::info(
            "[IBOW][ANIM][WeaponSheathe] weaponSheatheHelper returned | usingBow={} bowEquipped={} "
            "pendingRestoreAfterSheathe={}",
            BowState::IsUsingBow(), BowState::IsBowEquipped(),
            ist.pendingRestoreAfterSheathe.load(std::memory_order_relaxed));

    }

    else if (tag == "bowReset"sv) {
        const bool autoDraw = IsAutoDrawEnabled();
        const bool autoHeld = BowState::IsAutoAttackHeld();
        const bool hotkeyDown = BowInput::IsHotkeyDown();
        spdlog::info("[IBOW][ANIM][bowReset] autoDraw={} autoHeld={} hotkeyDown={}", autoDraw, autoHeld, hotkeyDown);

        if (autoDraw && autoHeld && hotkeyDown) {
            spdlog::info(
                "[IBOW][ANIM][bowReset] restarting auto draw: StopAutoAttackDraw(); SetAutoAttackHeld(true); "
                "StartAutoAttackDraw()");
            StopAutoAttackDraw();

            BowState::SetAutoAttackHeld(true);
            StartAutoAttackDraw();
        }
    }

    return;
}

void BowInput::IntegratedBowInputHandler::ForceImmediateExit() {
    auto& ist = BowInput::Globals();
    auto& st = BowState::Get();

    spdlog::info(
        "[IBOW][FORCE_EXIT] state before | usingBow={} equipingBow={} wasCombatPosed={} bowEquipped={} "
        "waitingAutoAfterEquip={} autoHeld={} pendingRestoreAfterSheathe={} sheathReqByPlayer={} attackHold.active={} "
        "attackHold.secs={:.4f}",
        st.isUsingBow, st.isEquipingBow, st.wasCombatPosed, BowState::IsBowEquipped(),
        BowState::IsWaitingAutoAfterEquip(), BowState::IsAutoAttackHeld(),
        ist.pendingRestoreAfterSheathe.load(std::memory_order_relaxed),
        ist.sheathRequestedByPlayer.load(std::memory_order_relaxed),
        ist.attackHold.active.load(std::memory_order_relaxed), ist.attackHold.secs.load(std::memory_order_relaxed));

    st.chosenBow.extra = nullptr;

    st.prevRight.base = nullptr;
    st.prevRight.extra = nullptr;
    st.prevLeft.base = nullptr;
    st.prevLeft.extra = nullptr;
    st.prevRightFormID = 0;
    st.prevLeftFormID = 0;

    st.wasCombatPosed = false;
    st.isEquipingBow = false;
    st.isUsingBow = false;

    BowState::ClearPrevExtraEquipped();
    BowState::ClearPrevAmmo();

    BowState::SetAutoAttackHeld(false);
    BowState::SetWaitingAutoAfterEquip(false);
    BowState::SetBowEquipped(false);

    ist.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
    ist.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);

    ist.attackHold.active.store(false, std::memory_order_relaxed);
    ist.attackHold.secs.store(0.0f, std::memory_order_relaxed);

    ist.exit.pending = false;
    ist.exit.waitForEquip = false;
    ist.exit.waitEquipTimer = 0.0f;
    ist.exit.delayTimer = 0.0f;
    ist.exit.delayMs = 0;

    ist.hotkey.bowKeyDown.fill(false);
    ist.hotkey.bowPadDown.fill(false);
    ist.hotkey.kbdComboDown = false;
    ist.hotkey.padComboDown = false;
    ist.hotkey.hotkeyDown = false;

    ist.mode.smartPending = false;
    ist.mode.smartTimer = 0.0f;
}

bool BowInput::IsUnequipAllowed() noexcept { return BowInput::Globals().allowUnequip.load(std::memory_order_relaxed); }

void BowInput::BlockUnequipForMs(std::uint64_t ms) noexcept {
    auto& st = BowInput::Globals();
    st.allowUnequip.store(false, std::memory_order_relaxed);
    st.allowUnequipReenableMs.store(NowMs() + ms, std::memory_order_relaxed);
}

void BowInput::ForceAllowUnequip() noexcept {
    auto& st = BowInput::Globals();
    st.allowUnequip.store(true, std::memory_order_relaxed);
    st.allowUnequipReenableMs.store(0, std::memory_order_relaxed);
}