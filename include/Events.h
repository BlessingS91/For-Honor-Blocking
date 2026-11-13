#pragma once

#include <RE/Skyrim.h>
#include <REL/Relocation.h>

namespace FHB {
    class Events {
    public:
        static void AddEventSink();
        static void ActivateParryKnockdown(RE::Actor* actor);
        static void ActivateGuardBroken(RE::Actor* actor);
        static void ActivateUnblockableReaction(RE::Actor* actor, int reaction);

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