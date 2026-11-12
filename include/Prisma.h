#pragma once

#include <cstdint>
#include <vector>

struct AttackWarningVisual {
    std::uint32_t id{0};
    int direction{0}; // NPC: 1 top, 2 right, 3 left (mirrored in the UI)
    float xPercent{50.0f};
    float yPercent{50.0f};
    float resolutionScale{1.0f};
    float distanceScale{1.0f};
    bool hitFrameFlash{false};
    bool unblockable{false};
};

class Prisma {
public:
    static void Install();
    static void Preload();
    static void Show();
    static void Hide();
    static bool IsHidden();
    static bool IsReady();
    static void ApplyUISettings();

    // Read the player animation graph and update the PrismaUI shield each frame.
    // Called by the existing PlayerCharacter::Update hook; no new hooks.
    static void UpdatePlayerHUD();
    static void UpdateParrySuccess(bool success);
    static void UpdateDirection(int direction);
    static void UpdateAllBlock(bool allBlock);
    static void UpdateUnblockable(bool unblockable);
    static void UpdateRuntimeState(bool eligible, bool previewEligible, float xPercent,
                                   float yPercent, float resolutionScale, bool attached);
    static void UpdateAttackWarnings(const std::vector<AttackWarningVisual>& warnings);
    static void Reset();
    // Each UI tab renders its own controls; Menu.cpp only registers these.
    static void RenderPlayerSettings();
    static void RenderNPCSettings();
};
