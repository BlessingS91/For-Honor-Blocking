#pragma once

#include <array>
#include <string>

namespace Settings {
    // Existing blocking settings and save/load API (kept intact).
    extern float blockSkillLevel;
    extern bool hasEmpoweredBlock;
    extern bool empoweredBlockRequired;
    extern float lightAttackReductionPerLevel;
    extern float powerAttackReductionPerLevel;
    extern float lightAttackerStagger;
    extern float lightDefenderStagger;
    extern float powerAttackerStagger;
    extern float powerDefenderStagger;
    extern std::string superiorBlockPerk;
    extern float staminaOneHandSword;
    extern float staminaDagger;
    extern float staminaOneHandAxe;
    extern float staminaMace;
    extern float staminaTwoHandSword;
    extern float staminaTwoHandAxe;
    extern float staminaBow;
    extern float staminaCrossbow;
    extern float staminaStaff;
    extern float staminaUnarmed;
    extern float staminaOther;
    extern bool debugLogging;

    // ========================================================================
    // PLAYER HUD SETTINGS - Used by PrismaUI's shared three-segment shield.
    // ========================================================================
    struct PlayerUISettings {
        bool enabled = true;
        bool editMode = false;
        bool showCenter = true;
        bool moveCenterWithDirection = false;
        bool centerInThirdPerson = false;
        bool scaleWithResolution = true;
        int positionXPercent = 50;
        int positionYPercent = 50;
        int attachOffsetXPixels = 0;
        int attachOffsetYPixels = 0;
        int scalePercent = 100;
        int diameterPixels = 260;
        int centerPieceSizePercent = 100;
        int rotationDegrees = 0;
        int opacityPercent = 100;        // Shield/background opacity; never dims active indicators.
        int activeOpacityPercent = 100;  // Active and unblockable indicator opacity.
        int centerMovementPixels = 20;
        int centerMovementDurationMs = 100;
        std::array<float, 4> activeColor{0.95f, 0.72f, 0.22f, 1.0f};
        std::array<float, 4> unblockableColor{1.0f, 0.2f, 0.1f, 1.0f};
        std::array<float, 4> inactiveColor{0.08f, 0.09f, 0.10f, 0.72f};
        std::array<float, 4> borderColor{0.80f, 0.80f, 0.76f, 0.90f};
        std::array<float, 4> centerColor{0.04f, 0.04f, 0.05f, 0.86f};
        std::array<float, 4> centerActiveColor{0.95f, 0.72f, 0.22f, 1.0f};
        std::string sharedSegmentImage;
        std::array<std::string, 3> segmentImages{};
        std::string centerImage;
    };

    // ========================================================================
    // NPC HUD SETTINGS - Same shield/center geometry, independent values.
    // ========================================================================
    struct NPCUISettings {
        bool enabled = true;
        bool alwaysShowInCombat = false;
        bool scaleWithResolution = true;
        bool scaleWithDistance = true;
        bool mirrorAttackDirections = true;
        float topThreshold = 30.0f;
        bool showCenter = true;
        int scalePercent = 52;
        int opacityPercent = 100;        // Shield/background opacity; never dims active indicators.
        int activeOpacityPercent = 100;  // Active and unblockable indicator opacity.
        int offsetXPixels = 0;
        int offsetYPixels = 0;
        int rotationDegrees = 0;
        int diameterPixels = 260;
        int centerPieceSizePercent = 100;
        std::array<float, 4> activeColor{0.95f, 0.72f, 0.22f, 1.0f};
        std::array<float, 4> unblockableColor{1.0f, 0.2f, 0.1f, 1.0f};
        std::array<float, 4> inactiveColor{0.08f, 0.09f, 0.10f, 0.72f};
        std::array<float, 4> borderColor{0.80f, 0.80f, 0.76f, 0.90f};
        std::array<float, 4> centerColor{0.04f, 0.04f, 0.05f, 0.86f};
        std::string sharedSegmentImage;
        std::array<std::string, 3> segmentImages{};
        std::string centerImage;
    };
    extern PlayerUISettings PlayerUI;
    extern NPCUISettings NPCUI;

    void Load();
    void Save();
    void ResetToDefaults();
}
