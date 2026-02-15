#include "BowInputHandler.h"

#include <atomic>
#include <chrono>
#include <ranges>
#include <string_view>

#include "../PCH.h"
#include "../config/BowConfig.h"
#include "../patchs/SkipEquipController.h"
#include "BowModeController.h"
#include "HotkeyDetector.h"
#include "InputGate.h"
#include "InputState.h"

using namespace std::literals;

namespace BowInput {
    namespace {
        constexpr std::uint64_t kPostExitAttackDownDelayMs = 100;
        constexpr std::uint64_t kPostExitAttackTapMs = 200;

        inline std::uint64_t NowMs() noexcept {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }

        inline bool HasTransformArchetype(const RE::MagicItem* item) {
            if (!item) return false;

            using ArchetypeID = RE::EffectArchetypes::ArchetypeID;

            return std::ranges::any_of(item->effects, [](const auto* effect) {
                if (!effect || !effect->baseEffect) return false;
                const auto arch = effect->baseEffect->GetArchetype();
                return arch == ArchetypeID::kWerewolf || arch == ArchetypeID::kVampireLord;
            });
        }

        inline const std::vector<RE::SpellItem*>& GetTransformPowers() {
            static std::vector<RE::SpellItem*> s_powers;  // NOSONAR
            if (!s_powers.empty()) return s_powers;

            auto* dh = RE::TESDataHandler::GetSingleton();
            if (!dh) return s_powers;

            auto const& spells = dh->GetFormArray<RE::SpellItem>();
            s_powers.reserve(spells.size());

            for (auto* spell : spells) {
                if (!spell) continue;
                if (spell->GetSpellType() != RE::MagicSystem::SpellType::kPower) continue;
                if (HasTransformArchetype(spell)) s_powers.push_back(spell);
            }
            return s_powers;
        }

        inline bool IsCurrentTransformPower(RE::Actor* actor) {
            if (!actor) return false;
            for (auto* power : GetTransformPowers()) {
                if (actor->IsCurrentShout(power)) return true;
            }
            return false;
        }

        struct CaptureState {
            std::atomic_bool requested{false};
            std::atomic_int capturedEncoded{-1};
        };

        CaptureState g_capture;  // NOSONAR

        HotkeyConfig g_hotkeyConfig{.bowKeyScanCodes = {0x2F, -1, -1}, .bowPadButtons = {-1, -1, -1}};  // NOSONAR

        HotkeyRuntime g_hotkeyRuntime;  // NOSONAR
    }

    BowInputHandler* BowInputHandler::GetSingleton() {
        static BowInputHandler s_instance;  // NOSONAR
        return &s_instance;
    }

    void BowInputHandler::ProcessOneButton(const RE::ButtonEvent* button, RE::PlayerCharacter* player) const {
        if (!button || !player) return;

        const auto dev = button->GetDevice();
        const auto code = static_cast<int>(button->idCode);
        const bool isPressed = button->IsPressed();
        const bool isDownEdge = button->IsDown();
        const bool isUpEdge = button->IsUp();

        Inputs().OnButton(dev, code, isPressed, isDownEdge, isUpEdge);

        if (g_capture.requested.load(std::memory_order_relaxed)) {
            if (isDownEdge) {
                g_capture.capturedEncoded.store(InputUtil::EncodeCapture(dev, code), std::memory_order_relaxed);
                g_capture.requested.store(false, std::memory_order_relaxed);
            }
            return;
        }

        if (!isDownEdge && !isUpEdge) return;

        const auto& ue = button->QUserEvent();
        auto& ctrl = BowModeController::Get();

        {
            auto const& cfg = IntegratedBow::GetBowConfig();
            const bool isMouse = (dev == RE::INPUT_DEVICE::kMouse);
            const bool isGp = (dev == RE::INPUT_DEVICE::kGamepad);

            if (cfg.cancelHoldExitDelayOnAttackPatch.load(std::memory_order_relaxed) && button->IsDown() &&
                (isMouse || isGp) && InputGate::IsAttackEvent(ue) && ctrl.IsInHoldAutoExitDelay() &&
                !ctrl.AttackHold().active.load(std::memory_order_relaxed)) {
                ctrl.CompleteExit();
                g_hotkeyRuntime.suppressUntilReleased = true;

                const std::uint64_t now = NowMs();
                auto& pea = ctrl.PostExitAttack();
                pea.pending = true;
                pea.stage = 0;
                pea.downAtMs = now + kPostExitAttackDownDelayMs;
                pea.upAtMs = pea.downAtMs + kPostExitAttackTapMs;
                return;
            }
        }

        if (ue == "Shout"sv && button->IsDown() && IsCurrentTransformPower(player)) {
            ctrl.ForceImmediateExit();
            g_hotkeyRuntime.suppressUntilReleased = true;
            return;
        }

        if (ue == "Ready Weapon"sv && button->IsDown()) {
            const auto& bowSt = BowState::Get();
            const std::uint64_t now = NowMs();
            const std::uint64_t lastHotkey = ctrl.lastHotkeyPressMs.load(std::memory_order_relaxed);
            const bool nearHotkey = (lastHotkey != 0) && (now - lastHotkey) < 250;

            const bool isKbOrGp = (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kGamepad);

            if (isKbOrGp && bowSt.isUsingBow && !ctrl.hotkeyDown && !nearHotkey && BowState::IsBowEquipped() &&
                !bowSt.isEquipingBow) {
                ctrl.sheathRequestedByPlayer.store(true, std::memory_order_relaxed);
            }
        }
    }

    void BowInputHandler::ProcessButtonEvents(RE::InputEvent* const* a_events, RE::PlayerCharacter* player) const {
        if (!a_events) return;
        for (auto* e = *a_events; e; e = e->next) {
            if (auto const* btn = e->AsButtonEvent()) {
                ProcessOneButton(btn, player);
            }
        }
    }

    float BowInputHandler::CalculateDeltaTime() {
        using clock = std::chrono::steady_clock;
        static clock::time_point s_last = clock::now();  // NOSONAR

        const auto now = clock::now();
        float dt = std::chrono::duration<float>(now - s_last).count();
        s_last = now;

        if (dt < 0.0f || dt > 0.5f) dt = 0.0f;
        return dt;
    }

    RE::BSEventNotifyControl BowInputHandler::ProcessEvent(RE::InputEvent* const* a_events,
                                                           RE::BSTEventSource<RE::InputEvent*>*) {
        if (!a_events) return RE::BSEventNotifyControl::kContinue;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return RE::BSEventNotifyControl::kContinue;

        const float dt = CalculateDeltaTime();
        auto& ctrl = BowModeController::Get();
        const bool blocked = InputGate::IsInputBlockedByMenus();

        ctrl.UpdateUnequipGate();

        ProcessButtonEvents(a_events, player);

        const bool requireExclusive =
            IntegratedBow::GetBowConfig().requireExclusiveHotkeyPatch.load(std::memory_order_relaxed);

        HotkeyDetector::Tick(player, dt, g_hotkeyConfig, Inputs(), requireExclusive, blocked, ctrl.hotkeyDown,
                             g_hotkeyRuntime, ctrl);

        ctrl.UpdateSmartMode(player, dt);
        if (ctrl.UpdateExitPending(dt)) {
            g_hotkeyRuntime.suppressUntilReleased = true;
            g_hotkeyRuntime.prevRawKbComboDown = false;
            g_hotkeyRuntime.prevRawGpComboDown = false;
            g_hotkeyRuntime.exclusivePendingSrc = 0;
            g_hotkeyRuntime.exclusivePendingTimer = 0.0f;
        }
        ctrl.PumpPostExitAttackTap();
        ctrl.PumpAttackHold(dt);

        if (ctrl.fakeEnableBumperAtMs != 0 && NowMs() >= ctrl.fakeEnableBumperAtMs) {
            ctrl.fakeEnableBumperAtMs = 0;

            if (BowState::IsWaitingAutoAfterEquip() && BowState::IsUsingBow()) {
                ctrl.OnAnimEvent("EnableBumper", player);
            }
        }

        IntegratedBow::SkipEquipController::Tick();

        if (auto* equipMgr = RE::ActorEquipManager::GetSingleton()) {
            BowState::UpdateDeferredFinalize(player, equipMgr, dt);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void RegisterInputHandler() {
        if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
            mgr->AddEventSink(BowInputHandler::GetSingleton());
        }
    }

    void SetMode(int mode) {
        auto& ctrl = BowModeController::Get();
        ctrl.hotkeyDown = false;
        ctrl.Mode().smartPending = false;
        ctrl.Mode().smartTimer = 0.0f;

        switch (mode) {
            case 0:
                ctrl.Mode().holdMode = true;
                ctrl.Mode().smartMode = false;
                break;
            case 1:
                ctrl.Mode().holdMode = false;
                ctrl.Mode().smartMode = false;
                break;
            case 2:
            default:
                ctrl.Mode().holdMode = false;
                ctrl.Mode().smartMode = true;
                break;
        }
    }

    void SetKeyScanCodes(int k1, int k2, int k3) {
        g_hotkeyConfig.bowKeyScanCodes = {k1, k2, k3};

        auto& ctrl = BowModeController::Get();
        ctrl.hotkeyDown = false;
        g_hotkeyRuntime.prevRawKbComboDown = false;
        g_hotkeyRuntime.exclusivePendingSrc = 0;
        g_hotkeyRuntime.exclusivePendingTimer = 0.0f;
    }

    void SetGamepadButtons(int b1, int b2, int b3) {
        g_hotkeyConfig.bowPadButtons = {b1, b2, b3};

        auto& ctrl = BowModeController::Get();
        ctrl.hotkeyDown = false;
        g_hotkeyRuntime.prevRawGpComboDown = false;
        g_hotkeyRuntime.exclusivePendingSrc = 0;
        g_hotkeyRuntime.exclusivePendingTimer = 0.0f;
    }

    void RequestGamepadCapture() {
        g_capture.requested.store(true, std::memory_order_relaxed);
        g_capture.capturedEncoded.store(-1, std::memory_order_relaxed);
    }

    int PollCapturedGamepadButton() {
        if (const int v = g_capture.capturedEncoded.load(std::memory_order_relaxed); v != -1) {
            g_capture.capturedEncoded.store(-1, std::memory_order_relaxed);
            return v;
        }
        return -1;
    }

    bool IsHotkeyDown() { return BowModeController::Get().hotkeyDown; }

    bool IsUnequipAllowed() noexcept { return BowModeController::Get().allowUnequip.load(std::memory_order_relaxed); }

    void BlockUnequipForMs(std::uint64_t ms) noexcept {
        auto& ctrl = BowModeController::Get();
        ctrl.allowUnequip.store(false, std::memory_order_relaxed);
        ctrl.allowUnequipReenableMs.store(NowMs() + ms, std::memory_order_relaxed);
    }

    void ForceAllowUnequip() noexcept {
        auto& ctrl = BowModeController::Get();
        ctrl.allowUnequip.store(true, std::memory_order_relaxed);
        ctrl.allowUnequipReenableMs.store(0, std::memory_order_relaxed);
    }

    void HandleAnimEvent(const RE::BSAnimationGraphEvent* ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
        if (!ev || !ev->holder) return;

        auto* actor = ev->holder->As<RE::Actor>();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!actor || actor != player) return;

        const std::string_view tag{ev->tag.c_str(), ev->tag.size()};
        BowModeController::Get().OnAnimEvent(tag, player);
    }

}