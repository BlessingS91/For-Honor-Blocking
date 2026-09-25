#include "DirectionalHandler.h"

namespace DirectionalHandler {
    namespace {
        AttackEffects g_attackEffects;
    }

    bool Initialize() {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        if (!dataHandler) {
            return false;
        }

        if (!dataHandler->LookupModByName("For Honor Balance Patch.esp")) {
            return false;
        }

        g_attackEffects.leftLight = dataHandler->LookupForm<RE::EffectSetting>(0x3880, "For Honor Balance Patch.esp");

        g_attackEffects.rightLight = dataHandler->LookupForm<RE::EffectSetting>(0x3881, "For Honor Balance Patch.esp");

        g_attackEffects.leftHeavy = dataHandler->LookupForm<RE::EffectSetting>(0x3882, "For Honor Balance Patch.esp");

        g_attackEffects.rightHeavy = dataHandler->LookupForm<RE::EffectSetting>(0x3883, "For Honor Balance Patch.esp");

        g_attackEffects.backHeavy = dataHandler->LookupForm<RE::EffectSetting>(0x3887, "For Honor Balance Patch.esp");

        g_attackEffects.backLight = dataHandler->LookupForm<RE::EffectSetting>(0x388A, "For Honor Balance Patch.esp");

        g_attackEffects.initialized = g_attackEffects.leftLight && g_attackEffects.rightLight &&
                                      g_attackEffects.leftHeavy && g_attackEffects.rightHeavy &&
                                      g_attackEffects.backHeavy && g_attackEffects.backLight;

        return g_attackEffects.initialized;
    }

    const AttackEffects& GetAttackEffects() { return g_attackEffects; }

    Direction GetBlockDirection(int cameraMovement) {
        switch (cameraMovement) {
            case 8:
            case 1:
            case 2:
            case 5:
            case 0:
                return Direction::Top;

            case 3:
            case 4:
                return Direction::Right;

            case 6:
            case 7:
                return Direction::Left;

            default:
                return Direction::Top;
        }
    }

    Direction GetAttackDirection(int attackDirectionNPC) {
        switch (attackDirectionNPC) {
            case 8:
            case 1:
            case 2:
            case 5:
                return Direction::Top;

            case 3:
            case 4:
                return Direction::Left;

            case 6:
            case 7:
                return Direction::Right;

            case 0:
                return Direction::None;

            default:
                return Direction::None;
        }
    }

    bool IsSuccessfulBlock(int attackDirectionNPC, int cameraMovement) {
        const auto attack = GetAttackDirection(attackDirectionNPC);
        const auto block = GetBlockDirection(cameraMovement);

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