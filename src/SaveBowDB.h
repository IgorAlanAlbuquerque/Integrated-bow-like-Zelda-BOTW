#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace IntegratedBow {

    struct TransparentSaveKeyHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
        std::size_t operator()(const std::string& s) const noexcept { return (*this)(std::string_view{s}); }
        std::size_t operator()(const char* s) const noexcept { return (*this)(std::string_view{s}); }
    };

    struct SaveBowPrefs {
        std::uint32_t bow{0};
        std::uint32_t arrow{0};
    };

    class SaveBowDB {
    public:
        static SaveBowDB& Get();

        void LoadFromDisk();
        void SaveToDisk();

        bool IsLoadOK() const;

        void Upsert(std::string_view saveKey, const SaveBowPrefs& prefs);
        bool TryGet(std::string_view saveKey, SaveBowPrefs& outPrefs) const;
        void Erase(std::string_view saveKey);

        bool TryGetNormalized(std::string_view normalizedKey, SaveBowPrefs& outPrefs) const;
        void EraseNormalized(std::string_view normalizedKey);

        static std::string NormalizeKeyCopy(std::string_view key);
        static std::filesystem::path JsonPath();
        static std::string NormalizeKey(std::string key);

    private:
        SaveBowDB() = default;

        bool _loadOK{true};

        mutable std::mutex _mtx;
        std::unordered_map<std::string, SaveBowPrefs, TransparentSaveKeyHash, std::equal_to<>> _bySave;
    };

}
