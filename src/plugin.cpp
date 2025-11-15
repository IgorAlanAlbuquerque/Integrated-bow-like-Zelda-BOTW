#include <Windows.h>

#include <filesystem>

#include "BowConfig.h"
#include "BowInput.h"
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
    BowInput::SetKeyScanCode(cfg.keyboardScanCode.load(std::memory_order_relaxed));
    BowInput::SetGamepadButton(cfg.gamepadButton.load(std::memory_order_relaxed));

    SKSE::AllocTrampoline(1 << 14);
    Hooks::Install_Hooks();
    BowInput::RegisterInputHandler();

    auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (!message) return;
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            IntegratedBow_UI::Register();
        }
    });

    return true;
}