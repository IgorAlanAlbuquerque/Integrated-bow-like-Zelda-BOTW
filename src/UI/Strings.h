#pragma once
#include <string>
#include <string_view>

namespace IntegratedBow::Strings {
    void Load();

    std::string Get(std::string_view key, std::string_view fallback);
}
