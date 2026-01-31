
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
#include "patchs/SkipEquipController.h"

using namespace std::literals;

namespace {
    constexpr float kSmartClickThreshold = 0.18f;
    constexpr std::uint64_t kFakeEnableBumperDelayMs = 150;
    constexpr std::uint64_t kDisableSkipEquipDelayMs = 500;
    constexpr std::uint64_t kPostExitAttackDownDelayMs = 40;
    constexpr std::uint64_t kPostExitAttackTapMs = 60;
    constexpr int kMaxCode = 400;
    constexpr float kExclusiveConfirmDelaySec = 0.10f;

    constexpr int kDIK_W = 0x11;
    constexpr int kDIK_A = 0x1E;
    constexpr int kDIK_S = 0x1F;
    constexpr int kDIK_D = 0x20;

    enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };

    std::array<std::atomic_bool, kMaxCode> g_kbDown{};  // NOSONAR
    std::array<std::atomic_bool, kMaxCode> g_gpDown{};  // NOSONAR

    inline bool IsAllowedExtra_Keyboard_MoveOrCamera(int code) {
        switch (code) {
            case kDIK_W:
            case kDIK_A:
            case kDIK_S:
            case kDIK_D:
                return true;
            default:
                return false;
        }
    }

    inline bool IsAllowedExtra_Gamepad_MoveOrCamera(int) { return false; }

    bool AnyEnabled(const std::array<int, BowInput::kMaxComboKeys>& a) {
        return std::ranges::any_of(a, [](int v) { return v != -1; });
    }

    template <class DownArr>
    bool ComboDown(const std::array<int, BowInput::kMaxComboKeys>& combo, const DownArr& down) {
        if (!AnyEnabled(combo)) {
            return false;
        }

        for (int code : combo) {
            if (code == -1) {
                continue;
            }
            if (code < 0 || code >= kMaxCode) {
                return false;
            }
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) {
                return false;
            }
        }

        return true;
    }

    inline bool ComboContains(const std::array<int, BowInput::kMaxComboKeys>& combo, int code) {
        return std::ranges::contains(combo, code);
    }

    template <class DownArr, class AllowedFn>
    bool ComboExclusiveNow(const std::array<int, BowInput::kMaxComboKeys>& combo, const DownArr& down,
                           AllowedFn isAllowedExtra) {
        if (!ComboDown(combo, down)) {
            return false;
        }

        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) {
                continue;
            }
            if (ComboContains(combo, code)) {
                continue;
            }
            if (isAllowedExtra(code)) {
                continue;
            }
            return false;
        }

        return true;
    }

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

        ist.attackHold.active.store(true, std::memory_order_relaxed);
        ist.attackHold.secs.store(0.0f, std::memory_order_relaxed);
        ist.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);

        auto* ev = MakeAttackButtonEvent(1.0f, 0.0f);

        DispatchAttackButtonEvent(ev);
    }

    void StopAutoAttackDraw() {
        auto& ist = BowInput::Globals();
        using namespace BowState::detail;

        float held = ist.attackHold.secs.load(std::memory_order_relaxed);
        if (held <= 0.0f) {
            held = 0.1f;
        }

        auto* ev = MakeAttackButtonEvent(0.0f, held);

        DispatchAttackButtonEvent(ev);

        ist.attackHold.active.store(false, std::memory_order_relaxed);
        ist.attackHold.secs.store(0.0f, std::memory_order_relaxed);
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

        auto& st = BowState::Get();

        const bool pendingRestore = ist.pendingRestoreAfterSheathe.load(std::memory_order_relaxed);
        const bool sheathReqPeek = ist.sheathRequestedByPlayer.load(std::memory_order_relaxed);

        if (!pendingRestore && !sheathReqPeek) {
            return;
        }

        if (!player) {
            return;
        }

        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        if (!equipMgr) {
            ist.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
            st.isUsingBow = false;
            BowState::ClearPrevWeapons();
            BowState::ClearPrevExtraEquipped();
            BowState::ClearPrevAmmo();
            BowState::SetBowEquipped(false);

            return;
        }

        if (pendingRestore) {
            ist.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);
            BowState::SetBowEquipped(false);

            BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);

            return;
        }

        ist.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);

        if (sheathReqPeek && st.isUsingBow) {
            BowState::SetBowEquipped(false);

            BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);

            return;
        }
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

    inline bool IsInHoldAutoExitDelay() noexcept {
        auto const& ist = BowInput::Globals();

        if (!ist.mode.holdMode) {
            return false;
        }

        if (!ist.exit.pending || ist.exit.delayMs <= 0) {
            return false;
        }

        if (ist.hotkey.hotkeyDown) {
            return false;
        }

        if (!IsAutoDrawEnabled()) {
            return false;
        }

        const auto target = static_cast<float>(ist.exit.delayMs);
        return ist.exit.delayTimer < target;
    }

    inline bool IsAttackEvent(std::string_view ue) noexcept {
        return ue == "Left Attack Attack/Block"sv || ue == "Right Attack/Block"sv;
    }

    inline void ResetPostExitAttackState(BowInput::GlobalState& st) noexcept {
        st.postExitAttackPending = false;
        st.postExitAttackStage = 0;
        st.postExitAttackDownAtMs = 0;
        st.postExitAttackUpAtMs = 0;
    }

    inline void PumpPostExitAttackTap() {
        auto& ist = BowInput::Globals();
        if (!ist.postExitAttackPending) {
            return;
        }

        const std::uint64_t now = NowMs();

        if (ist.postExitAttackStage == 0) {
            if (ist.postExitAttackDownAtMs != 0 && now >= ist.postExitAttackDownAtMs) {
                auto* ev = BowState::detail::MakeAttackButtonEvent(1.0f, 0.0f);
                BowState::detail::DispatchAttackButtonEvent(ev);

                ist.postExitAttackStage = 1;
            }
            return;
        }

        if (ist.postExitAttackStage == 1 && (ist.postExitAttackUpAtMs != 0 && now >= ist.postExitAttackUpAtMs)) {
            auto* ev = BowState::detail::MakeAttackButtonEvent(0.0f, 0.1f);
            BowState::detail::DispatchAttackButtonEvent(ev);

            ResetPostExitAttackState(ist);
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

    if (anyNow && !st.hotkey.hotkeyDown) {
        st.hotkey.hotkeyDown = true;

        if (!blocked) {
            OnKeyPressed(player);
        }
    } else if (!anyNow && st.hotkey.hotkeyDown) {
        st.hotkey.hotkeyDown = false;

        if (!blocked) {
            OnKeyReleased();
        }
    }
}

void BowInput::IntegratedBowInputHandler::HandleSmartModePressed(bool blocked) const {
    auto& st = BowInput::Globals();

    st.hotkey.hotkeyDown = true;

    if (!blocked) {
        st.mode.smartPending = true;
        st.mode.smartTimer = 0.0f;

    } else {
        st.mode.smartPending = false;
        st.mode.smartTimer = 0.0f;
    }
}

void BowInput::IntegratedBowInputHandler::HandleSmartModeReleased(RE::PlayerCharacter* player, bool blocked) const {
    auto& st = BowInput::Globals();

    st.hotkey.hotkeyDown = false;

    if (!blocked) {
        if (st.mode.smartPending) {
            st.mode.smartPending = false;
            st.mode.smartTimer = 0.0f;
            st.mode.holdMode = false;

            OnKeyPressed(player);
        }

        OnKeyReleased();
    } else {
        st.mode.smartPending = false;
        st.mode.smartTimer = 0.0f;
    }
}

void BowInput::IntegratedBowInputHandler::HandleKeyboardButton(const RE::ButtonEvent* a_event,
                                                               RE::PlayerCharacter*) const {
    const auto code = static_cast<int>(a_event->idCode);
    auto& st = BowInput::Globals();

    if (const bool captureReq = st.capture.captureRequested.load(std::memory_order_relaxed); captureReq) {
        if (a_event->IsDown()) {
            const int encoded = code;
            st.capture.capturedEncoded.store(encoded, std::memory_order_relaxed);
            st.capture.captureRequested.store(false, std::memory_order_relaxed);
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

    if (a_event->IsDown()) {
        st.hotkey.bowKeyDown[idx] = true;

    } else if (a_event->IsUp()) {
        st.hotkey.bowKeyDown[idx] = false;

    } else {
        return;
    }

    st.hotkey.kbdComboDown = AreAllActiveKeysDown();
}

void BowInput::IntegratedBowInputHandler::ResetExitState() const {
    auto& st = BowInput::Globals();

    st.exit.pending = false;
    st.exit.waitForEquip = false;
    st.exit.waitEquipTimer = 0.0f;
    st.exit.delayTimer = 0.0f;
    st.exit.delayMs = 0;
}

void BowInput::IntegratedBowInputHandler::HandleGamepadButton(const RE::ButtonEvent* a_event,
                                                              RE::PlayerCharacter*) const {
    const auto code = static_cast<int>(a_event->idCode);
    auto& st = BowInput::Globals();

    if (const bool captureReq = st.capture.captureRequested.load(std::memory_order_relaxed); captureReq) {
        if (a_event->IsDown()) {
            const int encoded = -(code + 1);
            st.capture.capturedEncoded.store(encoded, std::memory_order_relaxed);
            st.capture.captureRequested.store(false, std::memory_order_relaxed);
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

    if (a_event->IsDown()) {
        st.hotkey.bowPadDown[idx] = true;

    } else if (a_event->IsUp()) {
        st.hotkey.bowPadDown[idx] = false;

    } else {
        return;
    }

    st.hotkey.padComboDown = AreAllActivePadButtonsDown();
}

void BowInput::IntegratedBowInputHandler::OnKeyPressed(RE::PlayerCharacter* player) const {
    BowInput::Globals().lastHotkeyPressMs.store(NowMs(), std::memory_order_relaxed);
    auto* equipMgr = RE::ActorEquipManager::GetSingleton();
    auto const& ist = BowInput::Globals();

    if (ist.mode.holdMode && ist.exit.pending) {
        ResetExitState();
    }

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

    if (ist.mode.holdMode) {
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
            const bool waitForEquip = !BowState::IsBowEquipped();
            ScheduleExitBowMode(waitForEquip, 0);
        } else {
            EnterBowMode(player, equipMgr, st);
        }
    }
}

void BowInput::IntegratedBowInputHandler::OnKeyReleased() const {
    if (auto const& ist = BowInput::Globals(); !ist.mode.holdMode) {
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

    const bool waitForEquip = !BowState::IsBowEquipped();

    ScheduleExitBowMode(waitForEquip, delayMs);
}

bool BowInput::IntegratedBowInputHandler::IsWeaponDrawn(RE::Actor* actor) {
    if (!actor) {
        return false;
    }

    auto const* state = actor->AsActorState();

    if (!state) {
        return false;
    }

    const bool drawn = state->IsWeaponDrawn();

    return drawn;
}

void BowInput::IntegratedBowInputHandler::SetWeaponDrawn(RE::Actor* actor, bool drawn) {
    if (!actor) {
        return;
    }

    actor->DrawWeaponMagicHands(drawn);
}

RE::ExtraDataList* BowInput::IntegratedBowInputHandler::GetPrimaryExtra(RE::InventoryEntryData* entry) {
    if (!entry) {
        return nullptr;
    }
    if (!entry->extraLists) {
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
    auto& ist = BowInput::Globals();
    ist.sheathRequestedByPlayer.store(false, std::memory_order_relaxed);
    ist.pendingRestoreAfterSheathe.store(false, std::memory_order_relaxed);

    if (!player || !equipMgr) {
        return;
    }

    RE::TESAmmo* currentAmmo = player->GetCurrentAmmo();
    BowState::SetPrevAmmo(currentAmmo);

    std::vector<BowState::ExtraEquippedItem> wornBefore;
    BowState::CaptureWornArmorSnapshot(wornBefore);

    RE::TESObjectWEAP* bow = st.chosenBow.base ? st.chosenBow.base->As<RE::TESObjectWEAP>() : nullptr;
    RE::ExtraDataList* bowExtra = st.chosenBow.extra;

    if (!bow) {
        return;
    }

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

    auto const& cfg = IntegratedBow::GetBowConfig();

    if (cfg.skipEquipBowAnimationPatch.load(std::memory_order_relaxed)) {
        IntegratedBow::SkipEquipController::EnableAndArmDisable(player, 0, false, kDisableSkipEquipDelayMs);
    }

    equipMgr->EquipObject(player, bow, bowExtra, 1, nullptr, true, false, true, false);

    st.isUsingBow = true;
    st.isEquipingBow = false;

    std::vector<BowState::ExtraEquippedItem> wornAfter;
    BowState::CaptureWornArmorSnapshot(wornAfter);

    auto removed = BowState::DiffArmorSnapshot(wornBefore, wornAfter);

    BowState::SetPrevExtraEquipped(std::move(removed));

    if (const bool hiddenEnabled = HiddenItemsPatch::IsEnabled(); hiddenEnabled) {
        auto const& ids = HiddenItemsPatch::GetHiddenFormIDs();

        BowState::ApplyHiddenItemsPatch(player, equipMgr, ids);
    }

    if (auto* preferred = BowState::GetPreferredArrow()) {
        auto inv = player->GetInventory([preferred](RE::TESBoundObject& obj) { return &obj == preferred; });

        if (!inv.empty()) {
            equipMgr->EquipObject(player, preferred, nullptr, 1, nullptr, true, false, true, false);
        }
    }

    if (!alreadyDrawn) {
        SetWeaponDrawn(player, true);
    }

    BowState::SetAutoAttackHeld(false);

    const bool shouldWaitAuto = (ist.mode.holdMode && IsAutoDrawEnabled() && BowInput::IsHotkeyDown());

    BowState::SetWaitingAutoAfterEquip(shouldWaitAuto);
    ist.allowUnequip.store(false, std::memory_order_relaxed);
    ist.allowUnequipReenableMs.store(NowMs() + 2000, std::memory_order_relaxed);

    if (shouldWaitAuto && cfg.skipEquipBowAnimationPatch.load(std::memory_order_relaxed)) {
        ist.fakeEnableBumperAtMs = NowMs() + kFakeEnableBumperDelayMs;
    } else {
        ist.fakeEnableBumperAtMs = 0;
    }
}

void BowInput::IntegratedBowInputHandler::ExitBowMode(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                                      BowState::IntegratedBowState& st) {
    auto& ist = BowInput::Globals();

    if (!player || !equipMgr) {
        return;
    }

    ist.fakeEnableBumperAtMs = 0;

    if (!st.wasCombatPosed && !player->IsInCombat()) {
        SetWeaponDrawn(player, false);
        ist.pendingRestoreAfterSheathe.store(true, std::memory_order_relaxed);
        st.isUsingBow = false;

        return;
    }

    BowState::RestorePrevWeaponsAndAmmo(player, equipMgr, st);
}

void BowInput::IntegratedBowInputHandler::ScheduleExitBowMode(bool waitForEquip, int delayMs) {
    auto& st = BowInput::Globals();

    st.exit.pending = true;
    st.exit.waitForEquip = waitForEquip;
    st.exit.waitEquipTimer = 0.0f;
    st.exit.delayTimer = 0.0f;
    st.exit.delayMs = delayMs;
}

void BowInput::IntegratedBowInputHandler::UpdateSmartMode(RE::PlayerCharacter* player, float dt) const {
    auto& st = BowInput::Globals();

    if (!st.mode.smartMode || !st.mode.smartPending || !st.hotkey.hotkeyDown) {
        return;
    }

    st.mode.smartTimer += dt;

    if (st.mode.smartTimer >= kSmartClickThreshold) {
        st.mode.smartPending = false;
        st.mode.smartTimer = 0.0f;
        st.mode.holdMode = true;

        const bool blocked = IsInputBlockedByMenus();

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

    st.exit.waitEquipTimer += dt;

    if (st.exit.waitEquipTimer >= st.exit.waitEquipMax) {
        st.exit.waitForEquip = false;
        st.exit.waitEquipTimer = 0.0f;
    }
}

bool BowInput::IntegratedBowInputHandler::IsExitDelayReady(float dt) const {
    auto& st = BowInput::Globals();

    if (st.exit.delayMs <= 0) {
        return true;
    }

    st.exit.delayTimer += dt * 1000.0f;

    const auto target = static_cast<float>(st.exit.delayMs);
    const bool ready = st.exit.delayTimer >= target;

    return ready;
}

void BowInput::IntegratedBowInputHandler::CompleteExit() const {
    if (auto* equipMgr = RE::ActorEquipManager::GetSingleton(); equipMgr) {
        auto* player = RE::PlayerCharacter::GetSingleton();

        if (player) {
            auto& bowSt = BowState::Get();

            ExitBowMode(player, equipMgr, bowSt);
        }
    }

    ResetExitState();
}

void BowInput::IntegratedBowInputHandler::UpdateExitPending(float dt) const {
    auto const& st = BowInput::Globals();
    if (!st.exit.pending) {
        return;
    }

    const bool bowEquipped = BowState::IsBowEquipped();

    if (st.exit.waitForEquip && !bowEquipped) {
        UpdateExitEquipWait(dt);
    } else if (IsExitDelayReady(dt)) {
        CompleteExit();
    }
}

void BowInput::IntegratedBowInputHandler::ProcessButtonEvent(const RE::ButtonEvent* button,
                                                             RE::PlayerCharacter* player) const {
    if (!button || !player) {
        return;
    }

    const auto dev = button->GetDevice();
    const int code = button->GetIDCode();

    const bool isDown = button->IsPressed();

    if (dev == RE::INPUT_DEVICE::kKeyboard && (code >= 0 && code < kMaxCode)) {
        g_kbDown[static_cast<std::size_t>(code)].store(isDown, std::memory_order_relaxed);

    } else if (dev == RE::INPUT_DEVICE::kGamepad && (code >= 0 && code < kMaxCode)) {
        g_gpDown[static_cast<std::size_t>(code)].store(isDown, std::memory_order_relaxed);
    }

    if (!button->IsDown() && !button->IsUp()) {
        return;
    }

    auto& ist = BowInput::Globals();
    const auto& ue = button->QUserEvent();

    {
        auto const& cfg = IntegratedBow::GetBowConfig();
        if (cfg.cancelHoldExitDelayOnAttackPatch.load(std::memory_order_relaxed) && button->IsDown() &&
            (dev == RE::INPUT_DEVICE::kMouse || dev == RE::INPUT_DEVICE::kGamepad) && IsAttackEvent(ue) &&
            IsInHoldAutoExitDelay() && (!ist.attackHold.active.load(std::memory_order_relaxed))) {
            CompleteExit();
            const std::uint64_t now = NowMs();
            ist.postExitAttackPending = true;
            ist.postExitAttackStage = 0;
            ist.postExitAttackDownAtMs = now + kPostExitAttackDownDelayMs;
            ist.postExitAttackUpAtMs = ist.postExitAttackDownAtMs + kPostExitAttackTapMs;

            return;
        }
    }

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
        return;
    }

    for (auto e = *a_events; e; e = e->next) {
        if (auto button = e->AsButtonEvent()) {
            ProcessButtonEvent(button, player);
        }
    }
}

float BowInput::IntegratedBowInputHandler::CalculateDeltaTime() const {
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();

    const auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    if (dt < 0.0f || dt > 0.5f) {
        dt = 0.0f;
    }

    return dt;
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
    auto& ist = BowInput::Globals();
    float dt = CalculateDeltaTime();

    UpdateUnequipGate();
    ProcessInputEvents(a_events, player);
    RecomputeHotkeyEdges(player, dt);
    UpdateSmartMode(player, dt);
    UpdateExitPending(dt);
    PumpPostExitAttackTap();
    PumpAttackHold(dt);

    if (ist.fakeEnableBumperAtMs != 0 && NowMs() >= ist.fakeEnableBumperAtMs) {
        ist.fakeEnableBumperAtMs = 0;

        if (BowState::IsWaitingAutoAfterEquip() && BowState::IsUsingBow() && player) {
            RE::BSAnimationGraphEvent ev{RE::BSFixedString("EnableBumper"), player, RE::BSFixedString()};
            BowInput::HandleAnimEvent(&ev, nullptr);
        }
    }

    IntegratedBow::SkipEquipController::Tick();

    if (auto* equipMgr = RE::ActorEquipManager::GetSingleton(); equipMgr) {
        BowState::UpdateDeferredFinalize(player, equipMgr, dt);
    }

    return RE::BSEventNotifyControl::kContinue;
}

void BowInput::IntegratedBowInputHandler::ScheduleAutoAttackDraw() {
    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !player) {
        return;
    }

    if (auto const& bowSt = BowState::Get(); !bowSt.isUsingBow) {
        return;
    }

    BowState::SetWaitingAutoAfterEquip(true);
}

BowInput::IntegratedBowInputHandler* BowInput::IntegratedBowInputHandler::GetSingleton() {
    static IntegratedBowInputHandler instance;
    auto* ptr = std::addressof(instance);

    return ptr;
}

void BowInput::RegisterInputHandler() {
    if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
        auto* sink = IntegratedBowInputHandler::GetSingleton();

        mgr->AddEventSink(sink);
    }
}

void BowInput::SetMode(int mode) {
    auto& st = BowInput::Globals();

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
}

void BowInput::SetKeyScanCodes(int k1, int k2, int k3) {
    auto& st = BowInput::Globals();
    std::array<int, kMaxComboKeys> vals{k1, k2, k3};

    for (int i = 0; i < kMaxComboKeys; ++i) {
        st.hotkeyConfig.bowKeyScanCodes[i] = vals[i];
        st.hotkey.bowKeyDown[i] = false;
    }

    st.hotkey.kbdComboDown = false;
    st.hotkey.hotkeyDown = false;
}

void BowInput::SetGamepadButtons(int b1, int b2, int b3) {
    auto& st = BowInput::Globals();
    std::array<int, kMaxComboKeys> vals{b1, b2, b3};

    for (int i = 0; i < kMaxComboKeys; ++i) {
        st.hotkeyConfig.bowPadButtons[i] = vals[i];
        st.hotkey.bowPadDown[i] = false;
    }

    st.hotkey.padComboDown = false;
    st.hotkey.hotkeyDown = false;
}

void BowInput::RequestGamepadCapture() {
    auto& st = BowInput::Globals();
    st.capture.captureRequested.store(true, std::memory_order_relaxed);
    st.capture.capturedEncoded.store(-1, std::memory_order_relaxed);
}

int BowInput::PollCapturedGamepadButton() {
    auto& st = BowInput::Globals();

    if (const int v = st.capture.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
        st.capture.capturedEncoded.store(-1, std::memory_order_relaxed);
        return v;
    }

    return -1;
}

bool BowInput::IsHotkeyDown() {
    auto const& st = BowInput::Globals();
    const bool v = st.hotkey.hotkeyDown;

    if (static bool last = false; v != last) {
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
    if (auto const* player = RE::PlayerCharacter::GetSingleton(); !actor || actor != player) {
        return;
    }

    if (std::string_view tag{ev->tag.c_str(), ev->tag.size()}; tag == "EnableBumper"sv) {
        BowState::SetBowEquipped(true);

        const bool waiting = BowState::IsWaitingAutoAfterEquip();
        const bool usingBow = BowState::IsUsingBow();
        const bool autoDraw = IsAutoDrawEnabled();
        const bool hotkeyDown = BowInput::IsHotkeyDown();
        const bool holdMode = ist.mode.holdMode;

        if (waiting && usingBow && holdMode && autoDraw && hotkeyDown) {
            BowState::SetWaitingAutoAfterEquip(false);

            if (!BowState::IsAutoAttackHeld()) {
                BowState::SetAutoAttackHeld(true);
                StartAutoAttackDraw();
            }
        }
    } else if (tag == "WeaponSheathe"sv) {
        weaponSheatheHelper();
    } else if (tag == "bowReset"sv) {
        const bool autoDraw = IsAutoDrawEnabled();
        const bool autoHeld = BowState::IsAutoAttackHeld();
        const bool hotkeyDown = BowInput::IsHotkeyDown();

        if (autoDraw && autoHeld && hotkeyDown) {
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

    ist.fakeEnableBumperAtMs = 0;
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

void BowInput::IntegratedBowInputHandler::RecomputeHotkeyEdges(RE::PlayerCharacter* player, float dt) const {
    auto& st = BowInput::Globals();
    auto const& cfg = IntegratedBow::GetBowConfig();

    const auto& hk = st.hotkeyConfig;

    const bool kbNow = ComboDown(hk.bowKeyScanCodes, g_kbDown);
    const bool gpNow = ComboDown(hk.bowPadButtons, g_gpDown);
    const bool rawNow = kbNow || gpNow;

    st.hotkey.kbdComboDown = kbNow;
    st.hotkey.padComboDown = gpNow;

    const bool prevAccepted = st.hotkey.hotkeyDown;

    bool acceptedNow = false;

    if (!cfg.requireExclusiveHotkeyPatch.load(std::memory_order_relaxed)) {
        st.exclusivePendingSrc = 0;
        st.exclusivePendingTimer = 0.0f;
        acceptedNow = rawNow;
    } else {
        if (prevAccepted) {
            acceptedNow = rawNow;
        } else {
            const auto pending = static_cast<PendingSrc>(st.exclusivePendingSrc);

            auto clearPending = [&]() {
                st.exclusivePendingSrc = 0;
                st.exclusivePendingTimer = 0.0f;
            };

            if (pending != PendingSrc::None) {
                if (!rawNow) {
                    bool stillExclusive = false;
                    if (pending == PendingSrc::Kb) {
                        stillExclusive =
                            ComboExclusiveNow(hk.bowKeyScanCodes, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera);
                    } else if (pending == PendingSrc::Gp) {
                        stillExclusive =
                            ComboExclusiveNow(hk.bowPadButtons, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                    }

                    clearPending();
                    acceptedNow = stillExclusive;
                } else {
                    bool stillExclusive = false;

                    if (pending == PendingSrc::Kb) {
                        if (!kbNow) {
                            clearPending();
                            acceptedNow = false;
                        } else {
                            stillExclusive =
                                ComboExclusiveNow(hk.bowKeyScanCodes, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera);
                        }
                    } else if (pending == PendingSrc::Gp) {
                        if (!gpNow) {
                            clearPending();
                            acceptedNow = false;
                        } else {
                            stillExclusive =
                                ComboExclusiveNow(hk.bowPadButtons, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                        }
                    }

                    if (!stillExclusive) {
                        clearPending();
                        acceptedNow = false;
                    } else {
                        st.exclusivePendingTimer -= dt;
                        if (st.exclusivePendingTimer <= 0.0f) {
                            clearPending();
                            acceptedNow = true;
                        } else {
                            acceptedNow = false;
                        }
                    }
                }
            } else {
                const bool kbPressedEdge = kbNow && !st.prevRawKbComboDown;
                const bool gpPressedEdge = gpNow && !st.prevRawGpComboDown;

                if (kbPressedEdge &&
                    ComboExclusiveNow(hk.bowKeyScanCodes, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera)) {
                    st.exclusivePendingSrc = static_cast<std::uint8_t>(PendingSrc::Kb);
                    st.exclusivePendingTimer = kExclusiveConfirmDelaySec;
                    acceptedNow = false;
                } else if (gpPressedEdge &&
                           ComboExclusiveNow(hk.bowPadButtons, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera)) {
                    st.exclusivePendingSrc = static_cast<std::uint8_t>(PendingSrc::Gp);
                    st.exclusivePendingTimer = kExclusiveConfirmDelaySec;
                    acceptedNow = false;
                } else {
                    acceptedNow = false;
                }
            }
        }
    }

    st.prevRawKbComboDown = kbNow;
    st.prevRawGpComboDown = gpNow;

    const bool blocked = IsInputBlockedByMenus();

    if (!st.mode.smartMode) {
        HandleNormalMode(player, acceptedNow, blocked);
        return;
    }

    if (acceptedNow && !prevAccepted) {
        HandleSmartModePressed(blocked);
    } else if (!acceptedNow && prevAccepted) {
        HandleSmartModeReleased(player, blocked);
    }
}