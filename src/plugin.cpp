#include <Windows.h>

#include <filesystem>

#include "Hooks.h"
#include "PCH.h"

#ifndef DLLEXPORT
    #include "REL/Relocation.h"
#endif
#ifndef DLLEXPORT
    #define DLLEXPORT __declspec(dllexport)
#endif

const std::filesystem::path& ERF_GetThisDllDir();

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

    auto* messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            Hooks::Install_Hooks();
        }
    });

    return true;
}

const std::filesystem::path& ERF_GetThisDllDir() {
    static std::filesystem::path cached;

    if (static bool init = false; !init) {
        init = true;

        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&SKSEPlugin_Load), &self)) {
            wchar_t buf[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
            if (n > 0) {
                cached = std::filesystem::path(buf).parent_path();
            }
        }
    }

    return cached;
}