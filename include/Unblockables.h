#pragma once

#include <optional>

namespace RE { class Actor; }

namespace Unblockables {
    // Reads the existing UnblockableFHB animation graph flag.
    bool IsUnblockable(RE::Actor* attacker);

    // Precision PreHit: for an unblockable, set defender reaction to 1 (blocking)
    // or 2 (unblocked) for 200 ms. Returns true when the hit is unblockable.
    bool OnPrecisionPreHit(RE::Actor* attacker, RE::Actor* defender);

    // Existing nonplayer creature weaponSwing detection (race keywords + ATKD).
    // nullopt: leave state alone (not a qualifying attack / no ATKD data).
    // true: ActivateWindow(UnblockableFHB); false: set UnblockableFHB = 0.
    // Outputs the original ATKD damage multiplier for existing debug logging.
    std::optional<bool> GetCreatureSwingUnblockable(RE::Actor* actor, float& damageMult);
}
