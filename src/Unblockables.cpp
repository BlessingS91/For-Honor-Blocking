#include "Unblockables.h"

#include <RE/Skyrim.h>

#include <cstdint>

#include "Events.h"
#include "Settings.h"
#include "logger.h"

namespace Unblockables {
    bool IsUnblockable(RE::Actor* attacker) {
        if (!attacker) return false;
        std::int32_t unblockable = 0;
        return attacker->GetGraphVariableInt("UnblockableFHB", unblockable) && unblockable == 1;
    }

    bool OnPrecisionPreHit(RE::Actor* attacker, RE::Actor* defender) {
        if (!attacker || !defender || !IsUnblockable(attacker)) {
            return false;
        }

        std::int32_t allBlock = 0;
        defender->GetGraphVariableInt("AllBlockFHB", allBlock);
        const bool blocking = defender->IsBlocking() || allBlock == 1;
        const int reaction = blocking ? 1 : 2;

        // 1 = shield slide, 2 = stumbling knockdown.
        // The existing Events.cpp timer clears this short-lived graph signal after 200 ms.
        FHB::Events::ActivateUnblockableReaction(defender, reaction);

        if (Settings::debugLogging) {
            logger::info("[UNBLOCKABLE] Attacker={} | Defender={} | Blocking={} | Reaction={}",
                         attacker->GetName(), defender->GetName(), blocking, reaction);
        }
        return true;
    }

    std::optional<bool> GetCreatureSwingUnblockable(RE::Actor* actor, float& damageMult) {
        // Original Events.cpp creature helper. Return nullopt when this isn't
        // a qualifying creature swing or its current ATKD is unavailable;
        // this means Events.cpp must leave UnblockableFHB unchanged.
        // A value (true OR false) means the attack data was found and should
        // activate/reset the existing graph variable exactly as before.
        if (!actor || actor->IsPlayerRef()) return std::nullopt;

        auto* race = actor->GetRace();
        auto* process = actor->GetActorRuntimeData().currentProcess;

        static auto* creatureKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13795);
        static auto* animalKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13798);
        static auto* dwarvenKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x1397A);

        const bool isCreature = race && ((creatureKeyword && race->HasKeyword(creatureKeyword)) ||
                                         (animalKeyword && race->HasKeyword(animalKeyword)) ||
                                         (dwarvenKeyword && race->HasKeyword(dwarvenKeyword)));

        if (!isCreature || !process || !process->high) return std::nullopt;

        const auto& attackData = process->high->attackData;
        if (!attackData) return std::nullopt;

        damageMult = attackData->data.damageMult;
        return damageMult > 1.0f;
    }
}
