#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <Windows.h>

using namespace std::literals;

#define DEBUG

#ifdef DEBUG
    #define BOW_DEBUG_LOG(...) spdlog::info(__VA_ARGS__)
#else
    #define BOW_DEBUG_LOG(...) ((void)0)
#endif
