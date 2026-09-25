#pragma once

#include "RE/Skyrim.h"

namespace DirectionalHandler {

    enum class Direction { None, Top, Right, Left };

    struct AttackEffects {
        RE::EffectSetting* leftLight{nullptr};
        RE::EffectSetting* rightLight{nullptr};
        RE::EffectSetting* leftHeavy{nullptr};
        RE::EffectSetting* rightHeavy{nullptr};
        RE::EffectSetting* backHeavy{nullptr};
        RE::EffectSetting* backLight{nullptr};

        bool initialized{false};
    };

    // Initializes directional attack effects if the
    // For Honor Balance Patch.esp plugin is loaded.
    bool Initialize();

    // Returns the cached directional attack effects.
    const AttackEffects& GetAttackEffects();

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