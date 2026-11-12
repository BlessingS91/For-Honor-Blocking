#include "Settings.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "InputTracking.h"
#include "logger.h"

namespace Settings {

    // ============================================================================
    // BLOCKING SETTINGS - RUNTIME VALUES
    // Persistent values are loaded from the [Blocking] section.
    // ============================================================================
    float blockSkillLevel = 0.0f;

    bool hasEmpoweredBlock = false;

    bool empoweredBlockRequired = false;

    float lightAttackReductionPerLevel = 1.0f;

    float powerAttackReductionPerLevel = 0.35f;

    float lightAttackerStagger = 25.0f;

    float lightDefenderStagger = 25.0f;

    float powerAttackerStagger = 0.0f;

    float powerDefenderStagger = 0.0f;

    // Block stamina cost for each attacking weapon type.
    float staminaOneHandSword = 10.0f;
    float staminaDagger = 6.0f;
    float staminaOneHandAxe = 11.0f;
    float staminaMace = 12.0f;
    float staminaTwoHandSword = 16.0f;
    float staminaTwoHandAxe = 18.0f;
    float staminaBow = 6.0f;
    float staminaCrossbow = 8.0f;
    float staminaStaff = 4.0f;
    float staminaUnarmed = 5.0f;
    float staminaOther = 10.0f;

    // Blocking-related identifiers and diagnostics.
    std::string superiorBlockPerk;

    bool debugLogging = false;

    // ========================================================================
    // HUD CONFIGURATION (shared geometry, independent Player/NPC values)
    // ========================================================================
    PlayerUISettings PlayerUI{};
    NPCUISettings NPCUI{};

    namespace {

        // ============================================================================
        // INI SECTIONS AND FILE LOCATION
        // Both modules share ForHonorBlocking.ini but use different sections.
        // ============================================================================
        constexpr const char* kSection = "Blocking";
        constexpr const char* kInputTrackingSection = "InputTracking";
        constexpr const char* kConfigPath = "Data/SKSE/Plugins/ForHonorBlocking.ini";

        // ============================================================================
        // INI READ HELPERS
        // Reads existing values without changing previous fallback behavior.
        // ============================================================================
        float GetFloatInSection(const char* section, const char* key, float defaultValue) {
            char buffer[64]{};

            GetPrivateProfileStringA(section, key, "", buffer, sizeof(buffer), kConfigPath);

            if (buffer[0] == '\0') {
                return defaultValue;
            }

            try {
                return std::stof(buffer);
            } catch (...) {
                return defaultValue;
            }
        }

        float GetFloat(const char* key, float defaultValue) { return GetFloatInSection(kSection, key, defaultValue); }

        int GetInputTrackingInt(const char* key, int defaultValue) {
            return static_cast<int>(GetPrivateProfileIntA(kInputTrackingSection, key, defaultValue, kConfigPath));
        }

        // ============================================================================
        // INI WRITE HELPERS
        // Writes updated values to the matching INI section.
        // ============================================================================
        void WriteFloatInSection(const char* section, const char* key, float value) {
            const std::string valueString = std::to_string(value);
            WritePrivateProfileStringA(section, key, valueString.c_str(), kConfigPath);
        }

        void WriteFloat(const char* key, float value) { WriteFloatInSection(kSection, key, value); }

        void WriteInputTrackingInt(const char* key, int value) {
            const std::string valueString = std::to_string(value);
            WritePrivateProfileStringA(kInputTrackingSection, key, valueString.c_str(), kConfigPath);
        }

        std::string GetString(const char* key, const char* defaultValue) {
            char buffer[256]{};

            GetPrivateProfileStringA(kSection, key, defaultValue, buffer, sizeof(buffer), kConfigPath);

            return buffer;
        }

        void WriteString(const char* key, const std::string& value) {
            WritePrivateProfileStringA(kSection, key, value.c_str(), kConfigPath);
        }
    }

    // ============================================================================
    // HUD INI HELPERS - PlayerHUD and NPCHUD are separate INI sections.
    // Existing [Blocking] and [InputTracking] sections remain unchanged.
    // ============================================================================
    namespace {
        int ReadHudInt(const char* section, const char* name, int fallback) {
            return static_cast<int>(GetPrivateProfileIntA(section, name, fallback, kConfigPath));
        }
        bool ReadHudBool(const char* section, const char* name, bool fallback) {
            return ReadHudInt(section, name, fallback ? 1 : 0) != 0;
        }
        std::string ReadHudString(const char* section, const char* name, const std::string& fallback) {
            char data[512]{};
            GetPrivateProfileStringA(section, name, fallback.c_str(), data, sizeof(data), kConfigPath);
            return data;
        }
        void WriteHudInt(const char* section, const char* name, int value) {
            const auto data = std::to_string(value);
            WritePrivateProfileStringA(section, name, data.c_str(), kConfigPath);
        }
        void WriteHudBool(const char* section, const char* name, bool value) {
            WriteHudInt(section, name, value ? 1 : 0);
        }
        void WriteHudString(const char* section, const char* name, const std::string& value) {
            WritePrivateProfileStringA(section, name, value.c_str(), kConfigPath);
        }
        std::array<float, 4> ReadHudColor(const char* section, const char* name, std::array<float, 4> fallback) {
            const auto value = ReadHudString(section, name, "");
            if (value.empty()) return fallback;
            float channels[4]{};
            if (std::sscanf(value.c_str(), "%f,%f,%f,%f", &channels[0], &channels[1], &channels[2], &channels[3]) != 4)
                return fallback;
            for (int i = 0; i < 4; ++i) {
                if (!std::isfinite(channels[i])) return fallback;
                fallback[i] = std::clamp(channels[i], 0.0f, 1.0f);
            }
            return fallback;
        }
        void WriteHudColor(const char* section, const char* name, const std::array<float, 4>& color) {
            const auto value = std::to_string(color[0]) + "," + std::to_string(color[1]) + "," +
                               std::to_string(color[2]) + "," + std::to_string(color[3]);
            WriteHudString(section, name, value);
        }
        void LoadHudPlayer() {
            constexpr auto section = "PlayerHUD";
            auto& s = PlayerUI;
            s.enabled = ReadHudBool(section, "enabled", s.enabled);
            s.editMode = ReadHudBool(section, "editMode", s.editMode);
            s.showCenter = ReadHudBool(section, "showCenter", s.showCenter);
            s.moveCenterWithDirection = ReadHudBool(section, "moveCenterWithDirection", s.moveCenterWithDirection);
            s.centerInThirdPerson = ReadHudBool(section, "centerInThirdPerson", s.centerInThirdPerson);
            s.scaleWithResolution = ReadHudBool(section, "scaleWithResolution", s.scaleWithResolution);
            s.positionXPercent = ReadHudInt(section, "positionXPercent", s.positionXPercent);
            s.positionYPercent = ReadHudInt(section, "positionYPercent", s.positionYPercent);
            s.attachOffsetXPixels = ReadHudInt(section, "attachOffsetXPixels", s.attachOffsetXPixels);
            s.attachOffsetYPixels = ReadHudInt(section, "attachOffsetYPixels", s.attachOffsetYPixels);
            s.scalePercent = ReadHudInt(section, "scalePercent", s.scalePercent);
            s.diameterPixels = ReadHudInt(section, "diameterPixels", s.diameterPixels);
            s.centerPieceSizePercent = ReadHudInt(section, "centerPieceSizePercent", s.centerPieceSizePercent);
            s.rotationDegrees = ReadHudInt(section, "rotationDegrees", s.rotationDegrees);
            s.opacityPercent = std::clamp(ReadHudInt(section, "opacityPercent", s.opacityPercent), 0, 100);
            s.activeOpacityPercent =
                std::clamp(ReadHudInt(section, "activeOpacityPercent", s.activeOpacityPercent), 0, 100);
            s.centerMovementPixels = ReadHudInt(section, "centerMovementPixels", s.centerMovementPixels);
            s.centerMovementDurationMs = ReadHudInt(section, "centerMovementDurationMs", s.centerMovementDurationMs);
            s.activeColor = ReadHudColor(section, "activeColor", s.activeColor);
            s.unblockableColor = ReadHudColor(section, "unblockableColor", s.unblockableColor);
            s.inactiveColor = ReadHudColor(section, "inactiveColor", s.inactiveColor);
            s.borderColor = ReadHudColor(section, "borderColor", s.borderColor);
            s.centerColor = ReadHudColor(section, "centerColor", s.centerColor);
            s.centerActiveColor = ReadHudColor(section, "centerActiveColor", s.centerActiveColor);
            s.sharedSegmentImage = ReadHudString(section, "sharedSegmentImage", s.sharedSegmentImage);
            s.centerImage = ReadHudString(section, "centerImage", s.centerImage);
            for (int index = 0; index < 3; ++index) {
                const auto key = "segmentImage" + std::to_string(index + 1);
                s.segmentImages[index] = ReadHudString(section, key.c_str(), s.segmentImages[index]);
            }
        }
        void LoadHudNpc() {
            constexpr auto section = "NPCHUD";
            auto& s = NPCUI;
            s.enabled = ReadHudBool(section, "enabled", s.enabled);
            s.alwaysShowInCombat = ReadHudBool(section, "alwaysShowInCombat", s.alwaysShowInCombat);
            s.scaleWithResolution = ReadHudBool(section, "scaleWithResolution", s.scaleWithResolution);
            s.scaleWithDistance = ReadHudBool(section, "scaleWithDistance", s.scaleWithDistance);
            s.mirrorAttackDirections = ReadHudBool(section, "mirrorAttackDirections", s.mirrorAttackDirections);
            s.showCenter = ReadHudBool(section, "showCenter", s.showCenter);
            s.topThreshold = std::clamp(GetFloatInSection(section, "topThreshold", s.topThreshold), 0.0f, 200.0f);
            s.scalePercent = ReadHudInt(section, "scalePercent", s.scalePercent);
            s.opacityPercent = std::clamp(ReadHudInt(section, "opacityPercent", s.opacityPercent), 0, 100);
            s.activeOpacityPercent =
                std::clamp(ReadHudInt(section, "activeOpacityPercent", s.activeOpacityPercent), 0, 100);
            s.offsetXPixels = ReadHudInt(section, "offsetXPixels", s.offsetXPixels);
            s.offsetYPixels = ReadHudInt(section, "offsetYPixels", s.offsetYPixels);
            s.rotationDegrees = ReadHudInt(section, "rotationDegrees", s.rotationDegrees);
            s.diameterPixels = ReadHudInt(section, "diameterPixels", s.diameterPixels);
            s.centerPieceSizePercent = ReadHudInt(section, "centerPieceSizePercent", s.centerPieceSizePercent);
            s.activeColor = ReadHudColor(section, "activeColor", s.activeColor);
            s.unblockableColor = ReadHudColor(section, "unblockableColor", s.unblockableColor);
            s.inactiveColor = ReadHudColor(section, "inactiveColor", s.inactiveColor);
            s.borderColor = ReadHudColor(section, "borderColor", s.borderColor);
            s.centerColor = ReadHudColor(section, "centerColor", s.centerColor);
            s.sharedSegmentImage = ReadHudString(section, "sharedSegmentImage", s.sharedSegmentImage);
            s.centerImage = ReadHudString(section, "centerImage", s.centerImage);
            for (int index = 0; index < 3; ++index) {
                const auto key = "segmentImage" + std::to_string(index + 1);
                s.segmentImages[index] = ReadHudString(section, key.c_str(), s.segmentImages[index]);
            }
        }
        void SaveHudPlayer() {
            constexpr auto section = "PlayerHUD";
            const auto& s = PlayerUI;
            WriteHudBool(section, "enabled", s.enabled);
            WriteHudBool(section, "editMode", s.editMode);
            WriteHudBool(section, "showCenter", s.showCenter);
            WriteHudBool(section, "moveCenterWithDirection", s.moveCenterWithDirection);
            WriteHudBool(section, "centerInThirdPerson", s.centerInThirdPerson);
            WriteHudBool(section, "scaleWithResolution", s.scaleWithResolution);
            WriteHudInt(section, "positionXPercent", s.positionXPercent);
            WriteHudInt(section, "positionYPercent", s.positionYPercent);
            WriteHudInt(section, "attachOffsetXPixels", s.attachOffsetXPixels);
            WriteHudInt(section, "attachOffsetYPixels", s.attachOffsetYPixels);
            WriteHudInt(section, "scalePercent", s.scalePercent);
            WriteHudInt(section, "diameterPixels", s.diameterPixels);
            WriteHudInt(section, "centerPieceSizePercent", s.centerPieceSizePercent);
            WriteHudInt(section, "rotationDegrees", s.rotationDegrees);
            WriteHudInt(section, "opacityPercent", s.opacityPercent);
            WriteHudInt(section, "activeOpacityPercent", s.activeOpacityPercent);
            WriteHudInt(section, "centerMovementPixels", s.centerMovementPixels);
            WriteHudInt(section, "centerMovementDurationMs", s.centerMovementDurationMs);
            WriteHudColor(section, "activeColor", s.activeColor);
            WriteHudColor(section, "unblockableColor", s.unblockableColor);
            WriteHudColor(section, "inactiveColor", s.inactiveColor);
            WriteHudColor(section, "borderColor", s.borderColor);
            WriteHudColor(section, "centerColor", s.centerColor);
            WriteHudColor(section, "centerActiveColor", s.centerActiveColor);
            WriteHudString(section, "sharedSegmentImage", s.sharedSegmentImage);
            WriteHudString(section, "centerImage", s.centerImage);
            for (int index = 0; index < 3; ++index) {
                const auto key = "segmentImage" + std::to_string(index + 1);
                WriteHudString(section, key.c_str(), s.segmentImages[index]);
            }
        }
        void SaveHudNpc() {
            constexpr auto section = "NPCHUD";
            const auto& s = NPCUI;
            WriteHudBool(section, "enabled", s.enabled);
            WriteHudBool(section, "alwaysShowInCombat", s.alwaysShowInCombat);
            WriteHudBool(section, "scaleWithResolution", s.scaleWithResolution);
            WriteHudBool(section, "scaleWithDistance", s.scaleWithDistance);
            WriteHudBool(section, "mirrorAttackDirections", s.mirrorAttackDirections);
            WriteHudBool(section, "showCenter", s.showCenter);
            WriteFloatInSection(section, "topThreshold", s.topThreshold);
            WriteHudInt(section, "scalePercent", s.scalePercent);
            WriteHudInt(section, "opacityPercent", s.opacityPercent);
            WriteHudInt(section, "activeOpacityPercent", s.activeOpacityPercent);
            WriteHudInt(section, "offsetXPixels", s.offsetXPixels);
            WriteHudInt(section, "offsetYPixels", s.offsetYPixels);
            WriteHudInt(section, "rotationDegrees", s.rotationDegrees);
            WriteHudInt(section, "diameterPixels", s.diameterPixels);
            WriteHudInt(section, "centerPieceSizePercent", s.centerPieceSizePercent);
            WriteHudColor(section, "activeColor", s.activeColor);
            WriteHudColor(section, "unblockableColor", s.unblockableColor);
            WriteHudColor(section, "inactiveColor", s.inactiveColor);
            WriteHudColor(section, "borderColor", s.borderColor);
            WriteHudColor(section, "centerColor", s.centerColor);
            WriteHudString(section, "sharedSegmentImage", s.sharedSegmentImage);
            WriteHudString(section, "centerImage", s.centerImage);
            for (int index = 0; index < 3; ++index) {
                const auto key = "segmentImage" + std::to_string(index + 1);
                WriteHudString(section, key.c_str(), s.segmentImages[index]);
            }
        }
    }

    // ============================================================================
    // LOAD SETTINGS FROM INI
    // ============================================================================
    void Load() {
        // [Blocking] - damage reduction, stagger, and superior block.
        empoweredBlockRequired = GetFloat("EmpoweredBlockRequired", 0.0f) != 0.0f;

        lightAttackReductionPerLevel = GetFloat("LightAttackReductionPerLevel", 1.0f);

        powerAttackReductionPerLevel = GetFloat("PowerAttackReductionPerLevel", 0.35f);

        lightAttackerStagger = GetFloat("LightAttackerStagger", 25.0f);

        lightDefenderStagger = GetFloat("LightDefenderStagger", 25.0f);

        powerAttackerStagger = GetFloat("PowerAttackerStagger", 0.0f);

        powerDefenderStagger = GetFloat("PowerDefenderStagger", 0.0f);

        superiorBlockPerk = GetString("SuperiorBlockPerk", "");

        // [Blocking] - stamina cost by weapon type.
        staminaOneHandSword = GetFloat("StaminaOneHandSword", 10.0f);
        staminaDagger = GetFloat("StaminaDagger", 6.0f);
        staminaOneHandAxe = GetFloat("StaminaOneHandAxe", 10.0f);
        staminaMace = GetFloat("StaminaMace", 12.0f);
        staminaTwoHandSword = GetFloat("StaminaTwoHandSword", 16.0f);
        staminaTwoHandAxe = GetFloat("StaminaTwoHandAxe", 18.0f);
        staminaBow = GetFloat("StaminaBow", 6.0f);
        staminaCrossbow = GetFloat("StaminaCrossbow", 8.0f);
        staminaStaff = GetFloat("StaminaStaff", 4.0f);
        staminaUnarmed = GetFloat("StaminaUnarmed", 2.0f);
        staminaOther = GetFloat("StaminaOther", 10.0f);

        // [Blocking] - diagnostic logging.
        debugLogging = GetFloat("DebugLogging", 0.0f) != 0.0f;

        // [InputTracking] - mouse, right-stick, and directional zones.
        // FHC owns input tracking; its values remain in the existing INI section.
        InputTrackingTuning::Values tuning{};
        tuning.mouseSensitivity = GetFloatInSection(kInputTrackingSection, "MouseSensitivity", tuning.mouseSensitivity);
        tuning.mouseDeadzone = GetFloatInSection(kInputTrackingSection, "MouseDeadzone", tuning.mouseDeadzone);
        tuning.controllerSensitivity =
            GetFloatInSection(kInputTrackingSection, "ControllerSensitivity", tuning.controllerSensitivity);
        tuning.controllerDeadzone =
            GetFloatInSection(kInputTrackingSection, "ControllerDeadzone", tuning.controllerDeadzone);
        tuning.controllerSmoothing =
            GetFloatInSection(kInputTrackingSection, "ControllerSmoothing", tuning.controllerSmoothing);
        tuning.topSectorDegrees = GetInputTrackingInt("TopSectorDegrees", tuning.topSectorDegrees);
        tuning.bottomNeutralAngleDegrees =
            GetInputTrackingInt("BottomNeutralAngleDegrees", tuning.bottomNeutralAngleDegrees);
        tuning.directionSwitchDelayMs = GetInputTrackingInt("DirectionSwitchDelayMs", tuning.directionSwitchDelayMs);
        tuning.neutralDecayMs = GetInputTrackingInt("NeutralDecayMs", tuning.neutralDecayMs);
        InputTrackingTuning::Set(tuning);

        // Player/NPC HUD settings are independent.
        LoadHudPlayer();
        LoadHudNpc();

        logger::info(
            "SETTINGS LOAD | "
            "EmpoweredBlockRequired={} | "
            "LightAttackReductionPerLevel={} | "
            "PowerAttackReductionPerLevel={} | "
            "LightAttackerStagger={} | "
            "LightDefenderStagger={} | "
            "PowerAttackerStagger={} | "
            "PowerDefenderStagger={} | "
            "DebugLogging={}",
            empoweredBlockRequired, lightAttackReductionPerLevel, powerAttackReductionPerLevel, lightAttackerStagger,
            lightDefenderStagger, powerAttackerStagger, powerDefenderStagger, debugLogging);
    }

    // ============================================================================
    // SAVE SETTINGS TO INI
    // Persist the current module values to ForHonorBlocking.ini.
    // ============================================================================
    void Save() {
        // [Blocking] - damage reduction, stagger, and superior block.
        WriteFloat("EmpoweredBlockRequired", empoweredBlockRequired ? 1.0f : 0.0f);

        WriteFloat("LightAttackReductionPerLevel", lightAttackReductionPerLevel);

        WriteFloat("PowerAttackReductionPerLevel", powerAttackReductionPerLevel);

        WriteFloat("LightAttackerStagger", lightAttackerStagger);

        WriteFloat("LightDefenderStagger", lightDefenderStagger);

        WriteFloat("PowerAttackerStagger", powerAttackerStagger);

        WriteFloat("PowerDefenderStagger", powerDefenderStagger);

        WriteString("SuperiorBlockPerk", superiorBlockPerk);

        // [Blocking] - stamina cost by weapon type.
        WriteFloat("StaminaOneHandSword", staminaOneHandSword);
        WriteFloat("StaminaDagger", staminaDagger);
        WriteFloat("StaminaOneHandAxe", staminaOneHandAxe);
        WriteFloat("StaminaMace", staminaMace);
        WriteFloat("StaminaTwoHandSword", staminaTwoHandSword);
        WriteFloat("StaminaTwoHandAxe", staminaTwoHandAxe);
        WriteFloat("StaminaBow", staminaBow);
        WriteFloat("StaminaCrossbow", staminaCrossbow);
        WriteFloat("StaminaStaff", staminaStaff);
        WriteFloat("StaminaUnarmed", staminaUnarmed);
        WriteFloat("StaminaOther", staminaOther);

        // [Blocking] - diagnostic logging.
        WriteFloat("DebugLogging", debugLogging ? 1.0f : 0.0f);

        // [InputTracking] - save all controls from the input-tracking module.
        const auto tuning = InputTrackingTuning::Get();
        WriteFloatInSection(kInputTrackingSection, "MouseSensitivity", tuning.mouseSensitivity);
        WriteFloatInSection(kInputTrackingSection, "MouseDeadzone", tuning.mouseDeadzone);
        WriteFloatInSection(kInputTrackingSection, "ControllerSensitivity", tuning.controllerSensitivity);
        WriteFloatInSection(kInputTrackingSection, "ControllerDeadzone", tuning.controllerDeadzone);
        WriteFloatInSection(kInputTrackingSection, "ControllerSmoothing", tuning.controllerSmoothing);
        WriteInputTrackingInt("TopSectorDegrees", tuning.topSectorDegrees);
        WriteInputTrackingInt("BottomNeutralAngleDegrees", tuning.bottomNeutralAngleDegrees);
        WriteInputTrackingInt("DirectionSwitchDelayMs", tuning.directionSwitchDelayMs);
        WriteInputTrackingInt("NeutralDecayMs", tuning.neutralDecayMs);

        SaveHudPlayer();
        SaveHudNpc();
    }

    // ============================================================================
    // RESTORE DEFAULT SETTINGS
    // Restores in-memory values; the caller decides when to save the INI.
    // ============================================================================
    void ResetToDefaults() {
        // Blocking defaults.
        empoweredBlockRequired = false;

        lightAttackReductionPerLevel = 1.0f;

        powerAttackReductionPerLevel = 0.35f;

        lightAttackerStagger = 25.0f;

        lightDefenderStagger = 25.0f;

        powerAttackerStagger = 0.0f;

        powerDefenderStagger = 0.0f;

        superiorBlockPerk.clear();

        // Weapon stamina defaults.
        staminaOneHandSword = 10.0f;
        staminaDagger = 6.0f;
        staminaOneHandAxe = 11.0f;
        staminaMace = 12.0f;
        staminaTwoHandSword = 16.0f;
        staminaTwoHandAxe = 18.0f;
        staminaBow = 6.0f;
        staminaCrossbow = 8.0f;
        staminaStaff = 4.0f;
        staminaUnarmed = 5.0f;
        staminaOther = 10.0f;

        debugLogging = false;

        // Directional input defaults; provided by InputTracking.h.
        InputTrackingTuning::Reset();

        PlayerUI = PlayerUISettings{};
        NPCUI = NPCUISettings{};
    }

}
