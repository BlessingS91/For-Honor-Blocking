#include "Blocking.h"

#include "DirectionalHandler.h"
#include "PoiseAPI.h"
#include "PrecisionAPI.h"
#include "logger.h"

namespace Blocking {

    namespace {

        PRECISION_API::IVPrecision4* g_precision = nullptr;

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

        bool IsSuccessfulDirectionalBlock(RE::Actor* attacker, RE::Actor* defender) {
            if (!attacker || !defender) {
                return false;
            }

            if (!defender->IsBlocking()) {
                return false;
            }

            std::int32_t attackDirection = 0;
            std::int32_t blockDirection = 0;

            logger::debug("Directional block: querying attacker graph variable 'AttackDirectionNPC' | attacker={}",
                          GetActorName(attacker));

            if (!attacker->GetGraphVariableInt("AttackDirectionNPC", attackDirection)) {
                logger::debug("Directional block: FAILED to read graph variable 'AttackDirectionNPC' | attacker={}",
                              GetActorName(attacker));
                return false;
            }

            logger::debug("Directional block: attacker graph variable 'AttackDirectionNPC' = {} | attacker={}",
                          attackDirection, GetActorName(attacker));

            if (!defender->GetGraphVariableInt("CameraMovementCMF", blockDirection)) {
                logger::debug("Directional block: failed to read CameraMovementCMF for defender={}",
                              GetActorName(defender));
                return false;
            }

            const bool successful = DirectionalHandler::IsSuccessfulBlock(attackDirection, blockDirection);

            logger::debug("Directional block check: attacker={} defender={} attackDir={} blockDir={} result={}",
                          GetActorName(attacker), GetActorName(defender), attackDirection, blockDirection, successful);

            return successful;
        }

        bool IsPowerAttack(RE::Actor* attacker) {
            if (!attacker) {
                return false;
            }

            auto* attackerProcess = attacker->GetActorRuntimeData().currentProcess;

            if (!attackerProcess || !attackerProcess->high || !attackerProcess->high->attackData) {
                logger::debug("Power attack check: no attack data for attacker={}", GetActorName(attacker));

                return false;
            }

            auto* attackData = attackerProcess->high->attackData.get();

            const bool powerAttack = attackData->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack);

            logger::debug("Power attack check: attacker={} powerAttack={}", GetActorName(attacker), powerAttack);

            return powerAttack;
        }

        float PoiseDamageCallback(RE::Actor* attacker, RE::Actor* target, float damage) {
            if (!attacker || !target) {
                return damage;
            }

            if (!IsSuccessfulDirectionalBlock(attacker, target)) {
                return damage;
            }

            if (IsPowerAttack(attacker)) {
                const float modifiedDamage = damage * 0.5f;

                logger::info(
                    "Directional block POISE: POWER ATTACK blocked | "
                    "attacker={} target={} | damage {} -> {}",
                    GetActorName(attacker), GetActorName(target), damage, modifiedDamage);

                return modifiedDamage;
            }

            logger::info(
                "Directional block POISE: LIGHT ATTACK blocked | "
                "attacker={} target={} | poise {} -> 0",
                GetActorName(attacker), GetActorName(target), damage);

            return 0.0f;
        }

        PRECISION_API::PreHitCallbackReturn PreHitCallback(const PRECISION_API::PrecisionHitData& hitData) {
            PRECISION_API::PreHitCallbackReturn result{};

            auto* attacker = hitData.attacker;
            auto* target = hitData.target;

            if (!attacker || !target) {
                return result;
            }

            auto* defender = target->As<RE::Actor>();

            if (!defender) {
                return result;
            }

            if (!IsSuccessfulDirectionalBlock(attacker, defender)) {
                return result;
            }

            const bool powerAttack = IsPowerAttack(attacker);

            logger::info("Directional block SUCCESS: attacker={} defender={} type={}", GetActorName(attacker),
                         GetActorName(defender), powerAttack ? "POWER" : "LIGHT");

            if (powerAttack) {
                result.modifiers.emplace_back(PRECISION_API::PreHitModifier::ModifierType::Damage,
                                              PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative, 0.5f);

                result.modifiers.emplace_back(PRECISION_API::PreHitModifier::ModifierType::Stagger,
                                              PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative, 0.5f);

                logger::info(
                    "Power attack block: damage=0.5x stagger=0.5x | "
                    "attacker={} defender={}",
                    GetActorName(attacker), GetActorName(defender));

                return result;
            }

            // Successful light attack block:
            // completely ignore the incoming hit.
            result.bIgnoreHit = true;

            logger::info(
                "Light attack directional block: hit ignored | "
                "attacker={} defender={}",
                GetActorName(attacker), GetActorName(defender));

            // Interrupt the attack using the same sequence as the
            // existing working MCO parry implementation.
            attacker->NotifyAnimationGraph("MCO_Recovery");
            attacker->NotifyAnimationGraph("staggerStop");
            attacker->NotifyAnimationGraph("recoilStop");
            attacker->NotifyAnimationGraph("MCO_EndAnimation");
            attacker->NotifyAnimationGraph("attackStop");

            defender->NotifyAnimationGraph("blockStop");
            defender->NotifyAnimationGraph("blockHitStart");

            // Trigger the actual recoil animation.
            attacker->NotifyAnimationGraph("recoilLargeStart");

            logger::info(
                "Light attack directional block: attacker interrupted "
                "and recoiled | attacker={} defender={}",
                GetActorName(attacker), GetActorName(defender));

            return result;
        }

        void InstallPrecision() {
            logger::info("Installing Precision directional blocking callback...");

            g_precision = static_cast<PRECISION_API::IVPrecision4*>(
                PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V4));

            if (!g_precision) {
                logger::warn(
                    "Precision API V4 unavailable; "
                    "directional Precision blocking disabled");
                return;
            }

            logger::info("Precision API V4 acquired");

            const auto result = g_precision->AddPreHitCallback(SKSE::GetPluginHandle(), PreHitCallback);

            if (result != PRECISION_API::APIResult::OK) {
                logger::error("Failed to register Precision PreHit callback: result={}", static_cast<int>(result));

                g_precision = nullptr;
                return;
            }

            logger::info("Precision PreHit callback registered successfully");
        }

        void InstallPoise() {
            logger::info("Installing Poise directional blocking callback...");

            if (!PoiseAPI::Load()) {
                logger::warn(
                    "Poise API unavailable; "
                    "directional Poise blocking disabled");
                return;
            }

            PoiseAPI::RegisterDamageCallback(PoiseDamageCallback);

            logger::info("Poise damage callback registered successfully");
        }
    }

    void Install() {
        logger::info("Installing Blocking system...");

        InstallPrecision();
        InstallPoise();

        logger::info("Blocking system installation complete");
    }

}