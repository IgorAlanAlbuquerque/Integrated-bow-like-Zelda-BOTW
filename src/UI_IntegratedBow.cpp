#include "UI_IntegratedBow.h"

#include "BowConfig.h"
#include "BowInput.h"
#include "BowStrings.h"
#include "PCH.h"
#include "SKSEMenuFramework.h"

#define IM_ARRAYSIZE(_ARR) ((int)(sizeof(_ARR) / sizeof(*_ARR)))
using IntegratedBow::BowMode;
using IntegratedBow::GetBowConfig;

void __stdcall IntegratedBow_UI::DrawConfig() {
    auto& cfg = GetBowConfig();

    // Título
    const auto& title = IntegratedBow::Strings::Get("MenuTitle", "Integrated Bow");
    ImGui::TextUnformatted(title.c_str());
    ImGui::Separator();

    static bool pending = false;
    bool dirty = false;

    {
        BowMode mode = cfg.mode.load(std::memory_order_relaxed);
        int modeIndex = (mode == BowMode::Press) ? 1 : 0;

        const auto& lblMode = IntegratedBow::Strings::Get("Item_InputMode", "Bow mode");
        const auto& lblHold = IntegratedBow::Strings::Get("Item_InputMode_Hold", "Hold");
        const auto& lblPress = IntegratedBow::Strings::Get("Item_InputMode_Press", "Press");

        const char* items[] = {lblHold.c_str(), lblPress.c_str()};

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(lblMode.c_str(), &modeIndex, items, IM_ARRAYSIZE(items))) {
            cfg.mode.store(modeIndex == 1 ? BowMode::Press : BowMode::Hold, std::memory_order_relaxed);
            dirty = true;
        }
    }

    {
        int key = static_cast<int>(cfg.keyboardScanCode.load(std::memory_order_relaxed));
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_KeyboardKey", "Keyboard scan code").c_str(), &key)) {
            if (key < 0) key = 0;
            if (key > 255) key = 255;
            cfg.keyboardScanCode.store(static_cast<std::uint32_t>(key), std::memory_order_relaxed);
            dirty = true;
        }
    }

    {
        int btn = cfg.gamepadButton.load(std::memory_order_relaxed);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputInt(IntegratedBow::Strings::Get("Item_GamepadButton", "Gamepad button (-1 = disabled)").c_str(),
                            &btn)) {
            if (btn < -1) btn = -1;
            if (btn > 31) btn = 31;
            cfg.gamepadButton.store(btn, std::memory_order_relaxed);
            dirty = true;
        }
    }

    if (dirty) pending = true;

    if (pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", IntegratedBow::Strings::Get("Item_Pending", "(pending)").c_str());
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!pending);
    bool pressed =
        ImGui::Button(IntegratedBow::Strings::Get("Item_Apply", "Apply changes").c_str(), ImVec2{140.0f, 0.0f});
    ImGui::EndDisabled();

    if (pressed) {
        cfg.Save();

        const bool hold = (cfg.mode.load(std::memory_order_relaxed) == BowMode::Hold);
        BowInput::SetHoldMode(hold);
        BowInput::SetKeyScanCode(cfg.keyboardScanCode.load(std::memory_order_relaxed));
        BowInput::SetGamepadButton(cfg.gamepadButton.load(std::memory_order_relaxed));

        pending = false;

        ImGui::SameLine();
        ImGui::TextDisabled("✓");
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", IntegratedBow::Strings::Get("Item_Tip", "Tip: -1 disables gamepad binding.").c_str());
}

void IntegratedBow_UI::Register() {
    if (!SKSEMenuFramework::IsInstalled()) return;

    SKSEMenuFramework::SetSection(IntegratedBow::Strings::Get("SectionName", "Integrated Bow").c_str());
    SKSEMenuFramework::AddSectionItem(IntegratedBow::Strings::Get("SectionItemName", "Input").c_str(),
                                      IntegratedBow_UI::DrawConfig);
}
