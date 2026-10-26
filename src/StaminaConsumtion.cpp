#include <algorithm>

#include "Settings.h"
#include "StaminaConsumption.h"
#include "logger.h"

namespace StaminaConsumption {
    namespace {
        float GetBlockStaminaCost(RE::Actor* a_attacker) {
            if (!a_attacker) {
                return 0.0f;
            }

            auto* weapon = a_attacker->GetCurrentWeapon();

            if (!weapon) {
                return Settings::staminaUnarmed;
            }

            switch (weapon->GetWeaponType()) {
                case RE::WEAPON_TYPE::kOneHandSword:
                    return Settings::staminaOneHandSword;

                case RE::WEAPON_TYPE::kOneHandDagger:
                    return Settings::staminaDagger;

                case RE::WEAPON_TYPE::kOneHandAxe:
                    return Settings::staminaOneHandAxe;

                case RE::WEAPON_TYPE::kOneHandMace:
                    return Settings::staminaMace;

                case RE::WEAPON_TYPE::kTwoHandSword:
                    return Settings::staminaTwoHandSword;

                case RE::WEAPON_TYPE::kTwoHandAxe:
                    return Settings::staminaTwoHandAxe;

                case RE::WEAPON_TYPE::kBow:
                    return Settings::staminaBow;

                case RE::WEAPON_TYPE::kCrossbow:
                    return Settings::staminaCrossbow;

                case RE::WEAPON_TYPE::kStaff:
                    return Settings::staminaStaff;

                case RE::WEAPON_TYPE::kHandToHandMelee:
                    return Settings::staminaUnarmed;

                default:
                    return Settings::staminaOther;
            }
        }

        void ConsumeBlockStamina(RE::Actor* a_defender, RE::Actor* a_attacker) {
            if (!a_defender || !a_attacker) {
                return;
            }

            const float cost = GetBlockStaminaCost(a_attacker);

            if (cost <= 0.0f) {
                return;
            }

            auto* actorValueOwner = a_defender->AsActorValueOwner();

            if (!actorValueOwner) {
                return;
            }

            const float currentStamina = actorValueOwner->GetActorValue(RE::ActorValue::kStamina);

            if (currentStamina <= 0.0f) {
                return;
            }

            const float actualCost = std::min(cost, currentStamina);

            actorValueOwner->DamageActorValue(RE::ActorValue::kStamina, actualCost);

            if (Settings::debugLogging) {
                logger::info(
                    "BLOCK STAMINA | "
                    "Defender={} | "
                    "Attacker={} | "
                    "WeaponType={} | "
                    "Cost={} | "
                    "Stamina={} -> {}",
                    a_defender->GetName(), a_attacker->GetName(),
                    static_cast<int>(a_attacker->GetCurrentWeapon() ? a_attacker->GetCurrentWeapon()->GetWeaponType()
                                                                    : RE::WEAPON_TYPE::kHandToHandMelee),
                    actualCost, currentStamina, currentStamina - actualCost);
            }
        }
    }