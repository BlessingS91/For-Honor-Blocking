#include "Events.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "PowerAttackKey.h"
#include "Unblockables.h"
#include "logger.h"

namespace {
    using namespace std::chrono_literals;

    constexpr float kParryBaseDurationMs = 500.0f;
    constexpr float kParryScalingMsPerLevel = 2.0f;

    constexpr float kAllBlockBaseDurationMs = 250.0f;
    constexpr float kAllBlockScalingMsPerLevel = 4.0f;

    std::chrono::milliseconds GetScaledDuration(RE::Actor* actor, float baseDuration, float scaling) {
        const auto* actorValueOwner = actor->AsActorValueOwner();
        const float blockSkill =
            actorValueOwner ? std::clamp(actorValueOwner->GetActorValue(RE::ActorValue::kBlock), 0.0f, 100.0f) : 0.0f;
        const float duration = std::clamp(baseDuration + blockSkill * scaling, 1.0f, 10000.0f);
        return std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(duration)));
    }

    std::mutex resetMutex;
    std::unordered_map<std::uint64_t, std::uint64_t> resetGenerations;
    std::uint64_t nextGeneration = 0;

    void ActivateWindow(RE::Actor* actor, const char* variable, std::uint8_t variableID,
                        std::chrono::milliseconds duration, std::int32_t value = 1) {
        if (!actor) {
            return;
        }

        const auto handle = actor->GetHandle();
        if (!handle) {
            return;
        }

        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            return;
        }

        if (!actor->SetGraphVariableInt(variable, value)) {
            logger::warn("[FHB] Failed to set {}", variable);
            return;
        }

        const std::uint64_t key = (static_cast<std::uint64_t>(actor->GetFormID()) << 3) | variableID;
        std::uint64_t generation;
        {
            std::scoped_lock lock(resetMutex);
            generation = ++nextGeneration;
            resetGenerations[key] = generation;
        }

        logger::info("[FHB] {} = {}", variable, value);

        std::thread([handle, variable, key, generation, tasks, duration]() {
            std::this_thread::sleep_for(duration);

            tasks->AddTask([handle, variable, key, generation]() {
                {
                    std::scoped_lock lock(resetMutex);
                    const auto it = resetGenerations.find(key);
                    if (it == resetGenerations.end() || it->second != generation) {
                        return;
                    }
                    resetGenerations.erase(it);
                }

                if (auto actor = handle.get()) {
                    actor->SetGraphVariableInt(variable, 0);
                    logger::info("[FHB] {} = 0", variable);
                }
            });
        }).detach();
    }
}

namespace FHB {
    void Events::ActivateParryKnockdown(RE::Actor* actor) { ActivateWindow(actor, "ParryKnockdownFHB", 3, 200ms); }

    // Retained for other existing call sites; no longer used for unblockable reactions.
    void Events::ActivateGuardBroken(RE::Actor* actor) { ActivateWindow(actor, "GuardBrokenFHB", 4, 2000ms); }

    void Events::ActivateUnblockableReaction(RE::Actor* actor, int reaction) {
        if (reaction != 1 && reaction != 2) {
            return;
        }
        // Existing timer/generation handling prevents an older hit from clearing a newer signal.
        ActivateWindow(actor, "UnblockableReactionFHB", 5, 2000ms, reaction);
    }

    void Events::AddEventSink() {
        REL::Relocation<std::uintptr_t> pcVtbl{RE::VTABLE_PlayerCharacter[2]};
        REL::Relocation<std::uintptr_t> npcVtbl{RE::VTABLE_Character[2]};

        _PCProcessEvent = pcVtbl.write_vfunc(0x1, reinterpret_cast<std::uintptr_t>(&PCProcessEvent));
        _NPCProcessEvent = npcVtbl.write_vfunc(0x1, reinterpret_cast<std::uintptr_t>(&NPCProcessEvent));

        logger::info("[FHB] Animation event hooks installed.");
    }

    RE::BSEventNotifyControl Events::ProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                  RE::BSAnimationGraphEvent* a_event,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) {
        if (!a_event || !a_event->holder) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* actor = const_cast<RE::Actor*>(a_event->holder->As<RE::Actor>());
        if (!actor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto& tag = a_event->tag;

        if (actor->IsPlayerRef()) {
            PowerAttackKey::OnPlayerAnimationEvent(tag.c_str());
        }

        // Creature classification is kept in Unblockables.cpp. Event timing and
        // activation are the same as before this refactor.
        if (tag == "weaponSwing" && !actor->IsPlayerRef()) {
            float damageMult = 0.0f;
            if (const auto isUnblockable = Unblockables::GetCreatureSwingUnblockable(actor, damageMult)) {
                if (*isUnblockable) {
                    ActivateWindow(actor, "UnblockableFHB", 1, 2000ms);
                } else {
                    actor->SetGraphVariableInt("UnblockableFHB", 0);
                }

                logger::info("[FHB CREATURE] Actor={} | ATKD Damage Mult={} | Unblockable={}", actor->GetName(),
                             damageMult, *isUnblockable);
            }
        }

        if (tag == "ParryStartFHB") {
            actor->SetGraphVariableInt("ParrySuccessFHB", 0);
            actor->SetGraphVariableInt("ParryActiveFHB", 1);

            const auto duration = GetScaledDuration(actor, kParryBaseDurationMs, kParryScalingMsPerLevel);
            ActivateWindow(actor, "ParryWindowFHB", 0, duration);
            logger::info("[FHB] Parry window duration: {} ms", duration.count());

        } else if (tag == "UnblockableStartFHB") {
            ActivateWindow(actor, "UnblockableFHB", 1, 2000ms);

        } else if (tag == "AllBlockStartFHB") {
            const auto duration = GetScaledDuration(actor, kAllBlockBaseDurationMs, kAllBlockScalingMsPerLevel);
            ActivateWindow(actor, "AllBlockFHB", 2, duration);
            logger::info("[FHB] All-block window duration: {} ms", duration.count());

        } else if (tag == "staggerStop") {
            actor->SetGraphVariableInt("StaggerDirectionFHB", 0);

        } else if (tag == "MCO_WinOpen" || tag == "MCO_WinClose" || tag == "MCO_PowerWinOpen" ||
                   tag == "MCO_PowerWinClose" || tag == "MCO_Recovery" || tag == "StaggerStart" ||
                   tag == "BlockStart" || tag == "recoilStart" || tag == "attackStop") {
            actor->SetGraphVariableInt("ParryWindowFHB", 0);
            actor->SetGraphVariableInt("ParryActiveFHB", 0);
            actor->SetGraphVariableInt("ParrySuccessFHB", 0);
            actor->SetGraphVariableInt("AllBlockFHB", 0);

            actor->SetGraphVariableInt("UnblockableFHB", 0);

            logger::info("[FHB] Combat states reset | Event={}", tag.c_str());
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl Events::PCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                    RE::BSAnimationGraphEvent* a_event,
                                                    RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) {
        ProcessEvent(a_this, a_event, a_source);
        return _PCProcessEvent(a_this, a_event, a_source);
    }

    RE::BSEventNotifyControl Events::NPCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                     RE::BSAnimationGraphEvent* a_event,
                                                     RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) {
        ProcessEvent(a_this, a_event, a_source);
        return _NPCProcessEvent(a_this, a_event, a_source);
    }
}
