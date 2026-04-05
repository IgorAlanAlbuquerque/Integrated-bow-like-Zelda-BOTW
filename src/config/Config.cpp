#include "Config.h"

#include <SimpleIni.h>

#include <string>

#include "Config/ConfigPath.h"
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

        const int c1 = _getInt(ini, "Input", "KeyboardScanCode1", 0x2F);
        const int c2 = _getInt(ini, "Input", "KeyboardScanCode2", -1);
        const int c3 = _getInt(ini, "Input", "KeyboardScanCode3", -1);

        ScanCode1.store(c1, std::memory_order_relaxed);
        ScanCode2.store(c2, std::memory_order_relaxed);
        ScanCode3.store(c3, std::memory_order_relaxed);

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

        hideEquippedFromJsonPatch = _getBool(ini, "Patches", "HideEquippedFromJsonPatch", false);
        BlockUnequip = _getBool(ini, "Patches", "BlockPatch", false);
        skipEquipBowAnimationPatch.store(_getBool(ini, "Patches", "SkipEquipBowAnimationPatch", false),
                                         std::memory_order_relaxed);
        skipEquipReturnToMeleePatch.store(_getBool(ini, "Patches", "SkipEquipReturnToMeleePatch", false),
                                          std::memory_order_relaxed);
        cancelHoldExitDelayOnAttackPatch.store(_getBool(ini, "Patches", "CancelHoldExitDelayOnAttackPatch", false),
                                               std::memory_order_relaxed);
        requireExclusiveHotkeyPatch.store(_getBool(ini, "Patches", "RequireExclusiveHotkeyPatch", false),
                                          std::memory_order_relaxed);
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
        const int c1 = ScanCode1.load(std::memory_order_relaxed);
        const int c2 = ScanCode2.load(std::memory_order_relaxed);
        const int c3 = ScanCode3.load(std::memory_order_relaxed);
        ini.SetLongValue("Input", "ScanCode1", static_cast<long>(c1));
        ini.SetLongValue("Input", "ScanCode2", static_cast<long>(c2));
        ini.SetLongValue("Input", "ScanCode3", static_cast<long>(c3));
        ini.SetBoolValue("Input", "AutoDrawEnabled", autoDrawEnabled.load(std::memory_order_relaxed));
        ini.SetDoubleValue("Input", "SheathedDelaySeconds",
                           static_cast<double>(sheathedDelaySeconds.load(std::memory_order_relaxed)));
        ini.SetBoolValue("Patches", "HideEquippedFromJsonPatch", hideEquippedFromJsonPatch);
        ini.SetBoolValue("Patches", "BlockPatch", BlockUnequip);
        ini.SetBoolValue("Patches", "SkipEquipBowAnimationPatch",
                         skipEquipBowAnimationPatch.load(std::memory_order_relaxed));
        ini.SetBoolValue("Patches", "SkipEquipReturnToMeleePatch",
                         skipEquipReturnToMeleePatch.load(std::memory_order_relaxed));
        ini.SetBoolValue("Patches", "CancelHoldExitDelayOnAttackPatch",
                         cancelHoldExitDelayOnAttackPatch.load(std::memory_order_relaxed));
        ini.SetBoolValue("Patches", "RequireExclusiveHotkeyPatch",
                         requireExclusiveHotkeyPatch.load(std::memory_order_relaxed));

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    BowConfig& GetBowConfig() {
        static BowConfig g{};  // NOSONAR: Static state
        return g;
    }
}
