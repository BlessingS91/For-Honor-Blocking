#include "Settings.h"

#include <Windows.h>

#include <string>

#include "logger.h"

namespace Settings {

    float blockSkillLevel = 0.0f;

    bool hasEmpoweredBlock = false;

    bool empoweredBlockRequired = false;

    float lightAttackReductionPerLevel = 1.0f;

    float powerAttackReductionPerLevel = 0.35f;

    float lightAttackerStagger = 25.0f;

    float lightDefenderStagger = 25.0f;

    float powerAttackerStagger = 0.0f;

    float powerDefenderStagger = 0.0f;

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

    std::string superiorBlockPerk;

    bool debugLogging = false;

    namespace {

        constexpr const char* kSection = "Blocking";
        constexpr const char* kConfigPath = "Data/SKSE/Plugins/ForHonorBlocking.ini";

        float GetFloat(const char* key, float defaultValue) {
            char buffer[64]{};

            GetPrivateProfileStringA(kSection, key, "", buffer, sizeof(buffer), kConfigPath);

            if (buffer[0] == '\0') {
                return defaultValue;
            }

            try {
                return std::stof(buffer);
            } catch (...) {
                return defaultValue;
            }
        }

        void WriteFloat(const char* key, float value) {
            const std::string valueString = std::to_string(value);

            WritePrivateProfileStringA(kSection, key, valueString.c_str(), kConfigPath);
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

    void Load() {
        empoweredBlockRequired = GetFloat("EmpoweredBlockRequired", 0.0f) != 0.0f;

        lightAttackReductionPerLevel = GetFloat("LightAttackReductionPerLevel", 1.0f);

        powerAttackReductionPerLevel = GetFloat("PowerAttackReductionPerLevel", 0.35f);

        lightAttackerStagger = GetFloat("LightAttackerStagger", 25.0f);

        lightDefenderStagger = GetFloat("LightDefenderStagger", 25.0f);

        powerAttackerStagger = GetFloat("PowerAttackerStagger", 0.0f);

        powerDefenderStagger = GetFloat("PowerDefenderStagger", 0.0f);

        superiorBlockPerk = GetString("SuperiorBlockPerk", "");

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

        debugLogging = GetFloat("DebugLogging", 0.0f) != 0.0f;

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

    void Save() {
        WriteFloat("EmpoweredBlockRequired", empoweredBlockRequired ? 1.0f : 0.0f);

        WriteFloat("LightAttackReductionPerLevel", lightAttackReductionPerLevel);

        WriteFloat("PowerAttackReductionPerLevel", powerAttackReductionPerLevel);

        WriteFloat("LightAttackerStagger", lightAttackerStagger);

        WriteFloat("LightDefenderStagger", lightDefenderStagger);

        WriteFloat("PowerAttackerStagger", powerAttackerStagger);

        WriteFloat("PowerDefenderStagger", powerDefenderStagger);

        WriteString("SuperiorBlockPerk", superiorBlockPerk);

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

        WriteFloat("DebugLogging", debugLogging ? 1.0f : 0.0f);
    }

    void ResetToDefaults() {
        empoweredBlockRequired = false;

        lightAttackReductionPerLevel = 1.0f;

        powerAttackReductionPerLevel = 0.35f;

        lightAttackerStagger = 25.0f;

        lightDefenderStagger = 25.0f;

        powerAttackerStagger = 0.0f;

        powerDefenderStagger = 0.0f;

        superiorBlockPerk.clear();

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
    }

}