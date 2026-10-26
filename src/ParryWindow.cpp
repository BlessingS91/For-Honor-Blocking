#include "ParryWindow.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace {
    std::unordered_set<RE::Actor*> g_parryWindowActors;
    constexpr auto kParryWindowVariable = "FH_ParryWindow";
}

namespace ParryWindow {
    void Open(RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }

        g_parryWindowActors.insert(a_actor);
        a_actor->SetGraphVariableInt(kParryWindowVariable, 1);
    }

    void Close(RE::Actor* a_actor) {
        if (!a_actor) {
            return;
        }

        g_parryWindowActors.erase(a_actor);
        a_actor->SetGraphVariableInt(kParryWindowVariable, 0);
    }

    bool IsOpen(RE::Actor* a_actor) {
        if (!a_actor) {
            return false;
        }

        return g_parryWindowActors.contains(a_actor);
    }

    void Events::AddEventSink() {
        REL::Relocation<uintptr_t> PCProcessAnimGraphEventVtbl{RE::VTABLE_PlayerCharacter[2]};
        REL::Relocation<uintptr_t> NPCProcessAnimGraphEventVtbl{RE::VTABLE_Character[2]};

        _PCProcessEvent = PCProcessAnimGraphEventVtbl.write_vfunc(0x1, &PCProcessEvent);
        _NPCProcessEvent = NPCProcessAnimGraphEventVtbl.write_vfunc(0x1, &NPCProcessEvent);
    }

    void Events::ProcessEvent(RE::BSAnimationGraphEvent* a_event) {
        if (!a_event || !a_event->holder) {
            return;
        }

        auto* actor = a_event->holder->As<RE::Actor>();

        if (!actor) {
            return;
        }

        const std::string rawEventName = a_event->tag.c_str();

        std::string eventName = rawEventName;
        std::transform(eventName.begin(), eventName.end(), eventName.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (eventName.find("weaponleftswing") != std::string::npos ||
            eventName.find("weaponswing") != std::string::npos) {
            Open(actor);

            SKSE::log::debug("[ParryWindow] OPEN actor={:08X} tag={}", actor->GetFormID(), rawEventName);

        } else if (eventName == "hitframe" || eventName == "fakehit" || eventName == "mco_winopen" ||
                   eventName == "mco_powerwinopen" || eventName == "scar_combostart" || eventName == "staggerstart" ||
                   eventName == "blockstart" || eventName == "attackwinstart" || eventName == "attackstop" ||
                   eventName == "sbf_staggerstart" || eventName == "sbf_staggerstop") {
            Close(actor);

            SKSE::log::debug("[ParryWindow] CLOSE actor={:08X} tag={}", actor->GetFormID(), rawEventName);
        }
    }

    RE::BSEventNotifyControl Events::NPCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                     RE::BSAnimationGraphEvent* a_event,
                                                     RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src) {
        ProcessEvent(a_event);
        return _NPCProcessEvent(a_this, a_event, a_src);
    }

    RE::BSEventNotifyControl Events::PCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                    RE::BSAnimationGraphEvent* a_event,
                                                    RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_src) {
        ProcessEvent(a_event);
        return _PCProcessEvent(a_this, a_event, a_src);
    }
}