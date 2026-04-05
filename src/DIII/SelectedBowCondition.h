#pragma once

#include <optional>

#include "DIII_API.h"
#include "PCH.h"
#include "State/SessionState.h"

namespace IntegratedBow {
    class SelectedBowCondition final : public DIII::ICondition {
    public:
        explicit SelectedBowCondition(const Json::Value&) {}

        bool Match(RE::InventoryEntryData* entry) const override {
            auto& st = BowState::Get();

            if (!entry || !entry->GetObject()) {
                return false;
            }

            if (!st.chosenBow.base || !st.chosenBow.extra) {
                return false;
            }

            if (entry->GetObject() != st.chosenBow.base) {
                return false;
            }

            if (!entry->extraLists) {
                return false;
            }

            for (auto* extra : *entry->extraLists) {
                if (extra == st.chosenBow.extra) {
                    return true;
                }
            }

            return false;
        }
    };
}