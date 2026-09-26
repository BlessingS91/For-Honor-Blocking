#pragma once

#include "RE/M/MenuOpenCloseEvent.h"

namespace Hooks {
    inline float blockSkillLevel{0.0f};

    class MenuOpenCloseHandler final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuOpenCloseHandler* GetSingleton();

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>* eventSource) override;
    };

    void Register();
    void UpdatePlayerState();
}