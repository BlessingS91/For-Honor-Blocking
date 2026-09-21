#include "Blocking.h"

#include "DirectionalHandler.h"
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

        class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent> {
        public:
            static HitEventSink* GetSingleton() {
                static HitEventSink singleton;
                return std::addressof(singleton);
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event,
                                                  RE::BSTEventSource<RE::TESHitEvent>*) override {
                if (!event) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* target = event->target.get();
                auto* attacker = event->cause.get();

                if (!target || !attacker) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* defender = target->As<RE::Actor>();
                auto* aggressor = attacker->As<RE::Actor>();

                if (!defender || !aggressor) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Only process actual blocking attempts.
                if (!defender->IsBlocking()) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                std::int32_t attackDirection = 0;
                std::int32_t blockDirection = 0;

                const bool gotAttackDirection = aggressor->GetGraphVariableInt("AttackDirectionNPC", attackDirection);

                const bool gotBlockDirection = defender->GetGraphVariableInt("CameraMovementCMF", blockDirection);

                logger::info(
                    "BLOCK EVENT | "
                    "Attacker={} [{}] | "
                    "AttackDirectionNPC={} ({}) | "
                    "Defender={} [{}] | "
                    "CameraMovementCMF={} ({})",
                    GetActorName(aggressor), aggressor->IsPlayerRef() ? "PLAYER" : "NPC",
                    gotAttackDirection ? attackDirection : -1,
                    gotAttackDirection ? GraphDirectionName(attackDirection) : "READ FAILED", GetActorName(defender),
                    defender->IsPlayerRef() ? "PLAYER" : "NPC", gotBlockDirection ? blockDirection : -1,
                    gotBlockDirection ? GraphDirectionName(blockDirection) : "READ FAILED");

                if (!gotAttackDirection || !gotBlockDirection) {
                    logger::info("BLOCK EVENT RESULT | READ FAILED");

                    return RE::BSEventNotifyControl::kContinue;
                }

                const bool successful = DirectionalHandler::IsSuccessfulBlock(attackDirection, blockDirection);

                logger::info(
                    "BLOCK EVENT RESULT | "
                    "AttackDirectionNPC={} ({}) | "
                    "CameraMovementCMF={} ({}) | "
                    "RESULT={}",
                    attackDirection, GraphDirectionName(attackDirection), blockDirection,
                    GraphDirectionName(blockDirection), successful ? "SUCCESS" : "FAIL");

                if (!successful) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const bool powerAttack = event->flags.any(RE::TESHitEvent::Flag::kPowerAttack);

                logger::info(
                    "Directional block SUCCESS | "
                    "attacker={} [{}] | "
                    "defender={} [{}] | "
                    "type={}",
                    GetActorName(aggressor), aggressor->IsPlayerRef() ? "PLAYER" : "NPC", GetActorName(defender),
                    defender->IsPlayerRef() ? "PLAYER" : "NPC", powerAttack ? "POWER" : "LIGHT");

                // Only light attacks receive the recoil reaction.
                // Damage, stagger, and hit processing are handled elsewhere.
                if (powerAttack) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Interrupt attacker.
                aggressor->NotifyAnimationGraph("MCO_Recovery");
                aggressor->NotifyAnimationGraph("staggerStop");
                aggressor->NotifyAnimationGraph("recoilStop");
                aggressor->NotifyAnimationGraph("MCO_EndAnimation");
                aggressor->NotifyAnimationGraph("attackStop");

                // Recoil attacker.
                aggressor->NotifyAnimationGraph("recoilLargeStart");

                logger::info(
                    "Light attack directional block | "
                    "attacker interrupted and recoiled | "
                    "attacker={} | defender={}",
                    GetActorName(aggressor), GetActorName(defender));

                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            HitEventSink() = default;

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
        };

    }

    void Install() {
        logger::info("Installing Blocking TESHitEvent system...");

        auto* eventSource = RE::ScriptEventSourceHolder::GetSingleton();

        if (!eventSource) {
            logger::error("Failed to get ScriptEventSourceHolder");

            return;
        }

        eventSource->AddEventSink<RE::TESHitEvent>(HitEventSink::GetSingleton());

        logger::info("TESHitEvent blocking sink registered successfully");

        logger::info("Blocking system installation complete");
    }

}