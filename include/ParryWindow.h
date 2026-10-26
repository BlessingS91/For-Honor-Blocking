#pragma once

#include "RE/Skyrim.h"
#include "REL/Relocation.h"

namespace ParryWindow {
    void Open(RE::Actor* a_actor);
    void Close(RE::Actor* a_actor);
    bool IsOpen(RE::Actor* a_actor);

    class Events {
    public:
        static void AddEventSink();

        static RE::BSEventNotifyControl NPCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                        RE::BSAnimationGraphEvent* a_event,
                                                        RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src);

        static RE::BSEventNotifyControl PCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                       RE::BSAnimationGraphEvent* a_event,
                                                       RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src);

    private:
        static inline REL::Relocation<decltype(NPCProcessEvent)> _NPCProcessEvent;
        static inline REL::Relocation<decltype(PCProcessEvent)> _PCProcessEvent;

        static void ProcessEvent(RE::BSAnimationGraphEvent* a_event);
    };
}