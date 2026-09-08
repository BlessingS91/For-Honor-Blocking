#include "Blocking.h"
#include "logger.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (!message) {
        return;
    }

    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        logger::info("Data loaded - installing blocking systems");

        Blocking::Install();

        logger::info("Blocking systems installed");
    }

    if (message->type == SKSE::MessagingInterface::kNewGame ||
        message->type == SKSE::MessagingInterface::kPostLoadGame) {
        // Post-load logic
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();

    logger::info("Plugin loaded");

    SKSE::Init(skse);

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener(OnMessage)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    return true;
}