#include "HiddenItemsPatch.h"

#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <string>
#include <string_view>

#include "../PCH.h"

namespace {
    bool g_enabled = false;             // NOSONAR
    std::vector<RE::FormID> g_formIds;  // NOSONAR

    std::filesystem::path GetJsonPath() { return IntegratedBow::GetThisDllDir() / "HiddenEquipped.json"; }

    bool TryParseFormID(std::string_view s, RE::FormID& outId) {
        try {
            int base = 10;
            if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
                base = 16;
                s.remove_prefix(2);
            }
            if (s.empty()) {
                return false;
            }
            unsigned long v = std::stoul(std::string{s}, nullptr, base);
            if (v > std::numeric_limits<RE::FormID>::max()) {
                return false;
            }
            outId = static_cast<RE::FormID>(v);
            return true;
        } catch (...) {
            return false;
        }
    }

    void ParseLegacyJsonLikeFile(const std::string& text, std::vector<RE::FormID>& out) {
        out.clear();

        std::regex reHex(R"(0x[0-9A-Fa-f]+)");
        std::regex reDec(R"(\b[0-9]{1,10}\b)");

        std::smatch m;
        auto searchStart = text.cbegin();

        while (std::regex_search(searchStart, text.cend(), m, reHex)) {
            RE::FormID id{};
            if (const auto s = m.str(); TryParseFormID(s, id)) {
                out.push_back(id);
            }
            searchStart = m.suffix().first;
        }

        searchStart = text.cbegin();
        while (std::regex_search(searchStart, text.cend(), m, reDec)) {
            const auto s = m.str();
            if (s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
                searchStart = m.suffix().first;
                continue;
            }

            if (RE::FormID id{}; TryParseFormID(s, id)) {
                out.push_back(id);
            }
            searchStart = m.suffix().first;
        }
    }

    bool ParsePluginLocalIdsJson(const std::string& text, std::vector<RE::FormID>& out) {
        out.clear();

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            return false;
        }

        std::regex reObj(R"(\{[^}]*\"plugin\"\s*:\s*\"([^\"]+)\"[^}]*\"id\"\s*:\s*\"?((?:0x)?[0-9A-Fa-f]+)\"?[^}]*\})");

        std::smatch m;
        auto it = text.cbegin();
        bool any = false;

        while (std::regex_search(it, text.cend(), m, reObj)) {
            any = true;
            const std::string plugin = m[1].str();
            const std::string idStr = m[2].str();

            RE::FormID local{};
            if (!TryParseFormID(idStr, local)) {
                it = m.suffix().first;
                continue;
            }

            const auto runtime = dh->LookupFormID(local, plugin);
            if (runtime != 0) {
                out.push_back(runtime);
            }

            it = m.suffix().first;
        }

        return any;
    }
}

void HiddenItemsPatch::LoadConfigFile() {
    g_formIds.clear();

    const auto path = GetJsonPath();

    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream in(path);
    if (!in) {
        return;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const bool usedNew = ParsePluginLocalIdsJson(content, g_formIds);
    if (!usedNew) {
        ParseLegacyJsonLikeFile(content, g_formIds);
    }

    std::sort(g_formIds.begin(), g_formIds.end());
    g_formIds.erase(std::unique(g_formIds.begin(), g_formIds.end()), g_formIds.end());
}

void HiddenItemsPatch::SetEnabled(bool enabled) { g_enabled = enabled; }
bool HiddenItemsPatch::IsEnabled() { return g_enabled; }
const std::vector<RE::FormID>& HiddenItemsPatch::GetHiddenFormIDs() { return g_formIds; }