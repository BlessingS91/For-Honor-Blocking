#include "Hooks.h"

#include "RE/U/UI.h"
#include "SKSE/SKSE.h"
#include "Settings.h"
#include "logger.h"

namespace Hooks {

    MenuOpenCloseHandler* MenuOpenCloseHandler::GetSingleton() {
        static MenuOpenCloseHandler singleton;

        return &singleton;
    }

    void UpdatePlayerState() {
        SKSE::GetTaskInterface()->AddTask([]() {
            auto* player = RE::PlayerCharacter::GetSingleton();

            if (!player) {
                return;
            }

            auto* actorValueOwner = player->AsActorValueOwner();

            if (!actorValueOwner) {
                return;
            }

            Settings::blockSkillLevel = actorValueOwner->GetActorValue(RE::ActorValue::kBlock);

            auto* empoweredBlockPerk = RE::TESForm::LookupByEditorID<RE::BGSPerk>("HBLOCK_EmpoweredBlock");

            Settings::hasEmpoweredBlock = empoweredBlockPerk && player->HasPerk(empoweredBlockPerk);

            if (Settings::debugLogging) {
                logger::info("PLAYER UPDATE | Block skill level={} | Empowered Block={}", Settings::blockSkillLevel,
                             Settings::hasEmpoweredBlock ? "YES" : "NO");
            }
        });
    }

    RE::BSEventNotifyControl MenuOpenCloseHandler::ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                                                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (!event->opening) {
            return RE::BSEventNotifyControl::kContinue;
        }

        UpdatePlayerState();

        return RE::BSEventNotifyControl::kContinue;
    }

    void Register() {
        auto* ui = RE::UI::GetSingleton();

        if (!ui) {
            logger::error("Hooks | Failed to get RE::UI singleton for MenuOpenCloseEvent registration");

            return;
        }

        auto* eventSource = ui->GetEventSource<RE::MenuOpenCloseEvent>();

        if (!eventSource) {
            logger::error("Hooks | Failed to get MenuOpenCloseEvent event source");

            return;
        }

        eventSource->AddEventSink(MenuOpenCloseHandler::GetSingleton());

        logger::info("Hooks | MenuOpenCloseEvent registered successfully");

        UpdatePlayerState();
    }

}