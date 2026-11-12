#include "PowerAttackKey.h"

#include <ShlObj.h>
#include <SimpleIni.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <algorithm>
#include <cmath>
#include <string_view>

#include "AttackPosition.h"
#include "InputTracking.h"
#include "Prisma.h"
#include "RE/S/ScriptEventSourceHolder.h"
#include "RE/T/TESHitEvent.h"
#include "SKSE/InputMap.h"
#include "SKSEMenuFramework.h"
#include "TDMLockBridge.h"
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
        kRightGamepadKey,
        kLeftGamepadKey,
        kDualGamepadKey,
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
    // ============================================================================
    // POWER ATTACK SETTINGS: active values and pending changes from the menu.
    // These bindings and options are saved to SimplePowerAttack.ini.
    // ============================================================================
    std::atomic<uint32_t> g_altPowerAttackKey = kDefaultAltPowerAttackKey;
    uint32_t g_pendingAltPowerAttackKey = kDefaultAltPowerAttackKey;
    bool g_hasUnsavedKeyChange = false;
    std::atomic<uint32_t> g_altLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
    uint32_t g_pendingAltLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
    bool g_hasUnsavedLeftKeyChange = false;
    std::atomic<uint32_t> g_altDualPowerAttackKey = kKeyDisabled;
    uint32_t g_pendingAltDualPowerAttackKey = kKeyDisabled;
    bool g_hasUnsavedDualKeyChange = false;
    // Optional second binding for each action. Both device bindings stay active.
    std::atomic<uint32_t> g_rightGamepadPowerAttackKey{kKeyDisabled};
    uint32_t g_pendingRightGamepadPowerAttackKey = kKeyDisabled;
    bool g_hasUnsavedRightGamepadKeyChange = false;
    std::atomic<uint32_t> g_leftGamepadPowerAttackKey{kKeyDisabled};
    uint32_t g_pendingLeftGamepadPowerAttackKey = kKeyDisabled;
    bool g_hasUnsavedLeftGamepadKeyChange = false;
    std::atomic<uint32_t> g_dualGamepadPowerAttackKey{kKeyDisabled};
    uint32_t g_pendingDualGamepadPowerAttackKey = kKeyDisabled;
    bool g_hasUnsavedDualGamepadKeyChange = false;
    // Retain the original modifier slots for keyboard/mouse bindings only.
    // Controller buttons bypass modifier checks entirely.
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
    // Additional minimum stamina to START a power attack. 0 disables this threshold.
    // The existing zero-stamina checkbox remains independent for backward compatibility.
    constexpr float kMaxMinimumPowerAttackStamina = 50.0f;
    std::atomic<float> g_minimumPowerAttackStamina{0.0f};
    float g_pendingMinimumPowerAttackStamina = 0.0f;
    bool g_hasUnsavedMinimumPowerAttackStaminaChange = false;
    std::atomic<bool> g_pluginEnabled = true;
    bool g_pendingPluginEnabled = true;
    bool g_hasUnsavedEnabledChange = false;
    // ========================== END POWER ATTACK SETTINGS ==========================
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
    // Any incoming hit/reaction invalidates action requests created before it.
    // This also covers queued power attacks that did not begin from blocking.
    std::atomic<std::uint64_t> g_attackCancelGeneration{0};
    // Light from guard: 0 idle, 1 backward-normal waiting for block exit,
    // 2 normal action queued, 3 action submitted / awaiting start.
    // Parry light inputs use the immediate path; power attack handling is unchanged.
    // BlockKey already calls IsBlockExitPending() before restoring guard.
    std::atomic<int> g_lightBlockAttackPhase{0};
    std::atomic<unsigned> g_lightBlockAttackTicks{0};
    std::atomic<ULONGLONG> g_lightBlockAttackDeadline{0};
    std::atomic<BGSAction*> g_pendingBlockLightAction{nullptr};
    // Which AttackBlockHandler user event we consumed: 0 none, 1 right, 2 left.
    // Ignore its held/release events, but don't interfere with subsequent combo inputs.
    std::atomic<int> g_suppressedLightAttackSide{0};
    constexpr ULONGLONG kLightBlockExitTimeoutMs = 1000;
    constexpr ULONGLONG kLightAttackStartTimeoutMs = 1500;
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
        return player->IsBlocking() || (player->GetGraphVariableBool("IsBlocking", graphBlocking) && graphBlocking);
    }

    // This is the exact name exposed by the DirecionalCycleMoveset animation graph.
    // Only directly backward (5) selects the ordinary MCO attack from guard.
    // 0, 1-4, and 6-8 (or an unreadable variable) remain instant parries.
    bool IsDirectlyBackward(PlayerCharacter* player) {
        if (!player) return false;
        std::int32_t direction = 0;
        return player->GetGraphVariableInt("CameraMovementFHB", direction) && direction == 0;
    }

    void ResetFromBlockAttack(PlayerCharacter* player, const char* /*reason*/) {
        g_fromBlockAttackActive.exchange(false);
        g_fromBlockAttackStarted.store(false);
        g_fromBlockAttackDeadline.store(0);
        if (player) {
            player->SetGraphVariableInt(kBlockAttackGraphVariable, 0);
        }
    }

    void BeginFromBlockAttack(PlayerCharacter* player, const char* /*kind*/) {
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
            path /= L"Data\\SKSE\\Plugins";
            path /= kPrimaryConfigFileName;
            return path;
        }
        return std::filesystem::path(L"Data\\SKSE\\Plugins") / kPrimaryConfigFileName;
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
            // SKSE maps XInput and native PlayStation input to the same macro keycodes.
            return SKSE::InputMap::GamepadMaskToKeycode(keyMask);
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
    // The existing modifier applies to keyboard/mouse bindings only.
    // Controller bindings fire on a single press with no modifier required.
    bool MatchesAttackBinding(uint32_t keyCode, uint32_t keyboardMouseKey, uint32_t controllerKey,
                              uint32_t keyboardModifier) {
        return (controllerKey != kKeyDisabled && keyCode == controllerKey) ||
               (keyboardMouseKey != kKeyDisabled && keyCode == keyboardMouseKey && IsModifierHeld(keyboardModifier));
    }
    bool HasStaminaForPowerAttack(PlayerCharacter* player) {
        if (!player) return false;
        const auto* owner = player->AsActorValueOwner();
        if (!owner) return false;
        const float currentStamina = owner->GetActorValue(ActorValue::kStamina);
        if (g_noStaminaPowerAttack.load(std::memory_order_relaxed) && currentStamina <= 0.0f) {
            return false;
        }
        const float minimum = g_minimumPowerAttackStamina.load(std::memory_order_relaxed);
        return minimum <= 0.0f || currentStamina >= minimum;
    }

    PowerAttackKind ClassifyPowerAttack(uint32_t keyCode) {
        if (!g_pluginEnabled.load()) return PowerAttackKind::kNone;
        const bool isRightKey = MatchesAttackBinding(keyCode, g_altPowerAttackKey.load(),
                                                     g_rightGamepadPowerAttackKey.load(), g_rightModifier.load());
        const bool isLeftKey = MatchesAttackBinding(keyCode, g_altLeftPowerAttackKey.load(),
                                                    g_leftGamepadPowerAttackKey.load(), g_leftModifier.load());
        const bool isDualKey = MatchesAttackBinding(keyCode, g_altDualPowerAttackKey.load(),
                                                    g_dualGamepadPowerAttackKey.load(), g_dualModifier.load());
        if (!isRightKey && !isLeftKey && !isDualKey) return PowerAttackKind::kNone;
        auto* player = PlayerCharacter::GetSingleton();
        if (!player || !IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player)) return PowerAttackKind::kNone;
        if (!HasStaminaForPowerAttack(player)) return PowerAttackKind::kNone;
        if (isDualKey && g_dualPowerAttackAction && IsDualWielding(player)) {
            return PowerAttackKind::kDual;
        }
        if (isRightKey && g_rightPowerAttackAction) {
            return PowerAttackKind::kRight;
        }
        if (isLeftKey && g_leftPowerAttackAction && IsLeftHandValidForPowerAttack(player)) {
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
    bool IsCaptureInputForTarget(CaptureTarget target, uint32_t macroKey) {
        if (macroKey == kKeyDisabled || macroKey >= kMaxMacros || macroKey == kMouseLeftMacro) return false;
        switch (target) {
            case CaptureTarget::kRightKey:
            case CaptureTarget::kLeftKey:
            case CaptureTarget::kDualKey:
                return macroKey < kMacroGamepadOffset;
            case CaptureTarget::kRightGamepadKey:
            case CaptureTarget::kLeftGamepadKey:
            case CaptureTarget::kDualGamepadKey:
                return macroKey >= kMacroGamepadOffset;
            case CaptureTarget::kRightMod:
            case CaptureTarget::kLeftMod:
            case CaptureTarget::kDualMod:
                return macroKey < kMacroGamepadOffset;  // controller bindings never need a modifier
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
        // One log per fresh controller press while blocking, even before a bind is set.
        // This distinguishes a missing binding from an unrecognized button event.
        if (isDown && button->device.get() == INPUT_DEVICE::kGamepad &&
            IsActuallyBlocking(PlayerCharacter::GetSingleton())) {
            SKSE::log::info("[FHB SPA V2 GAMEPAD] rawMask={} macro={} binds(right,left,dual)=({},{},{})",
                            button->GetIDCode(), keyCode, g_rightGamepadPowerAttackKey.load(),
                            g_leftGamepadPowerAttackKey.load(), g_dualGamepadPowerAttackKey.load());
        }
        if (isDown && button->device.get() == INPUT_DEVICE::kGamepad &&
            (keyCode == g_rightGamepadPowerAttackKey.load() || keyCode == g_leftGamepadPowerAttackKey.load() ||
             (g_dualGamepadPowerAttackKey.load() != kKeyDisabled && keyCode == g_dualGamepadPowerAttackKey.load()))) {
            SKSE::log::info("[SPA GAMEPAD] power press key={} blocking={} eligible={} inputAllowed={}", keyCode,
                            IsActuallyBlocking(PlayerCharacter::GetSingleton()),
                            IsPlayerInValidCombatState(PlayerCharacter::GetSingleton()), IsGameplayInputAllowed());
        }
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
            const CaptureTarget captureTarget = g_captureTarget.load(std::memory_order_acquire);
            if (captureTarget != CaptureTarget::kNone) {
                const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;
                if (isDown && (macroKey == kEscapeScancode || IsCaptureInputForTarget(captureTarget, macroKey))) {
                    HandleCaptureInput(macroKey);
                    button->GetRuntimeData().value = 0.0f;  // consume the captured press
                }
                continue;  // no gameplay inputs while capturing
            }
            const bool consumed = HandleButtonEvent(button);
            if (consumed) {
                if (g_blockAttackPhase.load(std::memory_order_relaxed) != 0) {
                    g_suppressedPowerKey.store(macroKey, std::memory_order_relaxed);
                }
                button->GetRuntimeData().value = 0.0f;
            }
        }
    }
    void ClearBlockingState(PlayerCharacter* player) {
        if (!player) return;
        auto* state = player->AsActorState();
        if (!state) return;
        bool graphBlocking = false;
        const bool hasGraphBlocking = player->GetGraphVariableBool("IsBlocking", graphBlocking);
        if (!player->IsBlocking() && !state->actorState2.wantBlocking && !(hasGraphBlocking && graphBlocking)) return;

        // Stop requesting block, then let the graph exit on its own.
        // Do not force IsBlocking/IsBashing: they reflect actual graph state.
        state->actorState2.wantBlocking = false;
        player->SetGraphVariableInt("iWantBlock", 0);
        player->NotifyAnimationGraph("blockStop");
    }

    // Non-backward light attacks from guard are parry-first inputs. Request blockStop and
    // submit the ordinary light BGSAction in the SAME input callback. Unlike
    // power attacks, never wait for the native/graph IsBlocking flags to clear.
    // Existing MCO animations still handle later combo inputs as usual.
    void ResetLightAttackTransition(PlayerCharacter* player, const char* reason) {
        const int previous = g_lightBlockAttackPhase.exchange(0, std::memory_order_acq_rel);
        g_pendingBlockLightAction.store(nullptr);
        g_lightBlockAttackTicks.store(0);
        g_lightBlockAttackDeadline.store(0);
        if (previous != 0) {
            ResetFromBlockAttack(player, reason);
            SKSE::log::warn("[SPA LIGHT] Immediate light attack reset: {}", reason);
        }
    }

    void CancelQueuedAttacks(PlayerCharacter* player, const char* reason);

    bool BeginLightAttackFromBlock(PlayerCharacter* player, BGSAction* lightAction) {
        if (!player || !lightAction || !g_pluginEnabled.load() || !IsGameplayInputAllowed() ||
            !IsPlayerInValidCombatState(player) || g_blockAttackPhase.load(std::memory_order_acquire) != 0) {
            return false;
        }

        // Phase 3 means an action was submitted and we're waiting for its start.
        // It prevents a single physical click from starting duplicate actions,
        // but NEVER delays the initial parry input to await guard release.
        int expected = 0;
        if (!g_lightBlockAttackPhase.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) {
            return false;
        }

        // Capture attack-from-block origin before requesting guard exit.
        BeginFromBlockAttack(player, "normal");
        g_lightBlockAttackDeadline.store(GetTickCount64() + kLightAttackStartTimeoutMs);

        // Do this on the input thread, not on the task queue / next player frame.
        // The behavior graph may remain in its blocking state temporarily;
        // the initial light action is allowed to begin without a full MCO exit.
        ClearBlockingState(player);

        std::unique_ptr<TESActionData> data(TESActionData::Create());
        if (!data) {
            ResetLightAttackTransition(player, "TESActionData::Create failed");
            return false;  // Let the original input handler attempt a light attack.
        }

        data->source = NiPointer<TESObjectREFR>(player);
        data->action = lightAction;
        using ProcessAction_t = bool (*)(TESActionData*);
        REL::Relocation<ProcessAction_t> processAction{RELOCATION_ID(40551, 41557)};

        const bool accepted = processAction(data.get());
        if (!accepted) {
            ResetLightAttackTransition(player, "immediate light ProcessAction rejected");
            return false;  // Fallback to Skyrim's original AttackBlockHandler.
        }

        SKSE::log::debug("[SPA LIGHT] Light attack dispatched immediately after blockStop");
        return true;
    }

    // Backward (5) is the only normal MCO attack from guard. Unlike the
    // immediate parry, it waits for a completed graph/native block exit.
    bool BeginNormalLightAttackFromBlock(PlayerCharacter* player, BGSAction* lightAction) {
        if (!player || !lightAction || !g_task || !g_pluginEnabled.load() || !IsGameplayInputAllowed() ||
            !IsPlayerInValidCombatState(player) || g_blockAttackPhase.load(std::memory_order_acquire) != 0) {
            return false;
        }

        int expected = 0;
        if (!g_lightBlockAttackPhase.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            return false;
        }

        BeginFromBlockAttack(player, "normal");
        g_pendingBlockLightAction.store(lightAction);
        g_lightBlockAttackTicks.store(0);
        g_lightBlockAttackDeadline.store(GetTickCount64() + kLightBlockExitTimeoutMs);

        // Match the existing power-attack transition: exit on the SKSE task queue.
        // This executes exactly one normal BGSAction after block has really ended.
        g_task->AddTask([player]() {
            if (player == PlayerCharacter::GetSingleton() &&
                g_lightBlockAttackPhase.load(std::memory_order_acquire) == 1) {
                ClearBlockingState(player);
            }
        });
        SKSE::log::debug("[SPA LIGHT] Direction 5: queued normal MCO attack after block exit");
        return true;
    }

    void AdvanceLightAttackTransition(PlayerCharacter* player) {
        const int phase = g_lightBlockAttackPhase.load(std::memory_order_acquire);
        if (phase == 0 || !player || player != PlayerCharacter::GetSingleton()) return;

        if (phase == 3) {
            // Both paths use this latch once ProcessAction has been accepted.
            const auto* state = player->AsActorState();
            if (state && state->GetAttackState() != ATTACK_STATE_ENUM::kNone) {
                g_lightBlockAttackPhase.store(0, std::memory_order_release);
                g_pendingBlockLightAction.store(nullptr);
            } else if (GetTickCount64() >= g_lightBlockAttackDeadline.load()) {
                ResetLightAttackTransition(player, "light attack start timed out");
            }
            return;
        }

        if (phase != 1) return;  // Phase 2 owns its queued dispatch task.
        const unsigned frames = g_lightBlockAttackTicks.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto* state = player->AsActorState();
        if (!state || !g_pluginEnabled.load() || !IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player) ||
            GetTickCount64() >= g_lightBlockAttackDeadline.load()) {
            ResetLightAttackTransition(player, "normal light block exit failed or cancelled");
            return;
        }
        // Give the animation graph a full update to process blockStop.
        if (frames < 2) return;

        bool graphBlocking = false;
        const bool hasGraphBlocking = player->GetGraphVariableBool("IsBlocking", graphBlocking);
        if (player->IsBlocking() || state->actorState2.wantBlocking || (hasGraphBlocking && graphBlocking)) {
            return;
        }

        int expected = 1;
        if (!g_lightBlockAttackPhase.compare_exchange_strong(expected, 2, std::memory_order_acq_rel)) return;
        BGSAction* lightAction = g_pendingBlockLightAction.load();
        const auto generation = g_attackCancelGeneration.load(std::memory_order_acquire);

        g_task->AddTask([player, lightAction, generation]() {
            if (player != PlayerCharacter::GetSingleton() ||
                generation != g_attackCancelGeneration.load(std::memory_order_acquire) ||
                g_lightBlockAttackPhase.load(std::memory_order_acquire) != 2)
                return;

            bool graphBlockingNow = false;
            const bool hasGraphBlockingNow = player->GetGraphVariableBool("IsBlocking", graphBlockingNow);
            const auto* currentState = player->AsActorState();
            const bool blockedAgain = player->IsBlocking() || (hasGraphBlockingNow && graphBlockingNow) ||
                                      (currentState && currentState->actorState2.wantBlocking);
            if (!lightAction || blockedAgain || !g_pluginEnabled.load() || !IsGameplayInputAllowed() ||
                !IsPlayerInValidCombatState(player)) {
                ResetLightAttackTransition(player, "normal light action cancelled before dispatch");
                return;
            }

            std::unique_ptr<TESActionData> data(TESActionData::Create());
            if (!data) {
                ResetLightAttackTransition(player, "normal light TESActionData::Create failed");
                return;
            }
            data->source = NiPointer<TESObjectREFR>(player);
            data->action = lightAction;
            using ProcessAction_t = bool (*)(TESActionData*);
            REL::Relocation<ProcessAction_t> processAction{RELOCATION_ID(40551, 41557)};

            // Set this before ProcessAction: graph events may fire synchronously.
            g_lightBlockAttackDeadline.store(GetTickCount64() + kLightAttackStartTimeoutMs);
            g_lightBlockAttackPhase.store(3, std::memory_order_release);
            if (!processAction(data.get())) {
                ResetLightAttackTransition(player, "normal light ProcessAction rejected");
            }
        });
    }

    // Block-to-power transition, advanced by PlayerCharacter::Update (not input ticks).
    std::atomic<unsigned> g_blockAttackTicks{0};
    std::atomic<ULONGLONG> g_blockAttackDeadline{0};
    std::atomic<BGSAction*> g_pendingBlockPowerAction{nullptr};
    std::atomic<BGSAction*> g_pendingBlockSprintAction{nullptr};

    // Cancels a pending light/power attack without touching running MCO combos.
    // In particular, do not clear suppressed held-key state until the key is released.
    void CancelQueuedAttacks(PlayerCharacter* player, const char* reason) {
        g_attackCancelGeneration.fetch_add(1, std::memory_order_acq_rel);
        const int powerPhase = g_blockAttackPhase.exchange(0, std::memory_order_acq_rel);
        g_pendingBlockPowerAction.store(nullptr);
        g_pendingBlockSprintAction.store(nullptr);
        g_blockAttackTicks.store(0);
        g_blockAttackDeadline.store(0);
        const int lightPhase = g_lightBlockAttackPhase.load(std::memory_order_acquire);
        if (lightPhase != 0) {
            ResetLightAttackTransition(player, reason);
        }
        // Clear OAR's origin flag even if the deferred attack has been dispatched.
        ResetFromBlockAttack(player, reason);
        if (powerPhase != 0 || lightPhase != 0) {
            SKSE::log::info("[SPA] Pending attack discarded | {} | lightPhase={} powerPhase={}", reason, lightPhase,
                            powerPhase);
        }
    }

    // TESHitEvent also covers blocked weapon hits. Check TARGET, not aggressor.
    class PlayerHitSink final : public BSTEventSink<TESHitEvent> {
    public:
        BSEventNotifyControl ProcessEvent(const TESHitEvent* event, BSTEventSource<TESHitEvent>*) override {
            auto* player = PlayerCharacter::GetSingleton();
            if (event && player && event->target.get() == player) {
                CancelQueuedAttacks(player, "incoming TESHitEvent");
            }
            return BSEventNotifyControl::kContinue;
        }
    };

    bool RegisterPlayerHitSink() {
        static PlayerHitSink sink;
        static bool registered = false;
        if (registered) return true;
        auto* holder = ScriptEventSourceHolder::GetSingleton();
        if (!holder) return false;
        holder->AddEventSink(&sink);
        registered = true;
        SKSE::log::info("[SPA] Player hit cancellation listener registered");
        return true;
    }

    void ExecutePowerAction(PlayerCharacter* player, BGSAction* powerAction, BGSAction* sprintAction) {
        if (!player || !powerAction) return;
        // Recheck at dispatch: block-exit transitions can defer the action long
        // enough for the player's stamina to change after the original keypress.
        if (!HasStaminaForPowerAttack(player)) {
            if (g_blockAttackPhase.load(std::memory_order_acquire) != 0) {
                ResetFromBlockAttack(player, "power attack stamina fell below requirement");
            }
            return;
        }
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
        // Do not synthesize Hitframe: prior logs show the graph rejects it.
    }

    void PerformAction(PlayerCharacter* player, BGSAction* powerAction, BGSAction* sprintAction) {
        if (!g_task || !powerAction || !player) return;
        auto* state = player->AsActorState();
        const bool blocking = state && (player->IsBlocking() || state->actorState2.wantBlocking);
        if (g_blockAttackPhase.load(std::memory_order_acquire) != 0 ||
            g_lightBlockAttackPhase.load(std::memory_order_acquire) != 0) {
            return;
        }
        // Do not carry the out-of-block flag into an ordinary combo follow-up.
        // SimpleBlock can still request blocking when the actor isn't blocking.
        if (!IsActuallyBlocking(player) && g_fromBlockAttackActive.load()) {
            ResetFromBlockAttack(player, "follow-up power attack outside actual block");
        }
        if (!blocking) {
            const auto generation = g_attackCancelGeneration.load(std::memory_order_acquire);
            g_task->AddTask([player, powerAction, sprintAction, generation]() {
                if (generation != g_attackCancelGeneration.load(std::memory_order_acquire)) return;
                ExecutePowerAction(player, powerAction, sprintAction);
            });
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
        const auto generation = g_attackCancelGeneration.load(std::memory_order_acquire);
        g_task->AddTask([player, generation]() {
            if (generation != g_attackCancelGeneration.load(std::memory_order_acquire) ||
                g_blockAttackPhase.load(std::memory_order_acquire) != 1)
                return;
            ClearBlockingState(player);
        });
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
        // No fixed frame delay: proceed once native and graph blocking have ended.

        bool graphBlocking = false;
        const bool hasGraphBlocking = player->GetGraphVariableBool("IsBlocking", graphBlocking);
        if (player->IsBlocking() || (hasGraphBlocking && graphBlocking) || state->actorState2.wantBlocking) {
            return;
        }

        int expected = 1;
        if (!g_blockAttackPhase.compare_exchange_strong(expected, 2)) return;

        auto* power = g_pendingBlockPowerAction.load();
        auto* sprint = g_pendingBlockSprintAction.load();

        // Abort stale scheduled work after any hit or animation interruption.
        const auto generation = g_attackCancelGeneration.load(std::memory_order_acquire);
        g_task->AddTask([player, power, sprint, generation]() {
            if (generation != g_attackCancelGeneration.load(std::memory_order_acquire) ||
                g_blockAttackPhase.load(std::memory_order_acquire) != 2)
                return;
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
    uint32_t SanitizeKeyboardMouseKey(uint32_t keyCode) {
        return (keyCode > kKeyDisabled && keyCode < kMacroGamepadOffset) ? keyCode : kKeyDisabled;
    }
    uint32_t SanitizeGamepadKey(uint32_t keyCode) {
        return (keyCode >= kMacroGamepadOffset && keyCode < kMaxMacros) ? keyCode : kKeyDisabled;
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
            auto* player = PlayerCharacter::GetSingleton();
            auto* userEvents = UserEvents::GetSingleton();
            const bool isLeftAttack = a_event && userEvents && a_event->GetUserEvent() == userEvents->leftAttack;
            const bool isRightAttack = a_event && userEvents && a_event->GetUserEvent() == userEvents->rightAttack;

            bool filteredHeldAttack = false;
            if (a_event && g_pluginEnabled.load() && (isLeftAttack || isRightAttack)) {
                const float threshold = g_initialPowerAttackDelay->data.f;
                const float held = a_event->HeldDuration();
                filteredHeldAttack = a_event->Value() != 0.0f && held > 0.0f && held >= threshold &&
                                     IsMeleeWeaponInHand(player, isLeftAttack);
            }
            if (filteredHeldAttack) return;
            const int side = isRightAttack ? 1 : (isLeftAttack ? 2 : 0);
            if (a_event && side != 0) {
                // One physical light-button press must not be handled by both our
                // deferred action and the vanilla AttackBlockHandler.
                if (g_suppressedLightAttackSide.load(std::memory_order_acquire) == side) {
                    if (a_event->IsUp() || a_event->IsDown()) {
                        g_suppressedLightAttackSide.store(0, std::memory_order_release);
                    }
                    // A second fresh down is a separate click (e.g. missed release).
                    // Only suppress the held/release updates of the consumed press.
                    if (!a_event->IsDown()) return;
                }

                if (a_event->IsDown()) {
                    // Ignore duplicate input until the immediate light action starts.
                    if (g_lightBlockAttackPhase.load(std::memory_order_acquire) != 0) return;

                    if (IsActuallyBlocking(player)) {
                        BGSAction* action = isRightAttack ? g_rightAttackAction : g_leftAttackAction;
                        // Snapshot the direction on button-down, not on a later frame.
                        // 5 = directly backward -> normal attack with MCO-compatible exit.
                        // Everything else -> existing immediate parry attack.
                        const bool normalAttack = IsDirectlyBackward(player);
                        const bool accepted = normalAttack ? BeginNormalLightAttackFromBlock(player, action)
                                                           : BeginLightAttackFromBlock(player, action);
                        if (accepted) {
                            g_suppressedLightAttackSide.store(side, std::memory_order_release);
                            return;
                        }
                    } else if (g_fromBlockAttackActive.load() &&
                               g_blockAttackPhase.load(std::memory_order_acquire) == 0) {
                        ResetFromBlockAttack(player, "follow-up normal attack outside actual block");
                    }
                }
            }

            // Fallback to the original input path if the deferred action was unavailable.
            if (isRightAttack && a_event && a_event->IsDown() && player && player->IsBlocking()) {
                player->NotifyAnimationGraph("blockStop");
            }
            _ProcessButton(a_this, a_event, a_data);
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
            AdvanceLightAttackTransition(player);
            ExpireFromBlockAttack(player);
            TDMLockBridge::Update();
            // Inactivity decay reuses this existing player update hook.
            DirectionTracker::GetSingleton()->UpdateDecay();
            // PLAYER HUD: read the player's graph state after input decay.
            // NPC HUD remains independently driven by AttackPosition.
            Prisma::UpdatePlayerHUD();
            // NPC direction warnings: read NPC graph states and update PrismaUI.
            // Reuses the existing update hook; no additional hook or dispatcher.
            AttackPosition::GetSingleton()->UpdateHUD();
        }
        static inline Update_t _Update = nullptr;
    };

    bool SaveConfig(uint32_t keyCode, uint32_t leftKeyCode, uint32_t dualKeyCode, uint32_t rightGamepadKey,
                    uint32_t leftGamepadKey, uint32_t dualGamepadKey, uint32_t rightMod, uint32_t leftMod,
                    uint32_t dualMod, bool mcoMode, bool noStaminaPA, float minimumPowerAttackStamina,
                    bool pluginEnabled) {
        keyCode = SanitizeKeyboardMouseKey(keyCode);
        leftKeyCode = SanitizeKeyboardMouseKey(leftKeyCode);
        dualKeyCode = SanitizeKeyboardMouseKey(dualKeyCode);
        rightGamepadKey = SanitizeGamepadKey(rightGamepadKey);
        leftGamepadKey = SanitizeGamepadKey(leftGamepadKey);
        dualGamepadKey = SanitizeGamepadKey(dualGamepadKey);
        rightMod = SanitizeUnifiedModifier(rightMod);
        leftMod = SanitizeUnifiedModifier(leftMod);
        dualMod = SanitizeUnifiedModifier(dualMod);
        const std::filesystem::path savePath = GetPrimaryConfigPath();
        g_ini.Reset();
        g_ini.SetBoolValue("General", "bEnabled", pluginEnabled);
        g_ini.SetLongValue("General", "iKeycode", static_cast<long>(keyCode));
        g_ini.SetLongValue("General", "iLeftKeycode", static_cast<long>(leftKeyCode));
        g_ini.SetLongValue("General", "iDualKeycode", static_cast<long>(dualKeyCode));
        g_ini.SetLongValue("General", "iRightGamepadKeycode", static_cast<long>(rightGamepadKey));
        g_ini.SetLongValue("General", "iLeftGamepadKeycode", static_cast<long>(leftGamepadKey));
        g_ini.SetLongValue("General", "iDualGamepadKeycode", static_cast<long>(dualGamepadKey));
        g_ini.SetLongValue("General", "iRightMod", static_cast<long>(rightMod));
        g_ini.SetLongValue("General", "iLeftMod", static_cast<long>(leftMod));
        g_ini.SetLongValue("General", "iDualMod", static_cast<long>(dualMod));
        g_ini.SetBoolValue("MCO", "bMCOMode", mcoMode);
        g_ini.SetBoolValue("General", "bPowerAttackNoStamina", noStaminaPA);
        g_ini.SetDoubleValue("General", "fMinimumPowerAttackStamina",
                             std::clamp(static_cast<double>(minimumPowerAttackStamina), 0.0,
                                        static_cast<double>(kMaxMinimumPowerAttackStamina)));
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
            "Saved config enabled={} keyboard=[{},{},{}] gamepad=[{},{},{}] mods=[{},{},{}] mco={} "
            "noStaminaPA={} minStamina={} to '{}'",
            pluginEnabled, keyCode, leftKeyCode, dualKeyCode, rightGamepadKey, leftGamepadKey, dualGamepadKey, rightMod,
            leftMod, dualMod, mcoMode, noStaminaPA, minimumPowerAttackStamina, savePath.string());
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
                "None",     "Escape", "1",          "2",        "3",     "4",         "5",           "6",
                "7",        "8",      "9",          "0",        "-",     "=",         "Backspace",   "Tab",
                "Q",        "W",      "E",          "R",        "T",     "Y",         "U",           "I",
                "O",        "P",      "[",          "]",        "Enter", "Left Ctrl", "A",           "S",
                "D",        "F",      "G",          "H",        "J",     "K",         "L",           ";",
                "'",        "`",      "Left Shift", "\\\\\\\\", "Z",     "X",         "C",           "V",
                "B",        "N",      "M",          ",",        ".",     "/",         "Right Shift", "Num *",
                "Left Alt", "Space",  "Caps Lock",  "F1",       "F2",    "F3",        "F4",          "F5",
                "F6",       "F7",     "F8",         "F9",       "F10",   "Num Lock",  "Scroll Lock", "Num 7",
                "Num 8",    "Num 9",  "Num -",      "Num 4",    "Num 5", "Num 6",     "Num +",       "Num 1",
                "Num 2",    "Num 3",  "Num 0",      "Num .",    nullptr, nullptr,     nullptr,       "F11",
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
    enum class BindingType { kKeyboardMouse, kGamepad, kModifier };
    struct PendingBinding {
        uint32_t* pending;
        std::atomic<uint32_t>* committed;
        bool* unsaved;
        BindingType type;
    };
    PendingBinding GetBinding(CaptureTarget target) {
        switch (target) {
            case CaptureTarget::kRightKey:
                return {&g_pendingAltPowerAttackKey, &g_altPowerAttackKey, &g_hasUnsavedKeyChange,
                        BindingType::kKeyboardMouse};
            case CaptureTarget::kLeftKey:
                return {&g_pendingAltLeftPowerAttackKey, &g_altLeftPowerAttackKey, &g_hasUnsavedLeftKeyChange,
                        BindingType::kKeyboardMouse};
            case CaptureTarget::kDualKey:
                return {&g_pendingAltDualPowerAttackKey, &g_altDualPowerAttackKey, &g_hasUnsavedDualKeyChange,
                        BindingType::kKeyboardMouse};
            case CaptureTarget::kRightGamepadKey:
                return {&g_pendingRightGamepadPowerAttackKey, &g_rightGamepadPowerAttackKey,
                        &g_hasUnsavedRightGamepadKeyChange, BindingType::kGamepad};
            case CaptureTarget::kLeftGamepadKey:
                return {&g_pendingLeftGamepadPowerAttackKey, &g_leftGamepadPowerAttackKey,
                        &g_hasUnsavedLeftGamepadKeyChange, BindingType::kGamepad};
            case CaptureTarget::kDualGamepadKey:
                return {&g_pendingDualGamepadPowerAttackKey, &g_dualGamepadPowerAttackKey,
                        &g_hasUnsavedDualGamepadKeyChange, BindingType::kGamepad};
            case CaptureTarget::kRightMod:
                return {&g_pendingRightModifier, &g_rightModifier, &g_hasUnsavedRightMod, BindingType::kModifier};
            case CaptureTarget::kLeftMod:
                return {&g_pendingLeftModifier, &g_leftModifier, &g_hasUnsavedLeftMod, BindingType::kModifier};
            case CaptureTarget::kDualMod:
                return {&g_pendingDualModifier, &g_dualModifier, &g_hasUnsavedDualMod, BindingType::kModifier};
            default:
                return {nullptr, nullptr, nullptr, BindingType::kKeyboardMouse};
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
        const uint32_t sanitized = b.type == BindingType::kModifier  ? SanitizeUnifiedModifier(raw)
                                   : b.type == BindingType::kGamepad ? SanitizeGamepadKey(raw)
                                                                     : SanitizeKeyboardMouseKey(raw);
        *b.pending = sanitized;
        *b.unsaved = (sanitized != b.committed->load());
    }
    // Draws a "press to bind" button plus a Clear button for one binding.
    void DrawBindButton(const char* idSuffix, CaptureTarget target) {
        const PendingBinding b = GetBinding(target);
        if (!b.pending) return;
        const bool listening = (g_renderActiveCapture == target);
        const char* prompt = b.type == BindingType::kGamepad         ? ">> Press controller button (Esc cancels) <<"
                             : b.type == BindingType::kKeyboardMouse ? ">> Press keyboard/mouse (Esc cancels) <<"
                                                                     : ">> Press modifier (Esc cancels) <<";
        std::string label = listening ? std::string(prompt)
                                      : std::string(GetMacroKeyName(*b.pending, b.type == BindingType::kModifier));
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
    // ============================================================================
    // POWER ATTACK SETTINGS MENU - For Honor Blocking > Inputs
    // All settings controls for this module are rendered here.
    // ============================================================================
    void __stdcall RenderMenuFrameworkSection() {
        PollCaptureResult();
        ImGuiMCP::Text("FHB Power Attack - Dual Device Controls");
        ImGuiMCP::Text("Controller binding build: 2026-11-09 V2");
        ImGuiMCP::Separator();
        ImGuiMCP::TextWrapped(
            "Each attack can have both a Keyboard / Mouse and a Controller binding. "
            "Modifiers apply to Keyboard / Mouse only; controller presses need no modifier. "
            "Press Esc to cancel capture.");
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Right Power Attack");
        DrawBindRow("Keyboard / Mouse", "right_key", CaptureTarget::kRightKey);
        DrawBindRow("Controller", "right_gamepad_key", CaptureTarget::kRightGamepadKey);
        DrawBindRow("KB Modifier", "right_mod", CaptureTarget::kRightMod);
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Left Power Attack");
        DrawBindRow("Keyboard / Mouse", "left_key", CaptureTarget::kLeftKey);
        DrawBindRow("Controller", "left_gamepad_key", CaptureTarget::kLeftGamepadKey);
        DrawBindRow("KB Modifier", "left_mod", CaptureTarget::kLeftMod);
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Dual Power Attack");
        DrawBindRow("Keyboard / Mouse", "dual_key", CaptureTarget::kDualKey);
        DrawBindRow("Controller", "dual_gamepad_key", CaptureTarget::kDualGamepadKey);
        DrawBindRow("KB Modifier", "dual_mod", CaptureTarget::kDualMod);
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
        ImGuiMCP::Text("Power Attack Stamina Requirement");
        ImGuiMCP::TextWrapped(
            "Minimum stamina needed to start any power attack, including out of block. "
            "Set to 0 for no additional minimum. Does not change stamina consumption.");
        if (ImGuiMCP::SliderFloat("Minimum Stamina Required", &g_pendingMinimumPowerAttackStamina, 0.0f,
                                  kMaxMinimumPowerAttackStamina, "%.0f")) {
            g_hasUnsavedMinimumPowerAttackStaminaChange =
                (g_pendingMinimumPowerAttackStamina != g_minimumPowerAttackStamina.load());
        }
        ImGuiMCP::Separator();
        bool disableChecked = !g_pendingPluginEnabled;
        if (ImGuiMCP::Checkbox("Disable Power Attack Inputs", &disableChecked)) {
            g_pendingPluginEnabled = !disableChecked;
            g_hasUnsavedEnabledChange = (g_pendingPluginEnabled != g_pluginEnabled.load());
        }
        const bool hasUnsaved = g_hasUnsavedKeyChange || g_hasUnsavedLeftKeyChange || g_hasUnsavedDualKeyChange ||
                                g_hasUnsavedRightGamepadKeyChange || g_hasUnsavedLeftGamepadKeyChange ||
                                g_hasUnsavedDualGamepadKeyChange || g_hasUnsavedRightMod || g_hasUnsavedLeftMod ||
                                g_hasUnsavedDualMod || g_hasUnsavedMcoChange || g_hasUnsavedNoStaminaChange ||
                                g_hasUnsavedMinimumPowerAttackStaminaChange || g_hasUnsavedEnabledChange;
        ImGuiMCP::Separator();
        if (hasUnsaved) {
            ImGuiMCP::TextWrapped("You have unsaved changes.");
        }
        if (ImGuiMCP::Button("Save Power Attack Bindings")) {
            const uint32_t saveCode = SanitizeKeyboardMouseKey(g_pendingAltPowerAttackKey);
            const uint32_t saveLeftCode = SanitizeKeyboardMouseKey(g_pendingAltLeftPowerAttackKey);
            const uint32_t saveDualCode = SanitizeKeyboardMouseKey(g_pendingAltDualPowerAttackKey);
            const uint32_t saveRightGamepad = SanitizeGamepadKey(g_pendingRightGamepadPowerAttackKey);
            const uint32_t saveLeftGamepad = SanitizeGamepadKey(g_pendingLeftGamepadPowerAttackKey);
            const uint32_t saveDualGamepad = SanitizeGamepadKey(g_pendingDualGamepadPowerAttackKey);
            const uint32_t saveRMod = SanitizeUnifiedModifier(g_pendingRightModifier);
            const uint32_t saveLMod = SanitizeUnifiedModifier(g_pendingLeftModifier);
            const uint32_t saveDMod = SanitizeUnifiedModifier(g_pendingDualModifier);
            const bool saveMco = g_pendingMcoMode;
            const bool saveNoStamina = g_pendingNoStaminaPowerAttack;
            const float saveMinStamina =
                std::clamp(g_pendingMinimumPowerAttackStamina, 0.0f, kMaxMinimumPowerAttackStamina);
            const bool saveEnabled = g_pendingPluginEnabled;
            if (SaveConfig(saveCode, saveLeftCode, saveDualCode, saveRightGamepad, saveLeftGamepad, saveDualGamepad,
                           saveRMod, saveLMod, saveDMod, saveMco, saveNoStamina, saveMinStamina, saveEnabled)) {
                g_altPowerAttackKey = saveCode;
                g_pendingAltPowerAttackKey = saveCode;
                g_hasUnsavedKeyChange = false;
                g_altLeftPowerAttackKey = saveLeftCode;
                g_pendingAltLeftPowerAttackKey = saveLeftCode;
                g_hasUnsavedLeftKeyChange = false;
                g_altDualPowerAttackKey = saveDualCode;
                g_pendingAltDualPowerAttackKey = saveDualCode;
                g_hasUnsavedDualKeyChange = false;
                g_rightGamepadPowerAttackKey = saveRightGamepad;
                g_pendingRightGamepadPowerAttackKey = saveRightGamepad;
                g_hasUnsavedRightGamepadKeyChange = false;
                g_leftGamepadPowerAttackKey = saveLeftGamepad;
                g_pendingLeftGamepadPowerAttackKey = saveLeftGamepad;
                g_hasUnsavedLeftGamepadKeyChange = false;
                g_dualGamepadPowerAttackKey = saveDualGamepad;
                g_pendingDualGamepadPowerAttackKey = saveDualGamepad;
                g_hasUnsavedDualGamepadKeyChange = false;
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
                g_minimumPowerAttackStamina.store(saveMinStamina);
                g_pendingMinimumPowerAttackStamina = saveMinStamina;
                g_hasUnsavedMinimumPowerAttackStaminaChange = false;
                g_pluginEnabled = saveEnabled;
                g_pendingPluginEnabled = saveEnabled;
                g_hasUnsavedEnabledChange = false;
            }
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Discard Power Attack Changes")) {
            CancelCapture();
            g_pendingAltPowerAttackKey = g_altPowerAttackKey.load();
            g_hasUnsavedKeyChange = false;
            g_pendingAltLeftPowerAttackKey = g_altLeftPowerAttackKey.load();
            g_hasUnsavedLeftKeyChange = false;
            g_pendingAltDualPowerAttackKey = g_altDualPowerAttackKey.load();
            g_hasUnsavedDualKeyChange = false;
            g_pendingRightGamepadPowerAttackKey = g_rightGamepadPowerAttackKey.load();
            g_hasUnsavedRightGamepadKeyChange = false;
            g_pendingLeftGamepadPowerAttackKey = g_leftGamepadPowerAttackKey.load();
            g_hasUnsavedLeftGamepadKeyChange = false;
            g_pendingDualGamepadPowerAttackKey = g_dualGamepadPowerAttackKey.load();
            g_hasUnsavedDualGamepadKeyChange = false;
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
            g_pendingMinimumPowerAttackStamina = g_minimumPowerAttackStamina.load();
            g_hasUnsavedMinimumPowerAttackStaminaChange = false;
            g_pendingPluginEnabled = g_pluginEnabled.load();
            g_hasUnsavedEnabledChange = false;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button("Reload Power Attack Bindings")) {
            CancelCapture();
            LoadConfig();
        }
    }
    // ======================== END POWER ATTACK SETTINGS MENU =======================
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
            g_rightGamepadPowerAttackKey = kKeyDisabled;
            g_pendingRightGamepadPowerAttackKey = kKeyDisabled;
            g_hasUnsavedRightGamepadKeyChange = false;
            g_leftGamepadPowerAttackKey = kKeyDisabled;
            g_pendingLeftGamepadPowerAttackKey = kKeyDisabled;
            g_hasUnsavedLeftGamepadKeyChange = false;
            g_dualGamepadPowerAttackKey = kKeyDisabled;
            g_pendingDualGamepadPowerAttackKey = kKeyDisabled;
            g_hasUnsavedDualGamepadKeyChange = false;
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
            g_minimumPowerAttackStamina.store(0.0f);
            g_pendingMinimumPowerAttackStamina = 0.0f;
            g_hasUnsavedMinimumPowerAttackStaminaChange = false;
            g_pluginEnabled = true;
            g_pendingPluginEnabled = true;
            g_hasUnsavedEnabledChange = false;
            g_ini.Reset();
            return;
        }
        // Legacy i*Keycode values could contain any device. Migrate an old gamepad
        // binding to the new controller slot unless a newer explicit value exists.
        const auto readOldCode = [](long value) -> uint32_t {
            return value > 0 ? SanitizeKeyCode(static_cast<uint32_t>(value)) : kKeyDisabled;
        };
        const uint32_t oldRight = readOldCode(g_ini.GetLongValue("General", "iKeycode", kDefaultAltPowerAttackKey));
        const uint32_t oldLeft =
            readOldCode(g_ini.GetLongValue("General", "iLeftKeycode", kDefaultAltLeftPowerAttackKey));
        const uint32_t oldDual = readOldCode(g_ini.GetLongValue("General", "iDualKeycode", kKeyDisabled));
        const uint32_t parsedKey = SanitizeKeyboardMouseKey(oldRight);
        const uint32_t parsedLeftKey = SanitizeKeyboardMouseKey(oldLeft);
        const uint32_t parsedDualKey = SanitizeKeyboardMouseKey(oldDual);
        const auto loadGamepadKey = [&](const char* iniKey, uint32_t legacyKey) -> uint32_t {
            const long raw = g_ini.GetLongValue("General", iniKey, -1);
            // -1 means this is an older config. An explicit 0 keeps the slot cleared.
            if (raw == -1) return SanitizeGamepadKey(legacyKey);
            return raw > 0 ? SanitizeGamepadKey(static_cast<uint32_t>(raw)) : kKeyDisabled;
        };
        const uint32_t parsedRightGamepad = loadGamepadKey("iRightGamepadKeycode", oldRight);
        const uint32_t parsedLeftGamepad = loadGamepadKey("iLeftGamepadKeycode", oldLeft);
        const uint32_t parsedDualGamepad = loadGamepadKey("iDualGamepadKeycode", oldDual);
        g_altPowerAttackKey = parsedKey;
        g_pendingAltPowerAttackKey = parsedKey;
        g_hasUnsavedKeyChange = false;
        g_altLeftPowerAttackKey = parsedLeftKey;
        g_pendingAltLeftPowerAttackKey = parsedLeftKey;
        g_hasUnsavedLeftKeyChange = false;
        g_altDualPowerAttackKey = parsedDualKey;
        g_pendingAltDualPowerAttackKey = parsedDualKey;
        g_hasUnsavedDualKeyChange = false;
        g_rightGamepadPowerAttackKey = parsedRightGamepad;
        g_pendingRightGamepadPowerAttackKey = parsedRightGamepad;
        g_hasUnsavedRightGamepadKeyChange = false;
        g_leftGamepadPowerAttackKey = parsedLeftGamepad;
        g_pendingLeftGamepadPowerAttackKey = parsedLeftGamepad;
        g_hasUnsavedLeftGamepadKeyChange = false;
        g_dualGamepadPowerAttackKey = parsedDualGamepad;
        g_pendingDualGamepadPowerAttackKey = parsedDualGamepad;
        g_hasUnsavedDualGamepadKeyChange = false;
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
        const double rawMinimum = g_ini.GetDoubleValue("General", "fMinimumPowerAttackStamina", 0.0);
        const float parsedMinimum = std::clamp(std::isfinite(rawMinimum) ? static_cast<float>(rawMinimum) : 0.0f, 0.0f,
                                               kMaxMinimumPowerAttackStamina);
        g_minimumPowerAttackStamina.store(parsedMinimum);
        g_pendingMinimumPowerAttackStamina = parsedMinimum;
        g_hasUnsavedMinimumPowerAttackStaminaChange = false;
        const bool parsedEnabled = g_ini.GetBoolValue("General", "bEnabled", true);
        g_pluginEnabled = parsedEnabled;
        g_pendingPluginEnabled = parsedEnabled;
        g_hasUnsavedEnabledChange = false;
        g_ini.Reset();
        SKSE::log::info(
            "Loaded config enabled={} keyboard=[{},{},{}] gamepad=[{},{},{}] mods=[{},{},{}] mco={} "
            "noStaminaPA={} minStamina={} from '{}'",
            parsedEnabled, parsedKey, parsedLeftKey, parsedDualKey, parsedRightGamepad, parsedLeftGamepad,
            parsedDualGamepad, parsedRMod, parsedLMod, parsedDMod, parsedMco, parsedNoStamina, parsedMinimum,
            path.string());
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
            if (g_hasUnsavedRightGamepadKeyChange) {
                g_pendingRightGamepadPowerAttackKey = g_rightGamepadPowerAttackKey.load();
                g_hasUnsavedRightGamepadKeyChange = false;
            }
            if (g_hasUnsavedLeftGamepadKeyChange) {
                g_pendingLeftGamepadPowerAttackKey = g_leftGamepadPowerAttackKey.load();
                g_hasUnsavedLeftGamepadKeyChange = false;
            }
            if (g_hasUnsavedDualGamepadKeyChange) {
                g_pendingDualGamepadPowerAttackKey = g_dualGamepadPowerAttackKey.load();
                g_hasUnsavedDualGamepadKeyChange = false;
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
            if (g_hasUnsavedMinimumPowerAttackStaminaChange) {
                g_pendingMinimumPowerAttackStamina = g_minimumPowerAttackStamina.load();
                g_hasUnsavedMinimumPowerAttackStaminaChange = false;
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

    // Rendered by Menu::Install() inside For Honor Blocking > Inputs.
    // Keep initialization/input callbacks in OnDataLoaded, not a second menu registration.
    void RenderInputSettings() { RenderMenuFrameworkSection(); }

    void Initialize() { g_task = SKSE::GetTaskInterface(); }

    void OnDataLoaded() {
        SKSE::log::info("[FHB SPA V2] OnDataLoaded entered, configuring controller bindings");

        // The consolidated menu is registered once by Menu::Install().
        // Retain independent config and input/event listener registration here.
        if (SKSEMenuFramework::IsInstalled()) {
            LoadConfig();

            if (!g_menuFrameworkInputHook) {
                g_menuFrameworkInputHook = SKSEMenuFramework::AddInputEvent(OnMenuFrameworkInput);
            }
            if (!g_menuFrameworkEventHook) {
                g_menuFrameworkEventHook = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);
            }
            SKSE::log::info("[FHB SPA V2] Input hooks registered; settings under For Honor Blocking > Inputs");
        } else {
            SKSE::log::critical("[FHB SPA V2] SKSE Menu Framework is unavailable; SPA binds cannot receive input");
            return;
        }

        g_rightPowerAttackAction = TESForm::LookupByID<BGSAction>(0x13383);
        g_leftPowerAttackAction = TESForm::LookupByID<BGSAction>(0x02E2F6);
        g_dualPowerAttackAction = TESForm::LookupByID<BGSAction>(0x2E2F7);
        g_rightAttackAction = TESForm::LookupByID<BGSAction>(0x13005);
        g_leftAttackAction = TESForm::LookupByID<BGSAction>(0x13004);

        // A failed form lookup must not silently hide the configuration UI.
        if (!g_rightPowerAttackAction) {
            SKSE::log::critical("[FHB SPA V2] ActionRightPowerAttack (0x13383) unresolved; input hook not installed");
            return;
        }
        AttackBlockHook::Install();
        PlayerUpdateHook::Install();
        if (!RegisterPlayerHitSink()) {
            SKSE::log::warn("[SPA] Player hit listener unavailable; animation-event fallback remains active");
        }
        SKSE::log::info("[FHB SPA V2] Ready | keyboard=[{},{},{}] gamepad=[{},{},{}]", g_altPowerAttackKey.load(),
                        g_altLeftPowerAttackKey.load(), g_altDualPowerAttackKey.load(),
                        g_rightGamepadPowerAttackKey.load(), g_leftGamepadPowerAttackKey.load(),
                        g_dualGamepadPowerAttackKey.load());
    }

    bool IsBlockExitPending() {
        return g_blockAttackPhase.load(std::memory_order_acquire) != 0 ||
               g_lightBlockAttackPhase.load(std::memory_order_acquire) != 0;
    }

    void OnPlayerAnimationEvent(std::string_view tag) {
        // A stagger, recoil, or block-hit reaction invalidates pending attacks.
        // The hit-event sink is primary; this covers reactions without a TESHitEvent.
        if (tag == "staggerStart" || tag == "StaggerStart" || tag == "recoilStart" || tag == "recoilLargeStart" ||
            tag == "blockHit" || tag == "blockHitStart" || tag == "BlockHit" || tag == "blockHitLarge" ||
            tag == "blockHitHeavy") {
            CancelQueuedAttacks(PlayerCharacter::GetSingleton(), "player reaction animation");
            return;
        }
        // The deferred light dispatch has entered its attack animation.
        // Release SimpleBlock's transition guard; MCO owns the combo now.
        if ((tag == "attackStart" || tag == "MCO_WinOpen" || tag == "MCO_PowerWinOpen") &&
            g_lightBlockAttackPhase.load(std::memory_order_acquire) == 3) {
            g_lightBlockAttackPhase.store(0, std::memory_order_release);
            g_pendingBlockLightAction.store(nullptr);
        }
        if (!g_fromBlockAttackActive.load()) return;

        // An earlier attackStop may fire immediately after leaving block.
        // Do not clear the flag until THIS attack has started.
        if (tag == "attackStart" || tag == "MCO_WinOpen" || tag == "MCO_PowerWinOpen") {
            g_fromBlockAttackStarted.store(true);
            g_fromBlockAttackDeadline.store(GetTickCount64() + kBlockAttackEndTimeoutMs);
        } else if (tag == "attackStop" && g_fromBlockAttackStarted.load()) {
            ResetFromBlockAttack(PlayerCharacter::GetSingleton(), "attackStop");
        }
    }

    void OnGameLoaded() {
        // C++ static state survives save/reload even if the graph is recreated.
        g_attackCancelGeneration.fetch_add(1, std::memory_order_acq_rel);
        g_blockAttackPhase.store(0);
        g_pendingBlockPowerAction.store(nullptr);
        g_pendingBlockSprintAction.store(nullptr);
        g_blockAttackTicks.store(0);
        g_blockAttackDeadline.store(0);
        g_suppressedPowerKey.store(kKeyDisabled);
        g_lightBlockAttackPhase.store(0);
        g_pendingBlockLightAction.store(nullptr);
        g_lightBlockAttackTicks.store(0);
        g_lightBlockAttackDeadline.store(0);
        g_suppressedLightAttackSide.store(0);
        ResetFromBlockAttack(PlayerCharacter::GetSingleton(), "new game/save loaded");
    }
}
