#include "AttackPosition.h"
#include "RE/S/ScriptEventSourceHolder.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Prisma.h"
#include "Settings.h"

namespace
{
    // ========================================================================
    // NPC GRAPH DIRECTION STATE - preserved from Honor Combat's attack tracker
    // ========================================================================
    constexpr char kAttackDirectionVariable[] = "AttackDirectionFHB";

    std::mutex g_attackDirectionsMutex;
    std::unordered_map<RE::FormID, int> g_attackDirections;
    std::unordered_map<RE::FormID, RE::ActorHandle> g_activeWarnings;

    // One indicator chooses each swing's direction until the next attack starts.
    std::unordered_set<RE::FormID> g_indicatorDirectionLocked;
    std::unordered_set<RE::FormID> g_swingCompleted;
    std::unordered_set<RE::FormID> g_awaitingNextAttackStart;

    // ========================================================================
    // NPC ANIMATION SINK REGISTRATION - no dispatcher or extra update hook
    // ========================================================================
    std::mutex g_trackedNPCsMutex;
    std::unordered_set<RE::FormID> g_trackedNPCs;

    struct PendingRegistration
    {
        RE::ActorHandle actor;
        std::chrono::steady_clock::time_point nextAttempt;
        int attempts{0};
    };
    std::unordered_map<RE::FormID, PendingRegistration> g_pendingRegistrations;
    constexpr auto kRetryInterval = std::chrono::milliseconds(100);
    constexpr int kMaxRegisterAttempts = 20;

    void QueueRegistration(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef() || !actor->IsInCombat()) return;
        std::scoped_lock lock(g_trackedNPCsMutex);
        const auto id = actor->GetFormID();
        if (g_trackedNPCs.contains(id)) return;
        // Do not overwrite a retry deadline on repeated load/combat callbacks.
        g_pendingRegistrations.try_emplace(id, PendingRegistration{actor->GetHandle(),
            std::chrono::steady_clock::now() + kRetryInterval, 0});
    }

    // ========================================================================
    // POSITION CLASSIFICATION - original hand/creature-node logic
    // ========================================================================
    RE::NiAVObject* FindFirstNode(RE::NiAVObject* a_root, std::initializer_list<const char*> a_names)
    {
        if (!a_root)
        {
            return nullptr;
        }

        for (const auto* name : a_names)
        {
            if (auto* node = a_root->GetObjectByName(RE::BSFixedString(name)))
            {
                return node;
            }
        }

        return nullptr;
    }

    std::optional<RE::NiPoint3> GetHandPositionRelativeToPlayer(RE::Actor* a_actor, bool a_leftHand)
    {
        if (!a_actor || !a_actor->Is3DLoaded())
        {
            return std::nullopt;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded())
        {
            return std::nullopt;
        }

        auto* actorRoot = a_actor->Get3D(false);
        auto* playerRoot = player->Get3D(false);

        if (!actorRoot || !playerRoot)
        {
            return std::nullopt;
        }

        static auto* creatureKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13795);
        static auto* animalKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13798);
        static auto* dwarvenKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x1397A);

        auto* race = a_actor->GetRace();

        const bool isCreature = race && ((creatureKeyword && race->HasKeyword(creatureKeyword)) || (animalKeyword && race->HasKeyword(animalKeyword)) || (dwarvenKeyword && race->HasKeyword(dwarvenKeyword)));

        RE::NiAVObject* positionNode = nullptr;

        if (isCreature)
        {
            positionNode = FindFirstNode(actorRoot, {"NPC Spine2 [Spn2]", "NPC Spine1 [Spn1]", "NPC Spine [Spn0]"});

            if (!positionNode)
            {
                positionNode = actorRoot;
            }
        }
        else
        {
            positionNode = a_leftHand ? FindFirstNode(actorRoot, {"NPC L Hand [LHnd]", "NPC L Forearm [LLar]"}) : FindFirstNode(actorRoot, {"NPC R Hand [RHnd]", "NPC R Forearm [RLar]", "WEAPON"});
        }

        if (!positionNode)
        {
            return std::nullopt;
        }

        return playerRoot->world.Invert() * positionNode->world.translate;
    }

    int ClassifyHandPositionRelativeToPlayer(const RE::NiPoint3& a_handPosition)
    {
        const float horizontalThreshold = Settings::NPCUI.topThreshold;

        if (a_handPosition.x > horizontalThreshold)
        {
            return 3;  // Left
        }

        if (a_handPosition.x < -horizontalThreshold)
        {
            return 2;  // Right
        }

        return 1;  // Top
    }

    std::string ToLower(std::string_view a_value)
    {
        std::string result;
        result.reserve(a_value.size());

        for (const auto character : a_value)
        {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }

        return result;
    }
    // ========================================================================
    // HUD PROJECTION - reused from the original Honor Combat warning pipeline
    // ========================================================================
    bool HasPlayerAsCombatTarget(RE::Actor* attacker, RE::PlayerCharacter* player)
    {
        if (!attacker || !player) return false;
        const auto target = attacker->GetActorRuntimeData().currentCombatTarget.get();
        return target && target.get() == player;
    }

    RE::NiAVObject* GetTorsoNode(RE::Actor* actor)
    {
        auto* root = actor ? actor->Get3D(false) : nullptr;
        if (!root) return nullptr;

        constexpr const char* kTorsoNames[] = {
            "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Spine [Spn0]",
            "NPC COM [COM ]", "Bip01 Spine1", "Bip01 Spine2"
        };
        for (const auto* name : kTorsoNames)
        {
            if (auto* node = root->GetObjectByName(RE::BSFixedString(name))) return node;
        }
        return root;
    }

    bool ProjectToScreenPercent(const RE::NiPoint3& worldPosition,
        float& xPercent, float& yPercent)
    {
        auto* camera = RE::Main::WorldRootCamera();
        if (!camera) return false;

        float normalizedX = 0.0f;
        float normalizedY = 0.0f;
        float depth = 0.0f;
        if (!camera->WorldPtToScreenPt3(worldPosition, normalizedX, normalizedY, depth, 1e-5f))
            return false;
        if (!camera->PointInFrustum(worldPosition, 0.0f) ||
            !std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
            normalizedX < 0.0f || normalizedX > 1.0f ||
            normalizedY < 0.0f || normalizedY > 1.0f)
            return false;

        xPercent = normalizedX * 100.0f;
        yPercent = (1.0f - normalizedY) * 100.0f;
        return true;
    }

    float GetResolutionScale()
    {
        if (!Settings::NPCUI.scaleWithResolution) return 1.0f;
        const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
        if (screen.height == 0) return 1.0f;
        return std::clamp(static_cast<float>(screen.height) / 1080.0f, 0.5f, 4.0f);
    }

    void AppendWarning(RE::Actor* actor, RE::PlayerCharacter* player,
        float resolutionScale, std::vector<AttackWarningVisual>& warnings)
    {
        if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsDisabled() ||
            !actor->IsInCombat() || !HasPlayerAsCombatTarget(actor, player)) return;

        std::int32_t rawDirection = 0;
        if (!actor->GetGraphVariableInt(kAttackDirectionVariable, rawDirection) ||
            rawDirection < 0 || rawDirection > 3) return;
        if (rawDirection == 0 && !Settings::NPCUI.alwaysShowInCombat) return;

        const auto* torso = GetTorsoNode(actor);
        if (!torso) return;

        float xPercent = 50.0f;
        float yPercent = 50.0f;
        if (!ProjectToScreenPercent(torso->world.translate, xPercent, yPercent)) return;

        float distanceScale = 1.0f;
        if (Settings::NPCUI.scaleWithDistance)
        {
            const auto* camera = RE::Main::WorldRootCamera();
            const auto distance = camera ? camera->world.translate.GetDistance(torso->world.translate) : 300.0f;
            if (std::isfinite(distance))
                distanceScale = std::clamp(300.0f / std::max(distance, 300.0f), 0.35f, 1.0f);
        }

        std::int32_t unblockable = 0;
        actor->GetGraphVariableInt("UnblockableFHB", unblockable);
        // Keep the raw graph direction. The shared Shield frontend performs
        // NPC left/right mirroring exactly once, using its independent setting.
        warnings.push_back({actor->GetFormID(), rawDirection, xPercent, yPercent,
            resolutionScale, distanceScale, false, unblockable == 1});
    }

}

AttackPosition* AttackPosition::GetSingleton()
{
    static AttackPosition singleton;
    return std::addressof(singleton);
}

void AttackPosition::ResetAttackDirection(RE::Actor* a_actor)
{
    if (!a_actor)
    {
        return;
    }

    bool hadAssignedDirection = false;
    {
        std::scoped_lock lock(g_attackDirectionsMutex);
        const auto actorID = a_actor->GetFormID();
        hadAssignedDirection = g_attackDirections.erase(actorID) != 0;
        g_activeWarnings.erase(actorID);

        // Clearing the graph/HUD is not permission to choose again. In the
        // previous implementation, erasing this lock caused one animation
        // with multiple indicator events to repeatedly change its direction.
        if (hadAssignedDirection || g_indicatorDirectionLocked.contains(actorID))
        {
            g_indicatorDirectionLocked.insert(actorID);
            g_awaitingNextAttackStart.insert(actorID);
        }
        g_swingCompleted.erase(actorID);
    }

    // An already ended attack cannot send another graph write or End update.
    if (hadAssignedDirection)
    {
        const bool graphUpdated = a_actor->SetGraphVariableInt(kAttackDirectionVariable, 0);
        SKSE::log::info("[NPC Attack Direction] End actor={:08X} graphUpdated={}", a_actor->GetFormID(), graphUpdated);
    }
}

void AttackPosition::ResolveAttackDirection(RE::Actor* a_actor, bool a_leftHand)
{
    if (!a_actor)
    {
        return;
    }

    const auto actorID = a_actor->GetFormID();
    {
        std::scoped_lock lock(g_attackDirectionsMutex);
        if (g_indicatorDirectionLocked.contains(actorID))
        {
            return;  // Direction already chosen for this swing.
        }
    }

    const auto handPosition = GetHandPositionRelativeToPlayer(a_actor, a_leftHand);

    if (!handPosition)
    {
        SKSE::log::info("[NPC Attack Direction] Resolve failed actor={:08X}: {} hand not found.", a_actor->GetFormID(), a_leftHand ? "left" : "right");

        return;
    }

    const int direction = ClassifyHandPositionRelativeToPlayer(*handPosition);

    {
        std::scoped_lock lock(g_attackDirectionsMutex);
        if (!g_indicatorDirectionLocked.insert(actorID).second)
        {
            return;  // Another indicator already selected a direction.
        }
        g_attackDirections[actorID] = direction;
        g_activeWarnings[actorID] = a_actor->GetHandle();
        g_swingCompleted.erase(actorID);
        g_awaitingNextAttackStart.erase(actorID);
    }

    const bool graphUpdated = a_actor->SetGraphVariableInt(kAttackDirectionVariable, direction);


    SKSE::log::info("[NPC Attack Direction] NPC {:08X}: hand={} localX={:.2f} localY={:.2f} localZ={:.2f} direction={} graphUpdated={}.", a_actor->GetFormID(), a_leftHand ? "L" : "R", handPosition->x, handPosition->y, handPosition->z,
                    direction, graphUpdated);
}

RE::BSEventNotifyControl AttackPosition::NpcCycleSink::ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
    if (!a_event || !a_event->holder)
    {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* actor = a_event->holder->As<RE::Actor>();

    if (!actor || actor->IsPlayerRef())
    {
        return RE::BSEventNotifyControl::kContinue;
    }

    try
    {
        const std::string rawEventName = a_event->tag.c_str();
        const std::string eventName = ToLower(rawEventName);

        SKSE::log::debug("[NPC Attack Direction] Event actor={:08X} tag={}", actor->GetFormID(), rawEventName);

        if (eventName == "leftindicatorstart")
        {
            AttackPosition::GetSingleton()->ResolveAttackDirection(actor, true);
        }
        else if (eventName == "indicatorstart")
        {
            AttackPosition::GetSingleton()->ResolveAttackDirection(actor, false);
        }
        else if (eventName.find("weaponleftswing") != std::string::npos ||
                 eventName.find("weaponswing") != std::string::npos)
        {
            // The first indicator or weapon swing chooses the direction.
            // ResolveAttackDirection keeps it locked until an attack reset.
            const bool leftHand = eventName.find("weaponleftswing") != std::string::npos;
            AttackPosition::GetSingleton()->ResolveAttackDirection(actor, leftHand);

            std::scoped_lock lock(g_attackDirectionsMutex);
            const auto actorID = actor->GetFormID();
            if (g_attackDirections.contains(actorID))
            {
                g_swingCompleted.insert(actorID);
            }
        }
        else if (eventName == "attackstart" || eventName == "mco_attackstart" ||
                 eventName == "scar_combostart")
        {
            // Only a distinct attack start AFTER a previous swing or End
            // allows another indicator. Repeated starts BEFORE weaponSwing
            // do not re-arm the current attack.
            bool hadAssignedDirection = false;
            {
                std::scoped_lock lock(g_attackDirectionsMutex);
                const auto actorID = actor->GetFormID();
                const bool previousSwingFinished = g_swingCompleted.erase(actorID) != 0;
                const bool previousAttackEnded = g_awaitingNextAttackStart.erase(actorID) != 0;
                if (previousSwingFinished || previousAttackEnded)
                {
                    hadAssignedDirection = g_attackDirections.erase(actorID) != 0;
                    g_activeWarnings.erase(actorID);
                    g_indicatorDirectionLocked.erase(actorID);
                }
            }

            if (hadAssignedDirection)
            {
                // A new attack has started. Clear the old direction BEFORE
                // its own indicator event selects the next immutable value.
                actor->SetGraphVariableInt(kAttackDirectionVariable, 0);
            }
        }
        else if (eventName == "mco_winopen" || eventName == "mco_powerwinopen")
        {
            // These windows may open BEFORE the hit/swing in some movesets.
            // Only treat one as an ending boundary after weaponSwing. The
            // direction remains consumed until the NEXT attackStart.
            bool completedSwing = false;
            {
                std::scoped_lock lock(g_attackDirectionsMutex);
                completedSwing = g_swingCompleted.contains(actor->GetFormID());
            }
            if (completedSwing)
            {
                AttackPosition::GetSingleton()->ResetAttackDirection(actor);
            }
        }
        else if (eventName == "staggerstart" || eventName == "blockstart" ||
                 eventName == "attackstop" || eventName == "sbf_staggerstart")
        {
            // Interruption/end can clear the active warning, but not unlock
            // the attack. Another direction requires the next attackStart.
            AttackPosition::GetSingleton()->ResetAttackDirection(actor);
        }
    }
    catch (const std::exception& e)
    {
        SKSE::log::error("[NPC Attack Direction] Exception for NPC {:08X}: {}", actor->GetFormID(), e.what());
    }
    catch (...)
    {
        SKSE::log::error("[NPC Attack Direction] Unknown exception for NPC {:08X}.", actor->GetFormID());
    }

    return RE::BSEventNotifyControl::kContinue;
}

AttackPosition::NpcCycleSink* AttackPosition::NpcCycleSink::GetSingleton()
{
    static NpcCycleSink singleton;
    return std::addressof(singleton);
}


// ============================================================================
// NPC COMBAT AND LOAD EVENTS
// ============================================================================
RE::BSEventNotifyControl AttackPosition::NpcCombatTracker::ProcessEvent(
    const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*)
{
    if (!a_event) return RE::BSEventNotifyControl::kContinue;
    const auto actorPtr = a_event->actor.get();
    auto* actor = actorPtr ? actorPtr->As<RE::Actor>() : nullptr;
    if (!actor || actor->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;

    const auto newState = *a_event->newState;
    if (newState == RE::ACTOR_COMBAT_STATE::kCombat)
    {
        {
            std::scoped_lock lock(g_attackDirectionsMutex);
            const auto id = actor->GetFormID();
            if (g_awaitingNextAttackStart.erase(id) != 0)
                g_indicatorDirectionLocked.erase(id);
        }
        AttackPosition::GetSingleton()->RegisterSink(actor);
    }
    else if (newState == RE::ACTOR_COMBAT_STATE::kNone)
    {
        AttackPosition::GetSingleton()->ResetAttackDirection(actor);
        AttackPosition::GetSingleton()->UnregisterSink(actor);
    }
    return RE::BSEventNotifyControl::kContinue;
}

AttackPosition::NpcCombatTracker* AttackPosition::NpcCombatTracker::GetSingleton()
{
    static NpcCombatTracker singleton;
    return std::addressof(singleton);
}

void AttackPosition::RegisterNPCAnimationSink(RE::Actor* a_actor)
{
    RegisterSink(a_actor);
}

void AttackPosition::RegisterSink(RE::Actor* a_actor)
{
    if (!a_actor || a_actor->IsPlayerRef() || !a_actor->IsInCombat()) return;
    const auto id = a_actor->GetFormID();
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        if (g_trackedNPCs.contains(id)) return;
    }

    RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
    a_actor->GetAnimationGraphManager(graphManager);
    if (!graphManager)
    {
        QueueRegistration(a_actor);
        return;
    }

    const bool registered = a_actor->AddAnimationGraphEventSink(NpcCycleSink::GetSingleton());
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        if (registered)
        {
            g_trackedNPCs.insert(id);
            g_pendingRegistrations.erase(id);
        }
    }
    if (!registered) QueueRegistration(a_actor);
    SKSE::log::debug("[NPC Attack Direction] Register actor={:08X} success={}", id, registered);
}

void AttackPosition::UnregisterSink(RE::Actor* a_actor)
{
    if (!a_actor || a_actor->IsPlayerRef()) return;
    const auto id = a_actor->GetFormID();
    bool wasRegistered = false;
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        wasRegistered = g_trackedNPCs.erase(id) != 0;
        g_pendingRegistrations.erase(id);
    }
    if (wasRegistered && a_actor->Is3DLoaded())
        a_actor->RemoveAnimationGraphEventSink(NpcCycleSink::GetSingleton());
}

void AttackPosition::RegisterSinksForExistingCombatants()
{
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return;
    for (auto& actorHandle : processLists->highActorHandles)
    {
        auto actorPtr = actorHandle.get();
        auto* actor = actorPtr.get();
        if (actor && !actor->IsPlayerRef() && actor->IsInCombat())
            RegisterSink(actor);
    }
}

RE::BSEventNotifyControl AttackPosition::PC3DLoadEventHandler::ProcessEvent(
    const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
{
    if (!a_event) return RE::BSEventNotifyControl::kContinue;

    auto* form = RE::TESForm::LookupByID(a_event->formID);
    auto* actor = form ? form->As<RE::Actor>() : nullptr;
    if (!actor || actor->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;

    if (a_event->loaded)
    {
        // The graph may not exist until shortly after TESObjectLoadedEvent.
        // Queue a bounded retry checked by the already-installed update hook.
        if (actor->IsInCombat()) QueueRegistration(actor);
    }
    else
    {
        // Clear transient state before the actor's next 3D load.
        AttackPosition::GetSingleton()->ResetAttackDirection(actor);
        AttackPosition::GetSingleton()->UnregisterSink(actor);
    }
    return RE::BSEventNotifyControl::kContinue;
}

AttackPosition::PC3DLoadEventHandler* AttackPosition::PC3DLoadEventHandler::GetSingleton()
{
    static PC3DLoadEventHandler singleton;
    return std::addressof(singleton);
}

// ============================================================================
// REGISTRATION AND HUD UPDATE - called from the existing plugin lifecycle/hook
// ============================================================================
void AttackPosition::Initialize()
{
    auto* sources = RE::ScriptEventSourceHolder::GetSingleton();
    if (!sources)
    {
        SKSE::log::warn("[NPC Attack Direction] Event sources unavailable");
        return;
    }
    if (!npcCombatRegistered_)
    {
        sources->AddEventSink(NpcCombatTracker::GetSingleton());
        npcCombatRegistered_ = true;
    }
    if (!npcLoadRegistered_)
    {
        sources->AddEventSink(PC3DLoadEventHandler::GetSingleton());
        npcLoadRegistered_ = true;
    }
    RegisterSinksForExistingCombatants();
    SKSE::log::info("[NPC Attack Direction] Initialized (no dispatcher)");
}

void AttackPosition::OnGameLoaded()
{
    // When reloading a save in the same process, detach any old sinks before
    // forgetting their IDs. Otherwise re-registration may be rejected as a
    // duplicate and the new graph will never send indicator events.
    std::vector<RE::FormID> previousTracked;
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        previousTracked.assign(g_trackedNPCs.begin(), g_trackedNPCs.end());
    }
    for (const auto id : previousTracked)
    {
        auto* form = RE::TESForm::LookupByID(id);
        auto* actor = form ? form->As<RE::Actor>() : nullptr;
        if (actor && actor->Is3DLoaded())
            actor->RemoveAnimationGraphEventSink(NpcCycleSink::GetSingleton());
    }

    {
        std::scoped_lock lock(g_attackDirectionsMutex);
        g_attackDirections.clear();
        g_activeWarnings.clear();
        g_indicatorDirectionLocked.clear();
        g_swingCompleted.clear();
        g_awaitingNextAttackStart.clear();
    }
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        g_trackedNPCs.clear();
        g_pendingRegistrations.clear();
    }
    Prisma::UpdateAttackWarnings({});
    RegisterSinksForExistingCombatants();
}

void AttackPosition::UpdateHUD()
{
    // Retry only NPCs whose animation graph was unavailable during load.
    // This uses the existing player update, not a dispatcher or separate hook.
    const auto now = std::chrono::steady_clock::now();
    std::vector<RE::ActorHandle> readyToTry;
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        for (auto it = g_pendingRegistrations.begin(); it != g_pendingRegistrations.end();)
        {
            if (now < it->second.nextAttempt) { ++it; continue; }
            if (it->second.attempts >= kMaxRegisterAttempts)
            {
                SKSE::log::warn("[NPC Attack Direction] Registration retry exhausted for {:08X}", it->first);
                it = g_pendingRegistrations.erase(it);
                continue;
            }
            readyToTry.push_back(it->second.actor);
            ++it->second.attempts;
            it->second.nextAttempt = now + kRetryInterval;
            ++it;
        }
    }
    for (const auto& handle : readyToTry)
    {
        auto actorPtr = handle.get();
        auto* actor = actorPtr.get();
        if (actor && actor->IsInCombat()) RegisterSink(actor);
    }

    // No NPC GUI can be derived directly from a JavaScript animation graph.
    // Read each actor's graph direction in C++, then send visual state to Prisma.
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ui = RE::UI::GetSingleton();
    if (!Settings::NPCUI.enabled || !player || player->IsDead() ||
        (ui && ui->GameIsPaused()))
    {
        Prisma::UpdateAttackWarnings({});
        return;
    }

    std::vector<AttackWarningVisual> visuals;
    const float resolutionScale = GetResolutionScale();
    if (Settings::NPCUI.alwaysShowInCombat)
    {
        if (auto* lists = RE::ProcessLists::GetSingleton())
        {
            for (auto& handle : lists->highActorHandles)
            {
                auto actorPtr = handle.get();
                AppendWarning(actorPtr.get(), player, resolutionScale, visuals);
            }
        }
    }
    else
    {
        std::vector<RE::ActorHandle> activeActors;
        {
            std::scoped_lock lock(g_attackDirectionsMutex);
            activeActors.reserve(g_activeWarnings.size());
            for (const auto& [id, handle] : g_activeWarnings)
                activeActors.push_back(handle);
        }
        for (const auto& handle : activeActors)
        {
            auto actorPtr = handle.get();
            AppendWarning(actorPtr.get(), player, resolutionScale, visuals);
        }
    }

    std::ranges::sort(visuals, {}, &AttackWarningVisual::id);
    if (visuals.size() > 32) visuals.resize(32);
    Prisma::UpdateAttackWarnings(visuals);
}
