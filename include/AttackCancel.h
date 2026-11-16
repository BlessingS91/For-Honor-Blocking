#pragma once

// Attack cancel gameplay. Bindings are configured separately in BlockKey.cpp.
class AttackCancel final {
public:
    struct Config {
        bool enabled{true};
        int windowMs{250};         // Timed mode: 0 disables cancels; otherwise 0–1000 ms.
        bool useWeaponSwingWindow{false};  // If true, cancel only before weaponSwing/weaponLeftSwing.
        float staminaCost{15.0f};  // 0 makes cancels free; otherwise 0–100.
    };

    // Existing gameplay settings are stored in ForHonorBlocking.ini.
    // Separate keyboard/controller bindings are stored in SimpleBlock.ini.
    [[nodiscard]] static Config& GetConfig();

    static void Install();         // Existing plugin registration and root-motion hook.
    static void OnCancelPressed(); // Called on a fresh configured cancel-key press.
    static void Update();          // Existing player update hook.
    static void Reset();           // Reset per-attack state, preserving Config.
    static void RenderSettings();  // Existing Attack Cancel settings panel.

    AttackCancel() = delete;
};
