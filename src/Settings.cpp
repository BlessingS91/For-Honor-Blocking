#include "Settings.h"

#include <Windows.h>

#include <string>

#include "logger.h"

namespace Settings {

    float blockSkillLevel = 0.0f;
    bool hasEmpoweredBlock = false;

    float lightAttackReductionPerLevel = 1.0f;

    float powerAttackReductionPerLevel = 0.35f;

    float lightAttackerStagger = 25.0f;

    float lightDefenderStagger = 25.0f;

    float powerAttackerStagger = 0.0f;

    float powerDefenderStagger = 0.0f;

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
        lightAttackReductionPerLevel = GetFloat("LightAttackReductionPerLevel", 1.0f);

        powerAttackReductionPerLevel = GetFloat("PowerAttackReductionPerLevel", 0.35f);

        lightAttackerStagger = GetFloat("LightAttackerStagger", 25.0f);

        lightDefenderStagger = GetFloat("LightDefenderStagger", 25.0f);

        powerAttackerStagger = GetFloat("PowerAttackerStagger", 0.0f);

        powerDefenderStagger = GetFloat("PowerDefenderStagger", 0.0f);

        superiorBlockPerk = GetString("SuperiorBlockPerk", "");

        debugLogging = GetFloat("DebugLogging", 0.0f) != 0.0f;

        logger::info(

            "SETTINGS LOAD | "

            "LightAttackReductionPerLevel={} | "

            "PowerAttackReductionPerLevel={} | "

            "LightAttackerStagger={} | "

            "LightDefenderStagger={} | "

            "PowerAttackerStagger={} | "

            "PowerDefenderStagger={} | "

            "DebugLogging={}",

            lightAttackReductionPerLevel, lightAttackerStagger, powerAttackReductionPerLevel, lightDefenderStagger,

            powerAttackerStagger, powerDefenderStagger, debugLogging);
    }

    void Save() {
        WriteFloat("LightAttackReductionPerLevel", lightAttackReductionPerLevel);

        WriteFloat("PowerAttackReductionPerLevel", powerAttackReductionPerLevel);

        WriteFloat("LightAttackerStagger", lightAttackerStagger);

        WriteFloat("LightDefenderStagger", lightDefenderStagger);

        WriteFloat("PowerAttackerStagger", powerAttackerStagger);

        WriteFloat("PowerDefenderStagger", powerDefenderStagger);

        WriteString("SuperiorBlockPerk", superiorBlockPerk);

        WriteFloat("DebugLogging", debugLogging ? 1.0f : 0.0f);
    }

    void ResetToDefaults() {
        lightAttackReductionPerLevel = 1.0f;

        powerAttackReductionPerLevel = 0.35f;

        lightAttackerStagger = 25.0f;

        lightDefenderStagger = 25.0f;

        powerAttackerStagger = 0.0f;

        powerDefenderStagger = 0.0f;

        superiorBlockPerk.clear();

        debugLogging = false;
    }

}