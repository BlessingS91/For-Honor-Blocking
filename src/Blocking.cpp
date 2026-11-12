#include "Blocking.h"

#include <SKSEMenuFramework.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "DirectionalHandler.h"
#include "Events.h"
#include "Gameplay.h"
#include "Parry.h"
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

        float GetAllBlockReduction(RE::Actor* defender, bool powerAttack) {
            const auto* actorValues = defender->AsActorValueOwner();

            const float blockSkill = std::clamp(actorValues->GetActorValue(RE::ActorValue::kBlock), 0.0f, 100.0f);

            float shieldArmor = 0.0f;

            if (auto* equipped = defender->GetEquippedObject(true)) {
                if (auto* shield = equipped->As<RE::TESObjectARMO>()) {
                    shieldArmor = shield->GetArmorRating();
                }
            }

            float blockedPercent = 45.0f + 0.2f * shieldArmor * (1.0f + blockSkill * 0.015f);

            blockedPercent = std::min(blockedPercent, 85.0f);

            if (powerAttack) {
                blockedPercent *= 0.66f;
            }

            return std::clamp(blockedPercent / 100.0f, 0.0f, 0.85f);
        }

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

        bool IsBashing(RE::Actor* actor) {
            if (!actor) {
                return false;
            }

            const auto* state = actor->AsActorState();

            return state && state->GetAttackState() == RE::ATTACK_STATE_ENUM::kBash;
        }

        PRECISION_API::PreHitCallbackReturn OnPreHit(const PRECISION_API::PrecisionHitData& hitData) {
            PRECISION_API::PreHitCallbackReturn result{};

            auto* aggressor = hitData.attacker;
            auto* defender = hitData.target ? hitData.target->As<RE::Actor>() : nullptr;

            if (!aggressor || !defender) {
                return result;
            }

            std::int32_t parryWindow = 0;
            std::int32_t parryState = 0;
            std::int32_t parrySuccess = 0;

            const bool defenderParrying =
                defender->GetGraphVariableInt("ParryWindowFHB", parryWindow) && parryWindow == 1;

            const bool stateRead = aggressor->GetGraphVariableInt("ParryActiveFHB", parryState);

            const bool successRead = aggressor->GetGraphVariableInt("ParrySuccessFHB", parrySuccess);

            const bool attackerParrying = stateRead && parryState == 1;

            const bool attackerCounterattacking = aggressor->IsPlayerRef() && successRead && parrySuccess == 1;

            if (Settings::debugLogging) {
                logger::info(
                    "[PARRY ROUTE] Attacker={} Defender={} "
                    "StateRead={} State={} SuccessRead={} Success={} DefenderWindow={}",
                    aggressor->GetName(), defender->GetName(), stateRead, parryState, successRead, parrySuccess,
                    parryWindow);
            }

            std::int32_t defenderParrySuccess = 0;

            const bool defenderParryProtected =
                defender->IsPlayerRef() && defender->GetGraphVariableInt("ParrySuccessFHB", defenderParrySuccess) &&
                defenderParrySuccess == 1;

            if (defenderParrying || defenderParryProtected || attackerParrying || attackerCounterattacking) {
                return Parry::OnPreHit(hitData);
            }

            std::int32_t allBlock = 0;
            defender->GetGraphVariableInt("AllBlockFHB", allBlock);

            // Disable normal directional blocking while bashing.
            // Active all-block is explicitly allowed.
            if (IsBashing(defender) && allBlock != 1) {
                NotifyHonorCombatReset(aggressor);
                return result;
            }

            if (!defender->IsBlocking() && allBlock != 1) {
                NotifyHonorCombatReset(aggressor);
                return result;
            }

            std::int32_t attackDirection = 0;
            std::int32_t blockDirection = 0;
            std::int32_t unblockable = 0;

            const bool gotAttackDirection = aggressor->GetGraphVariableInt("AttackDirectionFHB", attackDirection);
            const bool gotBlockDirection = defender->GetGraphVariableInt("CameraMovementFHB", blockDirection);
            aggressor->GetGraphVariableInt("UnblockableFHB", unblockable);

            // Unblockable attack connects with an active guard.
            if (unblockable == 1) {
                FHB::Events::ActivateGuardBroken(defender);

                if (Settings::debugLogging) {
                    logger::info("[GUARD BREAK] Attacker={} | Defender={} | GuardBrokenFHB=1", aggressor->GetName(),
                                 defender->GetName());
                }
            }

            if (!gotAttackDirection || (!gotBlockDirection && allBlock != 1)) {
                if (Settings::debugLogging) {
                    logger::info(
                        "PRECISION BLOCK | Direction read failed | AttackDirectionFHB={} | CameraMovementFHB={}",
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

            const bool successful =
                DirectionalHandler::IsSuccessfulBlock(attackDirection, blockDirection, allBlock == 1, unblockable == 1);
            const bool powerAttack = aggressor->IsPowerAttacking();

            const auto* actorValueOwner = defender->AsActorValueOwner();
            const float blockSkillLevel =
                actorValueOwner ? actorValueOwner->GetActorValue(RE::ActorValue::kBlock) : 0.0f;

            const float baseCost = GetBlockStaminaCost(aggressor);
            const float staminaReduction = successful ? (blockSkillLevel / 100.0f) * 50.0f : 0.0f;
            const float staminaMultiplier = 1.0f - (staminaReduction / 100.0f);

            const float attackMultiplier = powerAttack ? 2.0f : 1.0f;
            const float staminaCost = baseCost * attackMultiplier * staminaMultiplier;

            if (staminaCost > 0.0f) {
                defender->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, staminaCost);
            }

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | AttackDirectionFHB={} ({}) | CameraMovementFHB={} ({}) | Type={} | RESULT={}",
                    attackDirection, GraphDirectionName(attackDirection), blockDirection,
                    GraphDirectionName(blockDirection), powerAttack ? "POWER" : "LIGHT",
                    successful ? "SUCCESS" : "FAIL");
            }

            NotifyHonorCombatReset(aggressor);
            if (!successful) {
                return result;
            }

            Gameplay::ApplyBlockSpells(aggressor, defender, powerAttack);

            const float reduction = blockSkillLevel * (powerAttack ? Settings::powerAttackReductionPerLevel
                                                                   : Settings::lightAttackReductionPerLevel);
            const float fhbMultiplier = std::max(0.0f, 1.0f - (reduction / 100.0f));

            float damageMultiplier = fhbMultiplier;

            if (allBlock == 1 && !defender->IsBlocking()) {
                const float vanillaReduction = GetAllBlockReduction(defender, powerAttack);

                damageMultiplier *= (1.0f - vanillaReduction);
            }

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Damage,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        damageMultiplier});

            result.modifiers.push_back({PRECISION_API::PreHitModifier::ModifierType::Stagger,
                                        PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative,
                                        fhbMultiplier});

            if (powerAttack) {
                if (Settings::debugLogging) {
                    logger::info(
                        "PRECISION BLOCK | Successful POWER block | BlockSkill={} | Reduction={} | "
                        "Damage/Stagger multiplier={} | Attacker Stagger={} | Defender Stagger={}",
                        blockSkillLevel, reduction, damageMultiplier, Settings::powerAttackerStagger,
                        Settings::powerDefenderStagger);
                }
                return result;
            }

            auto* empoweredBlockPerk = RE::TESForm::LookupByEditorID<RE::BGSPerk>("HBLOCK_EmpoweredBlock");
            const bool hasEmpoweredBlock = empoweredBlockPerk && defender->HasPerk(empoweredBlockPerk);

            const bool useLargeStagger = !Settings::empoweredBlockRequired || hasEmpoweredBlock;
            if (useLargeStagger) {
                Gameplay::ApplyLargeStagger(aggressor, defender);
            } else {
                Gameplay::ApplyNormalStagger(aggressor, defender);
            }

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | Successful LIGHT block | BlockSkill={} | Reduction={} | "
                    "Damage/Stagger multiplier={} | Attacker Stagger={} | Defender Stagger={} | Recoil={}",
                    blockSkillLevel, reduction, damageMultiplier, Settings::lightAttackerStagger,
                    Settings::lightDefenderStagger, useLargeStagger ? "LARGE" : "NORMAL");
            }
            return result;
        }
    }

    float OnPoiseDamage(RE::Actor* attacker, RE::Actor* target, float damage) {
        if (!attacker || !target) {
            return damage;
        }

        std::int32_t allBlock = 0;
        target->GetGraphVariableInt("AllBlockFHB", allBlock);
        if (!target->IsBlocking() && allBlock != 1) {
            return damage;
        }

        std::int32_t attackDirection = 0;
        std::int32_t blockDirection = 0;
        std::int32_t unblockable = 0;

        const bool gotAttackDirection = attacker->GetGraphVariableInt("AttackDirectionFHB", attackDirection);
        const bool gotBlockDirection = target->GetGraphVariableInt("CameraMovementFHB", blockDirection);
        attacker->GetGraphVariableInt("UnblockableFHB", unblockable);

        if (!gotAttackDirection || (!gotBlockDirection && allBlock != 1)) {
            return damage;
        }

        if (!DirectionalHandler::IsSuccessfulBlock(attackDirection, blockDirection, allBlock == 1, unblockable == 1)) {
            return damage;
        }

        const auto* actorValueOwner = target->AsActorValueOwner();
        const float blockSkillLevel = actorValueOwner ? actorValueOwner->GetActorValue(RE::ActorValue::kBlock) : 0.0f;

        const bool powerAttack = attacker->IsPowerAttacking();
        const float reduction = blockSkillLevel * (powerAttack ? Settings::powerAttackReductionPerLevel
                                                               : Settings::lightAttackReductionPerLevel);
        const float damageMultiplier = std::max(0.0f, 1.0f - (reduction / 100.0f));

        const float result = damage * damageMultiplier;

        if (Settings::debugLogging) {
            logger::info("POISE BLOCK | Type={} | Original={} | Reduction={} | Result={}",
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
            logger::error("Precision API unavailable. PreHit system was not installed.");
            return;
        }

        PRECISION_API::PreHitCallback callback = OnPreHit;
        const auto result = precisionAPI->AddPreHitCallback(pluginHandle, std::move(callback));
        if (result != PRECISION_API::APIResult::OK) {
            logger::error("Failed to register Precision PreHit callback. Result={}", static_cast<int>(result));
            return;
        }

        logger::info("Precision PreHit callback registered successfully");
        logger::info("Blocking system installation complete");
    }

    void RenderSettings() {
        namespace ui = ImGuiMCP;
        ui::Text("For Honor Blocking");
        ui::Separator();

        ui::Text("Damage Reduction from Block Skill");
        ui::Text("Controls how much damage is reduced based on the defender's Block skill.");

        const float previousLightReduction = Settings::lightAttackReductionPerLevel;
        ui::SliderFloat("Light Attack Reduction per Block level", &Settings::lightAttackReductionPerLevel, 0.0f, 1.0f,
                        "%.2f");
        if (Settings::lightAttackReductionPerLevel != previousLightReduction) {
            Settings::Save();
        }

        const float previousPowerReduction = Settings::powerAttackReductionPerLevel;
        ui::SliderFloat("Power Attack Reduction Block level.", &Settings::powerAttackReductionPerLevel, 0.0f, 1.0f,
                        "%.2f");
        if (Settings::powerAttackReductionPerLevel != previousPowerReduction) {
            Settings::Save();
        }

        ui::Separator();
        ui::Text("Block Stagger");
        ui::Text("Controls the stagger magnitude applied to the attacker and defender.");

        const float previousLightAttackerStagger = Settings::lightAttackerStagger;
        ui::SliderFloat("Light Block - Attacker Stagger", &Settings::lightAttackerStagger, 0.0f, 100.0f, "%.1f");
        if (Settings::lightAttackerStagger != previousLightAttackerStagger) {
            Settings::Save();
        }

        const float previousLightDefenderStagger = Settings::lightDefenderStagger;
        ui::SliderFloat("Light Block - Defender Stagger", &Settings::lightDefenderStagger, 0.0f, 100.0f, "%.1f");
        if (Settings::lightDefenderStagger != previousLightDefenderStagger) {
            Settings::Save();
        }

        const float previousPowerAttackerStagger = Settings::powerAttackerStagger;
        ui::SliderFloat("Power Block - Attacker Stagger", &Settings::powerAttackerStagger, 0.0f, 100.0f, "%.1f");
        if (Settings::powerAttackerStagger != previousPowerAttackerStagger) {
            Settings::Save();
        }

        const float previousPowerDefenderStagger = Settings::powerDefenderStagger;
        ui::SliderFloat("Power Block - Defender Stagger", &Settings::powerDefenderStagger, 0.0f, 100.0f, "%.1f");
        if (Settings::powerDefenderStagger != previousPowerDefenderStagger) {
            Settings::Save();
        }

        ui::Separator();
        ui::Text("Superior Block Perk");
        ui::Text("Defenders with this perk perform a large recoil instead of a normal recoil.");

        const bool previousEmpoweredBlockRequired = Settings::empoweredBlockRequired;
        ui::Checkbox("Require Empowered Block Perk", &Settings::empoweredBlockRequired);
        if (Settings::empoweredBlockRequired != previousEmpoweredBlockRequired) {
            Settings::Save();
        }

        char superiorBlockPerk[256]{};
        std::strncpy(superiorBlockPerk, Settings::superiorBlockPerk.c_str(), sizeof(superiorBlockPerk) - 1);
        if (ui::InputText("Perk Editor ID", superiorBlockPerk, sizeof(superiorBlockPerk))) {
            Settings::superiorBlockPerk = superiorBlockPerk;
            Settings::Save();
        }

        ui::Separator();
        ui::Text("Block Stamina Consumption");
        ui::Text("Controls stamina consumed when blocking an incoming attack.");

#define STAMINA_SLIDER(label, setting)                                    \
    do {                                                                  \
        const float previous = Settings::setting;                         \
        ui::SliderFloat(label, &Settings::setting, 0.0f, 100.0f, "%.1f"); \
        if (Settings::setting != previous) {                              \
            Settings::Save();                                             \
        }                                                                 \
    } while (false)

        STAMINA_SLIDER("One-Handed Sword", staminaOneHandSword);
        STAMINA_SLIDER("Dagger", staminaDagger);
        STAMINA_SLIDER("One-Handed Axe", staminaOneHandAxe);
        STAMINA_SLIDER("Mace", staminaMace);
        STAMINA_SLIDER("Two-Handed Sword", staminaTwoHandSword);
        STAMINA_SLIDER("Two-Handed Axe", staminaTwoHandAxe);
        STAMINA_SLIDER("Bow", staminaBow);
        STAMINA_SLIDER("Crossbow", staminaCrossbow);
        STAMINA_SLIDER("Staff", staminaStaff);
        STAMINA_SLIDER("Unarmed", staminaUnarmed);
        STAMINA_SLIDER("Other", staminaOther);

#undef STAMINA_SLIDER

        ui::Separator();
        ui::Text("Debug");

        const bool previousDebugLogging = Settings::debugLogging;
        ui::Checkbox("Debug Logging", &Settings::debugLogging);
        if (Settings::debugLogging != previousDebugLogging) {
            Settings::Save();
        }

        ui::Separator();
        if (ui::Button("Save Settings")) {
            Settings::Save();
        }
        ui::SameLine();
        if (ui::Button("Reset to Defaults")) {
            Settings::ResetToDefaults();
            Settings::Save();
        }
    }
}
