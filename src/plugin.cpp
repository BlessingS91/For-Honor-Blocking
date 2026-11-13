#include "AttackPosition.h"
#include "BlockBehavior.h"
#include "BlockKey.h"
#include "Blocking.h"
#include "Events.h"
#include "FormCache.h"
#include "Gameplay.h"
#include "Hooks.h"
#include "InputTracking.h"
#include "Menu.h"
#include "Parry.h"
#include "PowerAttackKey.h"
#include "Prisma.h"
#include "Settings.h"
#include "TDMLockBridge.h"
#include "WeaponClash.h"
#include "logger.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (!message) {
        return;
    }

    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded: {
            logger::info("Data loaded - loading For Honor Combat settings");

            Settings::Load();
            FormCache::LoadForms();
            DirectionTracker::GetSingleton()->Initialize();

            // Initialize PrismaUI.
            Prisma::Install();
            Prisma::Preload();

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
            Hooks::Install();
            TDMLockBridge::Initialize();
            BlockKey::OnDataLoaded();
            // Register combat animation events.
            FHB::Events::AddEventSink();

            // Initialize NPC attack-direction tracking.
            AttackPosition::GetSingleton()->Initialize();

            break;
        }

        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame: {
            MCO::GetSingleton()->RegisterAnimationSink();
            Gameplay::AssignPlayerPerks();
            BlockKey::OnGameLoaded();

            // Register NPC tracking for the loaded game.
            AttackPosition::GetSingleton()->OnGameLoaded();

            break;
        }

        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    logger::info("For Honor Combat plugin loaded");

    SKSE::Init(skse);
    PowerAttackKey::Initialize();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    return true;
}