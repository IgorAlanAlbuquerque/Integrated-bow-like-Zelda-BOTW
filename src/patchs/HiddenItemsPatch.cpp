#include "HiddenItemsPatch.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#include "../PCH.h"

namespace {
    bool g_enabled = false;
    std::vector<RE::FormID> g_formIds;

    std::filesystem::path GetJsonPath() { return IntegratedBow::GetThisDllDir() / "HiddenEquipped.json"; }

    void ParseJsonLikeFile(const std::string& text, std::vector<RE::FormID>& out) {
        out.clear();
        spdlog::info("[HiddenItemsPatch][ParseJson] parsing text with size={}", text.size());

        std::regex reHex(R"(0x[0-9A-Fa-f]+)");
        std::regex reDec(R"(\b[0-9]{1,10}\b)");

        std::smatch m;
        auto searchStart = text.cbegin();

        const auto tryParse = [](const std::string& s, int base, RE::FormID& outId) {
            try {
                unsigned long v = std::stoul(s, nullptr, base);
                if (v > std::numeric_limits<RE::FormID>::max()) {
                    spdlog::warn("[HiddenItemsPatch][ParseJson] value={} exceeds max FormID", v);
                    return false;
                }
                outId = static_cast<RE::FormID>(v);
                return true;
            } catch (const std::exception&) {
                spdlog::warn("[HiddenItemsPatch][ParseJson] invalid value '{}' for base={} -> skipping", s, base);
                return false;
            }
        };

        // Parse Hexadecimal
        while (std::regex_search(searchStart, text.cend(), m, reHex)) {
            RE::FormID id{};
            const auto s = m.str();
            if (tryParse(s, 16, id)) {
                out.push_back(id);
                spdlog::info("[HiddenItemsPatch][ParseJson] found hex ID={} (0x{:08X})", s, id);
            }
            searchStart = m.suffix().first;
        }

        // Parse Decimal
        searchStart = text.cbegin();
        while (std::regex_search(searchStart, text.cend(), m, reDec)) {
            const auto s = m.str();
            if (s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
                searchStart = m.suffix().first;
                continue;
            }

            if (RE::FormID id{}; tryParse(s, 10, id)) {
                out.push_back(id);
                spdlog::info("[HiddenItemsPatch][ParseJson] found decimal ID={} (0x{:08X})", s, id);
            }
            searchStart = m.suffix().first;
        }

        spdlog::info("[HiddenItemsPatch][ParseJson] parsing complete, total IDs found={}", out.size());
    }
}

void HiddenItemsPatch::LoadConfigFile() {
    g_formIds.clear();
    spdlog::info("[HiddenItemsPatch][LoadConfig] LoadConfigFile start");

    auto path = GetJsonPath();
    spdlog::info("[HiddenItemsPatch][LoadConfig] config file path: {}", path.string());

    if (!std::filesystem::exists(path)) {
        spdlog::info("[HiddenItemsPatch][LoadConfig] file does not exist at path {}", path.string());
        return;
    }

    std::ifstream in(path);
    if (!in) {
        spdlog::error("[HiddenItemsPatch][LoadConfig] failed to open file at {}", path.string());
        return;
    }

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    spdlog::info("[HiddenItemsPatch][LoadConfig] loaded file content size={}", content.size());

    ParseJsonLikeFile(content, g_formIds);

    spdlog::info("[HiddenItemsPatch][LoadConfig] parsed file, g_formIds size={}", g_formIds.size());

    std::sort(g_formIds.begin(), g_formIds.end());
    spdlog::info("[HiddenItemsPatch][LoadConfig] sorted g_formIds");

    g_formIds.erase(std::unique(g_formIds.begin(), g_formIds.end()), g_formIds.end());
    spdlog::info("[HiddenItemsPatch][LoadConfig] removed duplicates, final size={}", g_formIds.size());
}
void HiddenItemsPatch::SetEnabled(bool enabled) {
    g_enabled = enabled;
    spdlog::info("[HiddenItemsPatch][SetEnabled] g_enabled set to {}", g_enabled);
}

bool HiddenItemsPatch::IsEnabled() {
    spdlog::info("[HiddenItemsPatch][IsEnabled] returning {}", g_enabled);
    return g_enabled;
}

const std::vector<RE::FormID>& HiddenItemsPatch::GetHiddenFormIDs() {
    spdlog::info("[HiddenItemsPatch][GetHiddenFormIDs] returning vector of size={}", g_formIds.size());
    return g_formIds;
}
