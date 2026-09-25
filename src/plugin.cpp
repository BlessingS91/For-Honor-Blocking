#include "Blocking.h"
#include "Menu.h"
#include "Settings.h"
#include "logger.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (!message) {
        return;
    }

    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        logger::info("Data loaded - loading For Honor Blocking settings");

        Settings::Load();

        const auto pluginHandle = SKSE::GetPluginHandle();

        auto* precisionAPI = PRECISION_API::RequestPluginAPI();

        Blocking::Install(pluginHandle, static_cast<PRECISION_API::IVPrecision1*>(precisionAPI));

        logger::info("Blocking systems installed");

        Menu::Install();
    }

    if (message->type == SKSE::MessagingInterface::kNewGame ||
        message->type == SKSE::MessagingInterface::kPostLoadGame) {
        // Post-load logic
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