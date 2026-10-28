#include "Parry.h"

#include <cstdint>

#include "DirectionalHandler.h"
#include "Gameplay.h"
#include "Settings.h"
#include "logger.h"

namespace Parry {
    PRECISION_API::PreHitCallbackReturn OnPreHit(const PRECISION_API::PrecisionHitData& a_hitData) {
        PRECISION_API::PreHitCallbackReturn result{};

        auto* aggressor = a_hitData.attacker;
        auto* defender = a_hitData.target ? a_hitData.target->As<RE::Actor>() : nullptr;

        if (!aggressor || !defender) {
            return result;
        }

        // Parry window must be active.
        std::int32_t parryWindow = 0;

        if (!defender->GetGraphVariableInt("ParryFHB", parryWindow) || parryWindow != 1) {
            return result;
        }

        // Read directional graph variables.
        std::int32_t attackDirection = 0;
        std::int32_t parryDirection = 0;

        const bool gotAttackDirection = aggressor->GetGraphVariableInt("AttackDirectionFHB", attackDirection);

        const bool gotParryDirection = defender->GetGraphVariableInt("CameraMovementFHB", parryDirection);

        if (!gotAttackDirection || !gotParryDirection) {
            if (Settings::debugLogging) {
                logger::info("[PARRY] Direction read failed | AttackRead={} | ParryRead={}", gotAttackDirection,
                             gotParryDirection);
            }

            return result;
        }

        // Same directional comparison used by Blocking.cpp.
        const bool successful = DirectionalHandler::IsSuccessfulBlock(attackDirection, parryDirection);

        if (Settings::debugLogging) {
            logger::info(
                "[PARRY] Attacker={} | Defender={} | "
                "ParryWindow={} | AttackDirection={} | "
                "ParryDirection={} | Result={}",
                aggressor->GetName(), defender->GetName(), parryWindow, attackDirection, parryDirection,
                successful ? "SUCCESS" : "FAIL");
        }

        if (!successful) {
            return result;
        }

        const bool powerAttack = aggressor->IsPowerAttacking();

        auto* spell =
            RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x803, "For Honor Balance Patch.esp");

        if (spell) {
            if (auto* caster = defender->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
                caster->CastSpellImmediate(spell, false, defender, 1, false, 0.0f, defender);
            }
        }

        // Apply existing block spells, sparks, and flashes.
        Gameplay::ApplyBlockSpells(aggressor, defender, powerAttack);

        // Force large recoil on the attacker.
        Gameplay::ApplyLargeStagger(aggressor, defender);

        if (Settings::debugLogging) {
            logger::info(
                "[PARRY] SUCCESS | Attacker={} | Defender={} | "
                "AttackDirection={} | ParryDirection={} | Recoil=LARGE",
                aggressor->GetName(), defender->GetName(), attackDirection, parryDirection);
        }

        return result;
    }
}