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

            BlockingGameplay::ApplyBlockSpells(aggressor, defender, const_cast<bool*>(&powerAttack));

            if (powerAttack) {
                const float damageMultiplier = BlockingGameplay::GetPowerAttackDamageMultiplier(defender);

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
                        "DamageMultiplier={} | "
                        "Attacker Stagger={} | "
                        "Defender Stagger={}",
                        damageMultiplier, Settings::powerAttackerStagger, Settings::powerDefenderStagger);
                }

                return result;
            }

            const float damageMultiplier = BlockingGameplay::GetLightAttackDamageMultiplier(defender);

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Damage,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        damageMultiplier});

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Stagger,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        damageMultiplier});

            if (BlockingGameplay::HasSuperiorBlockPerk(defender)) {
                BlockingGameplay::ApplyLargeRecoil(aggressor, defender);
            } else {
                BlockingGameplay::ApplyNormalRecoil(aggressor, defender);
            }

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | "
                    "Successful LIGHT block | "
                    "DamageMultiplier={} | "
                    "Attacker Stagger={} | "
                    "Defender Stagger={} | "
                    "Attacker interrupted and recoiled",
                    damageMultiplier, Settings::lightAttackerStagger, Settings::lightDefenderStagger);
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

        const float multiplier = powerAttack ? BlockingGameplay::GetPowerAttackDamageMultiplier(target)
                                             : BlockingGameplay::GetLightAttackDamageMultiplier(target);

        const float result = damage * multiplier;

        if (Settings::debugLogging) {
            logger::info(
                "POISE BLOCK | "
                "Type={} | "
                "Original={} | "
                "Multiplier={} | "
                "Result={}",
                powerAttack ? "POWER" : "LIGHT", damage, multiplier, result);
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