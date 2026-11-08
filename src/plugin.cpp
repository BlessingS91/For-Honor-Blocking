#include "BlockBehavior.h"
#include "BlockKey.h"
#include "Blocking.h"
#include "Events.h"
#include "FormCache.h"
#include "Gameplay.h"
#include "Hooks.h"
#include "Menu.h"
#include "Parry.h"
#include "PowerAttackKey.h"
#include "Settings.h"
#include "WeaponClash.h"
#include "logger.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (!message) {
        return;
    }

    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded: {
            logger::info("Data loaded - loading For Honor Blocking settings");
            Settings::Load();
            FormCache::LoadForms();

            // MCO::InstallHooks() intentionally not called: its AttackBlockHandler
            // behavior was consolidated into PowerAttackKey's single input hook.

            const auto pluginHandle = SKSE::GetPluginHandle();
            auto* precisionAPI = static_cast<PRECISION_API::IVPrecision2*>(
                PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V2));

            Blocking::Install(pluginHandle, precisionAPI);
            WeaponClash::Install(pluginHandle, precisionAPI);
            Blocking::Install(pluginHandle, static_cast<PRECISION_API::IVPrecision1*>(precisionAPI));

            logger::info("Blocking systems installed");
            logger::info("Parry system registration attempted");

            Menu::Install();

            // Register SPA before SimpleBlock so its power-attack input callback
            // can consume the power chord before BlockKey handles regular blocking.
            PowerAttackKey::OnDataLoaded();
            BlockKey::OnDataLoaded();

            FHB::Events::AddEventSink();
            break;
        }

        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame: {
            MCO::GetSingleton()->RegisterAnimationSink();
            Gameplay::AssignPlayerPerks();
            BlockKey::OnGameLoaded();
            break;
        }

        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    logger::info("For Honor Blocking plugin loaded");

    SKSE::Init(skse);
    PowerAttackKey::Initialize();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    return true;
}
