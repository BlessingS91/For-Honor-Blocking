#include "Parry.h"

#include <cstdint>

#include "DirectionalHandler.h"
#include "Events.h"
#include "Gameplay.h"
#include "Settings.h"
#include "Unblockables.h"
#include "logger.h"

namespace Parry {
    PRECISION_API::PreHitCallbackReturn OnPreHit(const PRECISION_API::PrecisionHitData& a_hitData) {
        PRECISION_API::PreHitCallbackReturn result{};
        auto* aggressor = a_hitData.attacker;
        auto* defender = a_hitData.target ? a_hitData.target->As<RE::Actor>() : nullptr;
        if (!aggressor || !defender) return result;

        // Existing unblockable state prevents parry reactions (players and creatures).
        if (Unblockables::IsUnblockable(aggressor))
            return result;

        std::int32_t counterParrySuccess = 0;
        if (aggressor->IsPlayerRef() &&
            aggressor->GetGraphVariableInt("ParrySuccessFHB", counterParrySuccess) &&
            counterParrySuccess == 1) {
            FHB::Events::ActivateParryKnockdown(defender);
        }

        std::int32_t parryState = 0;
        std::int32_t parrySuccess = 0;
        const bool gotParryState = aggressor->GetGraphVariableInt("ParryActiveFHB", parryState);
        const bool gotParrySuccess = aggressor->GetGraphVariableInt("ParrySuccessFHB", parrySuccess);
        if (gotParryState && gotParrySuccess && parryState == 1 && parrySuccess == 0) {
            result.bIgnoreHit = true;
            Gameplay::ApplyNormalStagger(aggressor, defender);
            if (Settings::debugLogging) {
                logger::info("[PARRY HIT] Failed parry | Attacker={} | Target={} | Normal recoil applied",
                             aggressor->GetName(), defender->GetName());
            }
            return result;
        }

        std::int32_t defenderParrySuccess = 0;
        if (defender->IsPlayerRef() &&
            defender->GetGraphVariableInt("ParrySuccessFHB", defenderParrySuccess) &&
            defenderParrySuccess == 1 && !aggressor->IsPlayerRef()) {
            result.bIgnoreHit = true;
            Gameplay::ApplyLargeStagger(aggressor, defender);
            return result;
        }

        std::int32_t parryWindow = 0;
        if (!defender->GetGraphVariableInt("ParryWindowFHB", parryWindow) || parryWindow != 1)
            return result;

        std::int32_t attackDirection = 0;
        std::int32_t parryDirection = 0;
        const bool gotAttackDirection = aggressor->GetGraphVariableInt("AttackDirectionFHB", attackDirection);
        const bool gotParryDirection = defender->GetGraphVariableInt("CameraMovementFHB", parryDirection);
        if (!gotAttackDirection || !gotParryDirection) {
            if (Settings::debugLogging) {
                logger::info("[PARRY] Direction read failed | AttackRead={} | ParryRead={}",
                             gotAttackDirection, gotParryDirection);
            }
            return result;
        }

        const bool successful = DirectionalHandler::IsSuccessfulBlock(attackDirection, parryDirection);
        if (Settings::debugLogging) {
            logger::info("[PARRY] Attacker={} | Defender={} | ParryWindow={} | AttackDirection={} | ParryDirection={} | Result={}",
                         aggressor->GetName(), defender->GetName(), parryWindow, attackDirection,
                         parryDirection, successful ? "SUCCESS" : "FAIL");
        }
        if (!successful) return result;

        if (defender->SetGraphVariableInt("ParrySuccessFHB", 1)) {
            if (Settings::debugLogging)
                logger::info("[PARRY] ParrySuccessFHB = 1 | Actor={}", defender->GetName());
        } else {
            logger::warn("[PARRY] Failed to set ParrySuccessFHB | Actor={}", defender->GetName());
        }

        result.bIgnoreHit = true;
        Gameplay::ApplyLargeStagger(aggressor, defender);
        Gameplay::SpawnBlockEffects(defender, false, true);

        if (defender == RE::PlayerCharacter::GetSingleton() &&
            aggressor != RE::PlayerCharacter::GetSingleton()) {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            const auto* timeSlow = dataHandler->LookupForm<RE::SpellItem>(0x80C, "For Honor Blocking.esp");
            if (auto* caster = defender->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
                if (timeSlow) {
                    caster->CastSpellImmediate(const_cast<RE::SpellItem*>(timeSlow), false, defender,
                                               1.0f, false, 0.0f, defender);
                } else {
                    logger::warn("[PARRY] Spell not found: TimeSlow_FHB");
                }
                if (Settings::debugLogging)
                    logger::info("[PARRY] Successful parry spells triggered | NPC={}", aggressor->GetName());
            } else {
                logger::warn("[PARRY] Player instant magic caster unavailable");
            }
        }
        return result;
    }
}
