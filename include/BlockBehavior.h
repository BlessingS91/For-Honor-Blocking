
#pragma once

#include <string_view>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"

class MCO final : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
    static MCO* GetSingleton();

    static void InstallHooks();
    void RegisterAnimationSink();

    void ProcessBlock(std::string_view a_eventTag = {});

    RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                          RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource) override;

private:
    MCO() = default;

    MCO(const MCO&) = delete;
    MCO(MCO&&) = delete;

    MCO& operator=(const MCO&) = delete;
    MCO& operator=(MCO&&) = delete;

    struct Hooks {
        struct AttackBlockHandler_ProcessButton {
            static void thunk(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data);

            static inline REL::Relocation<decltype(thunk)> func;
        };

        static void Install();
    };
};
