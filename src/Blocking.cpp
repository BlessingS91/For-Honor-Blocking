#include "Blocking.h"

#include <format>
#include <string>

#include "DirectionalHandler.h"
#include "PoiseAPI.h"
#include "Settings.h"
#include "logger.h"

namespace Blocking {

    const char* GraphDirectionName(std::int32_t value) {
        switch (value) {
            case 0:
                return "No Movement";
            case 1:
                return "Forward";
            case 2:
                return "Forward-Right";
            case 3:
                return "Right";
            case 4:
                return "Backward-Right";
            case 5:
                return "Backward";
            case 6:
                return "Backward-Left";
            case 7:
                return "Left";
            case 8:
                return "Forward-Left";
            default:
                return "UNKNOWN";
        }
    }

    namespace {

        void DebugLog(const char* message) {
            if (Settings::debugLogging) {
                logger::info("PRECISION BLOCK | {}", message);
            }
        }

        void SpawnBlockEffects(RE::Actor* defender) {
            if (!defender) {
                return;
            }

            constexpr const char* effects[] = {"HBLOCK_SekiroSSparks", "HBLOCK_SekiroSSparksPhysics",
                                               "HBLOCK_SekiroSFlash", "HBLOCK_SekiroSFlashShield"};

            for (const auto* editorID : effects) {
                const auto effect = RE::TESForm::LookupByEditorID<RE::BGSExplosion>(editorID);

                if (!effect) {
                    if (Settings::debugLogging) {
                        logger::warn(
                            "PRECISION BLOCK | Explosion not found | "
                            "Actor={} | Explosion={}",
                            defender->GetName(), editorID);
                    }

                    continue;
                }

                defender->PlaceObjectAtMe(effect, false);

                if (Settings::debugLogging) {
                    logger::info(
                        "PRECISION BLOCK | Explosion spawned | "
                        "Actor={} | Explosion={}",
                        defender->GetName(), editorID);
                }
            }
        }

        void Cast_Spell(RE::Actor* a_actor, const char* a_spell, float a_mag) {
            if (!a_actor) {
                return;
            }

            const auto e_spell = RE::TESForm::LookupByEditorID<RE::MagicItem>(a_spell);

            if (!e_spell) {
                if (Settings::debugLogging) {
                    logger::warn(
                        "PRECISION BLOCK | Spell not found | "
                        "Actor={} | Spell={} | Magnitude={}",
                        a_actor->GetName(), a_spell, a_mag);
                }

                return;
            }

            const auto caster = a_actor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);

            if (!caster) {
                if (Settings::debugLogging) {
                    logger::warn(
                        "PRECISION BLOCK | Magic caster unavailable | "
                        "Actor={} | Spell={} | Magnitude={}",
                        a_actor->GetName(), a_spell, a_mag);
                }

                return;
            }

            caster->CastSpellImmediate(e_spell, false, a_actor, 1, false, a_mag, a_actor);

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | Spell cast | "
                    "Actor={} | Spell={} | Magnitude={}",
                    a_actor->GetName(), a_spell, a_mag);
            }
        }

        void ApplyBlockSpells(RE::Actor* aggressor, RE::Actor* defender, bool powerAttack) {
            if (!aggressor || !defender) {
                return;
            }

            if (powerAttack) {
                Cast_Spell(aggressor, "HBLOCK_BlockedPowerAttacker", Settings::powerAttackerStagger);

                Cast_Spell(defender, "HBLOCK_BlockedPowerDefender", Settings::powerDefenderStagger);
            } else {
                Cast_Spell(aggressor, "HBLOCK_BlockedNormalAttacker", Settings::lightAttackerStagger);

                Cast_Spell(defender, "HBLOCK_BlockedNormalDefender", Settings::lightDefenderStagger);
            }

            Cast_Spell(defender, "HBLOCK_Effects", powerAttack ? 0.50f : 0.25f);

            SpawnBlockEffects(defender);
        }

        std::string GetActorName(RE::Actor* actor) {
            if (!actor) {
                return "NULL";
            }

            const char* name = actor->GetName();

            if (name && *name) {
                return name;
            }

            return std::format("Actor {:08X}", actor->GetFormID());
        }

        PRECISION_API::PreHitCallbackReturn OnPreHit(const PRECISION_API::PrecisionHitData& hitData) {
            PRECISION_API::PreHitCallbackReturn result{};

            auto* aggressor = hitData.attacker;

            auto* defender = hitData.target ? hitData.target->As<RE::Actor>() : nullptr;

            if (!aggressor || !defender) {
                return result;
            }

            if (!defender->IsBlocking()) {
                return result;
            }

            std::int32_t attackDirection = 0;
            std::int32_t blockDirection = 0;

            const bool gotAttackDirection = aggressor->GetGraphVariableInt("AttackDirectionNPC", attackDirection);

            const bool gotBlockDirection = defender->GetGraphVariableInt("CameraMovementCMF", blockDirection);

            if (!gotAttackDirection || !gotBlockDirection) {
                if (Settings::debugLogging) {
                    logger::info(
                        "PRECISION BLOCK | Direction read failed | "
                        "AttackDirectionNPC={} | "
                        "CameraMovementCMF={}",
                        gotAttackDirection ? std::to_string(attackDirection) : "FAILED",
                        gotBlockDirection ? std::to_string(blockDirection) : "FAILED");
                }

                return result;
            }

            const bool successful = DirectionalHandler::IsSuccessfulBlock(attackDirection, blockDirection);

            const bool powerAttack = aggressor->IsPowerAttacking();

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | "
                    "AttackDirectionNPC={} ({}) | "
                    "CameraMovementCMF={} ({}) | "
                    "Type={} | "
                    "RESULT={}",
                    attackDirection, GraphDirectionName(attackDirection), blockDirection,
                    GraphDirectionName(blockDirection), powerAttack ? "POWER" : "LIGHT",
                    successful ? "SUCCESS" : "FAIL");
            }

            if (!successful) {
                return result;
            }

            ApplyBlockSpells(aggressor, defender, powerAttack);

            if (powerAttack) {
                const float reduction = Settings::blockSkillLevel * Settings::powerAttackReductionPerLevel;

                const float damageMultiplier = std::max(0.0f, 1.0f - (reduction / 100.0f));

                result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Damage,
                                            PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                            damageMultiplier});

                result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Stagger,
                                            PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                            damageMultiplier});

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

            const float reduction = Settings::blockSkillLevel * Settings::lightAttackReductionPerLevel;

            const float damageMultiplier = std::max(0.0f, 1.0f - (reduction / 100.0f));

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Damage,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        damageMultiplier});

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Stagger,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        damageMultiplier});

            aggressor->NotifyAnimationGraph("MCO_Recovery");
            aggressor->NotifyAnimationGraph("staggerStop");
            aggressor->NotifyAnimationGraph("recoilStop");
            aggressor->NotifyAnimationGraph("MCO_EndAnimation");
            aggressor->NotifyAnimationGraph("attackStop");
            aggressor->NotifyAnimationGraph("recoilLargeStart");

            defender->NotifyAnimationGraph("blockStop");
            defender->NotifyAnimationGraph("Maxsu_BlockHitWinOpen");

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | "
                    "Successful LIGHT block | "
                    "BlockSkill={} | "
                    "Reduction={} | "
                    "Damage/Stagger multiplier={} | "
                    "Attacker Stagger={} | "
                    "Defender Stagger={} | "
                    "Attacker interrupted and recoiled",
                    Settings::blockSkillLevel, reduction, damageMultiplier, Settings::lightAttackerStagger,
                    Settings::lightDefenderStagger);
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

        const bool gotAttackDirection = attacker->GetGraphVariableInt("AttackDirectionNPC", attackDirection);

        const bool gotBlockDirection = target->GetGraphVariableInt("CameraMovementCMF", blockDirection);

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