#pragma once
#include <vector>

#include "PCH.h"

namespace BowState {
    struct IntegratedBowState;
    struct ExtraEquippedItem;

    void CaptureWornArmorSnapshot(std::vector<ExtraEquippedItem>& out);

    [[nodiscard]] std::vector<ExtraEquippedItem> DiffArmorSnapshot(const std::vector<ExtraEquippedItem>& before,
                                                                   const std::vector<ExtraEquippedItem>& after);

    void ReequipPrevExtraEquipped(RE::Actor* actor, RE::ActorEquipManager* equipMgr);
    void AppendPrevExtraEquipped(const ExtraEquippedItem& item);
    [[nodiscard]] bool ContainsPrevExtraEquipped(const ExtraEquippedItem& item);

    void ApplyHiddenItemsPatch(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                               const std::vector<RE::FormID>& hiddenFormIDs);

    void RestorePrevWeaponsAndAmmo(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr,
                                   IntegratedBowState& st);

    void UpdateDeferredFinalize(RE::PlayerCharacter* player, RE::ActorEquipManager* equipMgr, float dt);
}