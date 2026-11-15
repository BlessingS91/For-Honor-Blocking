#pragma once

// Self-contained attack cancellation component.
// Integration with plugin.cpp, Menu.cpp, and Settings.cpp is intentionally left for later.
class AttackCancel final {
public:
    struct Config {
        bool enabled{true};
        int windowMs{250};         // Timed mode: 0 disables cancels; otherwise 0–1000 ms.
        bool useWeaponSwingWindow{false};  // If true, cancel only before weaponSwing/weaponLeftSwing.
        float staminaCost{15.0f};  // 0 makes cancels free; otherwise 0–100.
    };

    // One shared set of runtime options. The existing main INI can load/save
    // these values when the module is integrated later. No separate INI.
    [[nodiscard]] static Config& GetConfig();

    // These are NOT called automatically. Wire them up later.
    static void Install();         // Install root-motion correction (BlockKey owns inputs).
    static void OnBlockPressed();  // Called by BlockKey on a valid fresh block-key press.
    static void Update();          // Call from the existing player update hook.
    static void Reset();           // Clear transient attack state (not Config).
    static void RenderSettings();  // Register as its own SKSE Menu Framework tab later.

    AttackCancel() = delete;
};
