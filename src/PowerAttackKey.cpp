#include <ShlObj.h>
#include <SimpleIni.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include "SKSEMenuFramework.h"
#include "PowerAttackKey.h"
#include <string_view>
using namespace RE;
using namespace SKSE;
extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace {
    using InputEvents = InputEvent*;
    constexpr uint32_t kDefaultAltPowerAttackKey = 257;      // Mouse Right
    constexpr uint32_t kDefaultAltLeftPowerAttackKey = 259;  // Mouse Button 4
    constexpr uint32_t kKeyDisabled = 0;                     // 0 = key disabled
    constexpr uint32_t kModifierNone = 0;                    // 0 = no modifier
    constexpr uint32_t kMacroNumKeyboardKeys = 256;
    constexpr uint32_t kMacroNumMouseButtons = 8;
    constexpr uint32_t kMacroMouseWheelOffset = kMacroNumKeyboardKeys + kMacroNumMouseButtons;
    constexpr uint32_t kMacroGamepadOffset = kMacroMouseWheelOffset + 2;
    constexpr uint32_t kMaxMacros = kMacroGamepadOffset + 16;
    constexpr uint32_t kMouseLeftMacro = kMacroNumKeyboardKeys;  // 256 == Mouse Left (used to interact with the menu)
    constexpr uint32_t kEscapeScancode = 0x01;                   // DirectInput scancode for Escape (cancels capture)
    constexpr uint32_t kCaptureCancelled = 0xFFFFFFFFu;          // sentinel: capture ended without a new binding
    // Identifies which binding the "press a key" capture is currently listening for.
    enum class CaptureTarget : uint32_t {
        kNone = 0,
        kRightKey,
        kLeftKey,
        kDualKey,
        kRightMod,
        kLeftMod,
        kDualMod,
    };
    // Skyrim gamepad button IDs (from ButtonEvent::GetIDCode for kGamepad device).
    // These match the values the game engine uses internally regardless of controller type,
    // so they work with Xbox, PS5 DualSense, and any other controller via Steam Input.
    constexpr uint32_t kGamepadDpadUp = 0x0001;
    constexpr uint32_t kGamepadDpadDown = 0x0002;
    constexpr uint32_t kGamepadDpadLeft = 0x0004;
    constexpr uint32_t kGamepadDpadRight = 0x0008;
    constexpr uint32_t kGamepadStart = 0x0010;
    constexpr uint32_t kGamepadBack = 0x0020;
    constexpr uint32_t kGamepadLeftThumb = 0x0040;
    constexpr uint32_t kGamepadRightThumb = 0x0080;
    constexpr uint32_t kGamepadLeftShoulder = 0x0100;
    constexpr uint32_t kGamepadRightShoulder = 0x0200;
    constexpr uint32_t kGamepadA = 0x1000;
    constexpr uint32_t kGamepadB = 0x2000;
    constexpr uint32_t kGamepadX = 0x4000;
    constexpr uint32_t kGamepadY = 0x8000;
    constexpr uint32_t kGamepadLT = 0x0009;
    constexpr uint32_t kGamepadRT = 0x000A;
    constexpr const wchar_t* kPrimaryConfigFileName = L"SimplePowerAttack.ini";
    const TaskInterface* g_task = nullptr;
    BGSAction* g_rightPowerAttackAction = nullptr;
    BGSAction* g_leftPowerAttackAction = nullptr;
    BGSAction* g_dualPowerAttackAction = nullptr;
    BGSAction* g_rightAttackAction = nullptr;
    BGSAction* g_leftAttackAction = nullptr;
    std::atomic<uint32_t> g_altPowerAttackKey = kDefaultAltPowerAttackKey;
    uint32_t g_pendingAltPowerAttackKey = kDefaultAltPowerAttackKey;
    bool g_hasUnsavedKeyChange = false;
    std::atomic<uint32_t> g_altLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
    uint32_t g_pendingAltLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
    bool g_hasUnsavedLeftKeyChange = false;
    std::atomic<uint32_t> g_altDualPowerAttackKey = kKeyDisabled;
    uint32_t g_pendingAltDualPowerAttackKey = kKeyDisabled;
    bool g_hasUnsavedDualKeyChange = false;
    // Unified modifiers: a single slot per action that can hold a keyboard key OR a gamepad
    // button, since g_keyStates tracks every device by macro keycode.
    std::atomic<uint32_t> g_rightModifier = kModifierNone;
    uint32_t g_pendingRightModifier = kModifierNone;
    bool g_hasUnsavedRightMod = false;
    std::atomic<uint32_t> g_leftModifier = kModifierNone;
    uint32_t g_pendingLeftModifier = kModifierNone;
    bool g_hasUnsavedLeftMod = false;
    std::atomic<uint32_t> g_dualModifier = kModifierNone;
    uint32_t g_pendingDualModifier = kModifierNone;
    bool g_hasUnsavedDualMod = false;
    std::atomic<bool> g_mcoMode = false;
    bool g_pendingMcoMode = false;
    bool g_hasUnsavedMcoChange = false;
    std::atomic<bool> g_noStaminaPowerAttack = false;
    bool g_pendingNoStaminaPowerAttack = false;
    bool g_hasUnsavedNoStaminaChange = false;
    std::atomic<bool> g_pluginEnabled = true;
    bool g_pendingPluginEnabled = true;
    bool g_hasUnsavedEnabledChange = false;
    SKSEMenuFramework::Model::InputEvent* g_menuFrameworkInputHook = nullptr;
    SKSEMenuFramework::Model::Event* g_menuFrameworkEventHook = nullptr;
    std::atomic<CaptureTarget> g_captureTarget = CaptureTarget::kNone;
    std::atomic<uint32_t> g_capturedCode = kCaptureCancelled;
    CaptureTarget g_renderActiveCapture = CaptureTarget::kNone;
    // Controller-agnostic key state tracking, updated from input events each frame.
    // Indexed by macro keycode (0..kMaxMacros-1). Works with any controller type.
    std::array<std::atomic<bool>, kMaxMacros> g_keyStates{};
    CSimpleIniA g_ini(true, false, false);
    REL::Relocation<Setting*> g_initialPowerAttackDelay{RELOCATION_ID(509496, 381954)};
    // Phase: 0 idle, 1 waiting for block exit, 2 action queued for dispatch.
    std::atomic<int> g_blockAttackPhase{0};
    // Suppress the power-attack key's held input until its release is observed.
    // This matters when the configured power key also maps to Skyrim's block key.
    std::atomic<std::uint32_t> g_suppressedPowerKey{kKeyDisabled};

    // Origin of the next attack. Keep this separate from g_blockAttackPhase:
    // that phase is only for SPA's deferred block exit and can reach zero
    // before an attack animation has finished.
    constexpr const char* kBlockAttackGraphVariable = "FHB_BlockPowerAttack";
    constexpr ULONGLONG kBlockAttackStartTimeoutMs = 2500;
    constexpr ULONGLONG kBlockAttackEndTimeoutMs = 5000;
    std::atomic_bool g_fromBlockAttackActive{false};
    std::atomic_bool g_fromBlockAttackStarted{false};
    std::atomic<ULONGLONG> g_fromBlockAttackDeadline{0};

    bool IsActuallyBlocking(PlayerCharacter* player) {
        if (!player) return false;
        bool graphBlocking = false;
        return player->IsBlocking() ||
               (player->GetGraphVariableBool("IsBlocking", graphBlocking) && graphBlocking);
    }

    void ResetFromBlockAttack(PlayerCharacter* player, const char* reason) {
        const bool wasActive = g_fromBlockAttackActive.exchange(false);
        g_fromBlockAttackStarted.store(false);
        g_fromBlockAttackDeadline.store(0);
        if (player) {
            player->SetGraphVariableInt(kBlockAttackGraphVariable, 0);
        }
        if (wasActive) {
            SKSE::log::info("[FHB BLOCK ATTACK] {} = 0 | {}", kBlockAttackGraphVariable, reason);
        }
    }

    void BeginFromBlockAttack(PlayerCharacter* player, const char* kind) {
        // Do NOT use wantBlocking/iWantBlock here. SimpleBlock intentionally
        // retains those requests while MCO combos are in progress.
        if (!IsActuallyBlocking(player)) return;
        if (!player->SetGraphVariableInt(kBlockAttackGraphVariable, 1)) {
            SKSE::log::warn("[FHB BLOCK ATTACK] Cannot set {}; check Behavior Injector", kBlockAttackGraphVariable);
            return;
        }
        g_fromBlockAttackStarted.store(false);
        g_fromBlockAttackDeadline.store(GetTickCount64() + kBlockAttackStartTimeoutMs);
        g_fromBlockAttackActive.store(true);
        SKSE::log::info("[FHB BLOCK ATTACK] {} = 1 | {} initiated while blocking", kBlockAttackGraphVariable, kind);
    }

    void ExpireFromBlockAttack(PlayerCharacter* player) {
        if (!g_fromBlockAttackActive.load()) return;
        if (GetTickCount64() >= g_fromBlockAttackDeadline.load()) {
            ResetFromBlockAttack(player, "timeout (missing start/stop event)");
        }
    }

    void LoadConfig();
    void TriggerPowerAttack(PlayerCharacter* player);
    void TriggerLeftPowerAttack(PlayerCharacter* player);
    void TriggerDualPowerAttack(PlayerCharacter* player);
    std::filesystem::path GetPrimaryConfigPath() {
        wchar_t exePath[MAX_PATH]{};
        const auto written = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (written > 0 && written < MAX_PATH) {
            std::filesystem::path path(exePath);
            path.remove_filename();
            path /= L"Data\\\SKSE\\\Plugins";
            path /= kPrimaryConfigFileName;
            return path;
        }
        return std::filesystem::path(L"Data\\\SKSE\\\Plugins") / kPrimaryConfigFileName;
    }
    uint32_t GamepadMaskToKeycode(uint32_t keyMask) {
        switch (keyMask) {
            case kGamepadDpadUp:
                return kMacroGamepadOffset;
            case kGamepadDpadDown:
                return kMacroGamepadOffset + 1;
            case kGamepadDpadLeft:
                return kMacroGamepadOffset + 2;
            case kGamepadDpadRight:
                return kMacroGamepadOffset + 3;
            case kGamepadStart:
                return kMacroGamepadOffset + 4;
            case kGamepadBack:
                return kMacroGamepadOffset + 5;
            case kGamepadLeftThumb:
                return kMacroGamepadOffset + 6;
            case kGamepadRightThumb:
                return kMacroGamepadOffset + 7;
            case kGamepadLeftShoulder:
                return kMacroGamepadOffset + 8;
            case kGamepadRightShoulder:
                return kMacroGamepadOffset + 9;
            case kGamepadA:
                return kMacroGamepadOffset + 10;
            case kGamepadB:
                return kMacroGamepadOffset + 11;
            case kGamepadX:
                return kMacroGamepadOffset + 12;
            case kGamepadY:
                return kMacroGamepadOffset + 13;
            case kGamepadLT:
                return kMacroGamepadOffset + 14;
            case kGamepadRT:
                return kMacroGamepadOffset + 15;
            default:
                return kMaxMacros;
        }
    }
    uint32_t ToMacroKeyCode(const ButtonEvent* event) {
        const auto keyMask = event->GetIDCode();
        if (event->device.get() == INPUT_DEVICE::kMouse) {
            return kMacroNumKeyboardKeys + keyMask;
        }
        if (event->device.get() == INPUT_DEVICE::kGamepad) {
            return GamepadMaskToKeycode(keyMask);
        }
        return keyMask;
    }
    bool IsPlayerInValidCombatState(PlayerCharacter* player) {
        if (!player) {
            return false;
        }
        const auto actorState = player->AsActorState();
        if (!actorState) {
            return false;
        }
        // states
        if (actorState->GetWeaponState() != WEAPON_STATE::kDrawn) {
            return false;
        }
        if (actorState->actorState1.sitSleepState != SIT_SLEEP_STATE::kNormal) {
            return false;
        }
        if (actorState->actorState1.knockState != KNOCK_STATE_ENUM::kNormal) {
            return false;
        }
        if (actorState->actorState1.flyState != FLY_STATE::kNone) {
            return false;
        }
        if (player->IsInKillMove()) {
            return false;
        }
        TESForm* equippedObj = player->GetEquippedObject(false);
        if (equippedObj) {
            auto* weap = skyrim_cast<TESObjectWEAP*>(equippedObj);
            if (!weap) return false;  // spell or non-weapon in right hand
            if (weap->IsBow() || weap->IsStaff() || weap->IsCrossbow()) return false;
        }
        return true;
    }
    bool IsPlayerSprinting(PlayerCharacter* player) {
        if (!player) {
            return false;
        }
        bool sprinting = false;
        if (player->GetGraphVariableBool("IsSprinting", sprinting)) {
            return sprinting;
        }
        const auto actorState = player->AsActorState();
        return actorState && actorState->actorState1.sprinting;
    }
    bool IsDualWielding(PlayerCharacter* player) {
        auto* rightForm = player->GetEquippedObject(false);
        auto* leftForm = player->GetEquippedObject(true);
        if (!rightForm || !leftForm) return false;
        auto* rightWeap = skyrim_cast<TESObjectWEAP*>(rightForm);
        auto* leftWeap = skyrim_cast<TESObjectWEAP*>(leftForm);
        if (!rightWeap || !leftWeap) return false;
        auto isOneHandedMelee = [](TESObjectWEAP* w) {
            return w->IsOneHandedSword() || w->IsOneHandedDagger() || w->IsOneHandedAxe() || w->IsOneHandedMace();
        };
        return isOneHandedMelee(rightWeap) && isOneHandedMelee(leftWeap);
    }
    bool IsLeftHandValidForPowerAttack(PlayerCharacter* player) {
        TESForm* leftObj = player->GetEquippedObject(true);
        if (!leftObj) return false;
        auto* weap = skyrim_cast<TESObjectWEAP*>(leftObj);
        if (!weap) return false;
        if (weap->IsBow() || weap->IsStaff() || weap->IsCrossbow()) return false;
        return true;
    }
    bool IsGameplayInputAllowed() {
        auto* ui = UI::GetSingleton();
        auto* controlMap = ControlMap::GetSingleton();
        if (!ui || !controlMap) return false;
        const uint32_t requiredFlags = static_cast<uint32_t>(UserEvents::USER_EVENT_FLAG::kMovement);
        if (ui->numPausesGame > 0) return false;
        if ((controlMap->GetRuntimeData().enabledControls.underlying() & requiredFlags) != requiredFlags) return false;
        if (ui->IsMenuOpen("Dialogue Menu"sv)) return false;
        if (SKSEMenuFramework::IsAnyBlockingWindowOpened()) return false;
        return true;
    }
    bool IsModifierHeld(uint32_t modifierKey) {
        if (modifierKey == kModifierNone) return true;
        if (modifierKey >= kMaxMacros) return false;
        return g_keyStates[modifierKey].load(std::memory_order_relaxed);
    }
    enum class PowerAttackKind : uint32_t {
        kNone = 0,
        kRight,
        kLeft,
        kDual,
    };
    PowerAttackKind ClassifyPowerAttack(uint32_t keyCode) {
        if (!g_pluginEnabled.load()) return PowerAttackKind::kNone;
        const uint32_t rightKey = g_altPowerAttackKey.load();
        const uint32_t leftKey = g_altLeftPowerAttackKey.load();
        const uint32_t dualKey = g_altDualPowerAttackKey.load();
        const bool isRightKey = (rightKey != kKeyDisabled) && (keyCode == rightKey);
        const bool isLeftKey = (leftKey != kKeyDisabled) && (keyCode == leftKey);
        const bool isDualKey = (dualKey != kKeyDisabled) && (keyCode == dualKey);
        if (!isRightKey && !isLeftKey && !isDualKey) return PowerAttackKind::kNone;
        auto* player = PlayerCharacter::GetSingleton();
        if (!player || !IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player)) return PowerAttackKind::kNone;
        if (g_noStaminaPowerAttack.load() && player->AsActorValueOwner()->GetActorValue(ActorValue::kStamina) <= 0.0f) {
            return PowerAttackKind::kNone;
        }
        if (isDualKey && g_dualPowerAttackAction && IsModifierHeld(g_dualModifier.load()) && IsDualWielding(player)) {
            return PowerAttackKind::kDual;
        }
        if (isRightKey && g_rightPowerAttackAction && IsModifierHeld(g_rightModifier.load())) {
            return PowerAttackKind::kRight;
        }
        if (isLeftKey && g_leftPowerAttackAction && IsLeftHandValidForPowerAttack(player) &&
            IsModifierHeld(g_leftModifier.load())) {
            return PowerAttackKind::kLeft;
        }
        return PowerAttackKind::kNone;
    }
    bool TryPowerAttack(uint32_t keyCode) {
        auto* player = PlayerCharacter::GetSingleton();
        if (!player) return false;
        switch (ClassifyPowerAttack(keyCode)) {
            case PowerAttackKind::kDual:
                TriggerDualPowerAttack(player);
                return true;
            case PowerAttackKind::kRight:
                TriggerPowerAttack(player);
                return true;
            case PowerAttackKind::kLeft:
                TriggerLeftPowerAttack(player);
                return true;
            default:
                return false;
        }
    }
    void HandleCaptureInput(uint32_t macroKey) {
        if (macroKey == kEscapeScancode) {
            g_capturedCode.store(kCaptureCancelled, std::memory_order_relaxed);
        } else {
            g_capturedCode.store(macroKey, std::memory_order_relaxed);
        }
        g_captureTarget.store(CaptureTarget::kNone, std::memory_order_release);
    }
    bool HandleButtonEvent(ButtonEvent* button) {
        if (!button || !g_pluginEnabled.load()) {
            return false;
        }
        const uint32_t keyCode = ToMacroKeyCode(button);
        const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;
        return isDown && TryPowerAttack(keyCode);
    }
    void ProcessInputChain(RE::InputEvent* events) {
        for (auto* event = events; event; event = event->next) {
            if (event->eventType.get() != INPUT_EVENT_TYPE::kButton) {
                continue;
            }
            auto* button = event->AsButtonEvent();
            if (!button) continue;
            const uint32_t macroKey = ToMacroKeyCode(button);
            if (macroKey < kMaxMacros) {
                g_keyStates[macroKey].store(button->Value() != 0.0f, std::memory_order_relaxed);
            }
            // The first press was already consumed by SPA. Do not let subsequent
            // held events from the same key restart blocking before it is released.
            // Let the release event reach the game normally.
            if (macroKey == g_suppressedPowerKey.load(std::memory_order_relaxed) && macroKey != kKeyDisabled) {
                if (button->Value() != 0.0f) {
                    button->GetRuntimeData().value = 0.0f;
                    continue;
                }
                g_suppressedPowerKey.store(kKeyDisabled, std::memory_order_relaxed);
            }
            // Press-to-bind: while listening, the next fresh button-down becomes the new binding.
            // Mouse Left is skipped so the user can still click menu buttons to cancel/switch.
            if (g_captureTarget.load(std::memory_order_acquire) != CaptureTarget::kNone) {
                const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;
                if (isDown && macroKey < kMaxMacros && macroKey != kMouseLeftMacro) {
                    HandleCaptureInput(macroKey);
                    button->GetRuntimeData().value = 0.0f;  // consume so it doesn't leak to game/menu
                }
                continue;  // suppress gameplay handling while capturing
            }
            const bool consumed = HandleButtonEvent(button);
            if (consumed) {
                if (g_blockAttackPhase.load(std::memory_order_relaxed) != 0) {
                    g_suppressedPowerKey.store(macroKey, std::memory_order_relaxed);
                }
                if (auto* player = PlayerCharacter::GetSingleton()) {
                    const auto* state = player->AsActorState();
                    SKSE::log::info("[SPA INPUT] power bind={} consumed={} nativeBlocking={} wantBlocking={}", macroKey,
                                    consumed, player->IsBlocking(),
                                    state ? static_cast<std::uint32_t>(state->actorState2.wantBlocking) : 0u);
                }
                button->GetRuntimeData().value = 0.0f;
            }
        }
    }
    // Diagnostics only: snapshot native and graph state at the point of an attack request.
    void LogCombatState(PlayerCharacter* player, const char* stage) {
        if (!player) return;
        auto* state = player->AsActorState();
        if (!state) return;

        bool graphBlocking = false;
        bool graphBashing = false;
        bool attacking = false;
        bool recovering = false;
        int wantBlockGraph = -1;

        const bool hasBlocking = player->GetGraphVariableBool("IsBlocking", graphBlocking);
        const bool hasBashing = player->GetGraphVariableBool("IsBashing", graphBashing);
        const bool hasAttacking = player->GetGraphVariableBool("IsAttacking", attacking);
        const bool hasRecovery = player->GetGraphVariableBool("MCO_IsInRecovery", recovering);
        const bool hasWantBlock = player->GetGraphVariableInt("iWantBlock", wantBlockGraph);

        SKSE::log::info(
            "[SPA STATE] {} | nativeBlocking={} wantBlocking={} "
            "graphBlocking={}/{} graphBashing={}/{} attacking={}/{} "
            "recovery={}/{} iWantBlock={}/{}",
            stage, player->IsBlocking(), static_cast<std::uint32_t>(state->actorState2.wantBlocking), hasBlocking,
            graphBlocking, hasBashing, graphBashing, hasAttacking, attacking, hasRecovery, recovering, hasWantBlock,
            wantBlockGraph);
    }

    void ClearBlockingState(PlayerCharacter* player) {
        if (!player) return;
        auto* state = player->AsActorState();
        if (!state) return;
        if (!player->IsBlocking() && !state->actorState2.wantBlocking) return;

        // Stop requesting block, then let the graph exit on its own.
        // Do not force IsBlocking/IsBashing: they reflect actual graph state.
        state->actorState2.wantBlocking = false;
        player->SetGraphVariableInt("iWantBlock", 0);
        const bool stopped = player->NotifyAnimationGraph("blockStop");

        SKSE::log::info("[SPA] Block cancellation | blockStop={} blocking={} wantBlocking={}", stopped,
                        player->IsBlocking(), static_cast<std::uint32_t>(state->actorState2.wantBlocking));
    }

    // Block-to-power transition, advanced by PlayerCharacter::Update (not input ticks).
    std::atomic<unsigned> g_blockAttackTicks{0};
    std::atomic<ULONGLONG> g_blockAttackDeadline{0};
    std::atomic<BGSAction*> g_pendingBlockPowerAction{nullptr};
    std::atomic<BGSAction*> g_pendingBlockSprintAction{nullptr};

    void ExecutePowerAction(PlayerCharacter* player, BGSAction* powerAction, BGSAction* sprintAction) {
        if (!player || !powerAction) return;
        BGSAction* action = (sprintAction && IsPlayerSprinting(player)) ? sprintAction : powerAction;
        std::unique_ptr<TESActionData> data(TESActionData::Create());
        if (!data) {
            SKSE::log::error("[SPA TRANSITION] TESActionData::Create failed");
            if (g_blockAttackPhase.load() != 0) {
                ResetFromBlockAttack(player, "TESActionData creation failed");
            }
            return;
        }
        data->source = NiPointer<TESObjectREFR>(player);
        data->action = action;
        using ProcessAction_t = bool (*)(TESActionData*);
        REL::Relocation<ProcessAction_t> processAction{RELOCATION_ID(40551, 41557)};
        const bool accepted = processAction(data.get());
        if (!accepted && g_blockAttackPhase.load() != 0) {
            ResetFromBlockAttack(player, "deferred ProcessAction rejected");
        }
        SKSE::log::info("[SPA TRANSITION] ProcessAction accepted={} actionFormID={:08X}", accepted,
                        action->GetFormID());
        LogCombatState(player, "post deferred power action");
        // Do not synthesize Hitframe: prior logs show the graph rejects it.
    }

    void PerformAction(PlayerCharacter* player, BGSAction* powerAction, BGSAction* sprintAction) {
        if (!g_task || !powerAction || !player) return;
        auto* state = player->AsActorState();
        const bool blocking = state && (player->IsBlocking() || state->actorState2.wantBlocking);
        if (g_blockAttackPhase.load(std::memory_order_acquire) != 0) {
            SKSE::log::info("[SPA TRANSITION] Ignored power attack; transition already active");
            return;
        }
        // Do not carry the out-of-block flag into an ordinary combo follow-up.
        // SimpleBlock can still request blocking when the actor isn't blocking.
        if (!IsActuallyBlocking(player) && g_fromBlockAttackActive.load()) {
            ResetFromBlockAttack(player, "follow-up power attack outside actual block");
        }
        if (!blocking) {
            g_task->AddTask(
                [player, powerAction, sprintAction]() { ExecutePowerAction(player, powerAction, sprintAction); });
            return;
        }
        // Capture actual guard BEFORE SPA's deferred blockStop clears it.
        // A held block request alone does not count as an out-of-block attack.
        BeginFromBlockAttack(player, "power");
        g_pendingBlockPowerAction.store(powerAction);
        g_pendingBlockSprintAction.store(sprintAction);
        g_blockAttackTicks.store(0);
        g_blockAttackDeadline.store(GetTickCount64() + 1000);
        g_blockAttackPhase.store(1);
        SKSE::log::info("[SPA TRANSITION] Starting block exit before power attack");
        g_task->AddTask([player]() { ClearBlockingState(player); });
    }

    // Called after PlayerCharacter::Update, once per actual game update.
    // Never issue an attack until native AND graph blocking have ended.
    void AdvanceBlockAttackTransition(PlayerCharacter* player) {
        if (g_blockAttackPhase.load(std::memory_order_acquire) != 1) return;
        if (!player || player != PlayerCharacter::GetSingleton()) return;

        const unsigned frames = g_blockAttackTicks.fetch_add(1, std::memory_order_relaxed) + 1;
        const bool expired = GetTickCount64() >= g_blockAttackDeadline.load();
        const auto* state = player->AsActorState();

        if (!g_pluginEnabled.load() || !IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player) || !state ||
            expired) {
            SKSE::log::warn("[SPA TRANSITION] Cancelled block-to-power (frames={}, expired={})", frames, expired);
            g_pendingBlockPowerAction.store(nullptr);
            g_pendingBlockSprintAction.store(nullptr);
            g_blockAttackPhase.store(0, std::memory_order_release);
            ResetFromBlockAttack(player, "deferred block exit cancelled");
            return;
        }

        // Do not run on the same update that picked up the power input.
        if (frames < 2) return;

        bool graphBlocking = false;
        const bool hasGraphBlocking = player->GetGraphVariableBool("IsBlocking", graphBlocking);
        if (player->IsBlocking() || (hasGraphBlocking && graphBlocking) || state->actorState2.wantBlocking) {
            return;
        }

        int expected = 1;
        if (!g_blockAttackPhase.compare_exchange_strong(expected, 2)) return;

        auto* power = g_pendingBlockPowerAction.load();
        auto* sprint = g_pendingBlockSprintAction.load();
        SKSE::log::info("[SPA TRANSITION] Block ended after {} player updates; dispatch queued", frames);

        // ProcessAction already used the SKSE task queue in the original mod.
        g_task->AddTask([player, power, sprint]() {
            bool graphBlockingNow = false;
            const bool hasGraphBlockingNow = player->GetGraphVariableBool("IsBlocking", graphBlockingNow);
            const auto* currentState = player->AsActorState();
            const bool blockedAgain = player->IsBlocking() || (hasGraphBlockingNow && graphBlockingNow) ||
                                      (currentState && currentState->actorState2.wantBlocking);

            if (g_pluginEnabled.load() && !blockedAgain && IsGameplayInputAllowed() &&
                IsPlayerInValidCombatState(player)) {
                ExecutePowerAction(player, power, sprint);
            } else {
                SKSE::log::warn("[SPA TRANSITION] Cancelled queued power action: block/input state changed");
                ResetFromBlockAttack(player, "queued power action cancelled");
            }

            g_pendingBlockPowerAction.store(nullptr);
            g_pendingBlockSprintAction.store(nullptr);
            g_blockAttackPhase.store(0, std::memory_order_release);
            // Do not re-send blockStop or force graph state after ProcessAction.
        });
    }
    void TriggerPowerAttack(PlayerCharacter* player) {
        PerformAction(player, g_rightPowerAttackAction, g_rightAttackAction);
    }
    void TriggerLeftPowerAttack(PlayerCharacter* player) {
        PerformAction(player, g_leftPowerAttackAction, g_leftAttackAction);
    }
    void TriggerDualPowerAttack(PlayerCharacter* player) {
        PerformAction(player, g_dualPowerAttackAction, g_rightAttackAction);
    }
    uint32_t SanitizeKeyCode(uint32_t keyCode) {
        if (keyCode >= kMaxMacros) {
            return kKeyDisabled;
        }
        return keyCode;
    }
    uint32_t SanitizeUnifiedModifier(uint32_t keyCode) {
        if (keyCode == kModifierNone) return kModifierNone;
        if (keyCode >= 1 && keyCode < kMacroNumKeyboardKeys) return keyCode;         // keyboard key
        if (keyCode >= kMacroGamepadOffset && keyCode < kMaxMacros) return keyCode;  // gamepad button/trigger
        return kModifierNone;
    }
    bool IsMeleeWeaponInHand(PlayerCharacter* player, bool leftHand) {
        if (!player) return false;
        auto* actorState = player->AsActorState();
        if (!actorState || actorState->GetWeaponState() != WEAPON_STATE::kDrawn) return false;
        TESForm* obj = player->GetEquippedObject(leftHand);
        if (!obj) return false;
        auto* weap = skyrim_cast<TESObjectWEAP*>(obj);
        if (!weap) return false;
        if (weap->IsBow() || weap->IsCrossbow() || weap->IsStaff()) return false;
        return true;
    }
    struct AttackBlockHook {
        static void Install() {
            REL::Relocation<std::uintptr_t> vtbl{VTABLE_AttackBlockHandler[0]};
            _ProcessButton =
                reinterpret_cast<ProcessButton_t>(vtbl.write_vfunc(GetProcessButtonIndex(), Hook_ProcessButton));
        }

    private:
        static std::size_t GetProcessButtonIndex() {
            return REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99) ? 0x6 : 0x4;
        }
        using ProcessButton_t = void (*)(AttackBlockHandler*, ButtonEvent*, PlayerControlsData*);
        static void Hook_ProcessButton(AttackBlockHandler* a_this, ButtonEvent* a_event, PlayerControlsData* a_data) {
            // Diagnostic only. Do not suppress block input or change handler state.
            auto* player = PlayerCharacter::GetSingleton();
            auto* actorState = player ? player->AsActorState() : nullptr;
            auto* userEvents = UserEvents::GetSingleton();
            const bool isLeftAttack = a_event && userEvents && a_event->GetUserEvent() == userEvents->leftAttack;
            const bool isRightAttack = a_event && userEvents && a_event->GetUserEvent() == userEvents->rightAttack;
            // Log events while blocking, plus attack button events outside of block.
            const bool trace =
                a_event && (isLeftAttack || isRightAttack ||
                            (actorState && (player->IsBlocking() || actorState->actorState2.wantBlocking)));
            const auto beforeWant = actorState ? static_cast<std::uint32_t>(actorState->actorState2.wantBlocking) : 0u;
            const bool beforeBlocking = player && player->IsBlocking();

            bool filteredHeldAttack = false;
            if (a_event && g_pluginEnabled.load() && (isLeftAttack || isRightAttack)) {
                const float threshold = g_initialPowerAttackDelay->data.f;
                const float held = a_event->HeldDuration();
                filteredHeldAttack = a_event->Value() != 0.0f && held > 0.0f && held >= threshold &&
                                     IsMeleeWeaponInHand(player, isLeftAttack);
            }
            if (trace) {
                SKSE::log::info(
                    "[SPA INPUT] before | id={} side={} value={} held={} down={} up={} filtered={} blocking={} "
                    "wantBlocking={}",
                    a_event->GetIDCode(), isLeftAttack ? "leftAttack" : (isRightAttack ? "rightAttack" : "other"),
                    a_event->Value(), a_event->HeldDuration(), a_event->IsDown(), a_event->IsUp(), filteredHeldAttack,
                    beforeBlocking, beforeWant);
            }
            if (filteredHeldAttack) return;
            // Capture LIGHT/left/right attack origin before any blockStop.
            if (a_event && a_event->IsDown() && (isRightAttack || isLeftAttack)) {
                if (IsActuallyBlocking(player)) {
                    BeginFromBlockAttack(player, "normal");
                } else if (g_fromBlockAttackActive.load() && g_blockAttackPhase.load() == 0) {
                    ResetFromBlockAttack(player, "follow-up normal attack outside actual block");
                }
            }
            // Preserve FHB's existing block interruption behavior.
            if (isRightAttack && a_event && a_event->IsDown() && player && player->IsBlocking()) {
                const bool stopped = player->NotifyAnimationGraph("blockStop");

                SKSE::log::info("[BLOCK BEHAVIOR] Attack from block | blockStop={} blocking={}", stopped,
                                player->IsBlocking());
            }  // Keep SPA's existing held-attack filter unchanged.
            _ProcessButton(a_this, a_event, a_data);
            if (trace && actorState) {
                SKSE::log::info("[SPA INPUT] after  | blocking={} wantBlocking={} deltaWant={}", player->IsBlocking(),
                                static_cast<std::uint32_t>(actorState->actorState2.wantBlocking),
                                beforeWant != static_cast<std::uint32_t>(actorState->actorState2.wantBlocking));
            }
        }
        static inline ProcessButton_t _ProcessButton = nullptr;
    };
    // Skyrim AE 1.6.1170: PlayerCharacter::Update vtable slot 0xAD.
    // An update hook is required so a tap of the power key advances the queued
    // attack even if no more ButtonEvents arrive.
    struct PlayerUpdateHook {
        static void Install() {
            REL::Relocation<std::uintptr_t> vtbl{VTABLE_PlayerCharacter[0]};
            _Update = reinterpret_cast<Update_t>(vtbl.write_vfunc(0xAD, Hook_Update));
            SKSE::log::info("[SPA TRANSITION] PlayerCharacter::Update hook installed");
        }

    private:
        using Update_t = void (*)(PlayerCharacter*, float);
        static void Hook_Update(PlayerCharacter* player, float delta) {
            _Update(player, delta);
            AdvanceBlockAttackTransition(player);
            ExpireFromBlockAttack(player);
        }
        static inline Update_t _Update = nullptr;
    };

    bool SaveConfig(uint32_t keyCode, uint32_t leftKeyCode, uint32_t dualKeyCode, uint32_t rightMod, uint32_t leftMod,
                    uint32_t dualMod, bool mcoMode, bool noStaminaPA, bool pluginEnabled) {
        keyCode = SanitizeKeyCode(keyCode);
        leftKeyCode = SanitizeKeyCode(leftKeyCode);
        dualKeyCode = SanitizeKeyCode(dualKeyCode);
        rightMod = SanitizeUnifiedModifier(rightMod);
        leftMod = SanitizeUnifiedModifier(leftMod);
        dualMod = SanitizeUnifiedModifier(dualMod);
        const std::filesystem::path savePath = GetPrimaryConfigPath();
        g_ini.Reset();
        g_ini.SetBoolValue("General", "bEnabled", pluginEnabled);
        g_ini.SetLongValue("General", "iKeycode", static_cast<long>(keyCode));
        g_ini.SetLongValue("General", "iLeftKeycode", static_cast<long>(leftKeyCode));
        g_ini.SetLongValue("General", "iDualKeycode", static_cast<long>(dualKeyCode));
        g_ini.SetLongValue("General", "iRightMod", static_cast<long>(rightMod));
        g_ini.SetLongValue("General", "iLeftMod", static_cast<long>(leftMod));
        g_ini.SetLongValue("General", "iDualMod", static_cast<long>(dualMod));
        g_ini.SetBoolValue("MCO", "bMCOMode", mcoMode);
        g_ini.SetBoolValue("General", "bPowerAttackNoStamina", noStaminaPA);
        try {
            std::filesystem::create_directories(savePath.parent_path());
        } catch (const std::exception& e) {
            SKSE::log::error("Failed to create config directory: {}", e.what());
            g_ini.Reset();
            return false;
        }
        const SI_Error saveResult = g_ini.SaveFile(savePath.c_str());
        g_ini.Reset();
        if (saveResult < 0) {
            SKSE::log::error("Failed to save config to '{}'", savePath.string());
            return false;
        }
        SKSE::log::info(
            "Saved config enabled={} right={} left={} dual={} rmod={} lmod={} dmod={} mco={} noStaminaPA={} to '{}'",
            pluginEnabled, keyCode, leftKeyCode, dualKeyCode, rightMod, leftMod, dualMod, mcoMode, noStaminaPA,
            savePath.string());
        return true;
    }
    const char* GetMacroKeyName(uint32_t code, bool isModifier) {
        if (code == kKeyDisabled) {
            return isModifier ? "None" : "Not Set";
        }
        if (code >= kMacroNumKeyboardKeys && code < kMacroGamepadOffset) {
            static const char* mouseNames[] = {"Mouse Left",     "Mouse Right",     "Mouse Middle", "Mouse 4",
                                               "Mouse 5",        "Mouse 6",         "Mouse 7",      "Mouse 8",
                                               "Mouse Wheel Up", "Mouse Wheel Down"};
            const uint32_t idx = code - kMacroNumKeyboardKeys;
            if (idx < (sizeof(mouseNames) / sizeof(mouseNames[0]))) return mouseNames[idx];
            return "Mouse ?";
        }
        if (code >= kMacroGamepadOffset && code < kMaxMacros) {
            static const char* gamepadNames[] = {"D-Pad Up",
                                                 "D-Pad Down",
                                                 "D-Pad Left",
                                                 "D-Pad Right",
                                                 "Start",
                                                 "Back",
                                                 "Left Thumb (LS)",
                                                 "Right Thumb (RS)",
                                                 "Left Shoulder (LB)",
                                                 "Right Shoulder (RB)",
                                                 "A",
                                                 "B",
                                                 "X",
                                                 "Y",
                                                 "Left Trigger (LT)",
                                                 "Right Trigger (RT)"};
            const uint32_t idx = code - kMacroGamepadOffset;
            if (idx < (sizeof(gamepadNames) / sizeof(gamepadNames[0]))) return gamepadNames[idx];
            return "Gamepad ?";
        }
        // Keyboard DirectInput scancodes (1..255)
        if (code < kMacroNumKeyboardKeys) {
            static const char* keyNames[] = {
                "None",     "Escape", "1",          "2",     "3",     "4",         "5",           "6",
                "7",        "8",      "9",          "0",     "-",     "=",         "Backspace",   "Tab",
                "Q",        "W",      "E",          "R",     "T",     "Y",         "U",           "I",
                "O",        "P",      "[",          "]",     "Enter", "Left Ctrl", "A",           "S",
                "D",        "F",      "G",          "H",     "J",     "K",         "L",           ";",
                "'",        "`",      "Left Shift", "\\\\\\\\",  "Z",     "X",         "C",           "V",
                "B",        "N",      "M",          ",",     ".",     "/",         "Right Shift", "Num *",
                "Left Alt", "Space",  "Caps Lock",  "F1",    "F2",    "F3",        "F4",          "F5",
                "F6",       "F7",     "F8",         "F9",    "F10",   "Num Lock",  "Scroll Lock", "Num 7",
                "Num 8",    "Num 9",  "Num -",      "Num 4", "Num 5", "Num 6",     "Num +",       "Num 1",
                "Num 2",    "Num 3",  "Num 0",      "Num .", nullptr, nullptr,     nullptr,       "F11",
                "F12"};
            if (code < (sizeof(keyNames) / sizeof(keyNames[0])) && keyNames[code]) {
                return keyNames[code];
            }
            switch (code) {
                case 0x9C:
                    return "Num Enter";
                case 0x9D:
                    return "Right Ctrl";
                case 0xB5:
                    return "Num /";
                case 0xB8:
                    return "Right Alt";
                case 0xC7:
                    return "Home";
                case 0xC8:
                    return "Up Arrow";
                case 0xC9:
                    return "Page Up";
                case 0xCB:
                    return "Left Arrow";
                case 0xCD:
                    return "Right Arrow";
                case 0xCF:
                    return "End";
                case 0xD0:
                    return "Down Arrow";
                case 0xD1:
                    return "Page Down";
                case 0xD2:
                    return "Insert";
                case 0xD3:
                    return "Delete";
                default:
                    break;
            }
            return "Unknown Key";
        }
        return "Unknown";
    }
    // Maps a capture target to the render-thread state it edits.
    struct PendingBinding {
        uint32_t* pending;
        std::atomic<uint32_t>* committed;
        bool* unsaved;
        bool isModifier;
    };
    PendingBinding GetBinding(CaptureTarget target) {
        switch (target) {
            case CaptureTarget::kRightKey:
                return {&g_pendingAltPowerAttackKey, &g_altPowerAttackKey, &g_hasUnsavedKeyChange, false};
            case CaptureTarget::kLeftKey:
                return {&g_pendingAltLeftPowerAttackKey, &g_altLeftPowerAttackKey, &g_hasUnsavedLeftKeyChange, false};
            case CaptureTarget::kDualKey:
                return {&g_pendingAltDualPowerAttackKey, &g_altDualPowerAttackKey, &g_hasUnsavedDualKeyChange, false};
            case CaptureTarget::kRightMod:
                return {&g_pendingRightModifier, &g_rightModifier, &g_hasUnsavedRightMod, true};
            case CaptureTarget::kLeftMod:
                return {&g_pendingLeftModifier, &g_leftModifier, &g_hasUnsavedLeftMod, true};
            case CaptureTarget::kDualMod:
                return {&g_pendingDualModifier, &g_dualModifier, &g_hasUnsavedDualMod, true};
            default:
                return {nullptr, nullptr, nullptr, false};
        }
    }
    void StartCapture(CaptureTarget target) {
        g_renderActiveCapture = target;
        g_capturedCode.store(kCaptureCancelled, std::memory_order_relaxed);
        g_captureTarget.store(target, std::memory_order_release);
    }
    void CancelCapture() {
        g_renderActiveCapture = CaptureTarget::kNone;
        g_captureTarget.store(CaptureTarget::kNone, std::memory_order_release);
    }
    // Render thread: if the input thread finished a capture, apply the result to the pending value.
    void PollCaptureResult() {
        if (g_renderActiveCapture == CaptureTarget::kNone) return;
        if (g_captureTarget.load(std::memory_order_acquire) != CaptureTarget::kNone) return;  // still listening
        const uint32_t raw = g_capturedCode.load(std::memory_order_relaxed);
        const CaptureTarget target = g_renderActiveCapture;
        g_renderActiveCapture = CaptureTarget::kNone;
        if (raw == kCaptureCancelled) return;
        const PendingBinding b = GetBinding(target);
        if (!b.pending) return;
        const uint32_t sanitized = b.isModifier ? SanitizeUnifiedModifier(raw) : SanitizeKeyCode(raw);
        *b.pending = sanitized;
        *b.unsaved = (sanitized != b.committed->load());
    }
    // Draws a "press to bind" button plus a Clear button for one binding.
    void DrawBindButton(const char* idSuffix, CaptureTarget target) {
        const PendingBinding b = GetBinding(target);
        if (!b.pending) return;
        const bool listening = (g_renderActiveCapture == target);
        std::string label = listening ? std::string(">> Press any key / button...  (Esc to cancel) <<")
                                      : std::string(GetMacroKeyName(*b.pending, b.isModifier));
        label += "##";
        label += idSuffix;
        if (ImGuiMCP::Button(label.c_str(), ImGuiMCP::ImVec2{300.0f, 0.0f})) {
            if (listening) {
                CancelCapture();
            } else {
                StartCapture(target);
            }
        }
        ImGuiMCP::SameLine();
        std::string clearLabel = std::string("Clear##clr_") + idSuffix;
        if (ImGuiMCP::Button(clearLabel.c_str())) {
            *b.pending = kKeyDisabled;
            *b.unsaved = (kKeyDisabled != b.committed->load());
            if (listening) CancelCapture();
        }
    }
    void DrawBindRow(const char* rowLabel, const char* idSuffix, CaptureTarget target) {
        ImGuiMCP::Text(rowLabel);
        ImGuiMCP::SameLine(170.0f);
        DrawBindButton(idSuffix, target);
    }
    void __stdcall RenderMenuFrameworkSection() {
        PollCaptureResult();
        ImGuiMCP::Text("Simple Power Attack");
        ImGuiMCP::Separator();
        ImGuiMCP::TextWrapped(
            "Click a binding, then press any keyboard key, mouse button, or controller button to assign it. "
            "Press Esc to cancel. Works with keyboard, mouse, and any controller.");
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Right Power Attack");
        DrawBindRow("Key / Button", "right_key", CaptureTarget::kRightKey);
        DrawBindRow("Modifier", "right_mod", CaptureTarget::kRightMod);
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Left Power Attack");
        DrawBindRow("Key / Button", "left_key", CaptureTarget::kLeftKey);
        DrawBindRow("Modifier", "left_mod", CaptureTarget::kLeftMod);
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Dual Power Attack");
        DrawBindRow("Key / Button", "dual_key", CaptureTarget::kDualKey);
        DrawBindRow("Modifier", "dual_mod", CaptureTarget::kDualMod);
        ImGuiMCP::Separator();
        bool chosenMco = g_pendingMcoMode;
        if (ImGuiMCP::Checkbox("MCO Compatibility", &chosenMco)) {
            g_pendingMcoMode = chosenMco;
            g_hasUnsavedMcoChange = (g_pendingMcoMode != g_mcoMode.load());
        }
        bool chosenNoStamina = g_pendingNoStaminaPowerAttack;
        if (ImGuiMCP::Checkbox("Disable Power Attack at 0 Stamina", &chosenNoStamina)) {
            g_pendingNoStaminaPowerAttack = chosenNoStamina;
            g_hasUnsavedNoStaminaChange = (g_pendingNoStaminaPowerAttack != g_noStaminaPowerAttack.load());
        }
        bool disableChecked = !g_pendingPluginEnabled;
        if (ImGuiMCP::Checkbox("Disable Plugin", &disableChecked)) {
            g_pendingPluginEnabled = !disableChecked;
            g_hasUnsavedEnabledChange = (g_pendingPluginEnabled != g_pluginEnabled.load());
        }
        const bool hasUnsaved = g_hasUnsavedKeyChange || g_hasUnsavedLeftKeyChange || g_hasUnsavedDualKeyChange ||
                                g_hasUnsavedRightMod || g_hasUnsavedLeftMod || g_hasUnsavedDualMod ||
                                g_hasUnsavedMcoChange || g_hasUnsavedNoStaminaChange || g_hasUnsavedEnabledChange;
        ImGuiMCP::Separator();
        if (hasUnsaved) {
            ImGuiMCP::TextWrapped("You have unsaved changes.");
        }
        if (ImGuiMCP::Button("Save to INI")) {
            const uint32_t saveCode = SanitizeKeyCode(g_pendingAltPowerAttackKey);
            const uint32_t saveLeftCode = SanitizeKeyCode(g_pendingAltLeftPowerAttackKey);
            const uint32_t saveDualCode = SanitizeKeyCode(g_pendingAltDualPowerAttackKey);
            const uint32_t saveRMod = SanitizeUnifiedModifier(g_pendingRightModifier);
            const uint32_t saveLMod = SanitizeUnifiedModifier(g_pendingLeftModifier);
            const uint32_t saveDMod = SanitizeUnifiedModifier(g_pendingDualModifier);
            const bool saveMco = g_pendingMcoMode;
            const bool saveNoStamina = g_pendingNoStaminaPowerAttack;
            const bool saveEnabled = g_pendingPluginEnabled;
            if (SaveConfig(saveCode, saveLeftCode, saveDualCode, saveRMod, saveLMod, saveDMod, saveMco, saveNoStamina,
                           saveEnabled)) {
                g_altPowerAttackKey = saveCode;
                g_pendingAltPowerAttackKey = saveCode;
                g_hasUnsavedKeyChange = false;
                g_altLeftPowerAttackKey = saveLeftCode;
                g_pendingAltLeftPowerAttackKey = saveLeftCode;
                g_hasUnsavedLeftKeyChange = false;
                g_altDualPowerAttackKey = saveDualCode;
                g_pendingAltDualPowerAttackKey = saveDualCode;
                g_hasUnsavedDualKeyChange = false;
                g_rightModifier = saveRMod;
                g_pendingRightModifier = saveRMod;
                g_hasUnsavedRightMod = false;
                g_leftModifier = saveLMod;
                g_pendingLeftModifier = saveLMod;
                g_hasUnsavedLeftMod = false;
                g_dualModifier = saveDMod;
                g_pendingDualModifier = saveDMod;
                g_hasUnsavedDualMod = false;
                g_mcoMode = saveMco;
                g_pendingMcoMode = saveMco;
                g_hasUnsavedMcoChange = false;
                g_noStaminaPowerAttack = saveNoStamina;
                g_pendingNoStaminaPowerAttack = saveNoStamina;
                g_hasUnsavedNoStaminaChange = false;
                g_pluginEnabled = saveEnabled;
                g_pendingPluginEnabled = saveEnabled;
                g_hasUnsavedEnabledChange = false;
            }
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Discard")) {
            CancelCapture();
            g_pendingAltPowerAttackKey = g_altPowerAttackKey.load();
            g_hasUnsavedKeyChange = false;
            g_pendingAltLeftPowerAttackKey = g_altLeftPowerAttackKey.load();
            g_hasUnsavedLeftKeyChange = false;
            g_pendingAltDualPowerAttackKey = g_altDualPowerAttackKey.load();
            g_hasUnsavedDualKeyChange = false;
            g_pendingRightModifier = g_rightModifier.load();
            g_hasUnsavedRightMod = false;
            g_pendingLeftModifier = g_leftModifier.load();
            g_hasUnsavedLeftMod = false;
            g_pendingDualModifier = g_dualModifier.load();
            g_hasUnsavedDualMod = false;
            g_pendingMcoMode = g_mcoMode.load();
            g_hasUnsavedMcoChange = false;
            g_pendingNoStaminaPowerAttack = g_noStaminaPowerAttack.load();
            g_hasUnsavedNoStaminaChange = false;
            g_pendingPluginEnabled = g_pluginEnabled.load();
            g_hasUnsavedEnabledChange = false;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Reload from INI")) {
            CancelCapture();
            LoadConfig();
        }
    }
    void LoadConfig() {
        const std::filesystem::path path = GetPrimaryConfigPath();
        const SI_Error result = g_ini.LoadFile(path.c_str());
        if (result < 0) {
            SKSE::log::warn("Config not found at '{}', using defaults", path.string());
            g_altPowerAttackKey = kDefaultAltPowerAttackKey;
            g_pendingAltPowerAttackKey = kDefaultAltPowerAttackKey;
            g_hasUnsavedKeyChange = false;
            g_altLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
            g_pendingAltLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
            g_hasUnsavedLeftKeyChange = false;
            g_altDualPowerAttackKey = kKeyDisabled;
            g_pendingAltDualPowerAttackKey = kKeyDisabled;
            g_hasUnsavedDualKeyChange = false;
            g_rightModifier = kModifierNone;
            g_pendingRightModifier = kModifierNone;
            g_hasUnsavedRightMod = false;
            g_leftModifier = kModifierNone;
            g_pendingLeftModifier = kModifierNone;
            g_hasUnsavedLeftMod = false;
            g_dualModifier = kModifierNone;
            g_pendingDualModifier = kModifierNone;
            g_hasUnsavedDualMod = false;
            g_mcoMode = false;
            g_pendingMcoMode = false;
            g_hasUnsavedMcoChange = false;
            g_noStaminaPowerAttack = false;
            g_pendingNoStaminaPowerAttack = false;
            g_hasUnsavedNoStaminaChange = false;
            g_pluginEnabled = true;
            g_pendingPluginEnabled = true;
            g_hasUnsavedEnabledChange = false;
            g_ini.Reset();
            return;
        }
        const long rawValue = g_ini.GetLongValue("General", "iKeycode", static_cast<long>(kDefaultAltPowerAttackKey));
        uint32_t parsedKey = SanitizeKeyCode(static_cast<uint32_t>(rawValue < 0 ? 0 : rawValue));
        const long rawLeftValue =
            g_ini.GetLongValue("General", "iLeftKeycode", static_cast<long>(kDefaultAltLeftPowerAttackKey));
        uint32_t parsedLeftKey = SanitizeKeyCode(static_cast<uint32_t>(rawLeftValue < 0 ? 0 : rawLeftValue));
        g_altPowerAttackKey = parsedKey;
        g_pendingAltPowerAttackKey = parsedKey;
        g_hasUnsavedKeyChange = false;
        g_altLeftPowerAttackKey = parsedLeftKey;
        g_pendingAltLeftPowerAttackKey = parsedLeftKey;
        g_hasUnsavedLeftKeyChange = false;
        const long rawDualValue = g_ini.GetLongValue("General", "iDualKeycode", static_cast<long>(kKeyDisabled));
        uint32_t parsedDualKey = SanitizeKeyCode(static_cast<uint32_t>(rawDualValue < 0 ? 0 : rawDualValue));
        g_altDualPowerAttackKey = parsedDualKey;
        g_pendingAltDualPowerAttackKey = parsedDualKey;
        g_hasUnsavedDualKeyChange = false;
        auto loadUnifiedModifier = [&](const char* newKey, const char* oldKbKey, const char* oldGpKey) -> uint32_t {
            long v = g_ini.GetLongValue("General", newKey, -1);
            if (v < 0) {
                const long gp = g_ini.GetLongValue("General", oldGpKey, 0);
                v = (gp != 0) ? gp : g_ini.GetLongValue("General", oldKbKey, 0);
            }
            if (v < 0) v = 0;
            return SanitizeUnifiedModifier(static_cast<uint32_t>(v));
        };
        const uint32_t parsedRMod = loadUnifiedModifier("iRightMod", "iRightModifier", "iRightGamepadModifier");
        const uint32_t parsedLMod = loadUnifiedModifier("iLeftMod", "iLeftModifier", "iLeftGamepadModifier");
        const uint32_t parsedDMod = loadUnifiedModifier("iDualMod", "iDualModifier", "iDualGamepadModifier");
        g_rightModifier = parsedRMod;
        g_pendingRightModifier = parsedRMod;
        g_hasUnsavedRightMod = false;
        g_leftModifier = parsedLMod;
        g_pendingLeftModifier = parsedLMod;
        g_hasUnsavedLeftMod = false;
        g_dualModifier = parsedDMod;
        g_pendingDualModifier = parsedDMod;
        g_hasUnsavedDualMod = false;
        const bool parsedMco = g_ini.GetBoolValue("MCO", "bMCOMode", false);
        g_mcoMode = parsedMco;
        g_pendingMcoMode = parsedMco;
        g_hasUnsavedMcoChange = false;
        const bool parsedNoStamina = g_ini.GetBoolValue("General", "bPowerAttackNoStamina", false);
        g_noStaminaPowerAttack = parsedNoStamina;
        g_pendingNoStaminaPowerAttack = parsedNoStamina;
        g_hasUnsavedNoStaminaChange = false;
        const bool parsedEnabled = g_ini.GetBoolValue("General", "bEnabled", true);
        g_pluginEnabled = parsedEnabled;
        g_pendingPluginEnabled = parsedEnabled;
        g_hasUnsavedEnabledChange = false;
        g_ini.Reset();
        SKSE::log::info(
            "Loaded config enabled={} right={} left={} dual={} rmod={} lmod={} dmod={} mco={} noStaminaPA={} from '{}'",
            parsedEnabled, parsedKey, parsedLeftKey, parsedDualKey, parsedRMod, parsedLMod, parsedDMod, parsedMco,
            parsedNoStamina, path.string());
    }
    bool __stdcall OnMenuFrameworkInput(RE::InputEvent* events) {
        if (events) ProcessInputChain(events);
        return false;
    }
    void __stdcall OnMenuFrameworkEvent(SKSEMenuFramework::Model::EventType eventType) {
        if (eventType == SKSEMenuFramework::Model::kCloseMenu) {
            // Stop listening for a keybind and drop any unsaved edits back to the saved values.
            CancelCapture();
            if (g_hasUnsavedKeyChange) {
                g_pendingAltPowerAttackKey = g_altPowerAttackKey.load();
                g_hasUnsavedKeyChange = false;
            }
            if (g_hasUnsavedLeftKeyChange) {
                g_pendingAltLeftPowerAttackKey = g_altLeftPowerAttackKey.load();
                g_hasUnsavedLeftKeyChange = false;
            }
            if (g_hasUnsavedDualKeyChange) {
                g_pendingAltDualPowerAttackKey = g_altDualPowerAttackKey.load();
                g_hasUnsavedDualKeyChange = false;
            }
            if (g_hasUnsavedRightMod) {
                g_pendingRightModifier = g_rightModifier.load();
                g_hasUnsavedRightMod = false;
            }
            if (g_hasUnsavedLeftMod) {
                g_pendingLeftModifier = g_leftModifier.load();
                g_hasUnsavedLeftMod = false;
            }
            if (g_hasUnsavedDualMod) {
                g_pendingDualModifier = g_dualModifier.load();
                g_hasUnsavedDualMod = false;
            }
            if (g_hasUnsavedMcoChange) {
                g_pendingMcoMode = g_mcoMode.load();
                g_hasUnsavedMcoChange = false;
            }
            if (g_hasUnsavedNoStaminaChange) {
                g_pendingNoStaminaPowerAttack = g_noStaminaPowerAttack.load();
                g_hasUnsavedNoStaminaChange = false;
            }
            if (g_hasUnsavedEnabledChange) {
                g_pendingPluginEnabled = g_pluginEnabled.load();
                g_hasUnsavedEnabledChange = false;
            }
        }
    }
    std::filesystem::path GetLogPath() {
        wchar_t* docPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docPath))) {
            std::filesystem::path logDir(docPath);
            CoTaskMemFree(docPath);
            logDir /= "My Games";
            logDir /= "Skyrim Special Edition";
            logDir /= "SKSE";
            std::filesystem::create_directories(logDir);
            return logDir / "SimplePowerAttack.log";
        }
        if (docPath) CoTaskMemFree(docPath);
        char modulePath[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
        std::filesystem::path fallback(modulePath);
        fallback.replace_filename("SimplePowerAttack.log");
        return fallback;
    }
    void InitializeLogging() {
        auto logPath = GetLogPath();
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        std::shared_ptr<spdlog::logger> log;
        if (IsDebuggerPresent()) {
            auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            log = std::make_shared<spdlog::logger>("Global", spdlog::sinks_init_list{fileSink, msvcSink});
        } else {
            log = std::make_shared<spdlog::logger>("Global", fileSink);
        }
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    }
}
extern "C" __declspec(dllexport) bool SimplePowerAttack_IsChordActive(uint32_t a_macroKey) {
    return ClassifyPowerAttack(a_macroKey) != PowerAttackKind::kNone;
}
namespace PowerAttackKey {

    void Initialize() { g_task = SKSE::GetTaskInterface(); }

    void OnDataLoaded() {
        g_rightPowerAttackAction = TESForm::LookupByID<BGSAction>(0x13383);

        if (!g_rightPowerAttackAction) {
            SKSE::log::critical("Failed to resolve ActionRightPowerAttack (0x13383).");
            return;
        }

        g_leftPowerAttackAction = TESForm::LookupByID<BGSAction>(0x02E2F6);

        g_dualPowerAttackAction = TESForm::LookupByID<BGSAction>(0x2E2F7);

        g_rightAttackAction = TESForm::LookupByID<BGSAction>(0x13005);

        g_leftAttackAction = TESForm::LookupByID<BGSAction>(0x13004);

        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::critical("SKSE Menu Framework not installed.");
            return;
        }

        AttackBlockHook::Install();
        PlayerUpdateHook::Install();

        LoadConfig();

        SKSEMenuFramework::SetSection("SimplePowerAttack");

        SKSEMenuFramework::AddSectionItem("Settings", RenderMenuFrameworkSection);

        if (!g_menuFrameworkInputHook) {
            g_menuFrameworkInputHook = SKSEMenuFramework::AddInputEvent(OnMenuFrameworkInput);
        }

        if (!g_menuFrameworkEventHook) {
            g_menuFrameworkEventHook = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);
        }

        SKSE::log::info("[POWER ATTACK] Initialized | right={} left={} dual={}", g_altPowerAttackKey.load(),
                        g_altLeftPowerAttackKey.load(), g_altDualPowerAttackKey.load());
    }

    bool IsBlockExitPending() { return g_blockAttackPhase.load(std::memory_order_acquire) != 0; }

    void OnPlayerAnimationEvent(std::string_view tag) {
        if (!g_fromBlockAttackActive.load()) return;

        // An earlier attackStop may fire immediately after leaving block.
        // Do not clear the flag until THIS attack has started.
        if (tag == "attackStart" ||
            tag == "MCO_WinOpen" || tag == "MCO_PowerWinOpen") {
            g_fromBlockAttackStarted.store(true);
            g_fromBlockAttackDeadline.store(GetTickCount64() + kBlockAttackEndTimeoutMs);
        } else if (tag == "attackStop" && g_fromBlockAttackStarted.load()) {
            ResetFromBlockAttack(PlayerCharacter::GetSingleton(), "attackStop");
        }
    }

    void OnGameLoaded() {
        // C++ static state survives save/reload even if the graph is recreated.
        ResetFromBlockAttack(PlayerCharacter::GetSingleton(), "new game/save loaded");
    }
}
