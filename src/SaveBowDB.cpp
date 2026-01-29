#include "SaveBowDB.h"

#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

#include "BowConfigPath.h"
#include "PCH.h"

namespace IntegratedBow {

    SaveBowDB& SaveBowDB::Get() {
        static SaveBowDB inst;  // NOSONAR
        return inst;
    }

    std::filesystem::path SaveBowDB::JsonPath() { return GetThisDllDir() / "SaveBows.json"; }

    std::string SaveBowDB::NormalizeKey(std::string key) {
        for (char& c : key) {
            if (c == '/') c = '\\';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return key;
    }

    std::string SaveBowDB::NormalizeKeyCopy(std::string_view key) { return NormalizeKey(std::string{key}); }

    void SaveBowDB::LoadFromDisk() {
        std::scoped_lock lk(_mtx);
        _bySave.clear();

        std::ifstream f(JsonPath());
        if (!f.is_open()) {
            return;
        }

        nlohmann::json j;
        try {
            f >> j;
        } catch (...) {
            return;
        }

        auto parseEntry = [&](std::string_view key, const nlohmann::json& val) {
            SaveBowPrefs prefs{};

            if (val.is_object()) {
                if (auto it = val.find("bow"); it != val.end() && it->is_number_unsigned()) {
                    prefs.bow = it->get<std::uint32_t>();
                } else if (auto it2 = val.find("bow"); it2 != val.end() && it2->is_number_integer()) {
                    prefs.bow = static_cast<std::uint32_t>(it2->get<std::int64_t>());
                }

                if (auto it = val.find("arrow"); it != val.end() && it->is_number_unsigned()) {
                    prefs.arrow = it->get<std::uint32_t>();
                } else if (auto it2 = val.find("arrow"); it2 != val.end() && it2->is_number_integer()) {
                    prefs.arrow = static_cast<std::uint32_t>(it2->get<std::int64_t>());
                }
            } else if (val.is_number()) {
                prefs.bow = val.get<std::uint32_t>();
                prefs.arrow = 0;
            } else {
                return;
            }

            _bySave.try_emplace(NormalizeKey(std::string{key}), prefs);
        };

        auto itSaves = j.find("saves");
        if (itSaves != j.end() && itSaves->is_object()) {
            for (auto it = itSaves->begin(); it != itSaves->end(); ++it) {
                parseEntry(it.key(), it.value());
            }
            return;
        }

        if (j.is_object()) {
            for (auto it = j.begin(); it != j.end(); ++it) {
                parseEntry(it.key(), it.value());
            }
        }
    }

    void SaveBowDB::SaveToDisk() {
        nlohmann::json j;
        {
            std::scoped_lock lk(_mtx);

            nlohmann::json saves = nlohmann::json::object();
            for (auto const& [k, v] : _bySave) {
                saves[k] = nlohmann::json{
                    {"bow", v.bow},
                    {"arrow", v.arrow},
                };
            }

            j["version"] = 2;
            j["saves"] = std::move(saves);
        }

        std::ofstream f(JsonPath());
        if (!f.is_open()) {
            return;
        }
        f << j.dump(2);
    }

    void SaveBowDB::Upsert(std::string_view saveKey, const SaveBowPrefs& prefs) {
        const std::string norm = NormalizeKeyCopy(saveKey);
        std::scoped_lock lk(_mtx);
        _bySave[norm] = prefs;
    }

    bool SaveBowDB::TryGet(std::string_view saveKey, SaveBowPrefs& outPrefs) const {
        const std::string norm = NormalizeKeyCopy(saveKey);
        return TryGetNormalized(norm, outPrefs);
    }

    bool SaveBowDB::TryGetNormalized(std::string_view normalizedKey, SaveBowPrefs& outPrefs) const {
        std::scoped_lock lk(_mtx);

        auto it = _bySave.find(std::string{normalizedKey});
        if (it == _bySave.end()) {
            return false;
        }

        outPrefs = it->second;
        return true;
    }

    void SaveBowDB::Erase(std::string_view saveKey) {
        const std::string norm = NormalizeKeyCopy(saveKey);
        EraseNormalized(norm);
    }

    void SaveBowDB::EraseNormalized(std::string_view normalizedKey) {
        std::scoped_lock lk(_mtx);
        _bySave.erase(std::string{normalizedKey});
    }
}