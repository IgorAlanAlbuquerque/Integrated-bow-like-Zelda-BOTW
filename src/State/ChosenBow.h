#pragma once
#include <cstdint>

#include "PCH.h"

namespace BowState {

    // ── arco escolhido ─────────────────────────────────────────────────────

    void LoadChosenBow(RE::TESObjectWEAP* bow);
    void ClearChosenBow();
    bool EnsureChosenBowInInventory();
    void SetChosenBow(RE::TESObjectWEAP* bow, RE::ExtraDataList* extra);

    // ── flecha preferida ───────────────────────────────────────────────────

    RE::TESAmmo* GetPreferredArrow();
    void SetPreferredArrow(RE::TESAmmo* ammo);

    // ── utilitário de inventário (usado também em LoadoutRestore) ──────────

    [[nodiscard]] RE::ExtraDataList* FindAnyInstanceExtraForBase(RE::TESBoundObject* base);
    [[nodiscard]] RE::ExtraDataList* ResolveLiveExtra(RE::TESBoundObject* base, RE::ExtraDataList* candidate);
}