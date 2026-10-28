#include "Events.h"

#include <SKSE/SKSE.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {
    using namespace std::chrono_literals;

    constexpr auto kWindowDuration = 1000ms;

    std::mutex resetMutex;
    std::unordered_map<std::uint64_t, std::uint64_t> resetGenerations;
    std::uint64_t nextGeneration = 0;

    void ActivateWindow(RE::Actor* actor, const char* variable, std::uint8_t variableID) {
        if (!actor) {
            return;
        }

        const auto handle = actor->GetHandle();
        if (!handle) {
            return;
        }

        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            return;
        }

        if (!actor->SetGraphVariableInt(variable, 1)) {
            logger::warn("[FHB] Failed to set {}", variable);
            return;
        }

        const std::uint64_t key = (static_cast<std::uint64_t>(actor->GetFormID()) << 2) | variableID;

        std::uint64_t generation;

        {
            std::scoped_lock lock(resetMutex);
            generation = ++nextGeneration;
            resetGenerations[key] = generation;
        }

        logger::info("[FHB] {} = 1", variable);

        std::thread([handle, variable, key, generation, tasks]() {
            std::this_thread::sleep_for(kWindowDuration);

            tasks->AddTask([handle, variable, key, generation]() {
                {
                    std::scoped_lock lock(resetMutex);

                    const auto it = resetGenerations.find(key);

                    if (it == resetGenerations.end() || it->second != generation) {
                        return;
                    }

                    resetGenerations.erase(it);
                }

                if (auto actor = handle.get()) {
                    actor->SetGraphVariableInt(variable, 0);
                    logger::info("[FHB] {} = 0", variable);
                }
            });
        }).detach();
    }
}

namespace FHB {
    void Events::AddEventSink() {
        REL::Relocation<std::uintptr_t> pcVtbl{RE::VTABLE_PlayerCharacter[2]};

        REL::Relocation<std::uintptr_t> npcVtbl{RE::VTABLE_Character[2]};

        _PCProcessEvent = pcVtbl.write_vfunc(0x1, reinterpret_cast<std::uintptr_t>(&PCProcessEvent));

        _NPCProcessEvent = npcVtbl.write_vfunc(0x1, reinterpret_cast<std::uintptr_t>(&NPCProcessEvent));

        logger::info("[FHB] Animation event hooks installed.");
    }

    RE::BSEventNotifyControl Events::ProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                  RE::BSAnimationGraphEvent* a_event,
                                                  RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) {
        if (!a_event || !a_event->holder) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* actor = const_cast<RE::Actor*>(a_event->holder->As<RE::Actor>());

        if (!actor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto& tag = a_event->tag;

        if (tag == "ParryStartFHB") {
            ActivateWindow(actor, "ParryFHB", 0);
        } else if (tag == "UnblockableStartFHB") {
            ActivateWindow(actor, "UnblockableFHB", 1);
        } else if (tag == "AllBlockStartFHB") {
            ActivateWindow(actor, "AllBlockFHB", 2);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl Events::PCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                    RE::BSAnimationGraphEvent* a_event,
                                                    RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) {
        ProcessEvent(a_this, a_event, a_source);
        return _PCProcessEvent(a_this, a_event, a_source);
    }

    RE::BSEventNotifyControl Events::NPCProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
                                                     RE::BSAnimationGraphEvent* a_event,
                                                     RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source) {
        ProcessEvent(a_this, a_event, a_source);
        return _NPCProcessEvent(a_this, a_event, a_source);
    }
}