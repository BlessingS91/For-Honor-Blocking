#include "Gameplay.h"

#include "RE/B/BGSExplosion.h"
#include "RE/T/TESForm.h"
#include "Settings.h"
#include "logger.h"

namespace Gameplay {

    void ApplyNormalStagger(RE::Actor* aggressor, RE::Actor* defender) {
        if (!aggressor || !defender) {
            return;
        }

        aggressor->NotifyAnimationGraph("MCO_Recovery");
        aggressor->NotifyAnimationGraph("staggerStop");
        aggressor->NotifyAnimationGraph("recoilStop");
        aggressor->NotifyAnimationGraph("MCO_EndAnimation");
        aggressor->NotifyAnimationGraph("attackStop");
        aggressor->NotifyAnimationGraph("recoilStart");

        defender->NotifyAnimationGraph("blockStop");
        defender->NotifyAnimationGraph("Maxsu_BlockHitWinOpen");
    }

    void ApplyLargeStagger(RE::Actor* aggressor, RE::Actor* defender) {
        if (!aggressor || !defender) {
            return;
        }

        aggressor->NotifyAnimationGraph("MCO_Recovery");
        aggressor->NotifyAnimationGraph("staggerStop");
        aggressor->NotifyAnimationGraph("recoilStop");
        aggressor->NotifyAnimationGraph("MCO_EndAnimation");
        aggressor->NotifyAnimationGraph("attackStop");
        aggressor->NotifyAnimationGraph("recoilLargeStart");

        defender->NotifyAnimationGraph("blockStop");
        defender->NotifyAnimationGraph("Maxsu_BlockHitWinOpen");
    }

    void SpawnBlockEffects(RE::Actor* defender, bool powerAttack, bool largeRecoil) {
        if (!defender) {
            return;
        }

        constexpr const char* lightEffects[] = {"HBLOCK_SekiroSSparks", "HBLOCK_SekiroSSparksPhysics",
                                                "HBLOCK_SekiroSFlash", "HBLOCK_SekiroSFlashShield"};

        constexpr const char* heavyEffects[] = {"HBLOCK_SekiroSFlash", "HBLOCK_SekiroSFlashShield"};

        const bool useHeavyEffects = powerAttack || !largeRecoil;

        const char* const* effects = useHeavyEffects ? heavyEffects : lightEffects;

        const std::size_t effectCount = useHeavyEffects ? std::size(heavyEffects) : std::size(lightEffects);

        for (std::size_t i = 0; i < effectCount; ++i) {
            const auto* editorID = effects[i];

            const auto effect = RE::TESForm::LookupByEditorID<RE::BGSExplosion>(editorID);

            if (!effect) {
                if (Settings::debugLogging) {
                    logger::warn(
                        "BLOCK EFFECTS | Explosion not found | "
                        "Actor={} | Explosion={}",
                        defender->GetName(), editorID);
                }

                continue;
            }

            defender->PlaceObjectAtMe(effect, false);

            if (Settings::debugLogging) {
                logger::info(
                    "BLOCK EFFECTS | Explosion spawned | "
                    "Actor={} | Explosion={}",
                    defender->GetName(), editorID);
            }
        }
    }

    void Cast_Spell(RE::Actor* a_actor, const char* a_spell, float a_mag) {
        if (!a_actor) {
            return;
        }

        const auto e_spell = RE::TESForm::LookupByEditorID<RE::MagicItem>(a_spell);

        if (!e_spell) {
            if (Settings::debugLogging) {
                logger::warn(
                    "PRECISION BLOCK | Spell not found | "
                    "Actor={} | Spell={} | Magnitude={}",
                    a_actor->GetName(), a_spell, a_mag);
            }

            return;
        }

        const auto caster = a_actor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);

        if (!caster) {
            if (Settings::debugLogging) {
                logger::warn(
                    "PRECISION BLOCK | Magic caster unavailable | "
                    "Actor={} | Spell={} | Magnitude={}",
                    a_actor->GetName(), a_spell, a_mag);
            }

            return;
        }

        caster->CastSpellImmediate(e_spell, false, a_actor, 1, false, a_mag, a_actor);

        if (Settings::debugLogging) {
            logger::info(
                "PRECISION BLOCK | Spell cast | "
                "Actor={} | Spell={} | Magnitude={}",
                a_actor->GetName(), a_spell, a_mag);
        }
    }

    void ApplyBlockSpells(RE::Actor* aggressor, RE::Actor* defender, bool powerAttack) {
        if (!aggressor || !defender) {
            return;
        }

        if (powerAttack) {
            Cast_Spell(aggressor, "HBLOCK_BlockedPowerAttacker", Settings::powerAttackerStagger);

            Cast_Spell(defender, "HBLOCK_BlockedPowerDefender", Settings::powerDefenderStagger);
        } else {
            Cast_Spell(aggressor, "HBLOCK_BlockedNormalAttacker", Settings::lightAttackerStagger);

            Cast_Spell(defender, "HBLOCK_BlockedNormalDefender", Settings::lightDefenderStagger);
        }

        Cast_Spell(defender, "HBLOCK_Effects", powerAttack ? 0.50f : 0.50f);

        const bool largeRecoil = !powerAttack && (!Settings::empoweredBlockRequired || Settings::hasEmpoweredBlock);

        SpawnBlockEffects(defender, powerAttack, largeRecoil);
    }
}