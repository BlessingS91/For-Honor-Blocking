#pragma once

namespace DirectionalHandler {
    enum class Direction { None, Top, Right, Left };

    // Converts CameraMovementCMF into the player's
    // three directional block categories.
    Direction GetBlockDirection(int cameraMovement);

    // Converts AttackDirectionNPC into the NPC's
    // three directional attack categories.
    Direction GetAttackDirection(int attackDirectionNPC);

    // Determines whether the player's block direction
    // successfully counters the NPC's attack direction.
    bool IsSuccessfulBlock(int attackDirectionNPC, int cameraMovement);
}