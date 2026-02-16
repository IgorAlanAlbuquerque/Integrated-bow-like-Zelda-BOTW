#include "InputGate.h"

#include "RE/A/ActorState.h"
#include "RE/B/BSFixedString.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/U/UI.h"

namespace BowInput {
    bool InputGate::IsAttackEvent(std::string_view ue) noexcept {
        using namespace std::literals;
        return ue == "Left Attack Attack/Block"sv || ue == "Right Attack/Block"sv;
    }

    bool IsPlayerStateBlockingInput(RE::PlayerCharacter* player) noexcept {
        if (!player) return false;

        auto const* st = player->AsActorState();
        if (!st) return false;

        // Knocked / down / getup etc.
        if (st->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
            return true;
        }

        // Sitting / sleeping / mounting

        if (const auto sit = st->GetSitSleepState();
            sit == RE::SIT_SLEEP_STATE::kIsSitting || sit == RE::SIT_SLEEP_STATE::kIsSleeping ||
            sit == RE::SIT_SLEEP_STATE::kWantToSit || sit == RE::SIT_SLEEP_STATE::kWaitingForSitAnim ||
            sit == RE::SIT_SLEEP_STATE::kWantToSleep || sit == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim ||
            sit == RE::SIT_SLEEP_STATE::kWantToWake || sit == RE::SIT_SLEEP_STATE::kWantToStand) {
            return true;
        }

        // Bleedout/essential down etc (você já tem helper)
        if (st->IsBleedingOut() || st->IsUnconscious()) {
            return true;
        }

        // Diálogo "vanilla": esse flag existe no seu ActorState2
        if (st->actorState2.talkingToPlayer) {
            return true;
        }

        return false;
    }

    bool InputGate::IsInputBlockedByMenus() {
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return false;
        if (ui->GameIsPaused()) return true;

        static const RE::BSFixedString inventoryMenu{"InventoryMenu"};
        static const RE::BSFixedString magicMenu{"MagicMenu"};
        static const RE::BSFixedString statsMenu{"StatsMenu"};
        static const RE::BSFixedString mapMenu{"MapMenu"};
        static const RE::BSFixedString journalMenu{"Journal Menu"};
        static const RE::BSFixedString favoritesMenu{"FavoritesMenu"};
        static const RE::BSFixedString containerMenu{"ContainerMenu"};
        static const RE::BSFixedString barterMenu{"BarterMenu"};
        static const RE::BSFixedString trainingMenu{"Training Menu"};
        static const RE::BSFixedString craftingMenu{"Crafting Menu"};
        static const RE::BSFixedString giftMenu{"GiftMenu"};
        static const RE::BSFixedString lockpickingMenu{"Lockpicking Menu"};
        static const RE::BSFixedString sleepWaitMenu{"Sleep/Wait Menu"};
        static const RE::BSFixedString loadingMenu{"Loading Menu"};
        static const RE::BSFixedString mainMenu{"Main Menu"};
        static const RE::BSFixedString console{"Console"};
        static const RE::BSFixedString dialogueMenu{"Dialogue Menu"};
        static const RE::BSFixedString dialogueTopicMenu{"Dialogue Topic Menu"};
        static const RE::BSFixedString ostimMenu{"OstimSceneMenu"};
        static const RE::BSFixedString faderMenu{"Fader Menu"};

        if (static const RE::BSFixedString mcm{"Mod Configuration Menu"};
            ui->IsMenuOpen(inventoryMenu) || ui->IsMenuOpen(magicMenu) || ui->IsMenuOpen(statsMenu) ||
            ui->IsMenuOpen(mapMenu) || ui->IsMenuOpen(journalMenu) || ui->IsMenuOpen(favoritesMenu) ||
            ui->IsMenuOpen(containerMenu) || ui->IsMenuOpen(barterMenu) || ui->IsMenuOpen(trainingMenu) ||
            ui->IsMenuOpen(craftingMenu) || ui->IsMenuOpen(giftMenu) || ui->IsMenuOpen(lockpickingMenu) ||
            ui->IsMenuOpen(sleepWaitMenu) || ui->IsMenuOpen(loadingMenu) || ui->IsMenuOpen(mainMenu) ||
            ui->IsMenuOpen(console) || ui->IsMenuOpen(mcm) || ui->IsMenuOpen(dialogueMenu) ||
            ui->IsMenuOpen(dialogueTopicMenu) || ui->IsMenuOpen(ostimMenu) || ui->IsMenuOpen(faderMenu)) {
            return true;
        }

        if (auto* player = RE::PlayerCharacter::GetSingleton(); IsPlayerStateBlockingInput(player)) {
            return true;
        }

        return false;
    }
}
