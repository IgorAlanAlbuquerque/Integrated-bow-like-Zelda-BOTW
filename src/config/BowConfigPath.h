#pragma once
#include <windows.h>
#ifdef GetObject
    #undef GetObject
#endif

#include <filesystem>
#include <vector>

namespace IntegratedBow {
    inline const std::filesystem::path& GetThisDllDir() {
        static std::filesystem::path cached = []() {  // NOSONAR
            HMODULE hMod = nullptr;

            if (!::GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&GetThisDllDir), &hMod)) {  // NOSONAR - conversão de ponteiro
                return std::filesystem::current_path();
            }

            std::vector<wchar_t> buf(MAX_PATH);
            DWORD len = 0;
            for (;;) {
                len = ::GetModuleFileNameW(hMod, buf.data(), static_cast<DWORD>(buf.size()));
                if (len == 0) {
                    return std::filesystem::current_path();
                }
                if (len < buf.size() - 1) break;
                buf.resize(buf.size() * 2);
            }

            std::filesystem::path full{std::wstring{buf.data(), len}};
            return full.parent_path();
        }();

        return cached;
    }
}
