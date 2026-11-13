#include "Hooks.h"

#include <cstdint>

#include "AttackPosition.h"
#include "InputTracking.h"
#include "PowerAttackKey.h"
#include "Prisma.h"
#include "TDMLockBridge.h"
#include "logger.h"

// PowerAttackKey owns its queued attack transition logic. The shared update
// hook only dispatches to it and the other independent systems.
namespace PowerAttackKey {
    void OnPlayerUpdate(RE::PlayerCharacter* player);
}

namespace Hooks {
    namespace {
        // Skyrim AE 1.6.1170: PlayerCharacter::Update vtable slot 0xAD.
        struct PlayerUpdateHook {
            using Update_t = void (*)(RE::PlayerCharacter*, float);

            static void thunk(RE::PlayerCharacter* player, float delta) {
                func(player, delta);

                // Preserve the original update order exactly.
                PowerAttackKey::OnPlayerUpdate(player);
                TDMLockBridge::Update();
                DirectionTracker::GetSingleton()->UpdateDecay();
                Prisma::UpdatePlayerHUD();
                AttackPosition::GetSingleton()->UpdateHUD();
            }

            static inline Update_t func = nullptr;
        };
    }

    void Install() {
        static bool installed = false;
        if (installed) {
            return;
        }

        REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_PlayerCharacter[0]};
        PlayerUpdateHook::func = reinterpret_cast<PlayerUpdateHook::Update_t>(
            vtable.write_vfunc(0xAD, PlayerUpdateHook::thunk));

        installed = true;
        logger::info("[For Honor Combat] Shared PlayerCharacter::Update hook installed");
    }
}
