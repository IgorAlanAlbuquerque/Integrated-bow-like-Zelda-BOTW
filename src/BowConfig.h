#pragma once
#include <atomic>
#include <filesystem>

namespace IntegratedBow {
    enum class BowMode : std::uint32_t {
        Hold = 0,
        Press = 1,
    };

    struct BowConfig {
        std::atomic<BowMode> mode{BowMode::Hold};
        std::atomic<std::uint32_t> keyboardScanCode{0x2F};
        std::atomic<int> gamepadButton{-1};
        std::atomic<std::uint32_t> chosenBowFormID{0};

        void Load();
        void Save() const;

    private:
        static std::filesystem::path IniPath();
    };

    BowConfig& GetBowConfig();
}
