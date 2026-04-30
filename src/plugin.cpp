#include <Windows.h>
#ifdef GetObject
    #undef GetObject
#endif

#include <filesystem>
#include <mutex>

#include "Config/Config.h"
#include "DIII/SelectedArrowCondition.h"
#include "DIII/SelectedBowCondition.h"
#include "DIII_API.h"
#include "Hooks.h"
#include "Input/InputHandler.h"
#include "PCH.h"
#include "Patchs/HiddenItemsPatch.h"
#include "Persistence/SaveBowDB.h"
#include "UI/Strings.h"
#include "UI/UI_IntegratedBow.h"

namespace {
    static std::string g_pendingEssPath;    // NOSONAR
    static std::string g_currentEssPath;    // NOSONAR
    static std::once_flag g_dbOnce;         // NOSONAR
    static DIII::IAPI* g_diiiApi{nullptr};  // NOSONAR

    void EnsureSaveBowDBLoaded() {
        std::call_once(g_dbOnce, []() { IntegratedBow::SaveBowDB::Get().LoadFromDisk(); });
    }

    void ApplyPrefsToConfig(const IntegratedBow::SaveBowPrefs& p) {
        auto& cfg = IntegratedBow::GetBowConfig();
        cfg.chosenBowFormID.store(p.bow, std::memory_order_relaxed);
        cfg.chosenBowUniqueID.store(p.bowUniqueID, std::memory_order_relaxed);
        cfg.preferredArrowFormID.store(p.arrow, std::memory_order_relaxed);
    }

    void RefreshDIIIIcons() {
        if (!g_diiiApi) {
            spdlog::warn("[DIII] API not available; skipping RefreshItemIconData");
            return;
        }

        spdlog::info("[DIII] RefreshItemIconData called");
    }

    IntegratedBow::SaveBowPrefs ReadPrefsFromConfig() {
        auto const& cfg = IntegratedBow::GetBowConfig();
        IntegratedBow::SaveBowPrefs p{};
        p.bow = cfg.chosenBowFormID.load(std::memory_order_relaxed);
        p.bowUniqueID = cfg.chosenBowUniqueID.load(std::memory_order_relaxed);
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

    void GlobalMessageHandler(SKSE::MessagingInterface::Message* message) {  // NOSONAR
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
                HiddenItemsPatch::SetEnabled(cfg.hideEquippedFromJsonPatch);
                break;
            }

            case SKSE::MessagingInterface::kPostLoadGame: {
                {
                    auto const& cfg = IntegratedBow::GetBowConfig();
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

                    if (prefs.bow != 0) {
                        if (auto* bow = RE::TESForm::LookupByID<RE::TESObjectWEAP>(prefs.bow)) {
                            BowState::LoadChosenBow(bow, prefs.bowUniqueID);
                            BowState::EnsureChosenBowInInventory();
                        } else {
                            BowState::ClearChosenBow();
                        }
                    } else {
                        BowState::ClearChosenBow();
                    }

                    if (prefs.arrow != 0) {
                        if (auto* ammo = RE::TESForm::LookupByID<RE::TESAmmo>(prefs.arrow)) {
                            BowState::SetPreferredArrow(ammo);
                        } else {
                            BowState::SetPreferredArrow(nullptr);
                        }
                    } else {
                        BowState::SetPreferredArrow(nullptr);
                    }
                } else {
                    ApplyPrefsToConfig(IntegratedBow::SaveBowPrefs{});
                    BowState::ClearChosenBow();
                    BowState::SetPreferredArrow(nullptr);
                }

                RefreshDIIIIcons();
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

    void OnDIIIMessage(SKSE::MessagingInterface::Message* msg) {
        if (!msg) {
            return;
        }

        if (msg->type != DIII::kMessage_GetAPI) {
            return;
        }

        auto* api = static_cast<DIII::IAPI*>(msg->data);
        if (!api) {
            return;
        }
        g_diiiApi = api;

        const bool bowOk = api->RegisterCondition(
            "integratedBowSelected", [](const Json::Value& value, RE::FormType) -> std::unique_ptr<DIII::ICondition> {
                return std::make_unique<IntegratedBow::SelectedBowCondition>(value);
            });

        const bool arrowOk = api->RegisterCondition(
            "integratedArrowSelected", [](const Json::Value& value, RE::FormType) -> std::unique_ptr<DIII::ICondition> {
                return std::make_unique<IntegratedBow::SelectedArrowCondition>(value);
            });

        spdlog::info("[DIII] RegisterCondition bow={} arrow={}", bowOk, arrowOk);
    }
}
#ifdef SKYRIM_SUPPORT_AE
SKSEPluginVersion = []() {
    SKSE::PluginVersionData v{};
    v.PluginVersion(REL::Version{1, 5, 0, 0});
    v.PluginName("INTEGRATEDBOW");
    v.AuthorName("LoliManiaco");
    v.UsesAddressLibrary();
    v.UsesNoStructs();
    v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST});
    return v;
}();
#else
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info) {
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = "INTEGRATEDBOW";
    a_info->version = 15;

    if (const auto ver = a_skse->RuntimeVersion(); ver != SKSE::RUNTIME_SSE_LATEST) {
        SKSE::log::critical("Unsupported runtime version {}, this plugin is only compatible with version {}",
                            ver.string(), SKSE::RUNTIME_SSE_LATEST.string());
        return false;
    }
    return true;
}
#endif

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();

    auto& cfg = IntegratedBow::GetBowConfig();
    cfg.Load();
    IntegratedBow::Strings::Load();

    BowInput::SetMode(std::to_underlying(cfg.mode.load(std::memory_order_relaxed)));
    BowInput::SetCombo(cfg.ScanCode1.load(std::memory_order_relaxed), cfg.ScanCode2.load(std::memory_order_relaxed),
                       cfg.ScanCode3.load(std::memory_order_relaxed));

    if (const auto mi = SKSE::GetMessagingInterface()) {
        mi->RegisterListener(GlobalMessageHandler);
        DIII::ListenForRegistration(OnDIIIMessage);
    }

    return true;
}