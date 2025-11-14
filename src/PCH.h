#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

using namespace std::literals;

namespace stl {
    template <class T>
    void write_thunk_jump(std::uintptr_t a_src) {
        auto& trampoline = SKSE::GetTrampoline();
        const auto orig = trampoline.write_branch<5>(a_src, reinterpret_cast<std::uintptr_t>(&T::thunk));
        T::func = reinterpret_cast<typename T::Fn*>(orig);
    }

    template <class T, std::size_t size = 5>
    void write_thunk_call(std::uintptr_t a_src) {
        auto& trampoline = SKSE::GetTrampoline();
        const auto orig = trampoline.write_call<size>(a_src, reinterpret_cast<std::uintptr_t>(&T::thunk));
        T::func = reinterpret_cast<typename T::Fn*>(orig);
    }

    template <class F, std::size_t offset, class T>
    void write_vfunc() {
        REL::Relocation<std::uintptr_t> vtbl{F::VTABLE[offset]};
        const auto orig = vtbl.write_vfunc(T::idx, &T::thunk);
        T::func = reinterpret_cast<typename T::Fn*>(orig);
    }

    template <class F, class T>
    void write_vfunc() {
        write_vfunc<F, 0, T>();
    }
}
