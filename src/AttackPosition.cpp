#include "AttackPosition.h"
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
#include "AttackTrackingMode.h"
#include "NPCDirectionalBlock.h"
#include "Prisma.h"
#include "RE/S/ScriptEventSourceHolder.h"
#include "Settings.h"
namespace {
    // ========================================================================
    // SHARED PLAYER/NPC ATTACK DIRECTION STATE
    // ========================================================================
    constexpr char kAttackDirectionVariable[] = "AttackDirectionFHB";
    constexpr char kPlayerAttackIndicatorVariable[] = "PlayerAttackIndicatorFHB";
    std::mutex g_attackDirectionsMutex;
    std::unordered_map<RE::FormID, int> g_attackDirections;
    std::unordered_map<RE::FormID, RE::ActorHandle> g_activeWarnings;
    // Lock direction after the first indicator/swing, until an existing release event.
    std::unordered_set<RE::FormID> g_indicatorDirectionLocked;
    // Player-only indicator is independent of combat-target availability.
    bool g_playerIndicatorDirectionLocked = false;
    std::unordered_set<RE::FormID> g_swingStarted;
    // The same animation-event sink tracks player attacks; registration is
    // independent of NPC combat state and retried by the existing update.
    bool g_playerSinkRegistered = false;
    // ========================================================================
    // ANIMATION SINK REGISTRATION - no dispatcher or extra update hook
    // ========================================================================
    std::mutex g_trackedNPCsMutex;
    std::unordered_set<RE::FormID> g_trackedNPCs;
    struct PendingRegistration {
        RE::ActorHandle actor;
        std::chrono::steady_clock::time_point nextAttempt;
        int attempts{0};
    };
    std::unordered_map<RE::FormID, PendingRegistration> g_pendingRegistrations;
    constexpr auto kRetryInterval = std::chrono::milliseconds(100);
    constexpr int kMaxRegisterAttempts = 20;
    void QueueRegistration(RE::Actor* actor) {
        if (!actor || actor->IsPlayerRef() || !actor->IsInCombat()) return;
        std::scoped_lock lock(g_trackedNPCsMutex);
        const auto id = actor->GetFormID();
        if (g_trackedNPCs.contains(id)) return;
        // Do not overwrite a retry deadline on repeated load/combat callbacks.
        g_pendingRegistrations.try_emplace(
            id, PendingRegistration{actor->GetHandle(), std::chrono::steady_clock::now() + kRetryInterval, 0});
    }
    // ========================================================================
    // ATTACK POSITION CLASSIFICATION - defender-head height and horizontal center band
    // ========================================================================
    RE::NiAVObject* FindFirstNode(RE::NiAVObject* a_root, std::initializer_list<const char*> a_names) {
        if (!a_root) {
            return nullptr;
        }
        for (const auto* name : a_names) {
            if (auto* node = a_root->GetObjectByName(RE::BSFixedString(name))) {
                return node;
            }
        }
        return nullptr;
    }
    // Both player and NPC attacks use the defender's chest and local axes.
    RE::NiAVObject* GetTorsoNode(RE::Actor* actor);
    RE::NiAVObject* GetReferenceChestNode(RE::Actor* target) {
        auto* referenceRoot = target ? target->Get3D(false) : nullptr;
        auto* referenceChest = referenceRoot
            ? FindFirstNode(referenceRoot, {"NPC Spine2 [Spn2]", "NPC Spine1 [Spn1]",
                                            "NPC Spine [Spn0]", "Bip01 Spine2", "Bip01 Spine1"})
            : nullptr;
        return referenceChest ? referenceChest : GetTorsoNode(target);
    }
    // Return the defender's head Z relative to their chest in defender-local space.
    std::optional<float> GetTargetHeadHeightAboveChest(RE::Actor* target) {
        auto* root = target ? target->Get3D(false) : nullptr;
        auto* chest = GetReferenceChestNode(target);
        auto* head = root
            ? FindFirstNode(root, {"NPC Head [Head]", "Bip01 Head", "NPC Neck [Neck]", "Bip01 Neck"})
            : nullptr;
        if (!root || !chest || !head) return std::nullopt;
        const auto inverse = root->world.Invert();
        const RE::NiPoint3 localHead = inverse * head->world.translate;
        const RE::NiPoint3 localChest = inverse * chest->world.translate;
        const float headAboveChest = localHead.z - localChest.z;
        return std::isfinite(headAboveChest) ? std::optional<float>{headAboveChest} : std::nullopt;
    }
    std::optional<RE::NiPoint3> GetHandPositionRelativeToReference(RE::Actor* attacker, RE::Actor* target, bool leftHand) {
        if (!attacker || !attacker->Is3DLoaded() || !target || !target->Is3DLoaded()) return std::nullopt;
        // Sample the attacker's hand in the defender's local frame.
        auto* attackingRoot = attacker->Get3D(false);
        auto* referenceRoot = target->Get3D(false);
        auto* referenceChest = GetReferenceChestNode(target);
        if (!attackingRoot || !referenceRoot || !referenceChest) return std::nullopt;
        static auto* creatureKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13795);
        static auto* animalKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13798);
        static auto* dwarvenKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x1397A);
        auto* race = attacker->GetRace();
        const bool isCreature = race && ((creatureKeyword && race->HasKeyword(creatureKeyword)) ||
                                         (animalKeyword && race->HasKeyword(animalKeyword)) ||
                                         (dwarvenKeyword && race->HasKeyword(dwarvenKeyword)));
        RE::NiAVObject* handNode = nullptr;
        if (isCreature) {
            handNode = FindFirstNode(attackingRoot, {"NPC Spine2 [Spn2]", "NPC Spine1 [Spn1]", "NPC Spine [Spn0]"});
            if (!handNode) handNode = attackingRoot;
        } else {
            handNode = leftHand
                           ? FindFirstNode(attackingRoot, {"NPC L Hand [LHnd]", "NPC L Forearm [LLar]"})
                           : FindFirstNode(attackingRoot, {"NPC R Hand [RHnd]", "NPC R Forearm [RLar]", "WEAPON"});
        }
        if (!handNode) return std::nullopt;
        const auto inverse = referenceRoot->world.Invert();
        const RE::NiPoint3 localHand = inverse * handNode->world.translate;
        const RE::NiPoint3 localChest = inverse * referenceChest->world.translate;
        return RE::NiPoint3{localHand.x - localChest.x, localHand.y - localChest.y,
                            localHand.z - localChest.z};
    }
    // Precision uses the visual weapon model under WEAPON (right hand) or
    // SHIELD (left hand) to estimate weapon reach. Its collision tip offset is
    // along the attachment node's local +Y axis. We only need an approximate
    // *visual* tip, so do not use the collision capsule's gameplay multipliers.
    // Called once per attack window, not every frame.
    struct WeaponTipSample {
        RE::NiPoint3 worldPosition;
        float length;
    };
    std::optional<WeaponTipSample> GetWeaponTipWorldPosition(RE::Actor* attacker, bool leftHand) {
        if (!attacker || !attacker->Is3DLoaded()) return std::nullopt;
        // No visual weapon tip for fists, shields, bows, staves, spells, etc.
        auto* equipped = attacker->GetEquippedObject(leftHand);
        auto* weapon = equipped ? skyrim_cast<RE::TESObjectWEAP*>(equipped) : nullptr;
        if (!weapon || weapon->IsBow() || weapon->IsCrossbow() || weapon->IsStaff()) {
            return std::nullopt;
        }
        auto* root = attacker->Get3D(false);
        if (!root) return std::nullopt;
        // Names intentionally match Precision's AttackCollision.cpp.
        auto* attachment = root->GetObjectByName(RE::BSFixedString(leftHand ? "SHIELD" : "WEAPON"));
        auto* attachmentNode = attachment ? attachment->AsNode() : nullptr;
        if (!attachmentNode) return std::nullopt;
        // GetChildren() supports SE/AE/VR; direct .children is unavailable in
        // CommonLibSSE-NG when SKYRIM_CROSS_VR is enabled.
        const auto& children = attachmentNode->GetChildren();
        if (children.size() == 0 || !children[0]) return std::nullopt;
        // The first child is the equipped weapon's visual model in the normal
        // Skyrim skeleton. Use the *world* bound, then convert its center and
        // radius into attachment-local units before projecting the tip.
        auto* model = children[0].get();
        const auto& bound = model->worldBound;
        const float attachmentScale = std::fabs(attachment->world.scale);
        if (!std::isfinite(bound.radius) || bound.radius <= 0.0f ||
            !std::isfinite(attachmentScale) || attachmentScale < 0.001f) {
            return std::nullopt;
        }
        const RE::NiPoint3 modelCenter = attachment->world.Invert() * bound.center;
        const float localRadius = bound.radius / attachmentScale;
        const float length = std::fabs(modelCenter.y) + localRadius;
        // Invalid / uninitialized bounds should never override hand tracking.
        // The generous upper bound also excludes wildly incorrect model data.
        if (!std::isfinite(modelCenter.x) || !std::isfinite(modelCenter.y) ||
            !std::isfinite(modelCenter.z) || !std::isfinite(length) ||
            length < 2.0f || length > 500.0f) {
            return std::nullopt;
        }
        const RE::NiPoint3 worldTip = attachment->world * RE::NiPoint3{0.0f, length, 0.0f};
        if (!std::isfinite(worldTip.x) || !std::isfinite(worldTip.y) || !std::isfinite(worldTip.z)) {
            return std::nullopt;
        }
        return WeaponTipSample{worldTip, length};
    }
    struct AttackPositionSample {
        RE::NiPoint3 relativePosition;
        bool usedWeaponTip{false};
    };
    std::optional<AttackPositionSample> GetTrackedPositionRelativeToReference(RE::Actor* attacker, RE::Actor* target, bool leftHand) {
        // Mode is only sampled when an indicator/swing event requests a direction.
        // No continuous tracking or repeated model traversal while Hand is selected.
        if (AttackTrackingMode::useWeaponTip.load(std::memory_order_relaxed)) {
            if (const auto tip = GetWeaponTipWorldPosition(attacker, leftHand)) {
                auto* referenceRoot = target ? target->Get3D(false) : nullptr;
                auto* referenceChest = GetReferenceChestNode(target);
                if (referenceRoot && referenceChest) {
                    const auto inverse = referenceRoot->world.Invert();
                    const auto localTip = inverse * tip->worldPosition;
                    const auto localChest = inverse * referenceChest->world.translate;
                    const RE::NiPoint3 relativeTip{
                        localTip.x - localChest.x,
                        localTip.y - localChest.y,
                        localTip.z - localChest.z
                    };
                    if (std::isfinite(relativeTip.x) && std::isfinite(relativeTip.y) &&
                        std::isfinite(relativeTip.z)) {
                        return AttackPositionSample{relativeTip, true};
                    }
                }
            }
        }
        // Hand mode, or weapon unavailable/invalid. Both modes use the same
        // adjustable head-height AND horizontal-center-band test for Top.
        const auto hand = GetHandPositionRelativeToReference(attacker, target, leftHand);
        if (!hand) return std::nullopt;
        return AttackPositionSample{*hand, false};
    }
    // Top is deliberately strict in TWO independent dimensions:
    //   1. Sample Z must be above the defender's head plus the height offset.
    //   2. Sample X must be inside the center band around the defender's chest.
    // A high off-center hit is therefore still Right/Left. No cone-angle math.
    // Both Hand and Weapon Tip mode use this exact classifier.
    int ClassifyTrackedPosition(const RE::NiPoint3& relativePosition,
                                const std::optional<float>& headHeightAboveChest,
                                bool leftHand) {
        const float heightOffset = std::clamp(
            AttackTrackingMode::topHeadHeightOffset.load(std::memory_order_relaxed), -100.0f, 100.0f);
        const float centerHalfWidth = std::clamp(
            AttackTrackingMode::topCenterHalfWidth.load(std::memory_order_relaxed), 0.0f, 100.0f);
        if (headHeightAboveChest &&
            relativePosition.z > (*headHeightAboveChest + heightOffset) &&
            std::abs(relativePosition.x) <= centerHalfWidth) {
            return 1;  // Top
        }
        // The defender-relative left/right mapping is identical for all attackers.
        // Preserve the attacking-hand fallback when the selected point is centered.
        if (relativePosition.x > 0.5f) return 3;
        if (relativePosition.x < -0.5f) return 2;
        return leftHand ? 3 : 2;
    }
    // Preserve the original player-relative UI orientation, independent of
    // defender-relative AttackDirectionFHB used for gameplay.
    int ClassifyPlayerIndicatorPosition(const RE::NiPoint3& relativePosition,
                                        const std::optional<float>& headHeightAboveChest,
                                        bool leftHand) {
        const int direction = ClassifyTrackedPosition(relativePosition, headHeightAboveChest, leftHand);
        // Top and centered-hand fallback match the original player classifier.
        // The player HUD's off-center left/right mapping was the reverse of the
        // defender-relative NPC/gameplay mapping.
        if (direction == 1 || std::abs(relativePosition.x) <= 0.5f) return direction;
        return direction == 2 ? 3 : 2;
    }
    std::string ToLower(std::string_view a_value) {
        std::string result;
        result.reserve(a_value.size());
        for (const auto character : a_value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        return result;
    }
    // ========================================================================
    // HUD PROJECTION - reused from the original Honor Combat warning pipeline
    // ========================================================================
    bool HasPlayerAsCombatTarget(RE::Actor* attacker, RE::PlayerCharacter* player) {
        if (!attacker || !player) return false;
        const auto target = attacker->GetActorRuntimeData().currentCombatTarget.get();
        return target && target.get() == player;
    }
    RE::NiAVObject* GetTorsoNode(RE::Actor* actor) {
        auto* root = actor ? actor->Get3D(false) : nullptr;
        if (!root) return nullptr;
        constexpr const char* kTorsoNames[] = {"NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Spine [Spn0]",
                                               "NPC COM [COM ]",    "Bip01 Spine1",      "Bip01 Spine2"};
        for (const auto* name : kTorsoNames) {
            if (auto* node = root->GetObjectByName(RE::BSFixedString(name))) return node;
        }
        return root;
    }
    bool ProjectToScreenPercent(const RE::NiPoint3& worldPosition, float& xPercent, float& yPercent) {
        auto* camera = RE::Main::WorldRootCamera();
        if (!camera) return false;
        float normalizedX = 0.0f;
        float normalizedY = 0.0f;
        float depth = 0.0f;
        if (!camera->WorldPtToScreenPt3(worldPosition, normalizedX, normalizedY, depth, 1e-5f)) return false;
        if (!camera->PointInFrustum(worldPosition, 0.0f) || !std::isfinite(normalizedX) ||
            !std::isfinite(normalizedY) || normalizedX < 0.0f || normalizedX > 1.0f || normalizedY < 0.0f ||
            normalizedY > 1.0f)
            return false;
        xPercent = normalizedX * 100.0f;
        yPercent = (1.0f - normalizedY) * 100.0f;
        return true;
    }
    float GetResolutionScale() {
        if (!Settings::NPCUI.scaleWithResolution) return 1.0f;
        const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
        if (screen.height == 0) return 1.0f;
        return std::clamp(static_cast<float>(screen.height) / 1080.0f, 0.5f, 4.0f);
    }
    // Visual-only diagnostic. Log a change of HUD outcome immediately and
    // repeat unchanged outcomes no more than once per 350ms per NPC.
    void LogNpcHudDecision(RE::Actor* actor, std::string_view outcome, std::int32_t direction) {
        if (!actor) return;
        using Clock = std::chrono::steady_clock;
        static std::mutex logMutex;
        static std::unordered_map<RE::FormID, std::pair<std::string_view, Clock::time_point>> lastByActor;
        const auto now = Clock::now();
        {
            std::scoped_lock lock(logMutex);
            auto& last = lastByActor[actor->GetFormID()];
            if (last.first == outcome && now - last.second < std::chrono::milliseconds(350)) return;
            last = {outcome, now};
        }
        const auto target = actor->GetActorRuntimeData().currentCombatTarget.get();
        const auto* player = RE::PlayerCharacter::GetSingleton();
        SKSE::log::info(
            "[NPC TARGET DEBUG] HUD {} actor={:08X} direction={} inCombat={} target={:08X} targetsPlayer={}",
            outcome, actor->GetFormID(), direction, actor->IsInCombat(),
            target ? target->GetFormID() : 0, player && target && target.get() == player);
    }
    void AppendWarning(RE::Actor* actor, RE::PlayerCharacter* player, float resolutionScale,
                       std::vector<AttackWarningVisual>& warnings) {
        if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsDisabled()) return;
        // Same combat/target gate as before; only its rejection is now observable.
        if (!actor->IsInCombat() || !HasPlayerAsCombatTarget(actor, player)) {
            std::int32_t debugDirection = 0;
            if (actor->GetGraphVariableInt(kAttackDirectionVariable, debugDirection) && debugDirection != 0)
                LogNpcHudDecision(actor, "REJECTED_COMBAT_TARGET", debugDirection);
            return;
        }
        // NPC defensive animation states belong to the NPC, not the current
        // attack direction. They must remain visible even while direction is 0.
        std::int32_t allBlock = 0;
        std::int32_t unblockable = 0;
        std::int32_t parryActive = 0;
        std::int32_t parryWindow = 0;
        std::int32_t parrySuccess = 0;
        actor->GetGraphVariableInt("AllBlockFHB", allBlock);
        actor->GetGraphVariableInt("UnblockableFHB", unblockable);
        actor->GetGraphVariableInt("ParryActiveFHB", parryActive);
        actor->GetGraphVariableInt("ParryWindowFHB", parryWindow);
        actor->GetGraphVariableInt("ParrySuccessFHB", parrySuccess);
        const bool defensiveVisual = allBlock == 1 || parryActive == 1 || parryWindow == 1 || parrySuccess == 1;
        const bool specialVisual = defensiveVisual || unblockable == 1;
        // NPC guards use an independent native state/graph variable.
        // This stays valid even if the NPC animation graph lacks NPCBlockDirectionFHB.
        const std::int32_t blockDirection = NPCDirectionalBlock::GetDirection(actor);
        bool graphBlocking = false;
        actor->GetGraphVariableBool("IsBlocking", graphBlocking);
        const bool blocking = actor->IsBlocking() || graphBlocking || blockDirection != 0;
        std::int32_t rawDirection = 0;
        const bool graphRead = actor->GetGraphVariableInt(kAttackDirectionVariable, rawDirection);
        if (!graphRead || rawDirection < 0 || rawDirection > 3) {
            if (!specialVisual && !blocking) {
                bool trackedWarning = false;
                {
                    std::scoped_lock lock(g_attackDirectionsMutex);
                    trackedWarning = g_activeWarnings.contains(actor->GetFormID());
                }
                if (trackedWarning) LogNpcHudDecision(actor, "REJECTED_GRAPH", rawDirection);
                return;
            }
            // An NPC doesn't require a defensive direction graph variable.
            rawDirection = 0;
        }
        if (rawDirection == 0 && !specialVisual && !blocking && !Settings::NPCUI.alwaysShowInCombat) {
            bool trackedWarning = false;
            {
                std::scoped_lock lock(g_attackDirectionsMutex);
                trackedWarning = g_activeWarnings.contains(actor->GetFormID());
            }
            if (trackedWarning) LogNpcHudDecision(actor, "REJECTED_DIRECTION_ZERO", rawDirection);
            return;
        }
        const auto* torso = GetTorsoNode(actor);
        if (!torso) {
            if (rawDirection != 0) LogNpcHudDecision(actor, "REJECTED_NO_TORSO", rawDirection);
            return;
        }
        float xPercent = 50.0f;
        float yPercent = 50.0f;
        if (!ProjectToScreenPercent(torso->world.translate, xPercent, yPercent)) {
            if (rawDirection != 0) LogNpcHudDecision(actor, "REJECTED_PROJECTION", rawDirection);
            return;
        }
        float distanceScale = 1.0f;
        if (Settings::NPCUI.scaleWithDistance) {
            const auto* camera = RE::Main::WorldRootCamera();
            const auto distance = camera ? camera->world.translate.GetDistance(torso->world.translate) : 300.0f;
            if (std::isfinite(distance)) distanceScale = std::clamp(300.0f / std::max(distance, 300.0f), 0.35f, 1.0f);
        }
        // Keep the NPC's offensive direction for normal warning telegraphs.
        // Defensive feedback is independent of the direction value.
        AttackWarningVisual visual{actor->GetFormID(), rawDirection,  xPercent, yPercent,
                                   resolutionScale,    distanceScale, false,    unblockable == 1};
        visual.allBlock = allBlock == 1;
        visual.parryActive = parryActive == 1;
        visual.parryWindow = parryWindow == 1;
        visual.parrySuccess = parrySuccess == 1;
        visual.blockDirection = blockDirection;
        visual.blocking = blocking;
        warnings.push_back(visual);
        if (rawDirection != 0) LogNpcHudDecision(actor, "EMITTED_TO_PRISMA", rawDirection);
    }
}
AttackPosition* AttackPosition::GetSingleton() {
    static AttackPosition singleton;
    return std::addressof(singleton);
}
void AttackPosition::ResetAttackDirection(RE::Actor* a_actor) {
    if (!a_actor) {
        return;
    }
    bool hadAssignedDirection = false;
    bool hadPlayerIndicator = false;
    {
        std::scoped_lock lock(g_attackDirectionsMutex);
        const auto actorID = a_actor->GetFormID();
        hadAssignedDirection = g_attackDirections.erase(actorID) != 0;
        g_activeWarnings.erase(actorID);
        // Release both independent direction locks at the same attack end.
        g_indicatorDirectionLocked.erase(actorID);
        if (a_actor->IsPlayerRef()) {
            hadPlayerIndicator = g_playerIndicatorDirectionLocked;
            g_playerIndicatorDirectionLocked = false;
        }
        g_swingStarted.erase(actorID);
    }
    if (hadPlayerIndicator) {
        a_actor->SetGraphVariableInt(kPlayerAttackIndicatorVariable, 0);
    }
    // An already ended attack cannot send another graph write or End update.
    if (hadAssignedDirection) {
        const bool graphUpdated = a_actor->SetGraphVariableInt(kAttackDirectionVariable, 0);
        SKSE::log::info("[Attack Direction] End actor={:08X} graphUpdated={}", a_actor->GetFormID(), graphUpdated);
    }
}
void AttackPosition::ResolveAttackDirection(RE::Actor* a_actor, bool a_leftHand) {
    if (!a_actor) {
        return;
    }
    const auto actorID = a_actor->GetFormID();
    // All-block and unblockable animations have their own HUD states. Never
    // assign a normal attack-direction indicator while either is active.
    std::int32_t allBlock = 0;
    std::int32_t unblockable = 0;
    a_actor->GetGraphVariableInt("AllBlockFHB", allBlock);
    a_actor->GetGraphVariableInt("UnblockableFHB", unblockable);
    if (allBlock == 1 || unblockable == 1) {
        ResetAttackDirection(a_actor);
        // ResetAttackDirection deliberately preserves directions owned by an
        // external moveset. Suppress those too for these special states.
        std::int32_t existingDirection = 0;
        if (a_actor->GetGraphVariableInt(kAttackDirectionVariable, existingDirection) && existingDirection != 0) {
            a_actor->SetGraphVariableInt(kAttackDirectionVariable, 0);
        }
        return;
    }
    // Parry animations may emit the same indicator/swing events as attacks.
    // Never assign an attack direction when a parry is active.
    std::int32_t parryActive = 0;
    std::int32_t parryWindow = 0;
    a_actor->GetGraphVariableInt("ParryActiveFHB", parryActive);
    a_actor->GetGraphVariableInt("ParryWindowFHB", parryWindow);
    if (parryActive == 1 || parryWindow == 1) return;
    // The player HUD uses a separate, player-local sample. Do this before
    // looking up the combat target so untargeted swings still show an indicator.
    if (a_actor->IsPlayerRef()) {
        bool shouldResolveIndicator = false;
        {
            std::scoped_lock lock(g_attackDirectionsMutex);
            shouldResolveIndicator = !g_playerIndicatorDirectionLocked;
        }
        if (shouldResolveIndicator) {
            if (const auto indicatorSample = GetTrackedPositionRelativeToReference(a_actor, a_actor, a_leftHand)) {
                const auto playerHeadHeight = GetTargetHeadHeightAboveChest(a_actor);
                const int indicatorDirection = ClassifyPlayerIndicatorPosition(
                    indicatorSample->relativePosition, playerHeadHeight, a_leftHand);
                bool commitIndicator = false;
                {
                    std::scoped_lock lock(g_attackDirectionsMutex);
                    if (!g_playerIndicatorDirectionLocked) {
                        g_playerIndicatorDirectionLocked = true;
                        commitIndicator = true;
                    }
                }
                if (commitIndicator) {
                    const bool updated = a_actor->SetGraphVariableInt(kPlayerAttackIndicatorVariable, indicatorDirection);
                    SKSE::log::debug(
                        "[Player Attack Indicator] direction={} source={} graphUpdated={}",
                        indicatorDirection, indicatorSample->usedWeaponTip ? "weapon-tip" : "hand", updated);
                }
            }
        }
    }
    // Gameplay AttackDirectionFHB remains defender-relative for player and NPC.
    {
        std::scoped_lock lock(g_attackDirectionsMutex);
        if (g_indicatorDirectionLocked.contains(actorID)) {
            SKSE::log::debug("[Attack Direction] RESOLVE_LOCKED actor={:08X} leftHand={}", actorID, a_leftHand);
            return;  // Direction already chosen for this swing.
        }
    }
    // Use the current defender, not the player singleton or the attacker's own chest.
    // This event-time target may be absent, particularly for untargeted player swings.
    const auto targetHandle = a_actor->GetActorRuntimeData().currentCombatTarget.get();
    auto* target = targetHandle.get();
    if (!target || target == a_actor || !target->Is3DLoaded()) {
        SKSE::log::info("[Attack Direction] RESOLVE_NO_TARGET actor={:08X} isPlayer={} inCombat={}",
                        actorID, a_actor->IsPlayerRef(), a_actor->IsInCombat());
        a_actor->SetGraphVariableInt(kAttackDirectionVariable, 0);
        return;
    }
    const auto sample = GetTrackedPositionRelativeToReference(a_actor, target, a_leftHand);
    if (!sample) {
        SKSE::log::info("[Attack Direction] Resolve failed actor={:08X}: {} tracking position not found.",
                        a_actor->GetFormID(), a_leftHand ? "left" : "right");
        return;
    }
    const auto headHeightAboveChest = GetTargetHeadHeightAboveChest(target);
    const int direction = ClassifyTrackedPosition(sample->relativePosition, headHeightAboveChest, a_leftHand);
    {
        std::scoped_lock lock(g_attackDirectionsMutex);
        if (!g_indicatorDirectionLocked.insert(actorID).second) {
            SKSE::log::debug("[Attack Direction] RESOLVE_LOCK_RACE actor={:08X} leftHand={}", actorID, a_leftHand);
            return;  // Another indicator already selected a direction.
        }
        g_attackDirections[actorID] = direction;
        if (!a_actor->IsPlayerRef()) g_activeWarnings[actorID] = a_actor->GetHandle();
    }
    const bool graphUpdated = a_actor->SetGraphVariableInt(kAttackDirectionVariable, direction);
    if (!a_actor->IsPlayerRef()) {
        const auto target = a_actor->GetActorRuntimeData().currentCombatTarget.get();
        const auto* player = RE::PlayerCharacter::GetSingleton();
        SKSE::log::info(
            "[NPC TARGET DEBUG] RESOLVED actor={:08X} direction={} "
            "inCombat={} target={:08X} targetsPlayer={} graphUpdated={}",
            a_actor->GetFormID(), direction, a_actor->IsInCombat(), target ? target->GetFormID() : 0,
            target && target.get() == player, graphUpdated);
    }
    SKSE::log::info(
        "[Attack Direction] {} {:08X} target={:08X}: hand={} source={} relativeX={:.2f} relativeY={:.2f} "
        "heightAboveChest={:.2f} headAvailable={} heightAboveHead={:.2f} "
        "topHeightOffset={:.2f} topCenterHalfWidth={:.2f} direction={} graphUpdated={}.",
        a_actor->IsPlayerRef() ? "Player" : "NPC", a_actor->GetFormID(), target->GetFormID(), a_leftHand ? "L" : "R",
        sample->usedWeaponTip ? "weapon-tip" : "hand", sample->relativePosition.x, sample->relativePosition.y,
        sample->relativePosition.z, headHeightAboveChest.has_value(),
        headHeightAboveChest ? sample->relativePosition.z - *headHeightAboveChest : 0.0f,
        AttackTrackingMode::topHeadHeightOffset.load(std::memory_order_relaxed),
        AttackTrackingMode::topCenterHalfWidth.load(std::memory_order_relaxed), direction, graphUpdated);
}
RE::BSEventNotifyControl AttackPosition::NpcCycleSink::ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                                                    RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {
    if (!a_event || !a_event->holder) {
        return RE::BSEventNotifyControl::kContinue;
    }
    auto* actor = a_event->holder->As<RE::Actor>();
    if (!actor) {
        return RE::BSEventNotifyControl::kContinue;
    }
    try {
        const std::string rawEventName = a_event->tag.c_str();
        const std::string eventName = ToLower(rawEventName);
        // All-block or unblockable may activate after direction selection.
        // Clear a stale normal direction on any following animation event.
        std::int32_t allBlock = 0;
        std::int32_t unblockable = 0;
        actor->GetGraphVariableInt("AllBlockFHB", allBlock);
        actor->GetGraphVariableInt("UnblockableFHB", unblockable);
        if (allBlock == 1 || unblockable == 1) {
            AttackPosition::GetSingleton()->ResetAttackDirection(actor);
            std::int32_t existingDirection = 0;
            if (actor->GetGraphVariableInt(kAttackDirectionVariable, existingDirection) && existingDirection != 0) {
                actor->SetGraphVariableInt(kAttackDirectionVariable, 0);
            }
        }
        SKSE::log::debug("[Attack Direction] Event actor={:08X} tag={}", actor->GetFormID(), rawEventName);
        // Direction is resolved only on indicator or swing events.
        if (eventName == "indicatorstart" || eventName == "leftindicatorstart" ||
            eventName.find("weaponswing") != std::string::npos ||
            eventName.find("weaponleftswing") != std::string::npos) {
            SKSE::log::debug("[Attack Direction] EVENT actor={:08X} tag={}", actor->GetFormID(), rawEventName);
        }
        // Event-driven NPC guard behavior: player attack cue and NPC movement cues.
        // No additional animation sink, update hook, or per-frame random rolls.
        if (actor->IsPlayerRef() && eventName == "prehitframe") {
            NPCDirectionalBlock::OnPlayerPreHitFrame(actor);
        } else if (!actor->IsPlayerRef() &&
                   (eventName == "footleft" || eventName == "footright" ||
                    eventName == "footsprintleft" || eventName == "footsprintright" ||
                    eventName == "footstep")) {
            // Footstep annotations are delivered by the actor's existing graph sink.
            NPCDirectionalBlock::OnFootstep(actor);
        }
        if (eventName == "leftindicatorstart") {
            AttackPosition::GetSingleton()->ResolveAttackDirection(actor, true);
        } else if (eventName == "indicatorstart") {
            AttackPosition::GetSingleton()->ResolveAttackDirection(actor, false);
        } else if (eventName.find("weaponleftswing") != std::string::npos ||
                   eventName.find("weaponswing") != std::string::npos) {
            // The first indicator or weapon swing chooses the direction.
            // ResolveAttackDirection keeps it locked until an attack reset.
            const bool leftHand = eventName.find("weaponleftswing") != std::string::npos;

            bool previousSwing = false;
            {
                std::scoped_lock lock(g_attackDirectionsMutex);
                previousSwing = !g_swingStarted.insert(actor->GetFormID()).second;
            }

            if (previousSwing) {
                AttackPosition::GetSingleton()->ResetAttackDirection(actor);

                std::scoped_lock lock(g_attackDirectionsMutex);
                g_swingStarted.insert(actor->GetFormID());
            }

            AttackPosition::GetSingleton()->ResolveAttackDirection(actor, leftHand);
            // Read AttackDirectionFHB only after the player swing has resolved.
            // NPC guard reactions are event-driven and obey their own cooldown.
            if (actor->IsPlayerRef()) NPCDirectionalBlock::OnPlayerWeaponSwing(actor);
        } else if (eventName == "blockstartout" || eventName == "blockstart") {
            AttackPosition::GetSingleton()->ResetAttackDirection(actor);
            if (!actor->IsPlayerRef()) NPCDirectionalBlock::OnBlockStart(actor);
        } else if (eventName == "blockstop") {
            if (!actor->IsPlayerRef()) NPCDirectionalBlock::OnBlockEnd(actor);
        } else if (eventName == "mco_winopen" || eventName == "mco_powerwinopen") {
            AttackPosition::GetSingleton()->ResetAttackDirection(actor);
        } else if (eventName == "staggerstart" || eventName == "attackstop" || eventName == "sbf_staggerstart") {
            AttackPosition::GetSingleton()->ResetAttackDirection(actor);
            if (eventName == "staggerstart" || eventName == "sbf_staggerstart") {
                if (!actor->IsPlayerRef()) NPCDirectionalBlock::OnBlockEnd(actor);
            }
        }
    } catch (const std::exception& e) {
        SKSE::log::error("[Attack Direction] Event exception for actor {:08X}: {}", actor->GetFormID(), e.what());
    } catch (...) {
        SKSE::log::error("[Attack Direction] Unknown event exception for actor {:08X}.", actor->GetFormID());
    }
    return RE::BSEventNotifyControl::kContinue;
}
AttackPosition::NpcCycleSink* AttackPosition::NpcCycleSink::GetSingleton() {
    static NpcCycleSink singleton;
    return std::addressof(singleton);
}
// ============================================================================
// NPC COMBAT AND LOAD EVENTS
// ============================================================================
RE::BSEventNotifyControl AttackPosition::NpcCombatTracker::ProcessEvent(const RE::TESCombatEvent* a_event,
                                                                        RE::BSTEventSource<RE::TESCombatEvent>*) {
    if (!a_event) return RE::BSEventNotifyControl::kContinue;
    const auto actorPtr = a_event->actor.get();
    auto* actor = actorPtr ? actorPtr->As<RE::Actor>() : nullptr;
    if (!actor || actor->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
    const auto newState = *a_event->newState;
    if (newState == RE::ACTOR_COMBAT_STATE::kCombat) {
        AttackPosition::GetSingleton()->RegisterSink(actor);
    } else if (newState == RE::ACTOR_COMBAT_STATE::kNone) {
        AttackPosition::GetSingleton()->ResetAttackDirection(actor);
        AttackPosition::GetSingleton()->UnregisterSink(actor);
    }
    return RE::BSEventNotifyControl::kContinue;
}
AttackPosition::NpcCombatTracker* AttackPosition::NpcCombatTracker::GetSingleton() {
    static NpcCombatTracker singleton;
    return std::addressof(singleton);
}
void AttackPosition::RegisterNPCAnimationSink(RE::Actor* a_actor) { RegisterSink(a_actor); }
void AttackPosition::RegisterSink(RE::Actor* a_actor) {
    if (!a_actor) return;
    if (a_actor->IsPlayerRef()) {
        if (g_playerSinkRegistered || !a_actor->Is3DLoaded()) return;
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
        a_actor->GetAnimationGraphManager(graphManager);
        if (!graphManager) return;
        g_playerSinkRegistered = a_actor->AddAnimationGraphEventSink(NpcCycleSink::GetSingleton());
        if (g_playerSinkRegistered) {
            SKSE::log::info("[Attack Direction] Player animation sink registered");
        }
        return;
    }
    if (!a_actor->IsInCombat()) return;
    const auto id = a_actor->GetFormID();
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        if (g_trackedNPCs.contains(id)) return;
    }
    RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
    a_actor->GetAnimationGraphManager(graphManager);
    if (!graphManager) {
        QueueRegistration(a_actor);
        return;
    }
    const bool registered = a_actor->AddAnimationGraphEventSink(NpcCycleSink::GetSingleton());
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        if (registered) {
            g_trackedNPCs.insert(id);
            g_pendingRegistrations.erase(id);
        }
    }
    if (!registered) QueueRegistration(a_actor);
    SKSE::log::info("[NPC TARGET DEBUG] REGISTER actor={:08X} success={}", id, registered);
}
void AttackPosition::UnregisterSink(RE::Actor* a_actor) {
    if (!a_actor || a_actor->IsPlayerRef()) return;
    NPCDirectionalBlock::OnBlockEnd(a_actor);
    const auto id = a_actor->GetFormID();
    bool wasRegistered = false;
    {
        std::scoped_lock lock(g_trackedNPCsMutex);
        wasRegistered = g_trackedNPCs.erase(id) != 0;
        g_pendingRegistrations.erase(id);
    }
    if (wasRegistered && a_actor->Is3DLoaded()) {
        a_actor->RemoveAnimationGraphEventSink(NpcCycleSink::GetSingleton());
    }
}
void AttackPosition::RegisterSinksForExistingCombatants() {
        auto* processLists = RE::ProcessLists::GetSingleton();
        if (!processLists) return;
        for (auto& actorHandle : processLists->highActorHandles) {
            auto actorPtr = actorHandle.get();
            auto* actor = actorPtr.get();
            if (actor && !actor->IsPlayerRef() && actor->IsInCombat()) RegisterSink(actor);
        }
    }
    RE::BSEventNotifyControl AttackPosition::PC3DLoadEventHandler::ProcessEvent(
        const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;
        auto* form = RE::TESForm::LookupByID(a_event->formID);
        auto* actor = form ? form->As<RE::Actor>() : nullptr;
        if (!actor || actor->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
        if (a_event->loaded) {
            // The graph may not exist until shortly after TESObjectLoadedEvent.
            // Queue a bounded retry checked by the already-installed update hook.
            if (actor->IsInCombat()) QueueRegistration(actor);
        } else {
            // Clear transient state before the actor's next 3D load.
            AttackPosition::GetSingleton()->ResetAttackDirection(actor);
            AttackPosition::GetSingleton()->UnregisterSink(actor);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
    AttackPosition::PC3DLoadEventHandler* AttackPosition::PC3DLoadEventHandler::GetSingleton() {
        static PC3DLoadEventHandler singleton;
        return std::addressof(singleton);
    }
    // ============================================================================
    // REGISTRATION AND HUD UPDATE - called from the existing plugin lifecycle/hook
    // ============================================================================
    void AttackPosition::Initialize() {
        auto* sources = RE::ScriptEventSourceHolder::GetSingleton();
        if (!sources) {
            SKSE::log::warn("[NPC Attack Direction] Event sources unavailable");
            return;
        }
        if (!npcCombatRegistered_) {
            sources->AddEventSink(NpcCombatTracker::GetSingleton());
            npcCombatRegistered_ = true;
        }
        if (!npcLoadRegistered_) {
            sources->AddEventSink(PC3DLoadEventHandler::GetSingleton());
            npcLoadRegistered_ = true;
        }
        RegisterSinksForExistingCombatants();
        if (auto* player = RE::PlayerCharacter::GetSingleton()) RegisterSink(player);
        SKSE::log::info("[Attack Direction] Initialized (player + NPC event tracking)");
    }
    void AttackPosition::OnGameLoaded() {
        // When reloading a save in the same process, detach any old sinks before
        // forgetting their IDs. Otherwise re-registration may be rejected as a
        // duplicate and the new graph will never send indicator events.
        // A new save/new game can replace the player's animation graph.
        // Detach from the previous graph before attaching to the new one.
        if (g_playerSinkRegistered) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                ResetAttackDirection(player);
                if (player->Is3DLoaded()) player->RemoveAnimationGraphEventSink(NpcCycleSink::GetSingleton());
            }
            g_playerSinkRegistered = false;
        }
        std::vector<RE::FormID> previousTracked;
        {
            std::scoped_lock lock(g_trackedNPCsMutex);
            previousTracked.assign(g_trackedNPCs.begin(), g_trackedNPCs.end());
        }
        for (const auto id : previousTracked) {
            auto* form = RE::TESForm::LookupByID(id);
            auto* actor = form ? form->As<RE::Actor>() : nullptr;
            if (actor && actor->Is3DLoaded()) {
                NPCDirectionalBlock::OnBlockEnd(actor);
                actor->RemoveAnimationGraphEventSink(NpcCycleSink::GetSingleton());
            }
        }
        {
            std::scoped_lock lock(g_attackDirectionsMutex);
            g_attackDirections.clear();
            g_activeWarnings.clear();
            g_indicatorDirectionLocked.clear();
            g_playerIndicatorDirectionLocked = false;
            g_swingStarted.clear();
        }
        {
            std::scoped_lock lock(g_trackedNPCsMutex);
            g_trackedNPCs.clear();
            g_pendingRegistrations.clear();
        }
        Prisma::UpdateAttackWarnings({});
        NPCDirectionalBlock::Reset();
        RegisterSinksForExistingCombatants();
        if (auto* player = RE::PlayerCharacter::GetSingleton()) RegisterSink(player);
    }
    void AttackPosition::UpdateHUD() {
        // Player sink installation is independent of NPC combat status.
        // Retry through the existing update until the player's graph is loaded.
        if (!g_playerSinkRegistered) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) RegisterSink(player);
        }
        // Retry only NPCs whose animation graph was unavailable during load.
        // This uses the existing player update, not a dispatcher or separate hook.
        const auto now = std::chrono::steady_clock::now();
        std::vector<RE::ActorHandle> readyToTry;
        {
            std::scoped_lock lock(g_trackedNPCsMutex);
            for (auto it = g_pendingRegistrations.begin(); it != g_pendingRegistrations.end();) {
                if (now < it->second.nextAttempt) {
                    ++it;
                    continue;
                }
                if (it->second.attempts >= kMaxRegisterAttempts) {
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
        for (const auto& handle : readyToTry) {
            auto actorPtr = handle.get();
            auto* actor = actorPtr.get();
            if (actor && actor->IsInCombat()) RegisterSink(actor);
        }
        // NPC blocking intelligence runs through the existing update hook,
        // regardless of whether the NPC HUD is enabled.
        if (auto* processLists = RE::ProcessLists::GetSingleton()) {
            for (auto& handle : processLists->highActorHandles) {
                auto actorPtr = handle.get();
                auto* npc = actorPtr.get();
                if (npc && !npc->IsPlayerRef()) {
                    NPCDirectionalBlock::Update(npc);
                }
            }
        }
        // No NPC GUI can be derived directly from a JavaScript animation graph.
        // Read each actor's graph direction in C++, then send visual state to Prisma.
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (!Settings::NPCUI.enabled || !player || player->IsDead() || (ui && ui->GameIsPaused())) {
            Prisma::UpdateAttackWarnings({});
            return;
        }
        std::vector<AttackWarningVisual> visuals;
        const float resolutionScale = GetResolutionScale();
        if (Settings::NPCUI.alwaysShowInCombat) {
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                for (auto& handle : lists->highActorHandles) {
                    auto actorPtr = handle.get();
                    AppendWarning(actorPtr.get(), player, resolutionScale, visuals);
                }
            }
        } else {
            std::vector<RE::ActorHandle> activeActors;
            {
                std::scoped_lock lock(g_attackDirectionsMutex);
                activeActors.reserve(g_activeWarnings.size());
                for (const auto& [id, handle] : g_activeWarnings) activeActors.push_back(handle);
            }
            // The existing active-warning map only contains attacking NPCs.
            // Also inspect already-tracked combatants so all-block/parries appear
            // even when the NPC has no attack-direction indicator.
            std::vector<RE::FormID> trackedNPCs;
            {
                std::scoped_lock lock(g_trackedNPCsMutex);
                trackedNPCs.assign(g_trackedNPCs.begin(), g_trackedNPCs.end());
            }
            std::unordered_set<RE::FormID> visited;
            for (const auto& handle : activeActors) {
                auto actorPtr = handle.get();
                auto* actor = actorPtr.get();
                if (actor && visited.insert(actor->GetFormID()).second)
                    AppendWarning(actor, player, resolutionScale, visuals);
            }
            for (const auto id : trackedNPCs) {
                if (!visited.insert(id).second) continue;
                auto* form = RE::TESForm::LookupByID(id);
                auto* actor = form ? form->As<RE::Actor>() : nullptr;
                if (actor && actor->Is3DLoaded()) AppendWarning(actor, player, resolutionScale, visuals);
            }
        }
        std::ranges::sort(visuals, {}, &AttackWarningVisual::id);
        if (visuals.size() > 32) visuals.resize(32);
        Prisma::UpdateAttackWarnings(visuals);
    }
