#include "Blocking.h"
#include "FormCache.h"
#include "Hooks.h"
#include "Menu.h"
#include "Settings.h"
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

            const auto pluginHandle = SKSE::GetPluginHandle();
            auto* precisionAPI = PRECISION_API::RequestPluginAPI();
            Blocking::Install(pluginHandle, static_cast<PRECISION_API::IVPrecision1*>(precisionAPI));
            logger::info("Blocking systems installed");

            Menu::Install();
            break;
        }
        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame: {
            // Safe to register UI hooks or query the player AFTER a save/new game is fully loaded
            Hooks::Register();
            logger::info("Player world loaded - hooks registered");
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

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener(OnMessage)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    return true;
}