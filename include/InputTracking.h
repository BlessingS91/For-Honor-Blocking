#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

// For Honor Blocking loads and saves these values through Settings.cpp.
namespace InputTrackingTuning
{
    struct Values
    {
        float mouseSensitivity = 1.0f;
        float mouseDeadzone = 4.0f;
        float controllerSensitivity = 1.0f;
        float controllerDeadzone = 0.50f;
        float controllerSmoothing = 0.25f;
        // Full angular width of the top input sector. Left and right share the remainder equally.
        int topSectorDegrees = 120;
        // Full angular width of the neutral sector centered on straight down.
        int bottomNeutralAngleDegrees = 30;
        // Extra center clearance as a percentage of each device's deadzone.
        // 0 preserves the original thresholds. Separate from the bottom angular wedge.
        int centerNeutralZonePercent = 20;
        // Time a new non-neutral direction must remain selected before switching.
        // 0 disables direction-switch delay; no forced intermediate Neutral state.
        int directionSwitchDelayMs = 0;
        // Milliseconds with no camera movement before resetting to Neutral (0).
        // Set to 0 to disable automatic decay.
        int neutralDecayMs = 500;
    };

    Values Get();
    void Set(const Values& settings);
    void Reset();
    // SKSE Menu Framework page; kept with this module, not Menu.cpp.
    void RenderSettings();
}

class DirectionTracker final : public RE::BSTEventSink<RE::InputEvent*>
{
public:
    static DirectionTracker* GetSingleton();

    void Initialize();
    void Shutdown();

    // Called by the existing player update hook to commit stable direction changes
    // and process inactivity decay. No timer or dispatcher.
    void UpdateDecay();

    void ApplyAndDispatchState(int a_cameraState);

    // Reset tracked input and clear the graph direction on game/load reset.
    void ResetDirectionState();

    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

private:
    DirectionTracker() = default;
    ~DirectionTracker() = default;

    DirectionTracker(const DirectionTracker&) = delete;
    DirectionTracker(DirectionTracker&&) = delete;
    DirectionTracker& operator=(const DirectionTracker&) = delete;
    DirectionTracker& operator=(DirectionTracker&&) = delete;

    void UpdateDirection();

    void RequestDirectionState(int a_cameraState);

    void ResetCameraInput();

    void CommitCameraState(int a_cameraState);

    bool IsRightStickInput(const RE::ThumbstickEvent* a_event) const;

    float mouseCamX{0.0f};
    float mouseCamY{0.0f};

    // Controller filtering remains separate from raw mouse movement.
    float filteredStickX{0.0f};
    float filteredStickY{0.0f};

    bool m_up{false};
    bool m_down{false};
    bool m_left{false};
    bool m_right{false};

    bool rs_up{false};
    bool rs_down{false};
    bool rs_left{false};
    bool rs_right{false};

    std::atomic_bool initialized{false};
    std::mutex stateMutex{};

    int lastWrittenCameraState{-1};

    // Direction-change debounce. A new direction must remain selected for the
    // configured duration; rapid stick rotations keep restarting the candidate.
    int pendingCameraState{-1};
    std::uint64_t pendingCameraStateSinceMs{0};

    // Updated for any nonzero mouse movement or right-stick activity.
    std::atomic<std::uint64_t> lastCameraActivityMs{0};

};
