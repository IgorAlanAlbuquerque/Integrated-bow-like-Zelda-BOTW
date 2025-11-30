#include "UI_IntegratedBow.h"

#include <array>

#include "BowConfig.h"
#include "BowInput.h"
#include "BowStrings.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"

using IntegratedBow::BowMode;
using IntegratedBow::GetBowConfig;

namespace {
    bool g_pending = false;  // NOSONAR: estado global
    using enum IntegratedBow::BowMode;
    bool g_capturingHotkey = false;  // NOSONAR

    IntegratedBow::BowMode GetModeFromConfig(IntegratedBow::BowConfig& cfg) {
        return cfg.mode.load(std::memory_order_relaxed);
    }

    void DrawModeSection(IntegratedBow::BowConfig& cfg, bool& dirty) {
        BowMode mode = GetModeFromConfig(cfg);
        int modeIndex = (mode == Press) ? 1 : 0;

        const auto& lblMode = IntegratedBow::Strings::Get("Item_InputMode", "Bow mode");
        const auto& lblHold = IntegratedBow::Strings::Get("Item_InputMode_Hold", "Hold");
        const auto& lblPress = IntegratedBow::Strings::Get("Item_InputMode_Press", "Press");

        const std::array<const char*, 2> items = {lblHold.c_str(), lblPress.c_str()};

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(lblMode.c_str(), &modeIndex, items.data(), static_cast<int>(items.size()))) {
            cfg.mode.store(modeIndex == 1 ? Press : Hold, std::memory_order_relaxed);
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

        const bool hold = (cfg.mode.load(std::memory_order_relaxed) == BowMode::Hold);
        BowInput::SetHoldMode(hold);

        BowInput::SetKeyScanCodes(cfg.keyboardScanCode1.load(std::memory_order_relaxed),
                                  cfg.keyboardScanCode2.load(std::memory_order_relaxed),
                                  cfg.keyboardScanCode3.load(std::memory_order_relaxed));

        BowInput::SetGamepadButtons(cfg.gamepadButton1.load(std::memory_order_relaxed),
                                    cfg.gamepadButton2.load(std::memory_order_relaxed),
                                    cfg.gamepadButton3.load(std::memory_order_relaxed));

        g_pending = false;

        ImGui::SameLine();
        ImGui::TextDisabled("✓");
    }

    void DrawAutoDrawAndDelaySection(IntegratedBow::BowConfig& cfg) {
        if (bool autoDraw = cfg.autoDrawEnabled.load(std::memory_order_relaxed); ImGui::Checkbox(
                IntegratedBow::Strings::Get("Item_AutoDrawEnabled", "Auto draw arrow").c_str(), &autoDraw)) {
            cfg.autoDrawEnabled.store(autoDraw, std::memory_order_relaxed);
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
}

void __stdcall IntegratedBow_UI::DrawConfig() {
    auto& cfg = GetBowConfig();

    const auto& title = IntegratedBow::Strings::Get("MenuTitle", "Integrated Bow");
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    bool dirty = false;

    DrawModeSection(cfg, dirty);
    DrawKeyboardHotkeysSection(cfg, dirty);
    DrawGamepadHotkeysSection(cfg, dirty);

    if (dirty) {
        g_pending = true;
    }

    DrawPendingAndApplySection(cfg);

    ImGui::Separator();
    DrawAutoDrawAndDelaySection(cfg);
    DrawFinalTip();
}

void IntegratedBow_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection(IntegratedBow::Strings::Get("SectionName", "Integrated Bow"));
    SKSEMenuFramework::AddSectionItem(IntegratedBow::Strings::Get("SectionItemName", "Input"),
                                      IntegratedBow_UI::DrawConfig);
}
