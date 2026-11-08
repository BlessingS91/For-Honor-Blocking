#pragma once

#include "PrecisionAPI.h"

class WeaponClash {
public:
    static void Install(SKSE::PluginHandle pluginHandle, PRECISION_API::IVPrecision2* precisionAPI);

private:
    static PRECISION_API::WeaponCollisionCallbackReturn OnWeaponCollision(
        const PRECISION_API::PrecisionHitData& hitData);
};