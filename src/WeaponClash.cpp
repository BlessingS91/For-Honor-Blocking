#include "WeaponClash.h"

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
    std::unordered_map<RE::FormID, Clock::time_point> lastClashes;

    bool IsAttacking(RE::Actor* actor) {
        if (!actor) {
            return false;
        }

        const auto* state = actor->AsActorState();

        if (!state) {
            return false;
        }

        switch (state->GetAttackState()) {
            case RE::ATTACK_STATE_ENUM::kSwing:
            case RE::ATTACK_STATE_ENUM::kHit:
            case RE::ATTACK_STATE_ENUM::kFollowThrough:
                return true;

            default:
                return false;
        }
    }

    int GetWeaponTier(RE::Actor* actor) {
        if (!actor) {
            return 0;
        }

        auto* equipped = actor->GetEquippedObject(false);
        auto* weapon = equipped ? equipped->As<RE::TESObjectWEAP>() : nullptr;

        if (!weapon) {
            return 0;
        }

        switch (weapon->GetWeaponType()) {
            case RE::WEAPON_TYPE::kOneHandDagger:
                return 1;

            case RE::WEAPON_TYPE::kOneHandSword:
            case RE::WEAPON_TYPE::kOneHandAxe:
            case RE::WEAPON_TYPE::kOneHandMace:
                return 2;

            case RE::WEAPON_TYPE::kTwoHandSword:
                return 3;

            case RE::WEAPON_TYPE::kTwoHandAxe:
                return 4;

            default:
                return 0;
        }
    }

    bool CanTriggerClash(RE::Actor* enemy) {
        if (!enemy) {
            return false;
        }

        const auto now = Clock::now();

        std::scoped_lock lock(clashMutex);

        const auto id = enemy->GetFormID();
        const auto it = lastClashes.find(id);

        if (it != lastClashes.end() && now - it->second < kClashCooldown) {
            return false;
        }

        lastClashes[id] = now;

        return true;
    }
}

PRECISION_API::WeaponCollisionCallbackReturn WeaponClash::OnWeaponCollision(
    const PRECISION_API::PrecisionHitData& hitData) {
    PRECISION_API::WeaponCollisionCallbackReturn result{};

    auto* attacker = hitData.attacker;
    auto* target = hitData.target ? hitData.target->As<RE::Actor>() : nullptr;

    if (!attacker || !target || attacker == target) {
        return result;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!player) {
        return result;
    }

    RE::Actor* enemy = nullptr;

    if (attacker == player) {
        enemy = target;
    } else if (target == player) {
        enemy = attacker;
    } else {
        return result;
    }

    if (!enemy || enemy->IsDead() || enemy->IsDisabled()) {
        return result;
    }

    if (!IsAttacking(player) || !IsAttacking(enemy)) {
        return result;
    }

    std::int32_t playerDirection = 0;
    std::int32_t enemyDirection = 0;

    const bool gotPlayerDirection = player->GetGraphVariableInt("CameraMovementFHB", playerDirection);

    const bool gotEnemyDirection = enemy->GetGraphVariableInt("AttackDirectionFHB", enemyDirection);

    if (!gotPlayerDirection || !gotEnemyDirection) {
        return result;
    }

    if (playerDirection < 1 || playerDirection > 3 || enemyDirection < 1 || enemyDirection > 3 ||
        playerDirection != enemyDirection) {
        return result;
    }

    const int playerTier = GetWeaponTier(player);
    const int enemyTier = GetWeaponTier(enemy);

    if (playerTier == 0 || enemyTier == 0) {
        return result;
    }

    if (!CanTriggerClash(enemy)) {
        return result;
    }

    if (playerTier > enemyTier) {
        Gameplay::ApplyLargeStagger(enemy, player);
    } else {
        Gameplay::ApplyNormalStagger(enemy, player);
    }

    if (Settings::debugLogging) {
        logger::info(
            "[WEAPON CLASH] Player={} | Enemy={} | Direction={} | "
            "PlayerTier={} | EnemyTier={} | Recoil={}",
            player->GetName(), enemy->GetName(), playerDirection, playerTier, enemyTier,
            playerTier > enemyTier ? "LARGE" : "NORMAL");
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