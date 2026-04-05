#include "UI_IntegratedBow.h"

#include <array>

#include "Config/Config.h"
#include "Input/InputHandler.h"
#include "PCH.h"
#include "Patchs/HiddenItemsPatch.h"
#include "SKSEMenuFramework.h"
#include "UI/Strings.h"

using IntegratedBow::BowMode;
using IntegratedBow::GetBowConfig;

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace {
    bool g_pending = false;  // NOSONAR
    using enum IntegratedBow::BowMode;

    struct CaptureState {
        std::atomic<int>* field{nullptr};
        bool active{false};
    };

    CaptureState g_capture{};  // NOSONAR

    IntegratedBow::BowMode GetModeFromConfig(IntegratedBow::BowConfig const& cfg) {
        return cfg.mode.load(std::memory_order_relaxed);
    }

    void DrawPendingApplyBar(IntegratedBow::BowConfig const& cfg) {
        constexpr float kButtonWidth = 160.0f;

        ImGui::ImVec2 region{};
        ImGui::GetContentRegionAvail(&region);
        const float rightEdge = ImGui::GetCursorPosX() + region.x;
        ImGui::SetCursorPosX(rightEdge - kButtonWidth);

        ImGui::BeginDisabled(!g_pending);
        if (ImGui::Button(IntegratedBow::Strings::Get("Item_Apply", "Apply changes").c_str(),
                          ImVec2{kButtonWidth, 0.0f})) {
            cfg.Save();

            BowInput::SetMode(std::to_underlying(cfg.mode.load(std::memory_order_relaxed)));
            BowInput::SetCombo(cfg.ScanCode1.load(std::memory_order_relaxed),
                               cfg.ScanCode2.load(std::memory_order_relaxed),
                               cfg.ScanCode3.load(std::memory_order_relaxed));

            HiddenItemsPatch::SetEnabled(cfg.hideEquippedFromJsonPatch);

            g_pending = false;
        }
        ImGui::EndDisabled();
    }

    void DrawModeSection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        BowMode mode = GetModeFromConfig(cfg);

        int modeIndex = 0;
        switch (mode) {
            case Hold:
                modeIndex = 0;
                break;
            case Press:
                modeIndex = 1;
                break;
            case Smart:
                modeIndex = 2;
                break;
        }

        const auto& lblMode = IntegratedBow::Strings::Get("Item_InputMode", "Bow mode");
        const auto& lblHold = IntegratedBow::Strings::Get("Item_InputMode_Hold", "Hold");
        const auto& lblPress = IntegratedBow::Strings::Get("Item_InputMode_Press", "Press");
        const auto& lblSmart = IntegratedBow::Strings::Get("Item_InputMode_Smart", "Smart (click / hold)");

        const std::array<const char*, 3> items = {lblHold.c_str(), lblPress.c_str(), lblSmart.c_str()};

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(lblMode.c_str(), &modeIndex, items.data(), static_cast<int>(items.size()))) {
            BowMode newMode = Hold;
            if (modeIndex == 1) {
                newMode = Press;
            } else if (modeIndex == 2) {
                newMode = Smart;
            }

            cfg.mode.store(newMode, std::memory_order_relaxed);
            dirty = true;
        }
    }

    void DrawAutoDrawAndDelaySection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        if (bool autoDraw = cfg.autoDrawEnabled.load(std::memory_order_relaxed); ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_AutoDrawEnabled", "Auto draw arrow").c_str(), &autoDraw)) {
            cfg.autoDrawEnabled.store(autoDraw, std::memory_order_relaxed);
            dirty = true;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedBow::Strings::Get(
                          "Item_AutoDrawEnabled_Tip",
                          "If enabled, the bow will automatically start drawing an arrow while holding the hotkey.")
                          .c_str());
        }

        float delaySec = cfg.sheathedDelaySeconds.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(150.0f);

        if (ImGui::InputFloat(IntegratedBow::Strings::Get("Item_sheathedDelay", "S delay (s)").c_str(), &delaySec, 0.1f,
                              1.0f, "%.2f")) {
            if (delaySec < 0.0f) {
                delaySec = 0.0f;
            }
            cfg.sheathedDelaySeconds.store(delaySec, std::memory_order_relaxed);
            dirty = true;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedBow::Strings::Get(
                          "Item_sheathedDelay_Tip",
                          "Time in seconds after releasing the key (in hold mode) for the weapon to be sheathed.")
                          .c_str());
        }
    }

    void DrawInputWithCapture(std::atomic<int>& field, bool& dirty) {
        ImGui::PushID(static_cast<void*>(&field));

        int v = field.load(std::memory_order_relaxed);

        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##val", &v, 0, 0)) {
            if (v < -1) v = -1;
            field.store(v, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SameLine();

        const bool isActive = g_capture.active && g_capture.field == &field;

        if (isActive) {
            if (int code = BowInput::PollCapturedHotkey(); code != -1) {
                field.store(code, std::memory_order_relaxed);
                dirty = true;

                g_capture = {};
                BowInput::SetCaptureModeActive(false);
            }

            ImGui::TextDisabled("...");
            ImGui::SameLine();

            if (ImGui::SmallButton("X")) {
                g_capture = {};
                BowInput::CancelHotkeyCapture();
                BowInput::SetCaptureModeActive(false);
            }
        } else {
            if (g_capture.active) ImGui::BeginDisabled(true);

            if (ImGui::SmallButton("Cap")) {
                g_capture = {&field, true};
                BowInput::RequestHotkeyCapture();
                BowInput::SetCaptureModeActive(true);
            }

            if (g_capture.active) ImGui::EndDisabled();
        }

        ImGui::PopID();
    }

    void DrawHotkeysSection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        ImGui::TextUnformatted(
            IntegratedBow::Strings::Get("Item_InputKeys", "Input keys (keyboard / mouse / gamepad)").c_str());

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted(IntegratedBow::Strings::Get("Item_InputKey1", "Input 1").c_str());
        ImGui::SameLine(150);
        DrawInputWithCapture(cfg.ScanCode1, dirty);

        ImGui::TextUnformatted(IntegratedBow::Strings::Get("Item_InputKey2", "Input 2").c_str());
        ImGui::SameLine(150);
        DrawInputWithCapture(cfg.ScanCode2, dirty);

        ImGui::TextUnformatted(IntegratedBow::Strings::Get("Item_InputKey3", "Input 3").c_str());
        ImGui::SameLine(150);
        DrawInputWithCapture(cfg.ScanCode3, dirty);

        ImGui::Spacing();

        ImGui::TextDisabled(
            "%s", IntegratedBow::Strings::Get("Item_InputComboTip",
                                              "All inputs different from -1 must be held together at the same time.")
                      .c_str());

        ImGui::Spacing();

        ImGui::TextDisabled("%s",
                            IntegratedBow::Strings::Get(
                                "Item_InputGeneralTip",
                                "Press 'Cap' to capture an input. Supports keyboard, mouse and gamepad. -1 disables.")
                                .c_str());
    }

    void DrawGeneralTab(IntegratedBow::BowConfig& cfg, bool& dirty) {
        DrawModeSection(cfg, dirty);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        DrawAutoDrawAndDelaySection(cfg, dirty);
    }

    void DrawControlsTab(IntegratedBow::BowConfig& cfg, bool& dirty) { DrawHotkeysSection(cfg, dirty); }

    void DrawHudTab(bool&) {
        ImGui::TextDisabled("%s", IntegratedBow::Strings::Get("HUD_Empty", "HUD tab is empty for now.").c_str());
    }

    void DrawPatchesTab(IntegratedBow::BowConfig& cfg, bool& dirty) {
        if (bool hideFromJson = cfg.hideEquippedFromJsonPatch; ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_HideEquippedJsonPatch", "Hide extra equipped items from JSON list")
                    .c_str(),
                &hideFromJson)) {
            cfg.hideEquippedFromJsonPatch = hideFromJson;
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedBow::Strings::Get("Item_HideEquippedJsonPatch_Tip",
                                                  "When enabled, items whose FormIDs are listed in HiddenEquipped.json "
                                                  "will be unequipped while the bow is active and re-equipped on exit.")
                          .c_str());
        }

        if (bool blockUnequip = cfg.BlockUnequip; ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_BlockUnequipPatch", "Block unequip of bow/ammo during bow-mode entry")
                    .c_str(),
                &blockUnequip)) {
            cfg.BlockUnequip = blockUnequip;
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s",
                IntegratedBow::Strings::Get(
                    "Item_BlockUnequipPatch_Tip",
                    "When enabled, the plugin will temporarily block UnequipObject calls for bows/crossbows and ammo "
                    "while "
                    "entering bow mode. This can mitigate external interference that forces the bow to be unequipped.")
                    .c_str());
        }

        if (bool skipEquipBowAnim = cfg.skipEquipBowAnimationPatch.load(std::memory_order_relaxed); ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_SkipEquipBowAnimPatch", "Skip bow equip animation").c_str(),
                &skipEquipBowAnim)) {
            cfg.skipEquipBowAnimationPatch.store(skipEquipBowAnim, std::memory_order_relaxed);
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", IntegratedBow::Strings::Get(
                                        "Item_SkipEquipBowAnimPatch_Tip",
                                        "When enabled, the plugin will set behavior graph variables to skip the equip "
                                        "animation when entering bow mode. Requires the Skip Equip Animation mod.")
                                        .c_str());
        }

        if (bool skipReturn = cfg.skipEquipReturnToMeleePatch.load(std::memory_order_relaxed);
            ImGui::Checkbox(IntegratedBow::Strings::Get("Item_SkipEquipReturnToMeleePatch",
                                                        "Skip equip animation on return to melee")
                                .c_str(),
                            &skipReturn)) {
            cfg.skipEquipReturnToMeleePatch.store(skipReturn, std::memory_order_relaxed);
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s",
                IntegratedBow::Strings::Get(
                    "Item_SkipEquipReturnToMelee_Tip",
                    "When enabled, the plugin will skip equip animations when restoring your previous melee weapon(s) "
                    "after exiting bow mode. Requires Skip Equip Animation.")
                    .c_str());
        }

        if (bool cancelExitDelayOnAttack = cfg.cancelHoldExitDelayOnAttackPatch.load(std::memory_order_relaxed);
            ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_CancelHoldExitDelayOnAttackPatch", "Cancel hold exit delay on attack")
                    .c_str(),
                &cancelExitDelayOnAttack)) {
            cfg.cancelHoldExitDelayOnAttackPatch.store(cancelExitDelayOnAttack, std::memory_order_relaxed);
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "%s", IntegratedBow::Strings::Get(
                          "Item_CancelHoldExitDelayOnAttackPatch_Tip",
                          "In Hold + Auto Attack, after releasing the hotkey there is a short grace period before "
                          "exiting. If you attack during that grace period (without re-holding the hotkey), "
                          "exit bow mode immediately.")
                          .c_str());
        }

        if (bool exclusiveHotkey = cfg.requireExclusiveHotkeyPatch.load(std::memory_order_relaxed); ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_RequireExclusiveHotkeyPatch", "Require exclusive hotkey press")
                    .c_str(),
                &exclusiveHotkey)) {
            cfg.requireExclusiveHotkeyPatch.store(exclusiveHotkey, std::memory_order_relaxed);
            dirty = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s",
                              IntegratedBow::Strings::Get(
                                  "Item_RequireExclusiveHotkeyPatch_Tip",
                                  "When enabled, bow mode only activates if the bow hotkey is pressed "
                                  "exclusively (no other keys/buttons held), ignoring character movement keys (WASD).")
                                  .c_str());
        }
    }
}

void __stdcall IntegratedBow_UI::DrawSettings() {
    auto& cfg = IntegratedBow::GetBowConfig();
    bool dirty = false;

    DrawPendingApplyBar(cfg);
    ImGui::Spacing();

    if (ImGui::BeginTabBar("INTEGRATEDBOW_TABS")) {
        if (ImGui::BeginTabItem(IntegratedBow::Strings::Get("Tab_General", "General").c_str())) {
            DrawGeneralTab(cfg, dirty);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(IntegratedBow::Strings::Get("Tab_Controls", "Controls").c_str())) {
            DrawControlsTab(cfg, dirty);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(IntegratedBow::Strings::Get("Tab_HUD", "HUD").c_str())) {
            DrawHudTab(dirty);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(IntegratedBow::Strings::Get("Tab_Patches", "Patches").c_str())) {
            DrawPatchesTab(cfg, dirty);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (dirty) {
        g_pending = true;
    }
}

void IntegratedBow_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) {
        return;
    }

    SKSEMenuFramework::SetSection(IntegratedBow::Strings::Get("SectionName", "Integrated Bow"));
    SKSEMenuFramework::AddSectionItem(IntegratedBow::Strings::Get("SectionItem_Settings", "Settings"),
                                      IntegratedBow_UI::DrawSettings);
}