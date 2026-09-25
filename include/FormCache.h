#pragma once

namespace FormCache {
    // Explosions
    inline RE::BGSExplosion* sekiroSparks = nullptr;
    inline RE::BGSExplosion* sekiroSparksPhysics = nullptr;
    inline RE::BGSExplosion* sekiroFlash = nullptr;
    inline RE::BGSExplosion* sekiroFlashShield = nullptr;

    // Spells / Magic Items
    inline RE::MagicItem* blockedPowerAttacker = nullptr;
    inline RE::MagicItem* blockedNormalAttacker = nullptr;
    inline RE::MagicItem* blockedPowerDefender = nullptr;
    inline RE::MagicItem* blockedNormalDefender = nullptr;
    inline RE::MagicItem* blockEffects = nullptr;

    // Load function to call during plugin initialization
    void LoadForms();
}