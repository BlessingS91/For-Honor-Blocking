#include "NPCDirectionalBlock.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DirectionalHandler.h"

namespace NPCDirectionalBlock {
    namespace {
        constexpr char kDirectionVariable[] = "NPCBlockDirectionFHB";
        constexpr char kConfigPath[] = "Data/SKSE/Plugins/ForHonorBlocking.ini";
        constexpr char kSection[] = "NPCDirectionalBlock";
        constexpr std::array<float, 4> kDefaultColor{0.35f, 0.75f, 1.0f, 1.0f};

        using Clock = std::chrono::steady_clock;

        // Event-driven guard adjustment. No periodic random rolls.
        // PreHitFrame occurs late in some animations; these are reactive, not predictive, changes.
        constexpr float kPlayerSwingReactionChance = 0.45f;
        constexpr float kFootstepChangeChance = 0.12f;
        constexpr float kPlayerSwingReactionRange = 450.0f;  // Skyrim world units.
        constexpr auto kSwingReactionMinDelay = std::chrono::milliseconds(70);
        constexpr auto kSwingReactionMaxDelay = std::chrono::milliseconds(220);
        // A single minimum interval applies to ALL guard changes: initial guard,
        // delayed preHitFrame, footstep, and skill-based weaponSwing reactions.
        constexpr auto kGuardSwitchCooldown = std::chrono::milliseconds(650);

        // Chance to correctly read a player weaponSwing, scaled by the NPC's
        // Block actor value (clamped to 0..100). 10% at 0, 75% at 100.
        constexpr float kWeaponSwingChanceAtBlock0 = 0.10f;
        constexpr float kWeaponSwingChanceAtBlock100 = 0.75f;

        struct GuardState {
            std::int32_t direction = 0;
            bool observedBlocking = false;
            RE::ActorHandle actor;
            bool reactionPending = false;
            Clock::time_point reactionDue{};
            Clock::time_point lastDirectionChange{};
        };

        std::mutex g_mutex;
        std::unordered_map<RE::FormID, GuardState> g_guards;
        // After a block-end event, don't reassign based on an IsBlocking state
        // that hasn't cleared yet. The next explicit start event overrides this.
        std::unordered_set<RE::FormID> g_endPendingNeutral;

        bool CanSwitchGuard(const GuardState& guard, Clock::time_point now) {
            return now - guard.lastDirectionChange >= kGuardSwitchCooldown;
        }

        // Use the same directional comparison that actual NPC blocking uses,
        // rather than assuming that player and NPC left/right are identically oriented.
        std::int32_t FindMatchingGuardDirection(std::int32_t attackDirection) {
            if (attackDirection < 1 || attackDirection > 3) return 0;
            for (std::int32_t candidate = 1; candidate <= 3; ++candidate) {
                if (DirectionalHandler::IsSuccessfulBlock(attackDirection, candidate, false, false)) {
                    return candidate;
                }
            }
            return 0;
        }

        float WeaponSwingReactionChance(RE::Actor* npc) {
            if (!npc) return kWeaponSwingChanceAtBlock0;
            const auto* values = npc->AsActorValueOwner();
            const float rawSkill = values ? values->GetActorValue(RE::ActorValue::kBlock) : 0.0f;
            const float blockSkill = std::isfinite(rawSkill) ? std::clamp(rawSkill, 0.0f, 100.0f) : 0.0f;
            return kWeaponSwingChanceAtBlock0 +
                   (kWeaponSwingChanceAtBlock100 - kWeaponSwingChanceAtBlock0) * (blockSkill / 100.0f);
        }

        std::mt19937& RandomEngine() {
            static thread_local std::mt19937 engine{std::random_device{}()};
            return engine;
        }

        std::int32_t PickDirection() {
            return std::uniform_int_distribution<std::int32_t>(1, 3)(RandomEngine());
        }

        // Always select a different direction when a guard-change roll succeeds.
        std::int32_t PickDifferentDirection(std::int32_t previous) {
            if (previous < 1 || previous > 3) return PickDirection();
            const auto offset = std::uniform_int_distribution<int>(1, 2)(RandomEngine());
            return ((previous - 1 + offset) % 3) + 1;
        }

        bool RollChance(float chance) {
            return std::bernoulli_distribution(chance)(RandomEngine());
        }

        std::array<float, 4> ReadBlockingColor() {
            char buffer[128]{};
            GetPrivateProfileStringA(kSection, "BlockingColor", "", buffer, sizeof(buffer), kConfigPath);
            float rgba[4]{};
            if (std::sscanf(buffer, "%f,%f,%f,%f", &rgba[0], &rgba[1], &rgba[2], &rgba[3]) != 4) {
                return kDefaultColor;
            }
            std::array<float, 4> result{};
            for (std::size_t i = 0; i < result.size(); ++i) {
                if (!std::isfinite(rgba[i])) return kDefaultColor;
                result[i] = std::clamp(rgba[i], 0.0f, 1.0f);
            }
            return result;
        }
    }

    void OnBlockStart(RE::Actor* actor) {
        if (!actor || actor->IsPlayerRef() || !actor->Is3DLoaded()) return;

        std::int32_t chosen = 0;
        {
            std::scoped_lock lock(g_mutex);
            const auto id = actor->GetFormID();
            g_endPendingNeutral.erase(id);
            const auto [it, inserted] = g_guards.try_emplace(id, GuardState{});
            if (!inserted) return;  // One direction for the entire block.
            chosen = it->second.direction = PickDirection();
            it->second.actor = actor->GetHandle();
            it->second.lastDirectionChange = Clock::now();
        }

        const bool wrote = actor->SetGraphVariableInt(kDirectionVariable, chosen);
        if (!wrote) {
            SKSE::log::warn(
                "[NPC Guard] actor={:08X} selected={} but NPCBlockDirectionFHB is missing from the graph; "
                "HUD/gameplay will use the native direction cache.", actor->GetFormID(), chosen);
        } else {
            SKSE::log::info("[NPC Guard] START actor={:08X} direction={}", actor->GetFormID(), chosen);
        }
    }

    void OnPlayerPreHitFrame(RE::Actor* player) {
        if (!player || !player->IsPlayerRef() || player->IsDead()) return;

        // Snapshot handles rather than using live Actor pointers after releasing the lock.
        // Only NPCs currently holding a guard participate in the reaction roll.
        std::vector<RE::ActorHandle> blockingActors;
        {
            std::scoped_lock lock(g_mutex);
            blockingActors.reserve(g_guards.size());
            for (const auto& entry : g_guards) {
                blockingActors.push_back(entry.second.actor);
            }
        }

        const auto now = Clock::now();
        for (const auto& handle : blockingActors) {
            auto ptr = handle.get();
            auto* npc = ptr.get();
            if (!npc || !npc->Is3DLoaded() || npc->IsDead() || npc->IsDisabled() || !npc->IsInCombat()) continue;
            if (npc->GetPosition().GetDistance(player->GetPosition()) > kPlayerSwingReactionRange) continue;

            std::scoped_lock lock(g_mutex);
            auto it = g_guards.find(npc->GetFormID());
            if (it == g_guards.end()) continue;
            auto& guard = it->second;
            if (guard.reactionPending || !CanSwitchGuard(guard, now)) continue;
            if (!RollChance(kPlayerSwingReactionChance)) continue;

            // Schedule only. The already existing NPCDirectionalBlock::Update will
            // apply this change on the game update after the deadline is reached.
            const auto minMs = static_cast<int>(kSwingReactionMinDelay.count());
            const auto maxMs = static_cast<int>(kSwingReactionMaxDelay.count());
            const auto delayMs = std::uniform_int_distribution<int>(minMs, maxMs)(RandomEngine());
            guard.reactionDue = now + std::chrono::milliseconds(delayMs);
            guard.reactionPending = true;
            SKSE::log::debug("[NPC Guard] queued swing reaction actor={:08X} delayMs={}", npc->GetFormID(), delayMs);
        }
    }

    void OnPlayerWeaponSwing(RE::Actor* player) {
        if (!player || !player->IsPlayerRef() || player->IsDead()) return;

        // This event handler is called AFTER AttackPosition resolves the swing.
        // Read its graph direction, not the hand position or a separate classifier.
        std::int32_t attackDirection = 0;
        if (!player->GetGraphVariableInt("AttackDirectionFHB", attackDirection)) return;
        const std::int32_t correctGuard = FindMatchingGuardDirection(attackDirection);
        if (correctGuard == 0) return;

        // Parry animations can share weaponSwing annotations with regular attacks.
        std::int32_t parryActive = 0;
        std::int32_t parryWindow = 0;
        player->GetGraphVariableInt("ParryActiveFHB", parryActive);
        player->GetGraphVariableInt("ParryWindowFHB", parryWindow);
        if (parryActive == 1 || parryWindow == 1) return;

        std::vector<RE::ActorHandle> blockingActors;
        {
            std::scoped_lock lock(g_mutex);
            blockingActors.reserve(g_guards.size());
            for (const auto& entry : g_guards) {
                blockingActors.push_back(entry.second.actor);
            }
        }

        const auto now = Clock::now();
        for (const auto& handle : blockingActors) {
            auto ptr = handle.get();
            auto* npc = ptr.get();
            if (!npc || !npc->Is3DLoaded() || npc->IsDead() || npc->IsDisabled() || !npc->IsInCombat()) continue;
            if (npc->GetPosition().GetDistance(player->GetPosition()) > kPlayerSwingReactionRange) continue;

            // Compute chance outside the guard mutex; only one read per swing/NPC.
            const float chance = WeaponSwingReactionChance(npc);
            bool changed = false;
            {
                std::scoped_lock lock(g_mutex);
                const auto it = g_guards.find(npc->GetFormID());
                if (it == g_guards.end()) continue;
                auto& guard = it->second;

                // If already correct, do not let an earlier queued random change
                // immediately undo that correct guard.
                if (guard.direction == correctGuard) {
                    guard.reactionPending = false;
                    continue;
                }
                // A pending preHitFrame change is NOT an actual switch yet.
                // The universal cooldown governs actual changes; a successful
                // reaction supersedes and cancels the old pending random change.
                if (!CanSwitchGuard(guard, now) || !RollChance(chance)) continue;

                guard.direction = correctGuard;
                guard.lastDirectionChange = now;
                guard.reactionPending = false;
                changed = true;
            }

            if (changed) {
                const bool wrote = npc->SetGraphVariableInt(kDirectionVariable, correctGuard);
                SKSE::log::debug(
                    "[NPC Guard] skill reaction actor={:08X} attack={} guard={} chance={:.2f} wrote={}",
                    npc->GetFormID(), attackDirection, correctGuard, chance, wrote);
            }
        }
    }

    void OnFootstep(RE::Actor* actor) {
        if (!actor || actor->IsPlayerRef() || !actor->Is3DLoaded() || !actor->IsInCombat() || actor->IsDead()) return;

        std::int32_t newDirection = 0;
        {
            std::scoped_lock lock(g_mutex);
            const auto it = g_guards.find(actor->GetFormID());
            if (it == g_guards.end()) return;
            auto& guard = it->second;
            const auto now = Clock::now();
            if (guard.reactionPending || !CanSwitchGuard(guard, now)) return;
            if (!RollChance(kFootstepChangeChance)) return;

            newDirection = guard.direction = PickDifferentDirection(guard.direction);
            guard.lastDirectionChange = now;
        }

        actor->SetGraphVariableInt(kDirectionVariable, newDirection);
        SKSE::log::debug("[NPC Guard] footstep change actor={:08X} direction={}", actor->GetFormID(), newDirection);
    }

    void OnBlockEnd(RE::Actor* actor) {
        if (!actor || actor->IsPlayerRef()) return;
        bool hadGuard = false;
        {
            std::scoped_lock lock(g_mutex);
            const auto id = actor->GetFormID();
            hadGuard = g_guards.erase(id) != 0;
            if (hadGuard) g_endPendingNeutral.insert(id);
        }
        if (!hadGuard) return;
        if (actor->Is3DLoaded()) {
            actor->SetGraphVariableInt(kDirectionVariable, 0);
        }
        SKSE::log::info("[NPC Guard] END actor={:08X}", actor->GetFormID());
    }

    void Update(RE::Actor* actor) {
        if (!actor || actor->IsPlayerRef()) return;
        if (!actor->Is3DLoaded() || actor->IsDead() || actor->IsDisabled() || !actor->IsInCombat()) {
            OnBlockEnd(actor);
            return;
        }

        bool graphBlocking = false;
        actor->GetGraphVariableBool("IsBlocking", graphBlocking);
        const bool isBlocking = actor->IsBlocking() || graphBlocking;
        bool shouldAssign = false;
        bool shouldEnd = false;
        {
            std::scoped_lock lock(g_mutex);
            const auto id = actor->GetFormID();
            if (isBlocking) {
                if (const auto it = g_guards.find(id); it != g_guards.end()) {
                    it->second.observedBlocking = true;
                } else {
                    shouldAssign = !g_endPendingNeutral.contains(id);
                }
            } else {
                g_endPendingNeutral.erase(id);
                if (const auto it = g_guards.find(id); it != g_guards.end()) {
                    // Do not undo an early blockStartOut before the graph has
                    // actually entered its blocking state at least once.
                    shouldEnd = it->second.observedBlocking;
                }
            }
        }
        if (shouldAssign) OnBlockStart(actor);
        if (shouldEnd) {
            OnBlockEnd(actor);
            return;
        }

        // An existing update already checks guard lifecycle. Only a pending
        // PreHitFrame reaction needs a time comparison; randomness stays event-driven.
        if (!isBlocking) return;
        std::int32_t delayedDirection = 0;
        {
            std::scoped_lock lock(g_mutex);
            const auto it = g_guards.find(actor->GetFormID());
            const auto now = Clock::now();
            if (it != g_guards.end() && it->second.reactionPending && now >= it->second.reactionDue) {
                auto& guard = it->second;
                guard.reactionPending = false;
                // Another event may have changed this guard since it was queued.
                // The pending roll must never violate the common cooldown.
                if (CanSwitchGuard(guard, now)) {
                    guard.lastDirectionChange = now;
                    delayedDirection = guard.direction = PickDifferentDirection(guard.direction);
                }
            }
        }
        if (delayedDirection != 0) {
            actor->SetGraphVariableInt(kDirectionVariable, delayedDirection);
            SKSE::log::debug("[NPC Guard] delayed swing reaction actor={:08X} direction={}",
                             actor->GetFormID(), delayedDirection);
        }
    }

    std::int32_t GetDirection(RE::Actor* actor) {
        if (!actor || actor->IsPlayerRef()) return 0;
        std::scoped_lock lock(g_mutex);
        if (const auto it = g_guards.find(actor->GetFormID()); it != g_guards.end()) {
            return it->second.direction;
        }
        return 0;
    }

    bool IsSuccessfulBlock(std::int32_t attackDirection, RE::Actor* defender, bool allBlock) {
        if (!defender || defender->IsPlayerRef()) return false;
        return DirectionalHandler::IsSuccessfulBlock(attackDirection, GetDirection(defender), allBlock, false);
    }

    std::array<float, 4>& GetBlockingColor() {
        static auto color = ReadBlockingColor();
        return color;
    }

    void SaveBlockingColor() {
        const auto& color = GetBlockingColor();
        const std::string value = std::to_string(color[0]) + "," + std::to_string(color[1]) + "," +
                                  std::to_string(color[2]) + "," + std::to_string(color[3]);
        WritePrivateProfileStringA(kSection, "BlockingColor", value.c_str(), kConfigPath);
    }

    void ResetBlockingColor() {
        GetBlockingColor() = kDefaultColor;
        SaveBlockingColor();
    }

    void Reset() {
        std::scoped_lock lock(g_mutex);
        g_guards.clear();
        g_endPendingNeutral.clear();
    }
}
