#pragma once

#include "RE/Skyrim.h"

// ============================================================================
// NPC ATTACK DIRECTION TRACKER
// NPC animations write AttackDirectionFHB. The Prisma bridge reads this state
// and displays it using the same shield component as the player HUD.
// ============================================================================
class AttackPosition
{
public:
    static AttackPosition* GetSingleton();

    // Register existing combat/load event sources. Call at SKSE kDataLoaded.
    void Initialize();

    // Clear transient warning/registration state after loading a save/new game.
    void OnGameLoaded();

    // Reuse the existing PlayerCharacter::Update hook; do not add another hook.
    // Handles pending NPC graph registrations and forwards current graph states
    // to PrismaUI with per-actor screen positions.
    void UpdateHUD();

    void ResolveAttackDirection(RE::Actor* a_actor, bool a_leftHand);
    void ResetAttackDirection(RE::Actor* a_actor);
    void RegisterNPCAnimationSink(RE::Actor* a_actor);
    void RegisterSink(RE::Actor* a_actor);
    void UnregisterSink(RE::Actor* a_actor);
    void RegisterSinksForExistingCombatants();

    class NpcCycleSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        static NpcCycleSink* GetSingleton();
        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource) override;
    };

    class NpcCombatTracker final : public RE::BSTEventSink<RE::TESCombatEvent>
    {
    public:
        static NpcCombatTracker* GetSingleton();
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event,
            RE::BSTEventSource<RE::TESCombatEvent>* a_eventSource) override;
    };

    class PC3DLoadEventHandler final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
    {
    public:
        static PC3DLoadEventHandler* GetSingleton();
        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event,
            RE::BSTEventSource<RE::TESObjectLoadedEvent>* a_eventSource) override;
    };

private:
    bool npcCombatRegistered_ = false;
    bool npcLoadRegistered_ = false;
};
