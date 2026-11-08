
#include "BlockBehavior.h"

#include <atomic>
#include <cstdint>
#include <string_view>

#include "logger.h"

namespace {
    std::atomic_bool g_guardResumeDispatched{false};
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
    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!a_event || !player || a_event->holder != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const std::string_view tag = a_event->tag.c_str();

    if (tag == "attackStart" || tag == "MCO_PowerAttackInitiate" || tag == "MCO_WinOpen" || tag == "MCO_PowerWinOpen") {
        g_guardResumeDispatched.store(false);
    }

    if (tag == "MCO_WinClose" || tag == "MCO_PowerWinClose") {
        // ProcessBlock(tag);
    }

    return RE::BSEventNotifyControl::kContinue;
}

void MCO::ProcessBlock(std::string_view a_eventTag) {
    auto* player = RE::PlayerCharacter::GetSingleton();

    if (!player) {
        return;
    }

    std::int32_t iWantBlock = 0;

    if (!player->GetGraphVariableInt("iWantBlock", iWantBlock)) {
        return;
    }

    if (iWantBlock != 1) {
        return;
    }

    if (g_guardResumeDispatched.exchange(true)) {
        return;
    }

    const bool ended = player->NotifyAnimationGraph("MCO_EndAnimation");

    const bool guarded = player->NotifyAnimationGraph("blockStart");

    logger::info("[BLOCK BEHAVIOR] Guard resume | event={} iWantBlock={} end={} blockStart={}", a_eventTag, iWantBlock,
                 ended, guarded);
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

    if (rightAttack && a_event->IsDown() && player->IsBlocking()) {
        const bool stopped = player->NotifyAnimationGraph("blockStop");

        logger::info("[BLOCK BEHAVIOR] Light attack | blockStop={} blocking={}", stopped, player->IsBlocking());
    }

    func(a_this, a_event, a_data);
}

void MCO::Hooks::Install() {
    REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_AttackBlockHandler[0]};

    const auto index = REL::Module::IsVR() || REL::Module::get().version() >= SKSE::RUNTIME_SSE_1_7_99 ? 0x6 : 0x4;

    AttackBlockHandler_ProcessButton::func = vtable.write_vfunc(index, AttackBlockHandler_ProcessButton::thunk);

    logger::info("[BLOCK BEHAVIOR] AttackBlockHandler hook installed");
}
