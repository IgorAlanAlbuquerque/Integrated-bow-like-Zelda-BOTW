#include "BowConfig.h"

#include <SimpleIni.h>

#include <string>

#include "BowConfigPath.h"
#include "PCH.h"

using namespace std::string_literals;

namespace {
    int _getInt(CSimpleIniA& ini, const char* sec, const char* k, int defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
        char* end = nullptr;
        const long d = std::strtol(v, &end, 10);
        return (end && *end == '\0') ? static_cast<int>(d) : defVal;
    }

    std::string _getStr(CSimpleIniA& ini, const char* sec, const char* k, const char* defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        return v ? std::string{v} : std::string{defVal};
    }

    float _getFloat(CSimpleIniA& ini, const char* sec, const char* k, float defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
        char* end = nullptr;
        float out = std::strtof(v, &end);
        return end && end != v ? out : defVal;
    }

    bool _getBool(CSimpleIniA& ini, const char* sec, const char* k, bool defVal) {
        const char* v = ini.GetValue(sec, k, nullptr);
        if (!v) return defVal;
        return (std::strcmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
    }
}

namespace IntegratedBow {
    std::filesystem::path BowConfig::IniPath() {
        const auto& base = GetThisDllDir();
        return base / "IntegratedBow.ini";
    }

    void BowConfig::Load() {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        if (SI_Error rc = ini.LoadFile(path.string().c_str()); rc < 0) {
            return;
        }

        const auto modeStr = _getStr(ini, "Input", "Mode", "Hold");
        BowMode newMode = BowMode::Hold;
        if (_stricmp(modeStr.c_str(), "Press") == 0) {
            newMode = BowMode::Press;
        } else if (_stricmp(modeStr.c_str(), "Smart") == 0) {
            newMode = BowMode::Smart;
        }
        mode.store(newMode, std::memory_order_relaxed);

        const int k1 = _getInt(ini, "Input", "KeyboardScanCode1", 0x2F);
        const int k2 = _getInt(ini, "Input", "KeyboardScanCode2", -1);
        const int k3 = _getInt(ini, "Input", "KeyboardScanCode3", -1);

        keyboardScanCode1.store(k1, std::memory_order_relaxed);
        keyboardScanCode2.store(k2, std::memory_order_relaxed);
        keyboardScanCode3.store(k3, std::memory_order_relaxed);

        const int gp1 = _getInt(ini, "Input", "GamepadButton1", -1);
        const int gp2 = _getInt(ini, "Input", "GamepadButton2", -1);
        const int gp3 = _getInt(ini, "Input", "GamepadButton3", -1);

        gamepadButton1.store(gp1, std::memory_order_relaxed);
        gamepadButton2.store(gp2, std::memory_order_relaxed);
        gamepadButton3.store(gp3, std::memory_order_relaxed);

        const int bow = _getInt(ini, "Bow", "ChosenBowFormID", 0);
        chosenBowFormID.store(static_cast<std::uint32_t>(bow), std::memory_order_relaxed);

        const int arrow = _getInt(ini, "Bow", "PreferredArrowFormID", 0);
        preferredArrowFormID.store(static_cast<std::uint32_t>(arrow), std::memory_order_relaxed);

        {
            bool autoDraw = true;

            if (const char* v = ini.GetValue("Input", "AutoDrawEnabled", nullptr); !v) {
                v = ini.GetValue("Bow", "AutoDrawEnabled", nullptr);
                if (!v) {
                    autoDrawEnabled.store(autoDraw, std::memory_order_relaxed);
                } else {
                    autoDraw = (std::strcmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
                    autoDrawEnabled.store(autoDraw, std::memory_order_relaxed);
                }
            } else {
                autoDraw = (std::strcmp(v, "true") == 0 || std::strcmp(v, "1") == 0);
                autoDrawEnabled.store(autoDraw, std::memory_order_relaxed);
            }
        }

        {
            float delay = sheathedDelaySeconds.load(std::memory_order_relaxed);

            const char* delayStr = ini.GetValue("Input", "SheathedDelaySeconds", nullptr);
            if (!delayStr) {
                delayStr = ini.GetValue("Bow", "SheathedDelaySeconds", nullptr);
            }

            if (delayStr) {
                char* end = nullptr;
                const float v = std::strtof(delayStr, &end);
                if (end && *end == '\0' && v >= 0.0f) {
                    delay = v;
                }
            }

            sheathedDelaySeconds.store(delay, std::memory_order_relaxed);
        }

        noLeftBlockPatch = _getBool(ini, "Patches", "NoLeftBlockPatch", false);
        hideEquippedFromJsonPatch = _getBool(ini, "Patches", "HideEquippedFromJsonPatch", false);
        BlockUnequip = _getBool(ini, "Patches", "BlockPatch", false);
    }

    void BowConfig::Save() const {
        using enum BowMode;
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());

        const auto m = mode.load(std::memory_order_relaxed);
        const char* modeStr = nullptr;
        switch (m) {
            case Press:
                modeStr = "Press";
                break;
            case Smart:
                modeStr = "Smart";
                break;
            case Hold:
            default:
                modeStr = "Hold";
                break;
        }

        ini.SetValue("Input", "Mode", modeStr);
        const int k1 = keyboardScanCode1.load(std::memory_order_relaxed);
        const int k2 = keyboardScanCode2.load(std::memory_order_relaxed);
        const int k3 = keyboardScanCode3.load(std::memory_order_relaxed);
        ini.SetLongValue("Input", "KeyboardScanCode1", static_cast<long>(k1));
        ini.SetLongValue("Input", "KeyboardScanCode2", static_cast<long>(k2));
        ini.SetLongValue("Input", "KeyboardScanCode3", static_cast<long>(k3));
        const int gp1 = gamepadButton1.load(std::memory_order_relaxed);
        const int gp2 = gamepadButton2.load(std::memory_order_relaxed);
        const int gp3 = gamepadButton3.load(std::memory_order_relaxed);
        ini.SetLongValue("Input", "GamepadButton1", static_cast<long>(gp1));
        ini.SetLongValue("Input", "GamepadButton2", static_cast<long>(gp2));
        ini.SetLongValue("Input", "GamepadButton3", static_cast<long>(gp3));
        ini.SetLongValue("Bow", "ChosenBowFormID", static_cast<long>(chosenBowFormID.load(std::memory_order_relaxed)));
        ini.SetLongValue("Bow", "PreferredArrowFormID",
                         static_cast<long>(preferredArrowFormID.load(std::memory_order_relaxed)));
        ini.SetBoolValue("Input", "AutoDrawEnabled", autoDrawEnabled.load(std::memory_order_relaxed));
        ini.SetDoubleValue("Input", "SheathedDelaySeconds",
                           static_cast<double>(sheathedDelaySeconds.load(std::memory_order_relaxed)));
        ini.SetBoolValue("Patches", "NoLeftBlockPatch", noLeftBlockPatch);
        ini.SetBoolValue("Patches", "HideEquippedFromJsonPatch", hideEquippedFromJsonPatch);
        ini.SetBoolValue("Patches", "BlockPatch", BlockUnequip);

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    BowConfig& GetBowConfig() {
        static BowConfig g{};  // NOSONAR: Static state
        return g;
    }
}
