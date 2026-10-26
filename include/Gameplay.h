#pragma once

#include "RE/A/Actor.h"

namespace Gameplay {

    void ApplyNormalStagger(RE::Actor* aggressor, RE::Actor* defender);

    void ApplyLargeStagger(RE::Actor* aggressor, RE::Actor* defender);

    void SpawnBlockEffects(RE::Actor* defender, bool powerAttack, bool largeRecoil);

    void ApplyBlockSpells(RE::Actor* aggressor, RE::Actor* defender, bool powerAttack);

}