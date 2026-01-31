#include "UI_IntegratedBow.h"

#include <array>

#include "BowConfig.h"
#include "BowInput.h"
#include "BowStrings.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"
#include "patchs/HiddenItemsPatch.h"
#include "patchs/UnMapBlock.h"

using IntegratedBow::BowMode;
using IntegratedBow::GetBowConfig;

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace {
    bool g_pending = false;  // NOSONAR: estado global
    using enum IntegratedBow::BowMode;
    bool g_capturingHotkey = false;  // NOSONAR

    IntegratedBow::BowMode GetModeFromConfig(IntegratedBow::BowConfig& cfg) {
        return cfg.mode.load(std::memory_order_relaxed);
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

    void DrawKeyboardHotkeysSection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        int key1 = cfg.keyboardScanCode1.load(std::memory_order_relaxed);
        int key2 = cfg.keyboardScanCode2.load(std::memory_order_relaxed);
        int key3 = cfg.keyboardScanCode3.load(std::memory_order_relaxed);

        const auto& groupLabelK = IntegratedBow::Strings::Get("Item_KeyboardKey", "Keyboard keys (scan codes)");
        const auto& tipTextK = IntegratedBow::Strings::Get("Item_KeyboardComboTip",
                                                           "All non -1 keys must be held together at the same time.");

        ImGui::TextUnformatted(groupLabelK.c_str());
        ImGui::PushID("KeyboardHotkeys");

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_KeyboardKey1", "Key 1").c_str(), &key1)) {
            if (key1 < -1) key1 = -1;
            cfg.keyboardScanCode1.store(key1, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_KeyboardKey2", "Key 2").c_str(), &key2)) {
            if (key2 < -1) key2 = -1;
            cfg.keyboardScanCode2.store(key2, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_KeyboardKey3", "Key 3").c_str(), &key3)) {
            if (key3 < -1) key3 = -1;
            cfg.keyboardScanCode3.store(key3, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::PopID();
        ImGui::TextDisabled("%s", tipTextK.c_str());
    }

    void DrawGamepadHotkeysSection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        int gp1 = cfg.gamepadButton1.load(std::memory_order_relaxed);
        int gp2 = cfg.gamepadButton2.load(std::memory_order_relaxed);
        int gp3 = cfg.gamepadButton3.load(std::memory_order_relaxed);

        const auto& groupLabelP = IntegratedBow::Strings::Get("Item_GamepadButton", "Gamepad buttons");
        const auto& tipTextP = IntegratedBow::Strings::Get(
            "Item_GamepadComboTip", "All non -1 buttons must be held together at the same time.");

        ImGui::TextUnformatted(groupLabelP.c_str());
        ImGui::PushID("GamepadHotkeys");

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_GamepadButton1", "Btn 1").c_str(), &gp1)) {
            if (gp1 < -1) gp1 = -1;
            cfg.gamepadButton1.store(gp1, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_GamepadButton2", "Btn 2").c_str(), &gp2)) {
            if (gp2 < -1) gp2 = -1;
            cfg.gamepadButton2.store(gp2, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_GamepadButton3", "Btn 3").c_str(), &gp3)) {
            if (gp3 < -1) gp3 = -1;
            cfg.gamepadButton3.store(gp3, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::Spacing();
        if (!g_capturingHotkey) {
            if (ImGui::Button(
                    IntegratedBow::Strings::Get("Item_CaptureHotkey", "Capture next key/button after press esc")
                        .c_str())) {
                BowInput::RequestGamepadCapture();
                g_capturingHotkey = true;
            }
        } else {
            ImGui::TextDisabled("%s", IntegratedBow::Strings::Get(
                                          "Item_CaptureHotkey_Waiting",
                                          "Press ESC to close the menu then press a keyboard key or gamepad button...")
                                          .c_str());

            int encoded = BowInput::PollCapturedGamepadButton();
            if (encoded != -1) {
                if (encoded >= 0) {
                    int code = encoded;
                    cfg.keyboardScanCode1.store(code, std::memory_order_relaxed);
                    cfg.keyboardScanCode2.store(-1, std::memory_order_relaxed);
                    cfg.keyboardScanCode3.store(-1, std::memory_order_relaxed);
                    BowInput::SetKeyScanCodes(code, -1, -1);
                } else {
                    int code = -(encoded + 1);
                    cfg.gamepadButton1.store(code, std::memory_order_relaxed);
                    cfg.gamepadButton2.store(-1, std::memory_order_relaxed);
                    cfg.gamepadButton3.store(-1, std::memory_order_relaxed);
                    BowInput::SetGamepadButtons(code, -1, -1);
                }

                dirty = true;
                g_capturingHotkey = false;
            }
        }

        ImGui::PopID();
        ImGui::TextDisabled("%s", tipTextP.c_str());
    }

    void DrawPendingAndApplySection(IntegratedBow::BowConfig& cfg) {
        if (g_pending) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", IntegratedBow::Strings::Get("Item_Pending", "(pending)").c_str());
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(!g_pending);
        bool pressed =
            ImGui::Button(IntegratedBow::Strings::Get("Item_Apply", "Apply changes").c_str(), ImVec2{140.0f, 0.0f});
        ImGui::EndDisabled();

        if (!pressed) {
            return;
        }

        cfg.Save();

        BowInput::SetMode(std::to_underlying(cfg.mode.load(std::memory_order_relaxed)));

        BowInput::SetKeyScanCodes(cfg.keyboardScanCode1.load(std::memory_order_relaxed),
                                  cfg.keyboardScanCode2.load(std::memory_order_relaxed),
                                  cfg.keyboardScanCode3.load(std::memory_order_relaxed));

        BowInput::SetGamepadButtons(cfg.gamepadButton1.load(std::memory_order_relaxed),
                                    cfg.gamepadButton2.load(std::memory_order_relaxed),
                                    cfg.gamepadButton3.load(std::memory_order_relaxed));

        UnMapBlock::SetNoLeftBlockPatch(cfg.noLeftBlockPatch);
        HiddenItemsPatch::SetEnabled(cfg.hideEquippedFromJsonPatch);

        g_pending = false;

        ImGui::SameLine();
        ImGui::TextDisabled("✓");
    }

    void DrawAutoDrawAndDelaySection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        if (bool autoDraw = cfg.autoDrawEnabled.load(std::memory_order_relaxed); ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_AutoDrawEnabled", "Auto draw arrow").c_str(), &autoDraw)) {
            cfg.autoDrawEnabled.store(autoDraw, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s", IntegratedBow::Strings::Get(
                      "Item_AutoDrawEnabled_Tip",
                      "If enabled, the bow will automatically start drawing an arrow while holding the hotkey.")
                      .c_str());

        float delaySec = cfg.sheathedDelaySeconds.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(150.0f);

        if (ImGui::InputFloat(IntegratedBow::Strings::Get("Item_sheathedDelay", "S delay (s)").c_str(), &delaySec, 0.1f,
                              1.0f, "%.2f")) {
            if (delaySec < 0.0f) delaySec = 0.0f;
            cfg.sheathedDelaySeconds.store(delaySec, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            IntegratedBow::Strings::Get(
                                "Item_sheathedDelay_Tip",
                                "Time in seconds after releasing the key (in hold mode) for the weapon to be sheathed.")
                                .c_str());
    }

    void DrawFinalTip() {
        ImGui::Separator();
        ImGui::TextDisabled("%s", IntegratedBow::Strings::Get("Item_Tip", "Tip: -1 disables gamepad binding.").c_str());
    }

    void DrawPatchesSection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        bool noLeftBlock = cfg.noLeftBlockPatch;
        bool hideFromJson = cfg.hideEquippedFromJsonPatch;
        bool blockUnequip = cfg.BlockUnequip;
        bool noChosenTag = cfg.noChosenTag;
        bool skipEquipBowAnim = cfg.skipEquipBowAnimationPatch.load(std::memory_order_relaxed);
        bool skipReturn = cfg.skipEquipReturnToMeleePatch.load(std::memory_order_relaxed);
        bool cancelExitDelayOnAttack = cfg.cancelHoldExitDelayOnAttackPatch.load(std::memory_order_relaxed);

        const auto& lbl = IntegratedBow::Strings::Get("Item_NoLeftBlockPatch", "Disable vanilla left-hand block (LT)");
        const auto& tip = IntegratedBow::Strings::Get(
            "Item_NoLeftBlockPatch_Tip",
            "Unmaps the default left-hand block (leftAttack) from the gamepad LT. "
            "Use this if another mod provides a separate block key and you want to use LT only as the bow hotkey.");

        if (ImGui::Checkbox(lbl.c_str(), &noLeftBlock)) {
            cfg.noLeftBlockPatch = noLeftBlock;
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", tip.c_str());

        ImGui::Separator();

        const auto& lblJson =
            IntegratedBow::Strings::Get("Item_HideEquippedJsonPatch", "Hide extra equipped items from JSON list");
        const auto& tipJson =
            IntegratedBow::Strings::Get("Item_HideEquippedJsonPatch_Tip",
                                        "When enabled, items whose FormIDs are listed in HiddenEquipped.json "
                                        "will be unequipped while the bow is active and re-equipped on exit.");

        if (ImGui::Checkbox(lblJson.c_str(), &hideFromJson)) {
            cfg.hideEquippedFromJsonPatch = hideFromJson;
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", tipJson.c_str());

        ImGui::Separator();

        const auto& lblBlockUnequip =
            IntegratedBow::Strings::Get("Item_BlockUnequipPatch", "Block unequip of bow/ammo during bow-mode entry");
        const auto& tipBlockUnequip = IntegratedBow::Strings::Get(
            "Item_BlockUnequipPatch_Tip",
            "When enabled, the plugin will temporarily block UnequipObject calls for bows/crossbows and ammo while "
            "entering bow mode. This can mitigate external interference that forces the bow to be unequipped.");

        if (ImGui::Checkbox(lblBlockUnequip.c_str(), &blockUnequip)) {
            cfg.BlockUnequip = blockUnequip;
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", tipBlockUnequip.c_str());

        ImGui::Separator();

        const auto& lblNoChosenTag =
            IntegratedBow::Strings::Get("Item_NoChosenTagPatch", "Disable chosen-bow inventory tag");
        const auto& tipNoChosenTag = IntegratedBow::Strings::Get(
            "Item_NoChosenTagPatch_Tip",
            "When enabled, the plugin will NOT apply the chosen-bow tag to the selected bow instance. "
            "Use this if you don't want the marker/rename or if another mod expects the original instance metadata.");

        if (ImGui::Checkbox(lblNoChosenTag.c_str(), &noChosenTag)) {
            cfg.noChosenTag = noChosenTag;
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", tipNoChosenTag.c_str());

        ImGui::Separator();

        const auto& lblSkipEquip =
            IntegratedBow::Strings::Get("Item_SkipEquipBowAnimPatch", "Skip bow equip animation (SkipEquipAnimation)");
        const auto& tipSkipEquip =
            IntegratedBow::Strings::Get("Item_SkipEquipBowAnimPatch_Tip",
                                        "When enabled, the plugin will set behavior graph variables to skip the equip "
                                        "animation when entering bow mode. Requires the Skip Equip Animation mod.");

        if (ImGui::Checkbox(lblSkipEquip.c_str(), &skipEquipBowAnim)) {
            cfg.skipEquipBowAnimationPatch.store(skipEquipBowAnim, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", tipSkipEquip.c_str());

        ImGui::Separator();

        const auto& tipSkipReturn = IntegratedBow::Strings::Get(
            "Item_SkipEquipReturnToMelee_Tip",
            "When enabled, the plugin will also skip equip animations when restoring your previous melee weapon(s) "
            "after exiting bow mode. Requires Skip Equip Animation.");

        if (ImGui::Checkbox("Skip equip animation on return to melee", &skipReturn)) {
            cfg.skipEquipReturnToMeleePatch.store(skipReturn, std::memory_order_relaxed);
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%s", tipSkipEquip.c_str());

        const auto& lblCancelExitDelay =
            IntegratedBow::Strings::Get("Item_CancelHoldExitDelayOnAttackPatch", "Cancel hold exit delay on attack");
        const auto& tipCancelExitDelay = IntegratedBow::Strings::Get(
            "Item_CancelHoldExitDelayOnAttackPatch_Tip",
            "In Hold + Auto mode, after releasing the hotkey there is a short grace period before "
            "exiting. If you attack during that grace period (without re-holding the hotkey), "
            "exit bow mode immediately.");

        if (ImGui::Checkbox(lblCancelExitDelay.c_str(), &cancelExitDelayOnAttack)) {
            cfg.cancelHoldExitDelayOnAttackPatch.store(cancelExitDelayOnAttack, std::memory_order_relaxed);
            dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", tipCancelExitDelay.c_str());
    }
}

void __stdcall IntegratedBow_UI::DrawInputTab() {
    auto& cfg = IntegratedBow::GetBowConfig();

    const auto& title = IntegratedBow::Strings::Get("MenuTitle", "Integrated Bow - Input");
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    bool dirty = false;

    DrawModeSection(cfg, dirty);
    DrawKeyboardHotkeysSection(cfg, dirty);
    DrawGamepadHotkeysSection(cfg, dirty);
    DrawFinalTip();

    if (dirty) {
        g_pending = true;
    }

    DrawPendingAndApplySection(cfg);
}

void __stdcall IntegratedBow_UI::DrawBowTab() {
    auto& cfg = IntegratedBow::GetBowConfig();

    const auto& title = IntegratedBow::Strings::Get("MenuTitle_Bow", "Integrated Bow - Bow");
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    bool dirty = false;

    DrawAutoDrawAndDelaySection(cfg, dirty);

    if (dirty) {
        g_pending = true;
    }

    DrawPendingAndApplySection(cfg);
}

void __stdcall IntegratedBow_UI::DrawPatchesTab() {
    auto& cfg = IntegratedBow::GetBowConfig();

    const auto& title = IntegratedBow::Strings::Get("MenuTitle_Patches", "Integrated Bow - Patches");
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    bool dirty = false;

    DrawPatchesSection(cfg, dirty);

    if (dirty) {
        g_pending = true;
    }

    DrawPendingAndApplySection(cfg);
}

void IntegratedBow_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection(IntegratedBow::Strings::Get("SectionName", "Integrated Bow"));

    SKSEMenuFramework::AddSectionItem(IntegratedBow::Strings::Get("SectionItem_Input", "Input"),
                                      IntegratedBow_UI::DrawInputTab);

    SKSEMenuFramework::AddSectionItem(IntegratedBow::Strings::Get("SectionItem_Bow", "Bow"),
                                      IntegratedBow_UI::DrawBowTab);

    SKSEMenuFramework::AddSectionItem(IntegratedBow::Strings::Get("SectionItem_Patches", "Patches"),
                                      IntegratedBow_UI::DrawPatchesTab);
}
