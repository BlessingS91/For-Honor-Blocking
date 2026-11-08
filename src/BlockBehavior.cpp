
#include "BlockBehavior.h"

#include <cstdint>

#include "logger.h"

namespace {

    void CancelBlocking(RE::PlayerCharacter* player, const char* source) {
        if (!player) {
            return;
        }

        auto* state = player->AsActorState();

        if (!state) {
            return;
        }

        const bool wasBlocking = player->IsBlocking();
        const bool wantedBlocking = state->actorState2.wantBlocking;

        if (!wasBlocking && !wantedBlocking) {
            return;
        }

        // Stop blocking before allowing a light attack.
        const bool stopped = player->NotifyAnimationGraph("blockStop");

        player->SetGraphVariableBool("IsBlocking", false);
        player->SetGraphVariableBool("IsBashing", false);
        player->SetGraphVariableInt("iWantBlock", 0);

        state->actorState2.wantBlocking = false;

        logger::info("[BLOCK BEHAVIOR] {} | blockStop={} blocking={} wantBlocking={}", source, stopped,
                     player->IsBlocking(), static_cast<std::uint32_t>(state->actorState2.wantBlocking));
    }

    bool IsDualWielding(RE::PlayerCharacter* player) {
        if (auto leftHand = player->GetEquippedObject(true)) {
            if (auto leftWeapon = leftHand->As<RE::TESObjectWEAP>()) {
                if ((int)leftWeapon->GetWeaponType() > 0 && (int)leftWeapon->GetWeaponType() < 5) {
                    return true;
                }
            }
        }

        return !player->GetEquippedObject(false) && !player->GetEquippedObject(true);
    }
}

MCO* MCO::GetSingleton() {
    static MCO singleton;
    return &singleton;
}

void MCO::InstallHooks() { Hooks::Install(); }

void MCO::RegisterAnimationSink() {
    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!player) {
        return;
    }

    if (player->AddAnimationGraphEventSink(this)) {
        logger::info("[BLOCK BEHAVIOR] Player animation sink registered");
    } else {
        logger::warn("[BLOCK BEHAVIOR] Player animation sink registration failed");
    }
}

RE::BSEventNotifyControl MCO::ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                           RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!a_event || a_event->holder != RE::PlayerCharacter::GetSingleton()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // Keep automatic attack-to-block transitions disabled.
    // Preserves MCO attack movement and combos.
    return RE::BSEventNotifyControl::kContinue;
}

void MCO::ProcessBlock(std::string_view a_eventTag) {
    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!player) {
        return;
    }

    auto* state = player->AsActorState();

    if (!state) {
        return;
    }

    bool MCO_IsInRecovery = false;

    player->GetGraphVariableBool("MCO_IsInRecovery", MCO_IsInRecovery);

    const bool attackWindowClosed = a_eventTag == "MCO_WinClose" || a_eventTag == "AttackWinEnd";

    const bool wantBlocking = state->actorState2.wantBlocking;

    if (!MCO_IsInRecovery && !(attackWindowClosed && wantBlocking)) {
        return;
    }

    if (IsDualWielding(player)) {
        if (MCO_IsInRecovery || wantBlocking) {
            player->NotifyAnimationGraph("MCO_EndAnimation");
        }
    } else if (wantBlocking) {
        player->NotifyAnimationGraph("MCO_EndAnimation");
        player->NotifyAnimationGraph("blockStart");
    }
}

void MCO::Hooks::AttackBlockHandler_ProcessButton::thunk(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event,
                                                         RE::PlayerControlsData* a_data) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* userEvents = RE::UserEvents::GetSingleton();

    if (!player || !userEvents || !a_event) {
        func(a_this, a_event, a_data);
        return;
    }

    const bool rightAttack = a_event->QUserEvent() == userEvents->rightAttack;

    // Preserve light attacks from block.
    if (rightAttack && a_event->IsDown()) {
        CancelBlocking(player, "Light attack block cancellation");
    }

    // Let Skyrim process the attack normally.
    func(a_this, a_event, a_data);

    // Keep disabled: interrupted MCO combos/root motion.
    // MCO::GetSingleton()->ProcessBlock();
}

void MCO::Hooks::Install() {
    REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_AttackBlockHandler[0]};

    const auto index = REL::Module::IsVR() || REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_7_99 ? 0x6 : 0x4;

    AttackBlockHandler_ProcessButton::func = vtable.write_vfunc(index, AttackBlockHandler_ProcessButton::thunk);

    logger::info("[BLOCK BEHAVIOR] AttackBlockHandler hook installed");
}
