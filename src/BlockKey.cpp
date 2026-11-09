#include "BlockKey.h"

#include <SimpleIni.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include "PowerAttackKey.h"
#include "SKSE/InputMap.h"
#include "SKSEMenuFramework.h"
#include "TDMLockBridge.h"

using namespace RE;
using namespace SKSE;

// Implemented in the merged PowerAttackKey.cpp. This is a direct link, not GetProcAddress.
extern "C" bool SimplePowerAttack_IsChordActive(std::uint32_t a_macroKey);

namespace {
    constexpr uint32_t kKeyDisabled = 0;  // 0 = key disabled
    constexpr uint32_t kModifierNone = 0;

    constexpr uint32_t kMacroNumKeyboardKeys = 256;
    constexpr uint32_t kMacroNumMouseButtons = 8;
    constexpr uint32_t kMacroMouseWheelOffset = kMacroNumKeyboardKeys + kMacroNumMouseButtons;
    constexpr uint32_t kMacroGamepadOffset = kMacroMouseWheelOffset + 2;
    constexpr uint32_t kMaxMacros = kMacroGamepadOffset + 16;

    constexpr uint32_t kMouseLeftMacro = kMacroNumKeyboardKeys;  // 256 == Mouse Left (used to interact with the menu)
    constexpr uint32_t kEscapeScancode = 0x01;                   // DirectInput scancode for Escape (cancels capture)
    constexpr uint32_t kCaptureCancelled = 0xFFFFFFFFu;          // sentinel: capture ended without a new binding

    enum class CaptureTarget : uint32_t {
        kNone = 0,
        kBlockKey,
        kBlockControllerKey,
        kBlockMod,
    };

    constexpr const wchar_t* kPrimaryConfigFileName = L"SimpleBlock.ini";
    constexpr const wchar_t* kLegacyConfigFileName =
        L"SimplePowerAttack.ini";  // migration source for old block binding

    std::atomic<uint32_t> g_altBlockKey = kKeyDisabled;
    uint32_t g_pendingAltBlockKey = kKeyDisabled;
    bool g_hasUnsavedBlockKeyChange = false;
    // Dedicated controller block button. No modifier is required for this input.
    std::atomic<uint32_t> g_controllerBlockKey{kKeyDisabled};
    uint32_t g_pendingControllerBlockKey = kKeyDisabled;
    bool g_hasUnsavedControllerBlockKeyChange = false;
    std::atomic<uint32_t> g_blockModifier = kModifierNone;
    uint32_t g_pendingBlockModifier = kModifierNone;
    bool g_hasUnsavedBlockModChange = false;
    std::atomic<bool> g_pluginEnabled = true;
    bool g_pendingPluginEnabled = true;
    bool g_hasUnsavedEnabledChange = false;
    SKSEMenuFramework::Model::InputEvent* g_menuFrameworkInputHook = nullptr;
    SKSEMenuFramework::Model::Event* g_menuFrameworkEventHook = nullptr;

    std::atomic<CaptureTarget> g_captureTarget = CaptureTarget::kNone;
    std::atomic<uint32_t> g_capturedCode = kCaptureCancelled;
    CaptureTarget g_renderActiveCapture = CaptureTarget::kNone;
    std::array<std::atomic<bool>, kMaxMacros> g_keyStates{};
    bool g_blockComboActive = false;
    // Track the two physical block sources independently, so releasing one
    // does not release guard or TDM lock-on while the other is still held.
    bool g_keyboardBlockDown = false;
    bool g_controllerBlockDown = false;

    CSimpleIniA g_ini(true, false, false);

    void LoadConfig();

    std::filesystem::path GetConfigPath(const wchar_t* fileName) {
        wchar_t exePath[MAX_PATH]{};
        const auto written = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (written > 0 && written < MAX_PATH) {
            std::filesystem::path path(exePath);
            path.remove_filename();
            path /= L"Data\\SKSE\\Plugins";
            path /= fileName;
            return path;
        }

        return std::filesystem::path(L"Data\\SKSE\\Plugins") / fileName;
    }

    uint32_t ToMacroKeyCode(const ButtonEvent* event) {
        const auto keyMask = event->GetIDCode();
        if (event->device.get() == INPUT_DEVICE::kMouse) {
            return kMacroNumKeyboardKeys + keyMask;
        }
        if (event->device.get() == INPUT_DEVICE::kGamepad) {
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
            if (auto* weap = skyrim_cast<TESObjectWEAP*>(equippedObj)) {
                if (weap->IsBow() || weap->IsCrossbow()) return false;
            } else {
                TESForm* leftObj = player->GetEquippedObject(true);
                bool leftCanBlock = false;
                if (leftObj) {
                    if (auto* leftWeap = skyrim_cast<TESObjectWEAP*>(leftObj)) {
                        leftCanBlock = !leftWeap->IsBow() && !leftWeap->IsCrossbow();
                    } else if (auto* leftArmor = skyrim_cast<TESObjectARMO*>(leftObj)) {
                        leftCanBlock = leftArmor->IsShield();
                    }
                }
                if (!leftCanBlock) return false;
            }
        }

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

    bool IsPowerAttackChordActive(uint32_t keyCode) { return SimplePowerAttack_IsChordActive(keyCode); }

    bool IsKeyboardBlockHeld() {
        const uint32_t modifier = g_blockModifier.load(std::memory_order_relaxed);
        return g_keyboardBlockDown && (modifier == kModifierNone || g_blockComboActive);
    }

    bool IsAnyBlockHeld() { return IsKeyboardBlockHeld() || g_controllerBlockDown; }

    void HandleBlockEvent(ButtonEvent* button, uint32_t keyCode) {
        if (!button) return;

        const bool isController = button->device.get() == INPUT_DEVICE::kGamepad;
        const uint32_t configuredKey = isController ? g_controllerBlockKey.load(std::memory_order_relaxed)
                                                    : g_altBlockKey.load(std::memory_order_relaxed);
        if (configuredKey == kKeyDisabled || keyCode != configuredKey) return;

        const bool wasHeld = IsAnyBlockHeld();
        if (button->IsDown()) {
            if (isController) {
                g_controllerBlockDown = true;
            } else {
                g_keyboardBlockDown = true;
                g_blockComboActive = IsModifierHeld(g_blockModifier.load(std::memory_order_relaxed));
            }
        } else if (button->IsUp()) {
            if (isController) {
                g_controllerBlockDown = false;
            } else {
                g_keyboardBlockDown = false;
                g_blockComboActive = false;
            }
        }

        const bool stillHeld = IsAnyBlockHeld();
        // Preserve the original press-to-lock, release-to-unlock behavior.
        if (!wasHeld && stillHeld) TDMLockBridge::OnBlockPressed();
        if (wasHeld && !stillHeld) TDMLockBridge::OnBlockReleased();

        auto* player = PlayerCharacter::GetSingleton();
        if (!player) return;
        auto* actorState = player->AsActorState();
        if (!actorState) return;

        if (button->IsUp()) {
            // Another configured block input still holds guard.
            if (stillHeld) return;

            actorState->actorState2.wantBlocking = false;
            player->SetGraphVariableInt("iWantBlock", 0);
            bool isBlocking = false;
            if (player->GetGraphVariableBool("IsBlocking", isBlocking) && isBlocking) {
                player->NotifyAnimationGraph("blockStop");
            }
            return;
        }

        if (!button->IsPressed() || !g_pluginEnabled.load(std::memory_order_relaxed)) return;
        // The keyboard modifier applies only to the keyboard/mouse binding.
        if (isController ? !g_controllerBlockDown : !IsKeyboardBlockHeld()) return;
        if (!IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player)) return;

        // Leave both native and graph block states clear during SPA's deferred
        // block-to-attack transition.
        if (PowerAttackKey::IsBlockExitPending()) return;

        if (actorState->GetAttackState() != ATTACK_STATE_ENUM::kNone) {
            // Preserve held guard intent for FHB's MCO window-close handling.
            // Do not issue blockStart in the middle of an attack.
            player->SetGraphVariableInt("iWantBlock", 1);
            return;
        }

        actorState->actorState2.wantBlocking = true;
        player->SetGraphVariableInt("iWantBlock", 1);
        bool isBlocking = false;
        if (player->GetGraphVariableBool("IsBlocking", isBlocking) && !isBlocking) {
            player->NotifyAnimationGraph("blockStart");
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

    void ProcessInputChain(RE::InputEvent* events) {
        for (auto* event = events; event; event = event->next) {
            if (event->eventType.get() != INPUT_EVENT_TYPE::kButton) continue;
            auto* button = event->AsButtonEvent();
            if (!button) continue;

            const uint32_t macroKey = ToMacroKeyCode(button);
            const bool isController = button->device.get() == INPUT_DEVICE::kGamepad;
            if (macroKey < kMaxMacros) {
                g_keyStates[macroKey].store(button->Value() != 0.0f, std::memory_order_relaxed);
            }

            if (const auto capture = g_captureTarget.load(std::memory_order_acquire);
                capture != CaptureTarget::kNone) {
                const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;
                if (isDown && macroKey < kMaxMacros && macroKey != kMouseLeftMacro) {
                    const bool validDevice = capture == CaptureTarget::kBlockControllerKey ? isController : !isController;
                    if (validDevice) {
                        HandleCaptureInput(macroKey);
                        button->GetRuntimeData().value = 0.0f;
                    }
                }
                // Always allow a previously held block binding to be released,
                // even while a new key is being captured in the menu.
                if (button->IsUp()) HandleBlockEvent(button, macroKey);
                continue;
            }

            // Do not let the native input handler re-enter guard while SPA is
            // waiting for a block-to-power / block-to-light exit. Actual key-up
            // still goes through, so the physical and TDM held states clear.
            if (isController && macroKey == g_controllerBlockKey.load(std::memory_order_relaxed) &&
                macroKey != kKeyDisabled && PowerAttackKey::IsBlockExitPending() && button->Value() > 0.0f) {
                button->GetRuntimeData().value = 0.0f;
                continue;
            }

            const bool suppressBlock = button->Value() != 0.0f && IsPowerAttackChordActive(macroKey);
            if (!suppressBlock) HandleBlockEvent(button, macroKey);
        }
    }

    uint32_t SanitizeKeyCode(uint32_t keyCode) {
        if (keyCode >= kMaxMacros) {
            return kKeyDisabled;
        }
        return keyCode;
    }
    uint32_t SanitizeKeyboardMouseKey(uint32_t keyCode) {
        return keyCode > 0 && keyCode < kMacroGamepadOffset ? keyCode : kKeyDisabled;
    }

    uint32_t SanitizeControllerKey(uint32_t keyCode) {
        return keyCode >= kMacroGamepadOffset && keyCode < kMaxMacros ? keyCode : kKeyDisabled;
    }

    uint32_t SanitizeUnifiedModifier(uint32_t keyCode) {
        if (keyCode == kModifierNone) return kModifierNone;
        if (keyCode >= 1 && keyCode < kMacroNumKeyboardKeys) return keyCode;
        if (keyCode >= kMacroGamepadOffset && keyCode < kMaxMacros) return keyCode;
        return kModifierNone;
    }

    bool SaveConfig(uint32_t blockKeyCode, uint32_t controllerBlockKey, uint32_t blockMod, bool pluginEnabled) {
        blockKeyCode = SanitizeKeyboardMouseKey(blockKeyCode);
        controllerBlockKey = SanitizeControllerKey(controllerBlockKey);
        blockMod = SanitizeUnifiedModifier(blockMod);
        const std::filesystem::path savePath = GetConfigPath(kPrimaryConfigFileName);

        g_ini.Reset();
        g_ini.SetBoolValue("General", "bEnabled", pluginEnabled);
        g_ini.SetLongValue("General", "iBlockKeycode", static_cast<long>(blockKeyCode));
        g_ini.SetLongValue("General", "iBlockControllerKeycode", static_cast<long>(controllerBlockKey));
        g_ini.SetLongValue("General", "iBlockMod", static_cast<long>(blockMod));

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

        SKSE::log::info("Saved config enabled={} keyboardBlock={} controllerBlock={} mod={} to '{}'",
                        pluginEnabled, blockKeyCode, controllerBlockKey, blockMod, savePath.string());
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

    enum class BindingType { kKeyboardMouse, kController, kModifier };

    struct PendingBinding {
        uint32_t* pending;
        std::atomic<uint32_t>* committed;
        bool* unsaved;
        BindingType type;
    };

    PendingBinding GetBinding(CaptureTarget target) {
        switch (target) {
            case CaptureTarget::kBlockKey:
                return {&g_pendingAltBlockKey, &g_altBlockKey, &g_hasUnsavedBlockKeyChange,
                        BindingType::kKeyboardMouse};
            case CaptureTarget::kBlockControllerKey:
                return {&g_pendingControllerBlockKey, &g_controllerBlockKey, &g_hasUnsavedControllerBlockKeyChange,
                        BindingType::kController};
            case CaptureTarget::kBlockMod:
                return {&g_pendingBlockModifier, &g_blockModifier, &g_hasUnsavedBlockModChange,
                        BindingType::kModifier};
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

        const uint32_t sanitized = b.type == BindingType::kController ? SanitizeControllerKey(raw)
                                  : b.type == BindingType::kModifier ? SanitizeUnifiedModifier(raw)
                                  : SanitizeKeyboardMouseKey(raw);
        *b.pending = sanitized;
        *b.unsaved = (sanitized != b.committed->load());
    }

    // Draws a "press to bind" button plus a Clear button for one binding.
    void DrawBindButton(const char* idSuffix, CaptureTarget target) {
        const PendingBinding b = GetBinding(target);
        if (!b.pending) return;

        const bool listening = (g_renderActiveCapture == target);

        const char* prompt = b.type == BindingType::kController ? ">> Press controller button (Esc cancels) <<"
                             : b.type == BindingType::kModifier ? ">> Press modifier key (Esc cancels) <<"
                             : ">> Press keyboard / mouse (Esc cancels) <<";
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

    void __stdcall RenderMenuFrameworkSection() {
        PollCaptureResult();

        ImGuiMCP::Text("Simple Block");
        ImGuiMCP::Separator();
        ImGuiMCP::TextWrapped(
            "Keyboard/mouse and controller each have an active block binding. "
            "The modifier applies only to keyboard/mouse. Press Esc to cancel a capture.");
        ImGuiMCP::Separator();

        ImGuiMCP::Text("Block");
        ImGuiMCP::Text("Keyboard / Mouse");
        ImGuiMCP::SameLine(170.0f);
        DrawBindButton("block_key", CaptureTarget::kBlockKey);
        ImGuiMCP::Text("Modifier");
        ImGuiMCP::SameLine(170.0f);
        DrawBindButton("block_mod", CaptureTarget::kBlockMod);
        ImGuiMCP::Text("Controller");
        ImGuiMCP::SameLine(170.0f);
        DrawBindButton("block_controller_key", CaptureTarget::kBlockControllerKey);
        ImGuiMCP::Separator();

        bool disableChecked = !g_pendingPluginEnabled;
        if (ImGuiMCP::Checkbox("Disable Block Inputs", &disableChecked)) {
            g_pendingPluginEnabled = !disableChecked;
            g_hasUnsavedEnabledChange = (g_pendingPluginEnabled != g_pluginEnabled.load());
        }

        const bool hasUnsaved = g_hasUnsavedBlockKeyChange || g_hasUnsavedControllerBlockKeyChange ||
                                g_hasUnsavedBlockModChange || g_hasUnsavedEnabledChange;

        ImGuiMCP::Separator();

        if (hasUnsaved) {
            ImGuiMCP::TextWrapped("You have unsaved changes.");
        }

        if (ImGuiMCP::Button("Save Block Bindings")) {
            const uint32_t saveBlockCode = SanitizeKeyboardMouseKey(g_pendingAltBlockKey);
            const uint32_t saveControllerCode = SanitizeControllerKey(g_pendingControllerBlockKey);
            const uint32_t saveBlockMod = SanitizeUnifiedModifier(g_pendingBlockModifier);
            const bool saveEnabled = g_pendingPluginEnabled;
            if (SaveConfig(saveBlockCode, saveControllerCode, saveBlockMod, saveEnabled)) {
                g_altBlockKey = saveBlockCode;
                g_pendingAltBlockKey = saveBlockCode;
                g_hasUnsavedBlockKeyChange = false;
                g_controllerBlockKey = saveControllerCode;
                g_pendingControllerBlockKey = saveControllerCode;
                g_hasUnsavedControllerBlockKeyChange = false;
                g_blockModifier = saveBlockMod;
                g_pendingBlockModifier = saveBlockMod;
                g_hasUnsavedBlockModChange = false;
                g_pluginEnabled = saveEnabled;
                g_pendingPluginEnabled = saveEnabled;
                g_hasUnsavedEnabledChange = false;
            }
        }

        ImGuiMCP::SameLine();

        if (ImGuiMCP::Button("Discard Block Changes")) {
            CancelCapture();
            g_pendingAltBlockKey = g_altBlockKey.load();
            g_hasUnsavedBlockKeyChange = false;
            g_pendingControllerBlockKey = g_controllerBlockKey.load();
            g_hasUnsavedControllerBlockKeyChange = false;
            g_pendingBlockModifier = g_blockModifier.load();
            g_hasUnsavedBlockModChange = false;
            g_pendingPluginEnabled = g_pluginEnabled.load();
            g_hasUnsavedEnabledChange = false;
        }

        ImGuiMCP::SameLine();

        if (ImGuiMCP::Button("Reload Block Bindings")) {
            CancelCapture();
            LoadConfig();
        }
    }

    void LoadConfig() {
        const std::filesystem::path path = GetConfigPath(kPrimaryConfigFileName);
        const SI_Error result = g_ini.LoadFile(path.c_str());
        if (result < 0) {
            // One-time migration from SimplePowerAttack.ini if SimpleBlock.ini is absent.
            uint32_t migratedKey = kKeyDisabled;
            const std::filesystem::path legacyPath = GetConfigPath(kLegacyConfigFileName);
            if (g_ini.LoadFile(legacyPath.c_str()) >= 0) {
                const long rawLegacy = g_ini.GetLongValue("General", "iBlockKeycode", 0);
                migratedKey = SanitizeKeyCode(static_cast<uint32_t>(rawLegacy < 0 ? 0 : rawLegacy));
            }
            g_altBlockKey = SanitizeKeyboardMouseKey(migratedKey);
            g_pendingAltBlockKey = g_altBlockKey.load();
            g_hasUnsavedBlockKeyChange = false;
            g_controllerBlockKey = SanitizeControllerKey(migratedKey);
            g_pendingControllerBlockKey = g_controllerBlockKey.load();
            g_hasUnsavedControllerBlockKeyChange = false;
            g_blockModifier = kModifierNone;
            g_pendingBlockModifier = kModifierNone;
            g_hasUnsavedBlockModChange = false;
            g_pluginEnabled = true;
            g_pendingPluginEnabled = true;
            g_hasUnsavedEnabledChange = false;
            g_ini.Reset();
            SKSE::log::info("[BLOCK KEY] Migrated old binding: keyboard={} controller={}",
                            g_altBlockKey.load(), g_controllerBlockKey.load());
            return;
        }

        // In older SimpleBlock.ini versions, a controller code could be stored
        // in iBlockKeycode. Migrate it only when the dedicated field is absent.
        const long rawBlockValue = g_ini.GetLongValue("General", "iBlockKeycode", 0);
        const uint32_t oldBlockKey = SanitizeKeyCode(static_cast<uint32_t>(rawBlockValue < 0 ? 0 : rawBlockValue));
        const long rawControllerValue = g_ini.GetLongValue("General", "iBlockControllerKeycode", -1);
        const uint32_t parsedKeyboard = SanitizeKeyboardMouseKey(oldBlockKey);
        const uint32_t parsedController = rawControllerValue >= 0
            ? SanitizeControllerKey(static_cast<uint32_t>(rawControllerValue))
            : SanitizeControllerKey(oldBlockKey);

        g_altBlockKey = parsedKeyboard;
        g_pendingAltBlockKey = parsedKeyboard;
        g_hasUnsavedBlockKeyChange = false;
        g_controllerBlockKey = parsedController;
        g_pendingControllerBlockKey = parsedController;
        g_hasUnsavedControllerBlockKeyChange = false;

        const long rawModValue = g_ini.GetLongValue("General", "iBlockMod", 0);
        const uint32_t parsedBlockMod = SanitizeUnifiedModifier(static_cast<uint32_t>(rawModValue < 0 ? 0 : rawModValue));
        g_blockModifier = parsedBlockMod;
        g_pendingBlockModifier = parsedBlockMod;
        g_hasUnsavedBlockModChange = false;

        const bool parsedEnabled = g_ini.GetBoolValue("General", "bEnabled", true);
        g_pluginEnabled = parsedEnabled;
        g_pendingPluginEnabled = parsedEnabled;
        g_hasUnsavedEnabledChange = false;

        g_ini.Reset();
        SKSE::log::info("[BLOCK KEY] Loaded enabled={} keyboard={} controller={} modifier={} from '{}'",
                        parsedEnabled, parsedKeyboard, parsedController, parsedBlockMod, path.string());
    }

    bool __stdcall OnMenuFrameworkInput(RE::InputEvent* events) {
        if (events) ProcessInputChain(events);
        return false;
    }

    void __stdcall OnMenuFrameworkEvent(SKSEMenuFramework::Model::EventType eventType) {
        if (eventType == SKSEMenuFramework::Model::kCloseMenu) {
            // Stop listening for a keybind and drop any unsaved edits back to the saved values.
            CancelCapture();
            if (g_hasUnsavedBlockKeyChange) {
                g_pendingAltBlockKey = g_altBlockKey.load();
                g_hasUnsavedBlockKeyChange = false;
            }
            if (g_hasUnsavedControllerBlockKeyChange) {
                g_pendingControllerBlockKey = g_controllerBlockKey.load();
                g_hasUnsavedControllerBlockKeyChange = false;
            }
            if (g_hasUnsavedBlockModChange) {
                g_pendingBlockModifier = g_blockModifier.load();
                g_hasUnsavedBlockModChange = false;
            }
            if (g_hasUnsavedEnabledChange) {
                g_pendingPluginEnabled = g_pluginEnabled.load();
                g_hasUnsavedEnabledChange = false;
            }
        }
    }

    // The merged DLL uses ForHonorBlocking's SetupLog() from plugin.cpp.
}

namespace BlockKey {
    // Rendered by Menu::Install() inside For Honor Blocking > Inputs.
    // Do not register a second SKSE Menu Framework section here.
    void RenderInputSettings() { RenderMenuFrameworkSection(); }

    void OnDataLoaded() {
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::critical("[BLOCK KEY] SKSE Menu Framework not installed.");
            return;
        }

        LoadConfig();
        if (!g_menuFrameworkInputHook) {
            g_menuFrameworkInputHook = SKSEMenuFramework::AddInputEvent(OnMenuFrameworkInput);
        }
        if (!g_menuFrameworkEventHook) {
            g_menuFrameworkEventHook = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);
        }

        SKSE::log::info("[BLOCK KEY] Initialized keyboardBlock={} controllerBlock={} modifier={}; SPA chord coordination direct",
                        g_altBlockKey.load(), g_controllerBlockKey.load(), g_blockModifier.load());
    }

    void OnGameLoaded() {
        g_blockComboActive = false;
        g_keyboardBlockDown = false;
        g_controllerBlockDown = false;
        for (auto& key : g_keyStates) {
            key.store(false, std::memory_order_relaxed);
        }
    }
}
