#include "WeaponClash.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "Gameplay.h"
#include "Settings.h"
#include "logger.h"

namespace {
    using Clock = std::chrono::steady_clock;

    constexpr auto kClashCooldown = std::chrono::milliseconds(350);

    std::mutex clashMutex;
    std::unordered_map<std::uint64_t, Clock::time_point> lastClashes;

    // Combat score = current skill for the equipped melee weapon + Block skill.
    // Return false if the actor doesn't have a supported melee weapon equipped.
    bool GetClashScore(RE::Actor* actor, float& score) {
        if (!actor) {
            return false;
        }

        auto* avOwner = actor->AsActorValueOwner();
        if (!avOwner) {
            return false;
        }

        auto* equipped = actor->GetEquippedObject(false);
        auto* weapon = equipped ? equipped->As<RE::TESObjectWEAP>() : nullptr;

        // Fall back to the left hand if the right hand has no weapon.
        if (!weapon) {
            equipped = actor->GetEquippedObject(true);
            weapon = equipped ? equipped->As<RE::TESObjectWEAP>() : nullptr;
        }

        if (!weapon) {
            return false;
        }

        RE::ActorValue weaponSkill;
        switch (weapon->GetWeaponType()) {
            case RE::WEAPON_TYPE::kOneHandDagger:
            case RE::WEAPON_TYPE::kOneHandSword:
            case RE::WEAPON_TYPE::kOneHandAxe:
            case RE::WEAPON_TYPE::kOneHandMace:
                weaponSkill = RE::ActorValue::kOneHanded;
                break;

            case RE::WEAPON_TYPE::kTwoHandSword:
            case RE::WEAPON_TYPE::kTwoHandAxe:
                weaponSkill = RE::ActorValue::kTwoHanded;
                break;

            default:
                return false;
        }

        score = avOwner->GetActorValue(weaponSkill) + avOwner->GetActorValue(RE::ActorValue::kBlock);
        return true;
    }

    // Parry and unblockable attacks are managed by their own combat systems.
    // Weapon clashes must never interrupt either state.
    bool IsClashImmune(RE::Actor* actor) {
        if (!actor) {
            return false;
        }

        std::int32_t parryActive = 0;
        std::int32_t parryWindow = 0;
        std::int32_t unblockable = 0;

        actor->GetGraphVariableInt("ParryActiveFHB", parryActive);
        actor->GetGraphVariableInt("ParryWindowFHB", parryWindow);
        actor->GetGraphVariableInt("UnblockableFHB", unblockable);

        return parryActive == 1 || parryWindow == 1 || unblockable == 1;
    }

    bool CanTriggerClash(RE::Actor* actorA, RE::Actor* actorB) {
        const auto idA = actorA->GetFormID();
        const auto idB = actorB->GetFormID();
        const auto lowID = std::min(idA, idB);
        const auto highID = std::max(idA, idB);
        const std::uint64_t pairID = (static_cast<std::uint64_t>(lowID) << 32) | highID;

        const auto now = Clock::now();
        std::scoped_lock lock(clashMutex);

        const auto it = lastClashes.find(pairID);
        if (it != lastClashes.end() && now - it->second < kClashCooldown) {
            return false;
        }

        lastClashes[pairID] = now;
        return true;
    }
}

PRECISION_API::WeaponCollisionCallbackReturn WeaponClash::OnWeaponCollision(
    const PRECISION_API::PrecisionHitData& hitData) {
    PRECISION_API::WeaponCollisionCallbackReturn result{};

    auto* actorA = hitData.attacker;
    auto* actorB = hitData.target ? hitData.target->As<RE::Actor>() : nullptr;

    if (!actorA || !actorB || actorA == actorB || actorA->IsDead() || actorB->IsDead() ||
        actorA->IsDisabled() || actorB->IsDisabled()) {
        return result;
    }

    // Either special state makes the entire collision ineligible for a clash.
    // Do not apply recoil or consume the normal clash cooldown.
    if (IsClashImmune(actorA) || IsClashImmune(actorB)) {
        return result;
    }

    float scoreA = 0.0f;
    float scoreB = 0.0f;

    if (!GetClashScore(actorA, scoreA) || !GetClashScore(actorB, scoreB) || scoreA == scoreB) {
        return result;
    }

    if (!CanTriggerClash(actorA, actorB)) {
        return result;
    }

    const bool actorAWins = scoreA > scoreB;
    auto* winner = actorAWins ? actorA : actorB;
    auto* loser = actorAWins ? actorB : actorA;
    const float winningScore = actorAWins ? scoreA : scoreB;
    const float losingScore = actorAWins ? scoreB : scoreA;

    Gameplay::ApplyLargeStagger(loser, winner);

    if (Settings::debugLogging) {
        logger::info(
            "[WEAPON CLASH] Winner={} ({:.1f}) | Loser={} ({:.1f}) | LARGE recoil",
            winner->GetName(), winningScore, loser->GetName(), losingScore);
    }

    return result;
}

void WeaponClash::Install(SKSE::PluginHandle pluginHandle, PRECISION_API::IVPrecision2* precisionAPI) {
    if (!precisionAPI) {
        logger::error("[WEAPON CLASH] Precision API unavailable");
        return;
    }

    PRECISION_API::WeaponCollisionCallback callback = OnWeaponCollision;
    const auto result = precisionAPI->AddWeaponWeaponCollisionCallback(pluginHandle, std::move(callback));

    if (result != PRECISION_API::APIResult::OK) {
        logger::error("[WEAPON CLASH] Failed to register callback. Result={}", static_cast<int>(result));
        return;
    }

    logger::info("[WEAPON CLASH] Precision collision callback registered successfully");
}
