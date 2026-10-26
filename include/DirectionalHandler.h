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
    const AttackEffects* GetAttackEffects();

    // Converts CameraMovementFHB into the player's block direction.
    Direction GetBlockDirection(int cameraMovementFHB);

    // Converts AttackDirectionFHB into the NPC's attack direction.
    Direction GetAttackDirection(int attackDirectionFHB);

    // Determines whether the player's block counters the NPC's attack.
    bool IsSuccessfulBlock(int attackDirectionFHB, int cameraMovementFHB);

}