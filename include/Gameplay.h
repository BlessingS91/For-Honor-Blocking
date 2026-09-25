#pragma once

#include "RE/Skyrim.h"

namespace BlockingGameplay {

    void ApplyLargeRecoil(RE::Actor* aggressor, RE::Actor* defender);

    void ApplyNormalRecoil(RE::Actor* aggressor, RE::Actor* defender);

    bool HasSuperiorBlockPerk(RE::Actor* defender);

    float GetBlockDamageMultiplier(RE::Actor* defender, float reductionPerLevel);

    float GetLightAttackDamageMultiplier(RE::Actor* defender);

    float GetPowerAttackDamageMultiplier(RE::Actor* defender);

    void SpawnLightBlockEffects(RE::Actor* defender);

    void SpawnHeavyBlockEffects(RE::Actor* defender);

    void ApplyBlockEffects(RE::Actor* defender, bool* powerAttack);

    void Cast_Spell(RE::Actor* a_actor, const char* a_spell, float a_mag);

    void ApplyBlockSpells(RE::Actor* aggressor, RE::Actor* defender, bool* powerAttack);

}