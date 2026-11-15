#pragma once

#include <atomic>

// Shared switch for player and NPC attack-direction sampling.
// Uses an atomic because the menu and gameplay may run on different threads.
namespace AttackTrackingMode {
    inline std::atomic_bool useWeaponTip{false};

    // Top requires BOTH vertical clearance above the player's head and
    // horizontal proximity to the player's chest centerline. Values are game units.
    // A negative height offset allows attacks slightly below the head.
    inline std::atomic<float> topHeadHeightOffset{0.0f};
    inline std::atomic<float> topCenterHalfWidth{15.0f};
}
