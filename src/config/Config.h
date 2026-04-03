#pragma once
#include <atomic>
#include <filesystem>

namespace IntegratedBow {
    enum class BowMode : std::uint32_t {
        Hold = 0,
        Press = 1,
        Smart = 2,
    };

    struct BowConfig {
        std::atomic<BowMode> mode{BowMode::Hold};

        std::atomic<int> ScanCode1{0x2F};
        std::atomic<int> ScanCode2{-1};
        std::atomic<int> ScanCode3{-1};

        std::atomic<std::uint32_t> chosenBowFormID{0};
        std::atomic<std::uint16_t> chosenBowUniqueID{0};
        std::atomic<std::uint32_t> preferredArrowFormID{0};

        std::atomic<bool> autoDrawEnabled{true};
        std::atomic<float> sheathedDelaySeconds{1.0f};
        bool noLeftBlockPatch = false;
        bool hideEquippedFromJsonPatch = false;
        bool BlockUnequip = false;
        std::atomic_bool skipEquipBowAnimationPatch{false};
        std::atomic_bool skipEquipReturnToMeleePatch{false};
        std::atomic_bool cancelHoldExitDelayOnAttackPatch{false};
        std::atomic_bool requireExclusiveHotkeyPatch{false};

        void Load();
        void Save() const;

    private:
        static std::filesystem::path IniPath();
    };

    BowConfig& GetBowConfig();
}
