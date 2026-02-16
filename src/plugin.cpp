#include <Windows.h>
#ifdef GetObject
    #undef GetObject
#endif

#include <filesystem>
#include <mutex>

#include "BowState.h"
#include "Hooks.h"
#include "PCH.h"
#include "bow_input/BowInputHandler.h"
#include "config/BowConfig.h"
#include "config/SaveBowDB.h"
#include "menu/BowStrings.h"
#include "menu/UI_IntegratedBow.h"
#include "patchs/HiddenItemsPatch.h"
#include "patchs/UnMapBlock.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

namespace {
    static std::string g_pendingEssPath;  // NOSONAR
    static std::string g_currentEssPath;  // NOSONAR
    static std::once_flag g_dbOnce;       // NOSONAR

    void EnsureSaveBowDBLoaded() {
        std::call_once(g_dbOnce, []() { IntegratedBow::SaveBowDB::Get().LoadFromDisk(); });
    }

    void ApplyPrefsToConfig(const IntegratedBow::SaveBowPrefs& p) {
        auto& cfg = IntegratedBow::GetBowConfig();
        cfg.chosenBowFormID.store(p.bow, std::memory_order_relaxed);
        cfg.preferredArrowFormID.store(p.arrow, std::memory_order_relaxed);
    }

    IntegratedBow::SaveBowPrefs ReadPrefsFromConfig() {
        auto const& cfg = IntegratedBow::GetBowConfig();
        IntegratedBow::SaveBowPrefs p{};
        p.bow = cfg.chosenBowFormID.load(std::memory_order_relaxed);
        p.arrow = cfg.preferredArrowFormID.load(std::memory_order_relaxed);
        return p;
    }

    std::string ExtractKey(std::string s) {
        if (auto pos = s.find_last_of("\\/"); pos != std::string::npos) {
            s = s.substr(pos + 1);
        }
        if (s.size() >= 4) {
            auto tail = s.substr(s.size() - 4);
            for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tail == ".ess") {
                s.resize(s.size() - 4);
            }
        }
        return s;
    }

    std::string GetSaveKeyFromMsg(const SKSE::MessagingInterface::Message* message) {
        if (!message || !message->data) {
            return {};
        }
        const auto* p = static_cast<const char*>(message->data);
        if (!p || !*p) {
            return {};
        }

        std::string key = ExtractKey(std::string{p});
        return IntegratedBow::SaveBowDB::NormalizeKey(std::move(key));
    }

    void InitializeLogger() {
        if (auto path = SKSE::log::log_directory()) {
            *path /= "IntegratedBoW.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
            auto logger = std::make_shared<spdlog::logger>("global", sink);
            spdlog::set_default_logger(logger);
            spdlog::set_level(spdlog::level::info);
            spdlog::flush_on(spdlog::level::info);
            spdlog::info("Logger iniciado.");
        }
    }

    void GlobalMessageHandler(SKSE::MessagingInterface::Message* message) {
        if (!message) {
            return;
        }

        switch (message->type) {
            case SKSE::MessagingInterface::kPreLoadGame: {
                g_pendingEssPath = GetSaveKeyFromMsg(message);
                break;
            }

            case SKSE::MessagingInterface::kDataLoaded: {
                Hooks::Install_Hooks();
                IntegratedBow_UI::Register();
                HiddenItemsPatch::LoadConfigFile();
                break;
            }

            case SKSE::MessagingInterface::kNewGame: {
                g_pendingEssPath.clear();
                g_currentEssPath.clear();

                ApplyPrefsToConfig(IntegratedBow::SaveBowPrefs{});

                auto const& cfg = IntegratedBow::GetBowConfig();
                UnMapBlock::SetNoLeftBlockPatch(cfg.noLeftBlockPatch);
                HiddenItemsPatch::SetEnabled(cfg.hideEquippedFromJsonPatch);
                break;
            }

            case SKSE::MessagingInterface::kPostLoadGame: {
                {
                    auto const& cfg = IntegratedBow::GetBowConfig();
                    UnMapBlock::SetNoLeftBlockPatch(cfg.noLeftBlockPatch);
                    HiddenItemsPatch::SetEnabled(cfg.hideEquippedFromJsonPatch);
                }

                if (message->data == nullptr || g_pendingEssPath.empty()) {
                    g_pendingEssPath.clear();
                    break;
                }

                EnsureSaveBowDBLoaded();

                g_currentEssPath = g_pendingEssPath;
                g_pendingEssPath.clear();

                if (IntegratedBow::SaveBowPrefs prefs{};
                    IntegratedBow::SaveBowDB::Get().TryGetNormalized(g_currentEssPath, prefs)) {
                    ApplyPrefsToConfig(prefs);
                } else {
                    ApplyPrefsToConfig(IntegratedBow::SaveBowPrefs{});
                }
                break;
            }

            case SKSE::MessagingInterface::kSaveGame: {
                EnsureSaveBowDBLoaded();

                std::string key = GetSaveKeyFromMsg(message);
                if (key.empty()) {
                    key = g_currentEssPath;
                }
                if (key.empty()) {
                    break;
                }

                const auto prefs = ReadPrefsFromConfig();
                IntegratedBow::SaveBowDB::Get().Upsert(key, prefs);
                IntegratedBow::SaveBowDB::Get().SaveToDisk();
                break;
            }

            case SKSE::MessagingInterface::kDeleteGame: {
                EnsureSaveBowDBLoaded();

                std::string key = GetSaveKeyFromMsg(message);
                if (key.empty()) {
                    key = g_currentEssPath;
                }
                if (key.empty()) {
                    break;
                }

                IntegratedBow::SaveBowDB::Get().Erase(key);
                IntegratedBow::SaveBowDB::Get().SaveToDisk();

                if (!g_currentEssPath.empty() && IntegratedBow::SaveBowDB::NormalizeKeyCopy(g_currentEssPath) ==
                                                     IntegratedBow::SaveBowDB::NormalizeKeyCopy(key)) {
                    g_currentEssPath.clear();
                }
                break;
            }

            default:
                break;
        }
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();

    auto& cfg = IntegratedBow::GetBowConfig();
    cfg.Load();
    IntegratedBow::Strings::Load();

    BowInput::SetMode(std::to_underlying(cfg.mode.load(std::memory_order_relaxed)));
    BowInput::SetKeyScanCodes(cfg.keyboardScanCode1.load(std::memory_order_relaxed),
                              cfg.keyboardScanCode2.load(std::memory_order_relaxed),
                              cfg.keyboardScanCode3.load(std::memory_order_relaxed));

    BowInput::SetGamepadButtons(cfg.gamepadButton1.load(std::memory_order_relaxed),
                                cfg.gamepadButton2.load(std::memory_order_relaxed),
                                cfg.gamepadButton3.load(std::memory_order_relaxed));

    SKSE::AllocTrampoline(1 << 14);

    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener(GlobalMessageHandler);
    }

    return true;
}