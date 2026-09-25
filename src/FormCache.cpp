#include "FormCache.h"

#include "logger.h"

namespace FormCache {
    void LoadForms() {
        logger::info("Loading For Honor Blocking forms...");

        // Load Explosions
        sekiroSparks = RE::TESForm::LookupByEditorID<RE::BGSExplosion>("HBLOCK_SekiroSSparks");
        sekiroSparksPhysics = RE::TESForm::LookupByEditorID<RE::BGSExplosion>("HBLOCK_SekiroSSparksPhysics");
        sekiroFlash = RE::TESForm::LookupByEditorID<RE::BGSExplosion>("HBLOCK_SekiroSFlash");
        sekiroFlashShield = RE::TESForm::LookupByEditorID<RE::BGSExplosion>("HBLOCK_SekiroSFlashShield");

        // Load Spells
        blockedPowerAttacker = RE::TESForm::LookupByEditorID<RE::MagicItem>("HBLOCK_BlockedPowerAttacker");
        blockedNormalAttacker = RE::TESForm::LookupByEditorID<RE::MagicItem>("HBLOCK_BlockedNormalAttacker");
        blockedPowerDefender = RE::TESForm::LookupByEditorID<RE::MagicItem>("HBLOCK_BlockedPowerDefender");
        blockedNormalDefender = RE::TESForm::LookupByEditorID<RE::MagicItem>("HBLOCK_BlockedNormalDefender");
        blockEffects = RE::TESForm::LookupByEditorID<RE::MagicItem>("HBLOCK_Effects");

        // Validation logging
        bool allLoaded = true;

        auto checkForm = [](const char* name, void* ptr) {
            if (!ptr) {
                logger::error("FormCache: Failed to find form with EditorID: {}", name);
                return false;
            }
            return true;
        };

        allLoaded &= checkForm("HBLOCK_SekiroSSparks", sekiroSparks);
        allLoaded &= checkForm("HBLOCK_SekiroSSparksPhysics", sekiroSparksPhysics);
        allLoaded &= checkForm("HBLOCK_SekiroSFlash", sekiroFlash);
        allLoaded &= checkForm("HBLOCK_SekiroSFlashShield", sekiroFlashShield);
        allLoaded &= checkForm("HBLOCK_BlockedPowerAttacker", blockedPowerAttacker);
        allLoaded &= checkForm("HBLOCK_BlockedNormalAttacker", blockedNormalAttacker);
        allLoaded &= checkForm("HBLOCK_BlockedPowerDefender", blockedPowerDefender);
        allLoaded &= checkForm("HBLOCK_BlockedNormalDefender", blockedNormalDefender);
        allLoaded &= checkForm("HBLOCK_Effects", blockEffects);

        if (allLoaded) {
            logger::info("All For Honor Blocking forms loaded successfully!");
        } else {
            logger::warn("Some For Honor Blocking forms failed to load. Check your plugin load order.");
        }
    }
}