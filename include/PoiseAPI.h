#pragma once

#include <Windows.h>

#include <cstdint>

#include "RE/Skyrim.h"

using PoiseDamageCallback_t = float (*)(RE::Actor* attacker, RE::Actor* target, float damage);

using Poise_GetArmorReducedStagger_t = float (*)(uint32_t formID, float stagger);
using Poise_GetEffectiveMagicResistance_t = float (*)(RE::Actor* target);
using Poise_GetHandDamage_t = float (*)(uint32_t formID, bool leftHand);
using Poise_RegisterDamageCallback_t = bool (*)(PoiseDamageCallback_t callback);
using Poise_UnregisterDamageCallback_t = void (*)(PoiseDamageCallback_t callback);

namespace PoiseAPI {
    inline HMODULE g_module = nullptr;

    inline Poise_GetArmorReducedStagger_t GetArmorReducedStagger = nullptr;
    inline Poise_GetEffectiveMagicResistance_t GetEffectiveMagicResistance = nullptr;
    inline Poise_GetHandDamage_t GetHandDamage = nullptr;
    inline Poise_RegisterDamageCallback_t RegisterDamageCallback = nullptr;
    inline Poise_UnregisterDamageCallback_t UnregisterDamageCallback = nullptr;

    inline bool Load() {
        if (g_module) {
            return RegisterDamageCallback != nullptr;
        }

        g_module = GetModuleHandleW(L"ChocolatePoiseReforged.dll");

        if (!g_module) {
            return false;
        }

        GetArmorReducedStagger =
            reinterpret_cast<Poise_GetArmorReducedStagger_t>(GetProcAddress(g_module, "Poise_GetArmorReducedStagger"));

        GetEffectiveMagicResistance = reinterpret_cast<Poise_GetEffectiveMagicResistance_t>(
            GetProcAddress(g_module, "Poise_GetEffectiveMagicResistance"));

        GetHandDamage = reinterpret_cast<Poise_GetHandDamage_t>(GetProcAddress(g_module, "Poise_GetHandDamage"));

        RegisterDamageCallback =
            reinterpret_cast<Poise_RegisterDamageCallback_t>(GetProcAddress(g_module, "Poise_RegisterDamageCallback"));

        UnregisterDamageCallback = reinterpret_cast<Poise_UnregisterDamageCallback_t>(
            GetProcAddress(g_module, "Poise_UnregisterDamageCallback"));

        return RegisterDamageCallback != nullptr;
    }

    inline bool IsLoaded() { return g_module != nullptr && RegisterDamageCallback != nullptr; }
}