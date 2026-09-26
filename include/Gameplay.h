#pragma once

#include "RE/A/Actor.h"

namespace Gameplay {

    void ApplyNormalStagger(RE::Actor* aggressor, RE::Actor* defender);

    void ApplyLargeStagger(RE::Actor* aggressor, RE::Actor* defender);

}