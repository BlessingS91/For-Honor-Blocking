#include "DirectionalHandler.h"

namespace DirectionalHandler {

    Direction GetBlockDirection(int cameraMovement) {
        switch (cameraMovement) {
            // Top
            case 8:
            case 1:
            case 2:
            case 5:
                return Direction::Top;

            // Right
            case 3:
            case 4:
                return Direction::Right;

            // Left
            case 6:
            case 7:
                return Direction::Left;

            // No movement / unknown
            case 0:
            default:
                return Direction::None;
        }
    }

    Direction GetAttackDirection(int attackDirectionNPC) {
        switch (attackDirectionNPC) {
            // Top
            case 8:
            case 1:
            case 2:
            case 5:
                return Direction::Top;

            // Right
            case 3:
            case 4:
                return Direction::Right;

            // Left
            case 6:
            case 7:
                return Direction::Left;

            // No movement / unknown
            case 0:
            default:
                return Direction::None;
        }
    }

    bool IsSuccessfulBlock(int attackDirectionNPC, int cameraMovement) {
        const Direction attack = GetAttackDirection(attackDirectionNPC);
        const Direction block = GetBlockDirection(cameraMovement);

        if (attack == Direction::None || block == Direction::None) {
            return false;
        }

        switch (attack) {
            case Direction::Top:
                return block == Direction::Top;

            case Direction::Right:
                return block == Direction::Left;

            case Direction::Left:
                return block == Direction::Right;

            default:
                return false;
        }
    }

}