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
        }
        mode.store(newMode, std::memory_order_relaxed);

        const int key = _getInt(ini, "Input", "KeyboardScanCode", 0x2F);
        keyboardScanCode.store(static_cast<std::uint32_t>(key), std::memory_order_relaxed);

        const int btn = _getInt(ini, "Input", "GamepadButton", -1);
        gamepadButton.store(btn, std::memory_order_relaxed);
    }

    void BowConfig::Save() const {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = IniPath();
        ini.LoadFile(path.string().c_str());

        const auto m = mode.load(std::memory_order_relaxed);
        const char* modeStr = (m == BowMode::Press) ? "Press" : "Hold";

        ini.SetValue("Input", "Mode", modeStr);
        ini.SetLongValue("Input", "KeyboardScanCode",
                         static_cast<long>(keyboardScanCode.load(std::memory_order_relaxed)));
        ini.SetLongValue("Input", "GamepadButton", static_cast<long>(gamepadButton.load(std::memory_order_relaxed)));

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        ini.SaveFile(path.string().c_str());
    }

    BowConfig& GetBowConfig() {
        static BowConfig g{};
        return g;
    }
}
