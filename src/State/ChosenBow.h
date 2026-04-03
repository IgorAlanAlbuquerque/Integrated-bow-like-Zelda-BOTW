#pragma once
#include <cstdint>

#include "PCH.h"

namespace BowState {

    void LoadChosenBow(RE::TESObjectWEAP* bow, std::uint16_t uniqueID = 0);
    void ClearChosenBow();
    bool EnsureChosenBowInInventory();
    void SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra);

    RE::TESAmmo* GetPreferredArrow();
    void SetPreferredArrow(RE::TESAmmo const* ammo);

    [[nodiscard]] RE::ExtraDataList* FindAnyInstanceExtraForBase(RE::TESBoundObject const* base);
    [[nodiscard]] RE::ExtraDataList* ResolveLiveExtra(RE::TESBoundObject const* base,
                                                      RE::ExtraDataList const* candidate);
}