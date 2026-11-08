
#include "Hooks.h"

namespace Hooks {
    namespace {
        bool interceptingAttack = false;

        bool ProcessButton(RE::ButtonEvent* a_button) {
            if (!a_button) {
                return false;
            }

            auto* userEvents = RE::UserEvents::GetSingleton();
            auto* player = RE::PlayerCharacter::GetSingleton();

            if (!userEvents || !player) {
                return false;
            }

            const auto& control = a_button->QUserEvent();

            if (control != userEvents->rightAttack) {
                return false;
            }

            if (a_button->IsDown() && player->IsBlocking()) {
                const bool setBlocking = player->SetGraphVariableBool("IsBlocking", false);

                const bool setBashing = player->SetGraphVariableBool("IsBashing", false);

                const bool setWantBlock = player->SetGraphVariableInt("iWantBlock", 0);

                const bool blockStopped = player->NotifyAnimationGraph("blockStop");

                logger::info(
                    "[BashAttackTest] Block cancelled | "
                    "IsBlocking={} IsBashing={} iWantBlock={} "
                    "blockStop={} engineBlocking={}",
                    setBlocking, setBashing, setWantBlock, blockStopped, player->IsBlocking());
            }

            // Never suppress the input.
            // Skyrim receives the original press, hold, and release.
            return false;
        }
    }

    void InputHook::thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events) {
        if (!a_dispatcher || !a_events) {
            return;
        }

        RE::InputEvent* first = nullptr;
        RE::InputEvent* last = nullptr;

        for (auto* event = *a_events; event;) {
            auto* next = event->next;

            bool suppress = false;

            if (auto* button = event->AsButtonEvent()) {
                suppress = ProcessButton(button);
            }

            if (!suppress) {
                if (!first) {
                    first = event;
                } else {
                    last->next = event;
                }

                last = event;
            }

            event = next;
        }

        if (last) {
            last->next = nullptr;
        }

        RE::InputEvent* const forwarded[] = {first};
        func(a_dispatcher, forwarded);
    }

    void Install() {
        auto& trampoline = SKSE::GetTrampoline();
        trampoline.create(14);

        // Original No Loot While Armed call site.
        const REL::Relocation<std::uintptr_t> target{REL::RelocationID(67315, 68617)};

        InputHook::func = trampoline.write_call<5>(target.address() + 0x7B, InputHook::thunk);

        logger::info("[BashAttackTest] Bash-to-attack test hook installed");
    }
}
