#include "HiddenItemsPatch.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace {
    bool g_enabled = false;
    std::vector<RE::FormID> g_formIds;

    std::filesystem::path GetJsonPath() { return IntegratedBow::GetThisDllDir() / "HiddenEquipped.json"; }

    void ParseJsonLikeFile(const std::string& text, std::vector<RE::FormID>& out) {
        out.clear();

        std::regex reHex(R"(0x[0-9A-Fa-f]+)");
        std::regex reDec(R"(\b[0-9]{1,10}\b)");

        std::smatch m;
        auto searchStart = text.cbegin();

        const auto tryParse = [](const std::string& s, int base, RE::FormID& outId) {
            try {
                unsigned long v = std::stoul(s, nullptr, base);
                if (v > std::numeric_limits<RE::FormID>::max()) {
                    return false;
                }
                outId = static_cast<RE::FormID>(v);
                return true;
            } catch (const std::exception&) {
                return false;
            }
        };

        while (std::regex_search(searchStart, text.cend(), m, reHex)) {
            RE::FormID id{};

            if (const auto s = m.str(); tryParse(s, 16, id)) {
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

            if (RE::FormID id{}; tryParse(s, 10, id)) {
                out.push_back(id);
            }

            searchStart = m.suffix().first;
        }
    }
}

void HiddenItemsPatch::LoadConfigFile() {
    g_formIds.clear();

    auto path = GetJsonPath();
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream in(path);
    if (!in) {
        return;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    ParseJsonLikeFile(content, g_formIds);

    std::sort(g_formIds.begin(), g_formIds.end());
    g_formIds.erase(std::unique(g_formIds.begin(), g_formIds.end()), g_formIds.end());
}
void HiddenItemsPatch::SetEnabled(bool enabled) { g_enabled = enabled; }

bool HiddenItemsPatch::IsEnabled() { return g_enabled; }

const std::vector<RE::FormID>& HiddenItemsPatch::GetHiddenFormIDs() { return g_formIds; }
