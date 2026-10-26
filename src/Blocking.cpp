#include "Blocking.h"

#include <string>

#include "DirectionalHandler.h"
#include "Gameplay.h"
#include "PoiseAPI.h"
#include "Settings.h"
#include "logger.h"

namespace Blocking {

    const char* GraphDirectionName(std::int32_t value) {
        switch (value) {
            case 0:
                return "Neutral";
            case 1:
                return "Top";
            case 2:
                return "Right";
            case 3:
                return "Left";
            default:
                return "0";
        }
    }
    namespace {

        float GetBlockStaminaCost(RE::Actor* a_attacker) {
            if (!a_attacker) {
                return 0.0f;
            }

            auto* equipped = a_attacker->GetEquippedObject(false);
            if (!equipped) {
                equipped = a_attacker->GetEquippedObject(true);
            }

            auto* weapon = equipped ? equipped->As<RE::TESObjectWEAP>() : nullptr;

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

        void NotifyHonorCombatReset(RE::Actor* attacker) {
            if (!attacker) {
                return;
            }

            auto* eventSource = SKSE::GetModCallbackEventSource();
            if (!eventSource) {
                logger::warn("Honor Combat reset event source unavailable");
                return;
            }

            SKSE::ModCallbackEvent event{RE::BSFixedString("HonorCombatResetAttackDirection"), RE::BSFixedString(""),
                                         0.0f, attacker};

            eventSource->SendEvent(&event);
        }

        // void SpawnBlockEffects(RE::Actor* defender) {
        //     if (!defender) {
        //         return;
        //     }

        //     constexpr const char* effects[] = {"HBLOCK_SekiroSSparks", "HBLOCK_SekiroSSparksPhysics",
        //                                        "HBLOCK_SekiroSFlash", "HBLOCK_SekiroSFlashShield"};

        //     for (const auto* editorID : effects) {
        //         const auto effect = RE::TESForm::LookupByEditorID<RE::BGSExplosion>(editorID);

        //         if (!effect) {
        //             if (Settings::debugLogging) {
        //                 logger::warn(
        //                     "PRECISION BLOCK | Explosion not found | "
        //                     "Actor={} | Explosion={}",
        //                     defender->GetName(), editorID);
        //             }

        //             continue;
        //         }

        //         defender->PlaceObjectAtMe(effect, false);

        //         if (Settings::debugLogging) {
        //             logger::info(
        //                 "PRECISION BLOCK | Explosion spawned | "
        //                 "Actor={} | Explosion={}",
        //                 defender->GetName(), editorID);
        //         }
        //     }
        // }

        // void Cast_Spell(RE::Actor* a_actor, const char* a_spell, float a_mag) {
        //     if (!a_actor) {
        //         return;
        //     }

        //     const auto e_spell = RE::TESForm::LookupByEditorID<RE::MagicItem>(a_spell);

        //     if (!e_spell) {
        //         if (Settings::debugLogging) {
        //             logger::warn(
        //                 "PRECISION BLOCK | Spell not found | "
        //                 "Actor={} | Spell={} | Magnitude={}",
        //                 a_actor->GetName(), a_spell, a_mag);
        //         }

        //         return;
        //     }

        //     const auto caster = a_actor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);

        //     if (!caster) {
        //         if (Settings::debugLogging) {
        //             logger::warn(
        //                 "PRECISION BLOCK | Magic caster unavailable | "
        //                 "Actor={} | Spell={} | Magnitude={}",
        //                 a_actor->GetName(), a_spell, a_mag);
        //         }

        //         return;
        //     }

        //     caster->CastSpellImmediate(e_spell, false, a_actor, 1, false, a_mag, a_actor);

        //     if (Settings::debugLogging) {
        //         logger::info(
        //             "PRECISION BLOCK | Spell cast | "
        //             "Actor={} | Spell={} | Magnitude={}",
        //             a_actor->GetName(), a_spell, a_mag);
        //     }
        // }

        // void ApplyBlockSpells(RE::Actor* aggressor, RE::Actor* defender, bool powerAttack) {
        //     if (!aggressor || !defender) {
        //         return;
        //     }

        //     if (powerAttack) {
        //         Cast_Spell(aggressor, "HBLOCK_BlockedPowerAttacker", Settings::powerAttackerStagger);

        //         Cast_Spell(defender, "HBLOCK_BlockedPowerDefender", Settings::powerDefenderStagger);
        //     } else {
        //         Cast_Spell(aggressor, "HBLOCK_BlockedNormalAttacker", Settings::lightAttackerStagger);

        //         Cast_Spell(defender, "HBLOCK_BlockedNormalDefender", Settings::lightDefenderStagger);
        //     }

        //     Cast_Spell(defender, "HBLOCK_Effects", powerAttack ? 0.50f : 0.25f);

        //     SpawnBlockEffects(defender);
        // }
        PRECISION_API::PreHitCallbackReturn OnPreHit(const PRECISION_API::PrecisionHitData& hitData) {
            PRECISION_API::PreHitCallbackReturn result{};

            auto* aggressor = hitData.attacker;
            auto* defender = hitData.target ? hitData.target->As<RE::Actor>() : nullptr;

            if (!aggressor || !defender) {
                return result;
            }

            if (!defender->IsBlocking()) {
                NotifyHonorCombatReset(aggressor);
                return result;
            }

            std::int32_t attackDirection = 0;
            std::int32_t blockDirection = 0;

            const bool gotAttackDirection = aggressor->GetGraphVariableInt("AttackDirectionFHB", attackDirection);

            const bool gotBlockDirection = defender->GetGraphVariableInt("CameraMovementFHB", blockDirection);

            if (!gotAttackDirection || !gotBlockDirection) {
                if (Settings::debugLogging) {
                    logger::info(
                        "PRECISION BLOCK | Direction read failed | "
                        "AttackDirectionFHB={} | "
                        "CameraMovementFHB={}",
                        gotAttackDirection ? std::to_string(attackDirection) : "FAILED",
                        gotBlockDirection ? std::to_string(blockDirection) : "FAILED");
                }

                return result;
            }

            logger::info(
                "[BLOCK DEBUG] Defender={} IsBlocking={} AttackRead={} AttackDirection={} BlockRead={} "
                "BlockDirection={}",
                defender->GetName(), defender->IsBlocking(), gotAttackDirection, attackDirection, gotBlockDirection,
                blockDirection);

            const bool successful = DirectionalHandler::IsSuccessfulBlock(attackDirection, blockDirection);

            const bool powerAttack = aggressor->IsPowerAttacking();

            const float baseCost = GetBlockStaminaCost(aggressor);

            const float staminaReduction = successful ? (Settings::blockSkillLevel / 100.0f) * 50.0f : 0.0f;

            const float staminaMultiplier = 1.0f - (staminaReduction / 100.0f);
            const float attackMultiplier = powerAttack ? 2.0f : 1.0f;
            const float staminaCost = baseCost * attackMultiplier * staminaMultiplier;

            if (staminaCost > 0.0f) {
                defender->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, staminaCost);
            }

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | "
                    "AttackDirectionFHB={} ({}) | "
                    "CameraMovementFHB={} ({}) | "
                    "Type={} | "
                    "RESULT={}",
                    attackDirection, GraphDirectionName(attackDirection), blockDirection,
                    GraphDirectionName(blockDirection), powerAttack ? "POWER" : "LIGHT",
                    successful ? "SUCCESS" : "FAIL");
            }

            NotifyHonorCombatReset(aggressor);

            if (!successful) {
                return result;
            }

            Gameplay::ApplyBlockSpells(aggressor, defender, powerAttack);

            const float reduction = Settings::blockSkillLevel * (powerAttack ? Settings::powerAttackReductionPerLevel
                                                                             : Settings::lightAttackReductionPerLevel);

            const float damageMultiplier = std::max(0.0f, 1.0f - (reduction / 100.0f));

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Damage,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        damageMultiplier});

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Stagger,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        damageMultiplier});

            if (powerAttack) {
                if (Settings::debugLogging) {
                    logger::info(
                        "PRECISION BLOCK | "
                        "Successful POWER block | "
                        "BlockSkill={} | "
                        "Reduction={} | "
                        "Damage/Stagger multiplier={} | "
                        "Attacker Stagger={} | "
                        "Defender Stagger={}",
                        Settings::blockSkillLevel, reduction, damageMultiplier, Settings::powerAttackerStagger,
                        Settings::powerDefenderStagger);
                }

                return result;
            }

            const bool useLargeStagger = !Settings::empoweredBlockRequired || Settings::hasEmpoweredBlock;

            if (useLargeStagger) {
                Gameplay::ApplyLargeStagger(aggressor, defender);
            } else {
                Gameplay::ApplyNormalStagger(aggressor, defender);
            }

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | "
                    "Successful LIGHT block | "
                    "BlockSkill={} | "
                    "Reduction={} | "
                    "Damage/Stagger multiplier={} | "
                    "Attacker Stagger={} | "
                    "Defender Stagger={} | "
                    "Recoil={}",
                    Settings::blockSkillLevel, reduction, damageMultiplier, Settings::lightAttackerStagger,
                    Settings::lightDefenderStagger, useLargeStagger ? "LARGE" : "NORMAL");
            }

            return result;
        }
    }

    float OnPoiseDamage(RE::Actor* attacker, RE::Actor* target, float damage) {
        if (!attacker || !target || !target->IsBlocking()) {
            return damage;
        }

        std::int32_t attackDirection = 0;
        std::int32_t blockDirection = 0;

        const bool gotAttackDirection = attacker->GetGraphVariableInt("AttackDirectionFHB", attackDirection);

        const bool gotBlockDirection = target->GetGraphVariableInt("CameraMovementFHB", blockDirection);

        if (!gotAttackDirection || !gotBlockDirection) {
            return damage;
        }

        if (!DirectionalHandler::IsSuccessfulBlock(attackDirection, blockDirection)) {
            return damage;
        }

        const bool powerAttack = attacker->IsPowerAttacking();

        const float reduction = Settings::blockSkillLevel * (powerAttack ? Settings::powerAttackReductionPerLevel
                                                                         : Settings::lightAttackReductionPerLevel);

        const float damageMultiplier = std::max(0.0f, 1.0f - (reduction / 100.0f));

        const float result = damage * damageMultiplier;

        if (Settings::debugLogging) {
            logger::info(
                "POISE BLOCK | "
                "Type={} | "
                "Original={} | "
                "Reduction={} | "
                "Result={}",
                powerAttack ? "POWER" : "LIGHT", damage, reduction, result);
        }

        return result;
    }

    void Install(SKSE::PluginHandle pluginHandle, PRECISION_API::IVPrecision1* precisionAPI) {
        if (PoiseAPI::Load()) {
            if (!PoiseAPI::RegisterDamageCallback(&OnPoiseDamage)) {
                logger::error("Failed to register Poise damage callback");
            } else {
                logger::info("Poise damage callback registered successfully");
            }
        } else {
            logger::warn("Chocolate Poise Reforged API unavailable");
        }

        logger::info("Installing Blocking Precision PreHit system...");

        if (!precisionAPI) {
            logger::error(
                "Precision API unavailable. "
                "PreHit system was not installed.");

            return;
        }

        PRECISION_API::PreHitCallback callback = OnPreHit;

        const auto result = precisionAPI->AddPreHitCallback(pluginHandle, std::move(callback));

        if (result != PRECISION_API::APIResult::OK) {
            logger::error(
                "Failed to register Precision PreHit callback. "
                "Result={}",
                static_cast<int>(result));

            return;
        }

        logger::info("Precision PreHit callback registered successfully");

        logger::info("Blocking system installation complete");
    }

}