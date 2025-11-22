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
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    InitializeLogger();

    auto& cfg = IntegratedBow::GetBowConfig();
    cfg.Load();
    IntegratedBow::Strings::Load();

    const bool hold = (cfg.mode.load(std::memory_order_relaxed) == IntegratedBow::BowMode::Hold);
    BowInput::SetHoldMode(hold);
    BowInput::SetKeyScanCodes(cfg.keyboardScanCode1.load(std::memory_order_relaxed),
                              cfg.keyboardScanCode2.load(std::memory_order_relaxed),
                              cfg.keyboardScanCode3.load(std::memory_order_relaxed));

    BowInput::SetGamepadButtons(cfg.gamepadButton1.load(std::memory_order_relaxed),
                                cfg.gamepadButton2.load(std::memory_order_relaxed),
                                cfg.gamepadButton3.load(std::memory_order_relaxed));

    SKSE::AllocTrampoline(1 << 14);

    auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (!message) return;
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            Hooks::Install_Hooks();
            IntegratedBow_UI::Register();
        }
        if (message->type == SKSE::MessagingInterface::kPostLoadGame) {
            auto const& cfg = IntegratedBow::GetBowConfig();
            const auto bowID = cfg.chosenBowFormID.load(std::memory_order_relaxed);
            if (bowID != 0) {
                auto* bow = RE::TESForm::LookupByID<RE::TESObjectWEAP>(bowID);
                if (bow) {
                    BowState::LoadChosenBow(bow);
                } else {
                    spdlog::warn("IntegratedBow: saved bow FormID 0x{:08X} not found, ignoring", bowID);
                }
            }
        }
    });

    return true;
}