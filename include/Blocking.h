#pragma once

#include "PrecisionAPI.h"

namespace Blocking {
    void Install(SKSE::PluginHandle pluginHandle, PRECISION_API::IVPrecision1* precisionAPI);

    float OnPoiseDamage(RE::Actor* attacker, RE::Actor* target, float damage);
}