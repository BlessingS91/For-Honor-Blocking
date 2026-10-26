#pragma once

#include <string>

namespace Settings {

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

    // Block Stamina
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

    void Load();

    void Save();

    void ResetToDefaults();

}