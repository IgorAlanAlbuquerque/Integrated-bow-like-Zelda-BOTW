#include "BowStrings.h"

#include <fstream>
#include <sstream>
#include <unordered_map>

#include "BowConfigPath.h"
#include "PCH.h"

namespace IntegratedBow::Strings {
    namespace {
        std::unordered_map<std::string, std::string> g_strings;
        bool g_loaded = false;

        std::filesystem::path StringsPath() { return GetThisDllDir() / "IntegratedBow_Strings.txt"; }

        void _ensureLoaded() {
            if (g_loaded) return;

            g_loaded = true;
            g_strings.clear();

            auto path = StringsPath();
            std::ifstream in(path);
            if (!in.is_open()) {
                return;
            }

            std::string line;
            while (std::getline(in, line)) {
                auto posComment = line.find('#');
                if (posComment != std::string::npos) line = line.substr(0, posComment);

                auto trim = [](std::string& s) {
                    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
                    std::size_t i = 0;
                    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                    if (i > 0) s.erase(0, i);
                };

                trim(line);
                if (line.empty()) continue;

                auto posEq = line.find('=');
                if (posEq == std::string::npos) continue;

                std::string key = line.substr(0, posEq);
                std::string value = line.substr(posEq + 1);
                trim(key);
                trim(value);
                if (!key.empty()) {
                    g_strings[std::move(key)] = std::move(value);
                }
            }
        }
    }

    void Load() {
        g_loaded = false;
        _ensureLoaded();
    }

    std::string Get(std::string_view key, std::string_view fallback) {
        _ensureLoaded();
        auto it = g_strings.find(std::string{key});
        if (it != g_strings.end()) return it->second;
        return std::string{fallback};
    }
}
