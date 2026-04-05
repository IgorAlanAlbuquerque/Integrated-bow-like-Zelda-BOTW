#pragma once

#include <optional>

#include "DIII_API.h"
#include "PCH.h"
#include "State/ChosenBow.h"

namespace IntegratedBow {
    class SelectedArrowCondition final : public DIII::ICondition {
    public:
        explicit SelectedArrowCondition(const Json::Value&) {}

        bool Match(RE::InventoryEntryData* entry) const override {
            if (!entry || !entry->GetObject()) {
                return false;
            }

            auto* ammo = entry->GetObject()->As<RE::TESAmmo>();
            if (!ammo) {
                return false;
            }

            auto* preferred = BowState::GetPreferredArrow();
            if (!preferred) {
                return false;
            }

            return ammo == preferred;
        }
    };
}