#pragma once

#include <string>

namespace Settings {

    extern float lightAttackReductionPerLevel;

    extern float powerAttackReductionPerLevel;

    extern float lightAttackerStagger;

    extern float lightDefenderStagger;

    extern float powerAttackerStagger;

    extern float powerDefenderStagger;

    extern std::string superiorBlockPerk;

    extern bool debugLogging;

    void Load();

    void Save();

    void ResetToDefaults();

}