
#include "BowInput.h"

#include <atomic>
#include <chrono>
#include <ranges>
#include <string_view>
#include <thread>
#include <utility>

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
    constexpr int kMaxCode = 65536;
    constexpr float kExclusiveConfirmDelaySec = 0.10f;

    constexpr int kDIK_W = 0x11;
    constexpr int kDIK_A = 0x1E;
    constexpr int kDIK_S = 0x1F;
    constexpr int kDIK_D = 0x20;

    enum class PendingSrc : std::uint8_t { None = 0, Kb = 1, Gp = 2 };

    std::array<std::atomic_bool, kMaxCode> g_kbDown{};  // NOSONAR
    std::array<std::atomic_bool, kMaxCode> g_gpDown{};  // NOSONAR

    inline bool g_dbgGp = true;  // troque por config/atomic se quiser

    inline const char* DevName(RE::INPUT_DEVICE d) {
        switch (d) {
            case RE::INPUT_DEVICE::kKeyboard:
                return "kb";
            case RE::INPUT_DEVICE::kMouse:
                return "mouse";
            case RE::INPUT_DEVICE::kGamepad:
                return "gp";
            default:
                return "unk";
        }
    }

    inline int NormalizePadCode(int v) noexcept { return (v >= 0) ? v : (v == -1 ? -1 : (-v - 1)); }

    inline bool ComboContains(const std::array<int, BowInput::kMaxComboKeys>& combo, int code) {
        return std::ranges::any_of(combo, [&](int v) {
            if (v == -1) return false;
            return NormalizePadCode(v) == code;
        });
    }

    // Dump pequeno dos codes mais importantes
    template <size_t N>
    void DumpDownSet(const char* tag, const std::array<int, N>& combo, int extraCodeA = -1, int extraCodeB = -1) {
        if (!g_dbgGp) return;

        auto& st = BowInput::Globals();

        auto dumpOne = [&](int raw) {
            if (raw == -1) return;
            const int code = NormalizePadCode(raw);
            if (code < 0 || code >= kMaxCode) {
                spdlog::info("[GPDBG] {} code(raw={}) -> norm={} OUT_OF_RANGE", tag, raw, code);
                return;
            }
            const bool down = g_gpDown[(size_t)code].load(std::memory_order_relaxed);
            spdlog::info("[GPDBG] {} code(raw={}) norm={} down={}", tag, raw, code, down);
        };

        spdlog::info(
            "[GPDBG] {} -- hotkey combo raw=[{}, {}, {}] prevRawGpComboDown={} padComboDown={} hotkeyDown={} "
            "pendingSrc={} pendingT={:.3f}",
            tag, combo[0], combo[1], combo[2], st.prevRawGpComboDown, st.hotkey.padComboDown, st.hotkey.hotkeyDown,
            (int)st.exclusivePendingSrc, st.exclusivePendingTimer);

        dumpOne(combo[0]);
        dumpOne(combo[1]);
        dumpOne(combo[2]);

        if (extraCodeA >= 0) {
            const bool down = g_gpDown[(size_t)extraCodeA].load(std::memory_order_relaxed);
            spdlog::info("[GPDBG] {} extraA code={} down={}", tag, extraCodeA, down);
        }
        if (extraCodeB >= 0) {
            const bool down = g_gpDown[(size_t)extraCodeB].load(std::memory_order_relaxed);
            spdlog::info("[GPDBG] {} extraB code={} down={}", tag, extraCodeB, down);
        }
    }

    // Para não varrer 1024 codes em log toda hora, registre só quando “extra” aparece
    inline void LogGpExtraIfAny(const char* tag) {
        if (!g_dbgGp) return;
        auto& st = BowInput::Globals();
        const auto& hk = st.hotkeyConfig.bowPadButtons;

        int found = 0;
        for (int code = 0; code < kMaxCode; ++code) {
            if (!g_gpDown[(size_t)code].load(std::memory_order_relaxed)) continue;
            if (ComboContains(hk, code)) continue;

            spdlog::info("[GPDBG] {} EXTRA_DOWN code={} (not in hotkey combo)", tag, code);
            if (++found >= 8) {  // limite pra não explodir
                spdlog::info("[GPDBG] {} EXTRA_DOWN ... (more omitted)", tag);
                break;
            }
        }
    }

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

        for (int v : combo) {
            if (v == -1) continue;

            const int code = NormalizePadCode(v);

            if (code < 0 || code >= kMaxCode) {
                return false;
            }
            if (!down[static_cast<std::size_t>(code)].load(std::memory_order_relaxed)) {
                return false;
            }
        }
        return true;
    }

    template <class DownArr, class AllowedFn>
    bool ComboExclusiveNow(const std::array<int, BowInput::kMaxComboKeys>& combo, const DownArr& down,
                           AllowedFn isAllowedExtra) {
        if (!ComboDown(combo, down)) {
            if (g_dbgGp && (&down == (const DownArr*)&g_gpDown)) {  // hack simples p/ distinguir
                spdlog::info("[GPDBG] EXCL ComboDown=false (combo not fully down)");
                DumpDownSet("EXCL-ComboDown-false", combo);
            }
            return false;
        }

        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[(size_t)code].load(std::memory_order_relaxed)) continue;
            if (ComboContains(combo, code)) continue;
            if (isAllowedExtra(code)) continue;

            if (g_dbgGp && (&down == (const DownArr*)&g_gpDown)) {
                spdlog::info("[GPDBG] EXCL FAIL: extra code={} is down (not in combo, not allowed)", code);
            }
            return false;
        }

        if (g_dbgGp && (&down == (const DownArr*)&g_gpDown)) {
            spdlog::info("[GPDBG] EXCL OK: exclusive");
        }
        return true;
    }

    template <class DownArr, class AllowedFn>
    bool ComboExclusiveReleaseOk(const std::array<int, BowInput::kMaxComboKeys>& combo, const DownArr& down,
                                 AllowedFn isAllowedExtra) {
        for (int code = 0; code < kMaxCode; ++code) {
            if (!down[(size_t)code].load(std::memory_order_relaxed)) continue;

            if (ComboContains(combo, code)) continue;

            if (isAllowedExtra(code)) continue;

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

    if (g_dbgGp) {
        spdlog::info("[GPDBG] HNM anyNow={} hotkeyDown={} blocked={}", anyNow, st.hotkey.hotkeyDown, blocked);
    }

    if (anyNow && !st.hotkey.hotkeyDown) {
        st.hotkey.hotkeyDown = true;
        if (g_dbgGp) spdlog::info("[GPDBG] HNM -> PRESS (calling OnKeyPressed) blocked={}", blocked);
        if (!blocked) OnKeyPressed(player);

    } else if (!anyNow && st.hotkey.hotkeyDown) {
        st.hotkey.hotkeyDown = false;
        if (g_dbgGp) spdlog::info("[GPDBG] HNM -> RELEASE (calling OnKeyReleased) blocked={}", blocked);
        if (!blocked) OnKeyReleased();
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
            st.capture.capturedEncoded.store(code, std::memory_order_relaxed);
            st.capture.captureRequested.store(false, std::memory_order_relaxed);
        }
        return;
    }

    int idx = -1;
    for (int i = 0; i < kMaxComboKeys; ++i) {
        const int expected = st.hotkeyConfig.bowKeyScanCodes[i];
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
        st.sawKbHotkeyDownThisTick = true;

        st.sawKbHotkeyDownExclusiveOkThisTick =
            ComboExclusiveNow(st.hotkeyConfig.bowKeyScanCodes, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera);

    } else if (a_event->IsUp()) {
        st.hotkey.bowKeyDown[idx] = false;
        st.sawKbHotkeyUpThisTick = true;

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
    st.mode.smartPending = false;
    st.mode.smartTimer = 0.0f;
    st.hotkey.hotkeyDown = false;

    st.prevRawKbComboDown = false;
    st.prevRawGpComboDown = false;

    st.exclusivePendingSrc = 0;
    st.exclusivePendingTimer = 0.0f;
}

void BowInput::IntegratedBowInputHandler::HandleGamepadButton(const RE::ButtonEvent* a_event,
                                                              RE::PlayerCharacter*) const {
    const auto code = static_cast<int>(a_event->idCode);
    auto& st = BowInput::Globals();
    const bool nowPressed = a_event->IsPressed();

    if (g_dbgGp) {
        spdlog::info("[GPDBG] HGP enter code={} nowPressed={} IsDown={} IsUp={}", code, nowPressed, a_event->IsDown(),
                     a_event->IsUp());
    }

    int idx = -1;
    int expectedRawAtIdx = -1;
    int expectedNormAtIdx = -1;

    for (int i = 0; i < kMaxComboKeys; ++i) {
        const int expectedRaw = st.hotkeyConfig.bowPadButtons[i];
        if (expectedRaw == -1) continue;

        const int expected = NormalizePadCode(expectedRaw);
        if (expected >= 0 && code == expected) {
            idx = i;
            expectedRawAtIdx = expectedRaw;
            expectedNormAtIdx = expected;
            break;
        }
    }

    if (g_dbgGp) {
        spdlog::info("[GPDBG] HGP match idx={} expectedRaw={} expectedNorm={}", idx, expectedRawAtIdx,
                     expectedNormAtIdx);
        DumpDownSet("HGP-before", st.hotkeyConfig.bowPadButtons);
        LogGpExtraIfAny("HGP-before");
    }

    if (idx < 0) return;

    const bool wasPressed = st.hotkey.bowPadDown[idx];

    if (nowPressed && !wasPressed) {
        st.hotkey.bowPadDown[idx] = true;
        st.sawGpHotkeyDownThisTick = true;

        if (g_dbgGp) spdlog::info("[GPDBG] HGP idx={} DOWN edge", idx);

    } else if (!nowPressed && wasPressed) {
        st.hotkey.bowPadDown[idx] = false;
        st.sawGpHotkeyUpThisTick = true;

        if (g_dbgGp) spdlog::info("[GPDBG] HGP idx={} UP edge", idx);

    } else {
        if (g_dbgGp)
            spdlog::info("[GPDBG] HGP idx={} no-change (nowPressed={} wasPressed={})", idx, nowPressed, wasPressed);
        return;
    }

    st.hotkey.padComboDown = AreAllActivePadButtonsDown();

    if (g_dbgGp) {
        spdlog::info("[GPDBG] HGP padComboDown={} bowPadDown=[{}, {}, {}]", st.hotkey.padComboDown,
                     st.hotkey.bowPadDown[0], st.hotkey.bowPadDown[1], st.hotkey.bowPadDown[2]);
        DumpDownSet("HGP-after", st.hotkeyConfig.bowPadButtons);
        LogGpExtraIfAny("HGP-after");
    }
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
        auto inv = player->GetInventory([preferred](RE::TESBoundObject const& obj) { return &obj == preferred; });

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
    auto& st = BowInput::Globals();
    st.suppressHotkeyUntilReleased = true;
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
    if (!button || !player) return;

    const auto dev = button->GetDevice();
    const auto code = static_cast<int>(button->idCode);
    const bool isPressed = button->IsPressed();
    const bool isDownEdge = button->IsDown();
    const bool isUpEdge = button->IsUp();

    if (g_dbgGp && dev == RE::INPUT_DEVICE::kGamepad) {
        spdlog::info("[GPDBG] EVT dev={} code={} isPressed={} IsDown={} IsUp={} ue='{}'", DevName(dev), code, isPressed,
                     isDownEdge, isUpEdge, button->QUserEvent().c_str());
    }

    if (code >= 0 && code < kMaxCode) {
        if (dev == RE::INPUT_DEVICE::kKeyboard) {
            g_kbDown[(size_t)code].store(isPressed, std::memory_order_relaxed);
        } else if (dev == RE::INPUT_DEVICE::kGamepad) {
            const bool prev = g_gpDown[(size_t)code].load(std::memory_order_relaxed);
            g_gpDown[(size_t)code].store(isPressed, std::memory_order_relaxed);

            if (g_dbgGp && dev == RE::INPUT_DEVICE::kGamepad) {
                spdlog::info("[GPDBG] DOWNSTATE code={} prev={} now={}", code, prev, isPressed);
            }
        }
    }

    if (!isDownEdge && !isUpEdge) {
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

    if (ue == "Shout"sv && button->IsDown() && IsCurrentTransformPower(player)) {
        IntegratedBowInputHandler::ForceImmediateExit();
        return;
    }

    if (ue == "Ready Weapon"sv && button->IsDown()) {
        auto const& st = BowState::Get();
        const std::uint64_t now = NowMs();
        const std::uint64_t lastHotkey = ist.lastHotkeyPressMs.load(std::memory_order_relaxed);
        const bool nearHotkey = (lastHotkey != 0) && (now - lastHotkey) < 250;

        if ((dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kGamepad) && st.isUsingBow &&
            !ist.hotkey.hotkeyDown && !nearHotkey && BowState::IsBowEquipped() && !st.isEquipingBow) {
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

    const bool prevAccepted = st.hotkey.hotkeyDown;

    // Edges do combo completo (arquitetura MagicInput)
    const bool kbPressedEdge = kbNow && !st.prevRawKbComboDown;
    const bool gpPressedEdge = gpNow && !st.prevRawGpComboDown;

    if (st.suppressHotkeyUntilReleased) {
        if (rawNow) {
            // mantém prevRaw sincronizado pra não gerar edge falso
            st.prevRawKbComboDown = kbNow;
            st.prevRawGpComboDown = gpNow;

            // também é bom matar pending aqui
            st.exclusivePendingSrc = 0;
            st.exclusivePendingTimer = 0.0f;

            if (g_dbgGp) spdlog::info("[GPDBG] RHK SUPPRESS: waiting release rawNow=1");
            return;  // não chama HandleNormalMode nem smart
        } else {
            st.suppressHotkeyUntilReleased = false;
            if (g_dbgGp) spdlog::info("[GPDBG] RHK SUPPRESS cleared (rawNow=0)");
        }
    }

    if (g_dbgGp) {
        spdlog::info(
            "[GPDBG] RHK BEGIN dt={:.4f} cfg.excl={} kbNow={} gpNow={} rawNow={} "
            "prevAccepted={} prevRawKb={} prevRawGp={} kbEdge={} gpEdge={} "
            "pendingSrc={} pendingT={:.3f} smartMode={}",
            dt, (int)cfg.requireExclusiveHotkeyPatch.load(std::memory_order_relaxed), kbNow, gpNow, rawNow,
            prevAccepted, st.prevRawKbComboDown, st.prevRawGpComboDown, kbPressedEdge, gpPressedEdge,
            (int)st.exclusivePendingSrc, st.exclusivePendingTimer, (int)st.mode.smartMode);

        // Ajuda MUITO: ver exatamente o estado do combo do gamepad nesse tick
        DumpDownSet("RHK-begin", hk.bowPadButtons);
        LogGpExtraIfAny("RHK-begin");
    }

    bool acceptedNow = false;

    const bool requireExclusive = cfg.requireExclusiveHotkeyPatch.load(std::memory_order_relaxed);

    if (!requireExclusive) {
        st.exclusivePendingSrc = 0;
        st.exclusivePendingTimer = 0.0f;
        acceptedNow = rawNow;

        if (g_dbgGp) {
            spdlog::info("[GPDBG] RHK path=NON_EXCLUSIVE acceptedNow={} (rawNow)", acceptedNow);
        }

    } else {
        if (prevAccepted) {
            // Depois de aceitar, segue rawNow (mesmo comportamento do seu)
            acceptedNow = rawNow;

            if (g_dbgGp) {
                spdlog::info("[GPDBG] RHK path=PREV_ACCEPTED acceptedNow={} (rawNow)", acceptedNow);
            }

        } else {
            const auto pending = static_cast<PendingSrc>(st.exclusivePendingSrc);

            auto clearPending = [&]() {
                if (g_dbgGp) {
                    spdlog::info("[GPDBG] RHK clearPending src={} t={:.3f} -> None", (int)st.exclusivePendingSrc,
                                 st.exclusivePendingTimer);
                }
                st.exclusivePendingSrc = 0;
                st.exclusivePendingTimer = 0.0f;
            };

            if (pending != PendingSrc::None) {
                if (g_dbgGp) {
                    spdlog::info("[GPDBG] RHK path=PENDING pendingSrc={} pendingT={:.3f} rawNow={}", (int)pending,
                                 st.exclusivePendingTimer, rawNow);
                }

                // Estamos aguardando confirmar exclusividade
                if (!rawNow) {
                    // Combo soltou: se não tem extras (fora do combo) aceitamos no release
                    bool stillExclusive = false;

                    if (pending == PendingSrc::Kb) {
                        stillExclusive =
                            ComboExclusiveReleaseOk(hk.bowKeyScanCodes, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera);

                    } else if (pending == PendingSrc::Gp) {
                        stillExclusive =
                            ComboExclusiveReleaseOk(hk.bowPadButtons, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                    }

                    if (g_dbgGp) {
                        spdlog::info("[GPDBG] RHK pending RELEASE rawNow=0 stillExclusive={}", stillExclusive);
                    }

                    clearPending();
                    acceptedNow = stillExclusive;

                    if (g_dbgGp) {
                        spdlog::info("[GPDBG] RHK pending RELEASE -> acceptedNow={}", acceptedNow);
                    }

                } else {
                    // Combo ainda down: precisa continuar exclusivo até timer expirar
                    bool stillExclusive = false;

                    if (pending == PendingSrc::Kb) {
                        stillExclusive =
                            ComboExclusiveNow(hk.bowKeyScanCodes, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera);

                    } else if (pending == PendingSrc::Gp) {
                        stillExclusive =
                            ComboExclusiveNow(hk.bowPadButtons, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);
                    }

                    if (g_dbgGp) {
                        spdlog::info("[GPDBG] RHK pending HOLD rawNow=1 stillExclusive={} pendingT(before)={:.3f}",
                                     stillExclusive, st.exclusivePendingTimer);
                        if (pending == PendingSrc::Gp) {
                            DumpDownSet("RHK-pending-hold", hk.bowPadButtons);
                            LogGpExtraIfAny("RHK-pending-hold");
                        }
                    }

                    if (!stillExclusive) {
                        if (g_dbgGp) {
                            spdlog::info("[GPDBG] RHK pending HOLD -> NOT exclusive => clearPending, acceptedNow=0");
                        }
                        clearPending();
                        acceptedNow = false;

                    } else {
                        st.exclusivePendingTimer -= dt;

                        if (g_dbgGp) {
                            spdlog::info("[GPDBG] RHK pending HOLD timer -= dt => pendingT(after)={:.3f}",
                                         st.exclusivePendingTimer);
                        }

                        if (st.exclusivePendingTimer <= 0.0f) {
                            if (g_dbgGp) {
                                spdlog::info("[GPDBG] RHK pending HOLD timer expired => acceptedNow=1");
                            }
                            clearPending();
                            acceptedNow = true;
                        } else {
                            acceptedNow = false;
                        }
                    }
                }

            } else {
                // Não há pending ainda: tentar iniciar
                if (g_dbgGp) {
                    spdlog::info("[GPDBG] RHK path=NO_PENDING kbEdge={} gpEdge={}", kbPressedEdge, gpPressedEdge);
                }

                if (kbPressedEdge) {
                    const bool kbExclusive =
                        ComboExclusiveNow(hk.bowKeyScanCodes, g_kbDown, IsAllowedExtra_Keyboard_MoveOrCamera);

                    if (g_dbgGp) {
                        spdlog::info("[GPDBG] RHK startPending? src=KB kbExclusive={}", kbExclusive);
                    }

                    if (kbExclusive) {
                        st.exclusivePendingSrc = std::to_underlying(PendingSrc::Kb);
                        st.exclusivePendingTimer = kExclusiveConfirmDelaySec;

                        if (g_dbgGp) {
                            spdlog::info("[GPDBG] RHK pending START src=KB timer={:.3f}", st.exclusivePendingTimer);
                        }
                    }

                } else if (gpPressedEdge) {
                    const bool gpExclusive =
                        ComboExclusiveNow(hk.bowPadButtons, g_gpDown, IsAllowedExtra_Gamepad_MoveOrCamera);

                    if (g_dbgGp) {
                        spdlog::info("[GPDBG] RHK startPending? src=GP gpExclusive={}", gpExclusive);
                        DumpDownSet("RHK-startPending-GP", hk.bowPadButtons);
                        LogGpExtraIfAny("RHK-startPending-GP");
                    }

                    if (gpExclusive) {
                        st.exclusivePendingSrc = std::to_underlying(PendingSrc::Gp);
                        st.exclusivePendingTimer = kExclusiveConfirmDelaySec;

                        if (g_dbgGp) {
                            spdlog::info("[GPDBG] RHK pending START src=GP timer={:.3f}", st.exclusivePendingTimer);
                        }
                    }
                }

                acceptedNow = false;

                if (g_dbgGp) {
                    spdlog::info("[GPDBG] RHK noPending -> acceptedNow=0 (waiting confirm)");
                }
            }
        }
    }

    // Atualiza prevRaw (parte central do edge)
    st.prevRawKbComboDown = kbNow;
    st.prevRawGpComboDown = gpNow;

    // Flags antigas (não determinam mais aceitação)
    st.sawKbHotkeyDownThisTick = false;
    st.sawKbHotkeyUpThisTick = false;
    st.sawGpHotkeyDownThisTick = false;
    st.sawGpHotkeyUpThisTick = false;

    const bool blocked = IsInputBlockedByMenus();

    if (g_dbgGp) {
        spdlog::info(
            "[GPDBG] RHK END acceptedNow={} prevAccepted={} blocked={} willHandleSmart={} "
            "hotkeyDown(before apply)={} pendingSrc={} pendingT={:.3f}",
            acceptedNow, prevAccepted, blocked, (int)st.mode.smartMode, st.hotkey.hotkeyDown,
            (int)st.exclusivePendingSrc, st.exclusivePendingTimer);
    }

    if (!st.mode.smartMode) {
        if (g_dbgGp) {
            spdlog::info("[GPDBG] RHK -> HandleNormalMode(anyNow={})", acceptedNow);
        }
        HandleNormalMode(player, acceptedNow, blocked);
        return;
    }

    if (acceptedNow && !prevAccepted) {
        if (g_dbgGp) spdlog::info("[GPDBG] RHK -> HandleSmartModePressed");
        HandleSmartModePressed(blocked);

    } else if (!acceptedNow && prevAccepted) {
        if (g_dbgGp) spdlog::info("[GPDBG] RHK -> HandleSmartModeReleased");
        HandleSmartModeReleased(player, blocked);

    } else {
        if (g_dbgGp)
            spdlog::info("[GPDBG] RHK -> SmartMode no transition (acceptedNow={}, prevAccepted={})", acceptedNow,
                         prevAccepted);
    }
}