#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../PCH.h"

namespace HiddenItemsPatch {
    void LoadConfigFile();
    void SetEnabled(bool enabled);
    bool IsEnabled();

    const std::vector<RE::FormID>& GetHiddenFormIDs();
}
