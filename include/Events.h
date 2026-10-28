#pragma once

#include <RE/Skyrim.h>
#include <REL/Relocation.h>

namespace FHB {
    class Events {
    public:
        static void AddEventSink();

    private:
        static RE::BSEventNotifyControl ProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                     RE::BSAnimationGraphEvent* a_event,
                                                     RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source);

        static RE::BSEventNotifyControl PCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                       RE::BSAnimationGraphEvent* a_event,
                                                       RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source);

        static RE::BSEventNotifyControl NPCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                        RE::BSAnimationGraphEvent* a_event,
                                                        RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source);

        static inline REL::Relocation<decltype(&PCProcessEvent)> _PCProcessEvent;
        static inline REL::Relocation<decltype(&NPCProcessEvent)> _NPCProcessEvent;
    };
}