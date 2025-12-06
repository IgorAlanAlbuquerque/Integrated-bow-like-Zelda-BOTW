#include "HiddenItemsPatch.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace {
    bool g_enabled = false;
    std::vector<RE::FormID> g_formIds;

    std::filesystem::path GetJsonPath() {
        auto base = IntegratedBow::GetThisDllDir();
        return base / "HiddenEquipped.json";
    }

    void ParseJsonLikeFile(const std::string& text, std::vector<RE::FormID>& out) {
        out.clear();

        std::regex reHex(R"(0x[0-9A-Fa-f]+)");
        std::regex reDec(R"(\b[0-9]{1,10}\b)");

        std::smatch m;
        auto searchStart(text.cbegin());

        while (std::regex_search(searchStart, text.cend(), m, reHex)) {
            auto s = m.str();
            auto id = static_cast<RE::FormID>(std::stoul(s, nullptr, 16));
            out.push_back(id);
            searchStart = m.suffix().first;
        }

        searchStart = text.cbegin();
        while (std::regex_search(searchStart, text.cend(), m, reDec)) {
            auto s = m.str();

            if (s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
                searchStart = m.suffix().first;
                continue;
            }
            auto id = static_cast<RE::FormID>(std::stoul(s, nullptr, 10));
            out.push_back(id);
            searchStart = m.suffix().first;
        }
    }
}

void HiddenItemsPatch::LoadConfigFile() {
    g_formIds.clear();

    auto path = GetJsonPath();
    if (!std::filesystem::exists(path)) {
        spdlog::info("[IntegratedBow] HiddenEquipped.json not found at {}", path.string());
        return;
    }

    std::ifstream in(path);
    if (!in) {
        spdlog::warn("[IntegratedBow] Failed to open {}", path.string());
        return;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ParseJsonLikeFile(content, g_formIds);

    std::sort(g_formIds.begin(), g_formIds.end());
    g_formIds.erase(std::unique(g_formIds.begin(), g_formIds.end()), g_formIds.end());

    spdlog::info("[IntegratedBow] HiddenEquipped.json loaded with {} formIDs", g_formIds.size());
}

void HiddenItemsPatch::SetEnabled(bool enabled) { g_enabled = enabled; }

bool HiddenItemsPatch::IsEnabled() { return g_enabled; }

const std::vector<RE::FormID>& HiddenItemsPatch::GetHiddenFormIDs() { return g_formIds; }
