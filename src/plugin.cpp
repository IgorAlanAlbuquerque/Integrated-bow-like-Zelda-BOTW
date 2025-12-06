#include <Windows.h>
#ifdef GetObject
    #undef GetObject
#endif

#include <filesystem>

#include "BowConfig.h"
#include "BowInput.h"
#include "BowState.h"
#include "BowStrings.h"
#include "Hooks.h"
#include "PCH.h"
#include "UI_IntegratedBow.h"
#include "patchs/HiddenItemsPatch.h"
#include "patchs/UnMapBlock.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

namespace {
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
        if (!message) return;
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            Hooks::Install_Hooks();
            IntegratedBow_UI::Register();
        }
        if (message->type == SKSE::MessagingInterface::kPostLoadGame) {
            BowInput::RegisterAnimEventSink();
            auto const& cfg = IntegratedBow::GetBowConfig();

            if (const auto bowID = cfg.chosenBowFormID.load(std::memory_order_relaxed); bowID != 0) {
                auto* bow = RE::TESForm::LookupByID<RE::TESObjectWEAP>(bowID);
                if (bow) {
                    BowState::LoadChosenBow(bow);
                } else {
                    spdlog::warn("IntegratedBow: saved bow FormID 0x{:08X} not found, ignoring", bowID);
                }
            }
            UnMapBlock::SetNoLeftBlockPatch(cfg.noLeftBlockPatch);
            HiddenItemsPatch::SetEnabled(cfg.hideEquippedFromJsonPatch);
        }
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();

    auto& cfg = IntegratedBow::GetBowConfig();
    cfg.Load();
    IntegratedBow::Strings::Load();

    HiddenItemsPatch::LoadConfigFile();

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