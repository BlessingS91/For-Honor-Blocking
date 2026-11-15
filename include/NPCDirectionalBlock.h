#pragma once

#include <array>
#include <cstdint>

namespace RE {
    class Actor;
}

namespace NPCDirectionalBlock {
    // Guard lifecycle. AttackPosition.cpp supplies the existing animation events.
    void OnBlockStart(RE::Actor* actor);
    void OnBlockEnd(RE::Actor* actor);

    // Called by AttackPosition's existing animation sink.
    // Player preHitFrame: chance to schedule a delayed random change.
    // Player weaponSwing: Block-skill chance to match the player's graph attack direction.
    // NPC footsteps: occasional random change. All obey a shared switch cooldown.
    void OnPlayerPreHitFrame(RE::Actor* player);
    void OnPlayerWeaponSwing(RE::Actor* player);
    void OnFootstep(RE::Actor* actor);

    // Called through AttackPosition's existing update; no additional hook.
    void Update(RE::Actor* actor);

    // Native authoritative guard state, independent of graph read/write success.
    // 0 = neutral; 1 = top; 2 = right; 3 = left.
    std::int32_t GetDirection(RE::Actor* actor);

    // Reuse the established mirrored DirectionalHandler matching rules.
    bool IsSuccessfulBlock(std::int32_t attackDirection, RE::Actor* defender, bool allBlock);

    // Color belongs to this module, but persists in ForHonorBlocking.ini.
    std::array<float, 4>& GetBlockingColor();
    void SaveBlockingColor();
    void ResetBlockingColor();

    // Clear native state when switching saves/new games.
    void Reset();
}
