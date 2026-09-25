#include "Gameplay.h"

#include <algorithm>

#include "Settings.h"

namespace BlockingGameplay {

    void ApplyLargeRecoil(RE::Actor* aggressor, RE::Actor* defender) {
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

    void ApplyNormalRecoil(RE::Actor* aggressor, RE::Actor* defender) {
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

    bool HasSuperiorBlockPerk(RE::Actor* defender) {
        if (!defender) {
            return false;
        }

        const auto perk = RE::TESForm::LookupByEditorID<RE::BGSPerk>(Settings::superiorBlockPerk.c_str());

        if (!perk) {
            return false;
        }

        return defender->HasPerk(perk);
    }

    float GetBlockDamageMultiplier(RE::Actor* defender, float reductionPerLevel) {
        if (!defender) {
            return 1.0f;
        }

        const float blockLevel = std::clamp(defender->GetActorValue(RE::ActorValue::kBlock), 0.0f, 100.0f);

        const float totalReduction = std::clamp((blockLevel * reductionPerLevel) / 100.0f, 0.0f, 1.0f);

        return 1.0f - totalReduction;
    }

    float GetLightAttackDamageMultiplier(RE::Actor* defender) {
        return GetBlockDamageMultiplier(defender, Settings::lightAttackReductionPerLevel);
    }

    float GetPowerAttackDamageMultiplier(RE::Actor* defender) {
        return GetBlockDamageMultiplier(defender, Settings::powerAttackReductionPerLevel);
    }

    void SpawnLightBlockEffects(RE::Actor* defender) {
        if (!defender) {
            return;
        }

        constexpr const char* effects[] = {"HBLOCK_SekiroSSparks", "HBLOCK_SekiroSSparksPhysics", "HBLOCK_SekiroSFlash",
                                           "HBLOCK_SekiroSFlashShield"};

        for (const auto* editorID : effects) {
            const auto effect = RE::TESForm::LookupByEditorID<RE::BGSExplosion>(editorID);

            if (!effect) {
                if (Settings::debugLogging) {
                    logger::warn(
                        "PRECISION BLOCK | Explosion not found | "
                        "Actor={} | Explosion={}",
                        defender->GetName(), editorID);
                }
                continue;
            }

            defender->PlaceObjectAtMe(effect, false);

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | Explosion spawned | "
                    "Actor={} | Explosion={}",
                    defender->GetName(), editorID);
            }
        }
    }

    void SpawnHeavyBlockEffects(RE::Actor* defender) {
        if (!defender) {
            return;
        }

        constexpr const char* effects[] = {"HBLOCK_SekiroSFlash", "HBLOCK_SekiroSFlashShield"};

        for (const auto* editorID : effects) {
            const auto effect = RE::TESForm::LookupByEditorID<RE::BGSExplosion>(editorID);

            if (!effect) {
                if (Settings::debugLogging) {
                    logger::warn(
                        "PRECISION BLOCK | Explosion not found | "
                        "Actor={} | Explosion={}",
                        defender->GetName(), editorID);
                }
                continue;
            }

            defender->PlaceObjectAtMe(effect, false);

            if (Settings::debugLogging) {
                logger::info(
                    "PRECISION BLOCK | Explosion spawned | "
                    "Actor={} | Explosion={}",
                    defender->GetName(), editorID);
            }
        }
    }

    void ApplyBlockEffects(RE::Actor* defender, bool* powerAttack) {
        if (!defender || !powerAttack) {
            return;
        }

        if (*powerAttack || !HasSuperiorBlockPerk(defender)) {
            SpawnHeavyBlockEffects(defender);
        } else {
            SpawnLightBlockEffects(defender);
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
    void ApplyBlockSpells(RE::Actor* aggressor, RE::Actor* defender, bool* powerAttack) {
        if (!aggressor || !defender || !powerAttack) {
            return;
        }

        if (*powerAttack) {
            Cast_Spell(aggressor, "HBLOCK_BlockedPowerAttacker", Settings::powerAttackerStagger);
            Cast_Spell(defender, "HBLOCK_BlockedPowerDefender", Settings::powerDefenderStagger);
        } else {
            Cast_Spell(aggressor, "HBLOCK_BlockedNormalAttacker", Settings::lightAttackerStagger);
            Cast_Spell(defender, "HBLOCK_BlockedNormalDefender", Settings::lightDefenderStagger);
        }

        Cast_Spell(defender, "HBLOCK_Effects", *powerAttack ? 0.50f : 0.25f);
        ApplyBlockEffects(defender, powerAttack);
    }
}