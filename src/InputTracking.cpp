#include "InputTracking.h"

#include <SKSEMenuFramework.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>

#include "Settings.h"
#include "logger.h"
namespace {
    constexpr float kMouseClamp = 300.0f;
    constexpr float kStickActivityThreshold = 0.1f;

    // Monotonic timestamp used for inactivity decay (no timer/dispatcher).
    std::uint64_t SteadyMilliseconds() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }
    // Settings are managed by For Honor Blocking and persisted in its INI.
    std::atomic<float> g_mouseSensitivity{1.0f};
    // Accumulated mouse movement needed to activate an axis; not a neutral gap between directions.
    std::atomic<float> g_mouseDeadzone{4.0f};
    std::atomic<float> g_stickSensitivity{1.0f};
    // Stick magnitude required to enter a direction (exit uses 0.12 less for hysteresis).
    std::atomic<float> g_stickDeadzone{0.50f};
    std::atomic<float> g_stickSmoothing{0.25f};
    std::atomic<int> g_topSectorDegrees{120};
    std::atomic<int> g_bottomNeutralAngleDegrees{30};
    // Extra clearance around neutral, relative to the existing device deadzone.
    std::atomic<int> g_centerNeutralZonePercent{20};
    // Time a non-neutral candidate must stay selected; 0 = immediate switching.
    std::atomic<int> g_directionSwitchDelayMs{0};
    std::atomic<int> g_neutralDecayMs{500};
    // Both input devices use their existing deadzone as the baseline.
    // This extends the neutral region without a second, conflicting threshold.
    float EffectiveDeadzone(float base, int neutralPercent) {
        return base * (1.0f + static_cast<float>(neutralPercent) / 100.0f);
    }
    // Only the direction tracker's input callback updates these filter states.
    float LowPass(float previous, float target, float smoothing) {
        // 0 = no smoothing, 0.85 = moderately strong smoothing.
        const float alpha = 1.0f - std::clamp(smoothing, 0.0f, 0.85f);
        return previous + (target - previous) * alpha;
    }
    // Simple mouse accumulator: zero delta on one axis does not
    // erase that axis. Restart on an actual reversal to avoid sticky-left/right.
    void TrackMouseAxis(float delta, float& accumulated) {
        if (delta == 0.0f) return;
        if (accumulated * delta < 0.0f) accumulated = 0.0f;
        accumulated = std::clamp(accumulated + delta, -kMouseClamp, kMouseClamp);
    }
    bool IsBottomNeutral(float x, float y, int bottomAngleDegrees) {
        if (bottomAngleDegrees <= 0 || y >= 0.0f) return false;
        constexpr float kRadiansToDegrees = 57.29577951308232f;
        const float angleFromDown = std::atan2(x, -y) * kRadiansToDegrees;
        return std::abs(angleFromDown) <= static_cast<float>(bottomAngleDegrees) * 0.5f;
    }
    // The input plane has three angular sectors, centered on up, right, and left.
    // The top sector consumes topSectorDegrees; each side receives half of the remainder.
    // The magnitude threshold retains the existing deadzone hysteresis independently
    // from which direction is selected. Values: 0 neutral, 1 top, 2 right, 3 left.
    int ClassifyDirectionSector(float x, float y, float enter, float exit, int previousDirection, int topSectorDegrees,
                                int bottomNeutralAngleDegrees) {
        const float magnitude = std::hypot(x, y);
        if (magnitude <= (previousDirection == 0 ? enter : exit)) {
            return 0;
        }
        // atan2(x, y) measures the signed angle relative to the upward axis.
        constexpr float kRadiansToDegrees = 57.29577951308232f;
        const float angleFromUp = std::atan2(x, y) * kRadiansToDegrees;
        const float halfTop = static_cast<float>(topSectorDegrees) * 0.5f;
        if (std::abs(angleFromUp) < halfTop) {
            return 1;
        }
        // Keep a configurable neutral wedge centered on straight down.
        if (IsBottomNeutral(x, y, bottomNeutralAngleDegrees)) {
            return 0;
        }
        if (x > 0.0f) {
            return 2;
        }
        if (x < 0.0f) {
            return 3;
        }
        // Exactly downward input has no side. The bottom boundary is shared
        // by left/right, so it stays neutral when X is exactly zero.
        return 0;
    }
}
namespace InputTrackingTuning {
    Values Get() {
        return {g_mouseSensitivity.load(std::memory_order_relaxed),
                g_mouseDeadzone.load(std::memory_order_relaxed),
                g_stickSensitivity.load(std::memory_order_relaxed),
                g_stickDeadzone.load(std::memory_order_relaxed),
                g_stickSmoothing.load(std::memory_order_relaxed),
                g_topSectorDegrees.load(std::memory_order_relaxed),
                g_bottomNeutralAngleDegrees.load(std::memory_order_relaxed),
                g_centerNeutralZonePercent.load(std::memory_order_relaxed),
                g_directionSwitchDelayMs.load(std::memory_order_relaxed),
                g_neutralDecayMs.load(std::memory_order_relaxed)};
    }
    void Set(const Values& settings) {
        // Reject NaN/inf from malformed settings before clamping.
        const auto safe = [](float value, float fallback, float minimum, float maximum) {
            return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
        };
        g_mouseSensitivity.store(safe(settings.mouseSensitivity, 1.0f, 0.10f, 5.0f), std::memory_order_relaxed);
        g_mouseDeadzone.store(safe(settings.mouseDeadzone, 4.0f, 0.0f, 30.0f), std::memory_order_relaxed);
        g_stickSensitivity.store(safe(settings.controllerSensitivity, 1.0f, 0.30f, 2.5f), std::memory_order_relaxed);
        g_stickDeadzone.store(safe(settings.controllerDeadzone, 0.50f, 0.15f, 0.90f), std::memory_order_relaxed);
        g_stickSmoothing.store(safe(settings.controllerSmoothing, 0.25f, 0.0f, 0.85f), std::memory_order_relaxed);
        g_topSectorDegrees.store(std::clamp(settings.topSectorDegrees, 30, 240), std::memory_order_relaxed);
        g_bottomNeutralAngleDegrees.store(std::clamp(settings.bottomNeutralAngleDegrees, 0, 120),
                                          std::memory_order_relaxed);
        g_centerNeutralZonePercent.store(std::clamp(settings.centerNeutralZonePercent, 0, 100),
                                         std::memory_order_relaxed);
        g_directionSwitchDelayMs.store(std::clamp(settings.directionSwitchDelayMs, 0, 500), std::memory_order_relaxed);
        g_neutralDecayMs.store(std::clamp(settings.neutralDecayMs, 0, 2000), std::memory_order_relaxed);
    }
    void Reset() { Set(Values{}); }
}
DirectionTracker* DirectionTracker::GetSingleton() {
    static DirectionTracker singleton;
    return std::addressof(singleton);
}
void DirectionTracker::Initialize() {
    if (initialized.exchange(true)) {
        return;
    }
    auto* inputDispatcher = RE::BSInputDeviceManager::GetSingleton();
    if (inputDispatcher) {
        inputDispatcher->AddEventSink(this);
    }
    // Establish the initial neutral graph state immediately.
    lastCameraActivityMs.store(0, std::memory_order_relaxed);
    pendingCameraState = -1;
    pendingCameraStateSinceMs = 0;
    ApplyAndDispatchState(0);
    logger::info("DirectionTracker initialized");
}
void DirectionTracker::Shutdown() {
    if (!initialized.exchange(false)) {
        return;
    }
    auto* inputDispatcher = RE::BSInputDeviceManager::GetSingleton();
    if (inputDispatcher) {
        inputDispatcher->RemoveEventSink(this);
    }
    lastCameraActivityMs.store(0, std::memory_order_relaxed);
    pendingCameraState = -1;
    pendingCameraStateSinceMs = 0;
    ResetCameraInput();
    logger::info("DirectionTracker shutdown");
}
RE::BSEventNotifyControl DirectionTracker::ProcessEvent(RE::InputEvent* const* a_event,
                                                        RE::BSTEventSource<RE::InputEvent*>*) {
    if (!initialized || !a_event || !*a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    bool stateChanged = false;
    for (auto* event = *a_event; event; event = event->next) {
        if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kMouseMove) {
            const auto* mouseEvent = static_cast<RE::MouseMoveEvent*>(event);
            if (mouseEvent->mouseInputX == 0 && mouseEvent->mouseInputY == 0) continue;

            lastCameraActivityMs.store(SteadyMilliseconds(), std::memory_order_release);

            const float sensitivity = g_mouseSensitivity.load(std::memory_order_relaxed);
            const float deadzone = EffectiveDeadzone(g_mouseDeadzone.load(std::memory_order_relaxed),
                                                     g_centerNeutralZonePercent.load(std::memory_order_relaxed));
            const float deltaX = static_cast<float>(mouseEvent->mouseInputX) * sensitivity;
            const float deltaY = -static_cast<float>(mouseEvent->mouseInputY) * sensitivity;

            // Preserve the responsive per-axis mouse tracking. No angle-sector
            // classifier or old-direction fallback participates in mouse input.
            TrackMouseAxis(deltaX, mouseCamX);
            TrackMouseAxis(deltaY, mouseCamY);

            // Mouse movement is relative. A previous large horizontal input
            // must not prevent a new upward movement from selecting top.
            // Resolve competing axes using this event's movement, not the
            // camera orientation or older accumulated mouse history.
            if (std::abs(mouseCamX) > deadzone && std::abs(mouseCamY) > deadzone) {
                const bool downNeutral =
                    IsBottomNeutral(deltaX, deltaY, g_bottomNeutralAngleDegrees.load(std::memory_order_relaxed));
                if ((deltaY > 0.0f && std::abs(deltaY) >= std::abs(deltaX)) || downNeutral)
                    mouseCamX = 0.0f;
                else if (std::abs(deltaX) > std::abs(deltaY) || deltaY < 0.0f)
                    mouseCamY = 0.0f;
            }

            const bool newUp = mouseCamY > deadzone;
            const bool newDown = mouseCamY < -deadzone;
            const bool newLeft = mouseCamX < -deadzone;
            const bool newRight = mouseCamX > deadzone;

            // Prefer the last active device. Right-stick flags from an earlier
            // controller event must not interfere with current mouse movement.
            if (rs_up || rs_down || rs_left || rs_right) {
                rs_up = rs_down = rs_left = rs_right = false;
                stateChanged = true;
            }
            if (newUp != m_up || newDown != m_down || newLeft != m_left || newRight != m_right) {
                m_up = newUp;
                m_down = newDown;
                m_left = newLeft;
                m_right = newRight;
                stateChanged = true;
            }
        } else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
            auto* stickEvent = static_cast<RE::ThumbstickEvent*>(event);
            if (!IsRightStickInput(stickEvent)) {
                continue;
            }
            const float x = stickEvent->xValue;
            const float y = stickEvent->yValue;
            const float sensitivity = g_stickSensitivity.load(std::memory_order_relaxed);
            const float enter =
                std::min(0.95f, EffectiveDeadzone(g_stickDeadzone.load(std::memory_order_relaxed),
                                                  g_centerNeutralZonePercent.load(std::memory_order_relaxed)));
            // Keep the original 0.12 hysteresis gap to avoid edge flicker.
            const float exit = std::max(0.0f, enter - 0.12f);
            const float smoothing = g_stickSmoothing.load(std::memory_order_relaxed);
            // Thumbstick X/Y are absolute positions. A reversal must be
            // classified from the new sign immediately, never from old leftward
            // filter history. Smoothing still applies while moving on one side.
            if (x == 0.0f || filteredStickX * x < 0.0f)
                filteredStickX = x * sensitivity;
            else
                filteredStickX = LowPass(filteredStickX, x * sensitivity, smoothing);

            if (y == 0.0f || filteredStickY * y < 0.0f)
                filteredStickY = y * sensitivity;
            else
                filteredStickY = LowPass(filteredStickY, y * sensitivity, smoothing);

            const int oldDirection = rs_up ? 1 : (rs_right ? 2 : (rs_left ? 3 : 0));
            int newDirection = ClassifyDirectionSector(filteredStickX, filteredStickY, enter, exit, oldDirection,
                                                       g_topSectorDegrees.load(std::memory_order_relaxed),
                                                       g_bottomNeutralAngleDegrees.load(std::memory_order_relaxed));

            // Do not preserve the previous side while the physical stick is
            // pointing across it. It must be able to pass through neutral.
            // Do not restore a stale direction after entering the release
            // radius; this previously prevented neutral with a slightly noisy stick.
            const bool insideNeutralRadius = std::hypot(filteredStickX, filteredStickY) <= exit;
            if (newDirection == 0 && oldDirection != 0 && !insideNeutralRadius &&
                std::hypot(x, y) > kStickActivityThreshold &&
                !IsBottomNeutral(x, y, g_bottomNeutralAngleDegrees.load(std::memory_order_relaxed))) {
                const bool oppositeSide = (oldDirection == 3 && x > 0.0f) || (oldDirection == 2 && x < 0.0f) ||
                                          (oldDirection == 1 && y < 0.0f);
                if (!oppositeSide) newDirection = oldDirection;
            }
            const bool newUp = newDirection == 1;
            const bool newDown = false;
            const bool newLeft = newDirection == 3;
            const bool newRight = newDirection == 2;
            if (std::abs(x) > kStickActivityThreshold || std::abs(y) > kStickActivityThreshold) {
                lastCameraActivityMs.store(SteadyMilliseconds(), std::memory_order_release);
                mouseCamX = 0.0f;
                mouseCamY = 0.0f;
                if (m_up || m_down || m_left || m_right) {
                    m_up = false;
                    m_down = false;
                    m_left = false;
                    m_right = false;
                    stateChanged = true;
                }
            }
            if (newUp != rs_up || newDown != rs_down || newLeft != rs_left || newRight != rs_right) {
                logger::debug(
                    "[InputTracking] RightStick x={:.3f} y={:.3f} filteredX={:.3f} filteredY={:.3f} direction={}",

                    x, y, filteredStickX, filteredStickY, newDirection);
                rs_up = newUp;
                rs_down = newDown;
                rs_left = newLeft;
                rs_right = newRight;
                stateChanged = true;
            }
        }
    }
    if (stateChanged) {
        UpdateDirection();
    }
    return RE::BSEventNotifyControl::kContinue;
}
bool DirectionTracker::IsRightStickInput(const RE::ThumbstickEvent* a_event) const {
    if (!a_event) {
        return false;
    }
    return a_event->IsRight();
}
void DirectionTracker::UpdateDirection() {
    const bool CAM_UP = rs_up || m_up;
    const bool CAM_LEFT = rs_left || m_left;
    const bool CAM_RIGHT = rs_right || m_right;
    const bool mouseDownNeutral =
        m_down && IsBottomNeutral(mouseCamX, mouseCamY, g_bottomNeutralAngleDegrees.load(std::memory_order_relaxed));
    int CameraMovementFHB = 0;
    // The last active input device owns these flags. Up must be selectable
    // even if an earlier horizontal movement briefly remains in the history.
    // Straight down selects neutral, while diagonally down can select a side.
    if (mouseDownNeutral)
        CameraMovementFHB = 0;
    else if (CAM_UP)
        CameraMovementFHB = 1;
    else if (CAM_RIGHT)
        CameraMovementFHB = 2;
    else if (CAM_LEFT)
        CameraMovementFHB = 3;
    RequestDirectionState(CameraMovementFHB);
}
void DirectionTracker::ResetDirectionState() {
    lastCameraActivityMs.store(0, std::memory_order_release);
    pendingCameraState = -1;
    pendingCameraStateSinceMs = 0;
    ResetCameraInput();
    ApplyAndDispatchState(0);
}
// ============================================================================
// INPUT TRACKING - SWITCH DEBOUNCE AND INACTIVITY DECAY
// Reuses the existing PlayerCharacter::Update hook. No dispatcher or new hook.
// ============================================================================
void DirectionTracker::UpdateDecay() {
    if (!initialized.load(std::memory_order_acquire)) return;

    const auto now = SteadyMilliseconds();
    const int decayMs = g_neutralDecayMs.load(std::memory_order_relaxed);

    // Inactivity takes priority over a pending switch. Do not commit stale input.
    if (decayMs > 0) {
        auto lastActivity = lastCameraActivityMs.load(std::memory_order_acquire);
        if (lastActivity != 0 && now - lastActivity >= static_cast<std::uint64_t>(decayMs)) {
            // Only reset once per inactivity period.
            if (lastCameraActivityMs.compare_exchange_strong(lastActivity, 0, std::memory_order_acq_rel,
                                                             std::memory_order_relaxed)) {
                pendingCameraState = -1;
                pendingCameraStateSinceMs = 0;
                ResetCameraInput();
                ApplyAndDispatchState(0);
            }
            return;
        }
    }

    // A new direction is applied only after it remains selected for the set time.
    if (pendingCameraState != -1) {
        const int delayMs = g_directionSwitchDelayMs.load(std::memory_order_relaxed);
        if (delayMs <= 0 || now - pendingCameraStateSinceMs >= static_cast<std::uint64_t>(delayMs)) {
            const int nextDirection = pendingCameraState;
            pendingCameraState = -1;
            pendingCameraStateSinceMs = 0;
            if (nextDirection != lastWrittenCameraState) ApplyAndDispatchState(nextDirection);
        }
    }
}

void DirectionTracker::RequestDirectionState(int a_cameraState) {
    a_cameraState = std::clamp(a_cameraState, 0, 3);

    // Read the graph so other graph writers cannot leave the cached state stale.
    int activeState = lastWrittenCameraState;
    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        std::int32_t graphState = 0;
        if (player->GetGraphVariableInt("CameraMovementFHB", graphState) && graphState >= 0 && graphState <= 3) {
            activeState = graphState;
            lastWrittenCameraState = graphState;
        }
    }

    // Returning to the active direction cancels a switch in progress.
    if (a_cameraState == activeState) {
        pendingCameraState = -1;
        pendingCameraStateSinceMs = 0;
        return;
    }

    // Neutral input remains immediate; there is no forced neutral transition.
    if (a_cameraState == 0) {
        pendingCameraState = -1;
        pendingCameraStateSinceMs = 0;
        ApplyAndDispatchState(0);
        return;
    }

    const int delayMs = g_directionSwitchDelayMs.load(std::memory_order_relaxed);
    if (delayMs <= 0) {
        pendingCameraState = -1;
        pendingCameraStateSinceMs = 0;
        ApplyAndDispatchState(a_cameraState);
        return;
    }

    // New candidate: restart the delay. Repeated input in the same direction
    // does not restart it, so a stable choice can complete the transition.
    if (a_cameraState != pendingCameraState) {
        pendingCameraState = a_cameraState;
        pendingCameraStateSinceMs = SteadyMilliseconds();
    }
}
void DirectionTracker::ApplyAndDispatchState(int a_cameraState) { CommitCameraState(a_cameraState); }
void DirectionTracker::CommitCameraState(int a_cameraState) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        logger::error("[Camera] PlayerCharacter unavailable");
        return;
    }
    const RE::BSFixedString variable("CameraMovementFHB");
    a_cameraState = std::clamp(a_cameraState, 0, 3);
    const bool written = player->SetGraphVariableInt(variable, a_cameraState);
    std::int32_t actual = 0;
    if (!player->GetGraphVariableInt(variable, actual)) {
        logger::warn("[Camera] Could not read back CameraMovementFHB after write; HUD update skipped.");
        return;
    }
    if (!written || actual != a_cameraState) {
        logger::warn("[Camera] Direction write mismatch: requested={} actual={} written={}", a_cameraState, actual,
                     written);
    }
    // Only a confirmed write becomes the tracker's committed direction.
    // An unsuccessful write must not replace our desired state with an
    // unrelated graph value from another writer.
    if (written && actual == a_cameraState) lastWrittenCameraState = actual;

    // The HUD reads this graph variable directly during UpdateFrame().
    // Do not publish a second, potentially stale direction through mod events.
}
void DirectionTracker::ResetCameraInput() {
    std::scoped_lock lock(stateMutex);
    mouseCamX = 0.0f;
    mouseCamY = 0.0f;
    filteredStickX = 0.0f;
    filteredStickY = 0.0f;
    m_up = false;
    m_down = false;
    m_left = false;
    m_right = false;
    rs_up = false;
    rs_down = false;
    rs_left = false;
    rs_right = false;
}

// ============================================================================
// INPUT TRACKING SETTINGS MENU - For Honor Blocking > Input Tracking
// Direction classification, switch debounce, and inactivity decay controls.
// ============================================================================
namespace InputTrackingTuning {
    void RenderSettings() {
        namespace ui = ImGuiMCP;
        auto tuning = Get();
        bool changed = false;

        ui::Text("Directional Input Tracking");
        ui::Separator();

        ui::Text("Mouse");
        changed |= ui::SliderFloat("Mouse Sensitivity", &tuning.mouseSensitivity, 0.01f, 5.0f, "%.2f");
        changed |= ui::SliderFloat("Mouse Deadzone", &tuning.mouseDeadzone, 0.0f, 150.0f, "%.1f");

        ui::Separator();

        ui::Text("Controller Right Stick");
        changed |= ui::SliderFloat("Stick Sensitivity", &tuning.controllerSensitivity, 0.05f, 2.5f, "%.2f");
        changed |= ui::SliderFloat("Stick Deadzone", &tuning.controllerDeadzone, 0.0f, 1.0f, "%.2f");
        changed |= ui::SliderFloat("Stick Smoothing", &tuning.controllerSmoothing, 0.0f, 0.85f, "%.2f");

        ui::Separator();

        ui::Text("Directional Zones");
        changed |= ui::SliderInt("Top Sector (degrees)", &tuning.topSectorDegrees, 30, 240);
        changed |= ui::SliderInt("Bottom Neutral Zone (degrees)", &tuning.bottomNeutralAngleDegrees, 0, 180);

        ui::Text("Center zone extends mouse/stick deadzones; 0%% = original behavior.");
        changed |= ui::SliderInt("Center Neutral Zone (%)", &tuning.centerNeutralZonePercent, 0, 100);

        ui::Separator();

        ui::Text("Direction Switching");
        ui::Text("New directions must remain selected for this long. 0 = instant.");
        changed |= ui::SliderInt("Direction Switch Delay (ms)", &tuning.directionSwitchDelayMs, 0, 500);

        ui::Separator();

        ui::Text("Inactivity Reset");
        ui::Text("Return to Neutral when no mouse/right-stick movement is detected. 0 disables.");
        changed |= ui::SliderInt("Return to Neutral After Inactivity (ms)", &tuning.neutralDecayMs, 0, 2000);

        if (changed) {
            Set(tuning);
            Settings::Save();
        }

        ui::Separator();

        if (ui::Button("Reset Input Tracking Defaults")) {
            Reset();
            Settings::Save();
        }
    }
}
