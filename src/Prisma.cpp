#include "Prisma.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Settings.h"
#include "logger.h"

#include <SKSEMenuFramework.h>

#include "PrismaUI_API.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace
{
    PRISMA_UI_API::IVPrismaUI1* prismaUI = nullptr;
    PrismaView view = 0;
    bool domReady = false;
    // Committed player graph direction: 0 neutral, 1 top, 2 right, 3 left.
    int currentDirection = 0;
    bool currentAllBlock = false;
    bool currentBlocking = false;
    bool currentUnblockable = false;
    bool currentParrySuccess = false;
    bool runtimeEligible = false;
    bool runtimePreviewEligible = false;
    bool runtimeAttached = false;
    float runtimeXPercent = 50.0f;
    float runtimeYPercent = 50.0f;
    float runtimeResolutionScale = 1.0f;
    std::vector<AttackWarningVisual> attackWarnings;
    void SyncVisibility();
    bool IsViewValid() { return prismaUI && view && prismaUI->IsValid(view); }
    void WriteColor(rapidjson::Writer<rapidjson::StringBuffer>& writer, const std::array<float, 4>& color)
    {
        writer.StartArray();
        for (const float component : color)
        {
            writer.Double(component);
        }
        writer.EndArray();
    }

    std::string BuildSettingsPayload()
    {
        const auto& ui = Settings::PlayerUI;
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("enabled");
        writer.Bool(ui.enabled);
        writer.Key("editMode");
        writer.Bool(ui.editMode);
        writer.Key("showCenter");
        writer.Bool(ui.showCenter);
        writer.Key("moveCenterWithDirection");
        writer.Bool(ui.moveCenterWithDirection);
        writer.Key("centerInThirdPerson");
        writer.Bool(ui.centerInThirdPerson);
        writer.Key("scaleWithResolution");
        writer.Bool(ui.scaleWithResolution);
        writer.Key("positionXPercent");
        writer.Int(ui.positionXPercent);
        writer.Key("positionYPercent");
        writer.Int(ui.positionYPercent);
        writer.Key("attachOffsetXPixels");
        writer.Int(ui.attachOffsetXPixels);
        writer.Key("attachOffsetYPixels");
        writer.Int(ui.attachOffsetYPixels);
        writer.Key("scalePercent");
        writer.Int(ui.scalePercent);
        writer.Key("diameterPixels");
        writer.Int(ui.diameterPixels);
        writer.Key("centerPieceSizePercent");
        writer.Int(ui.centerPieceSizePercent);
        writer.Key("rotationDegrees");
        writer.Int(ui.rotationDegrees);
        writer.Key("opacityPercent");
        writer.Int(ui.opacityPercent);
        writer.Key("activeOpacityPercent");
        writer.Int(ui.activeOpacityPercent);
        writer.Key("centerMovementPixels");
        writer.Int(ui.centerMovementPixels);
        writer.Key("centerMovementDurationMs");
        writer.Int(ui.centerMovementDurationMs);
        writer.Key("activeColor");
        WriteColor(writer, ui.activeColor);
        writer.Key("unblockableColor");
        WriteColor(writer, ui.unblockableColor);
        writer.Key("inactiveColor");
        WriteColor(writer, ui.inactiveColor);
        writer.Key("borderColor");
        WriteColor(writer, ui.borderColor);
        writer.Key("centerColor");
        WriteColor(writer, ui.centerColor);
        writer.Key("centerActiveColor");
        WriteColor(writer, ui.centerActiveColor);
        // Empty paths tell PrismaUI to load images from its default assets folder.
        // This also overrides obsolete saved custom paths.
        writer.Key("sharedSegmentImage");
        writer.String("");
        writer.Key("segmentImages");
        writer.StartArray();
        for (std::size_t i = 0; i < ui.segmentImages.size(); ++i)
        {
            writer.String("");
        }
        writer.EndArray();
        writer.Key("centerImage");
        writer.String("");
        const auto& npc = Settings::NPCUI;
        writer.Key("npc");
        writer.StartObject();
        writer.Key("enabled");
        writer.Bool(npc.enabled);
        writer.Key("showCenter"); writer.Bool(npc.showCenter);
        writer.Key("centerPieceSizePercent"); writer.Int(npc.centerPieceSizePercent);
        writer.Key("centerColor"); WriteColor(writer, npc.centerColor);
        writer.Key("centerImage"); writer.String("");
        writer.Key("alwaysShowInCombat");
        writer.Bool(npc.alwaysShowInCombat);
        writer.Key("scaleWithResolution");
        writer.Bool(npc.scaleWithResolution);
        writer.Key("scaleWithDistance");
        writer.Bool(npc.scaleWithDistance);
        writer.Key("mirrorAttackDirections");
        writer.Bool(npc.mirrorAttackDirections);
        writer.Key("scalePercent");
        writer.Int(npc.scalePercent);
        writer.Key("opacityPercent");
        writer.Int(npc.opacityPercent);
        writer.Key("activeOpacityPercent");
        writer.Int(npc.activeOpacityPercent);
        writer.Key("offsetXPixels");
        writer.Int(npc.offsetXPixels);
        writer.Key("offsetYPixels");
        writer.Int(npc.offsetYPixels);
        writer.Key("rotationDegrees");
        writer.Int(npc.rotationDegrees);
        writer.Key("diameterPixels");
        writer.Int(npc.diameterPixels);
        writer.Key("activeColor");
        WriteColor(writer, npc.activeColor);
        writer.Key("unblockableColor");
        WriteColor(writer, npc.unblockableColor);
        writer.Key("inactiveColor");
        WriteColor(writer, npc.inactiveColor);
        writer.Key("borderColor");
        WriteColor(writer, npc.borderColor);
        writer.Key("sharedSegmentImage");
        writer.String("");
        writer.Key("segmentImages");
        writer.StartArray();
        for (std::size_t i = 0; i < npc.segmentImages.size(); ++i)
        {
            writer.String("");
        }
        writer.EndArray();
        writer.EndObject();
        writer.EndObject();
        return buffer.GetString();
    }

    void SendSettings()
    {
        if (!domReady || !IsViewValid())
        {
            return;
        }
        static std::string payload;
        payload = BuildSettingsPayload();
        prismaUI->InteropCall(view, "updateForHonorCombatSettings", payload.c_str());
    }

    void SendDirection()
    {
        if (!domReady || !IsViewValid())
        {
            return;
        }
        static std::string payload;
        payload = std::to_string(currentDirection) + "|" + (currentAllBlock ? "1" : "0") + "|" + (currentUnblockable ? "1" : "0") + "|" + (currentParrySuccess ? "1" : "0") + "|" + (currentBlocking ? "1" : "0");
        prismaUI->InteropCall(view, "updateForHonorCombatDirection", payload.c_str());
    }

    void SendRuntimeState()
    {
        if (!domReady || !IsViewValid())
        {
            return;
        }
        char payload[128]{};
        std::snprintf(payload, sizeof(payload), "%d|%d|%.3f|%.3f|%.3f|%d", runtimeEligible ? 1 : 0, runtimePreviewEligible ? 1 : 0, runtimeXPercent, runtimeYPercent, runtimeResolutionScale, runtimeAttached ? 1 : 0);
        prismaUI->InteropCall(view, "updateForHonorCombatRuntime", payload);
    }

    void SendAttackWarnings()
    {
        if (!domReady || !IsViewValid())
        {
            return;
        }
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartArray();
        for (const auto& warning : attackWarnings)
        {
            writer.StartObject();
            writer.Key("id");
            writer.Uint(warning.id);
            writer.Key("direction");
            writer.Int(warning.direction);
            writer.Key("xPercent");
            writer.Double(warning.xPercent);
            writer.Key("yPercent");
            writer.Double(warning.yPercent);
            writer.Key("resolutionScale");
            writer.Double(warning.resolutionScale);
            writer.Key("distanceScale");
            writer.Double(warning.distanceScale);
            writer.Key("hitFrameFlash");
            writer.Bool(warning.hitFrameFlash);
            writer.Key("unblockable");
            writer.Bool(warning.unblockable);
            writer.EndObject();
        }
        writer.EndArray();
        prismaUI->InteropCall(view, "updateForHonorCombatAttackWarnings", buffer.GetString());
    }

    void SendAllState()
    {
        SendSettings();
        SendDirection();
        SendRuntimeState();
        SendAttackWarnings();
    }

    bool EnsureView()
    {
        if (!prismaUI)
        {
            return false;
        }
        if (IsViewValid())
        {
            return true;
        }
        view = 0;
        domReady = false;
#ifdef DEV_SERVER
        constexpr const char* path = "http://localhost:5173";
#else
        constexpr const char* path = "ForHonorCombat/index.html";
#endif
        view = prismaUI->CreateView(path,
                                    [](PrismaView readyView)
                                    {
                                        if (!prismaUI || !view || readyView != view)
                                        {
                                            return;
                                        }
                                        domReady = true;
                                        SendAllState();
                                        SyncVisibility();
                                    });
        if (!view)
        {
            logger::error("For Honor Combat could not create its PrismaUI view.");
            return false;
        }
        prismaUI->RegisterJSListener(view, "hideForHonorCombat", [](const char*) { Prisma::Hide(); });
        return true;
    }

    void SyncVisibility()
    {
        const bool shouldShow = Settings::PlayerUI.enabled || (Settings::NPCUI.enabled && !attackWarnings.empty());
        const bool viewValid = IsViewValid();
        const bool hidden = viewValid ? prismaUI->IsHidden(view) : true;
        if (!shouldShow)
        {
            if (viewValid && !hidden)
            {
                prismaUI->Hide(view);
            }
            return;
        }
        if (!EnsureView() || !domReady)
        {
            return;
        }
        if (prismaUI->IsHidden(view))
        {
            prismaUI->Show(view);
        }
    }
}

void Prisma::Install()
{
    prismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI());
    if (!prismaUI)
    {
        logger::error("PrismaUI.dll was not found; For Honor Combat HUD is unavailable.");
    }
    else
    {
        logger::info("For Honor Combat linked to PrismaUI!");
    }
}

void Prisma::Preload()
{
    if (!EnsureView())
    {
        return;
    }
    prismaUI->Hide(view);
    SendAllState();
    SyncVisibility();
}

void Prisma::Show()
{
    if (EnsureView())
    {
        prismaUI->Show(view);
    }
}

void Prisma::Hide()
{
    if (IsViewValid())
    {
        prismaUI->Hide(view);
    }
}

bool Prisma::IsHidden() { return !IsViewValid() || prismaUI->IsHidden(view); }
bool Prisma::IsReady() { return domReady && IsViewValid(); }
void Prisma::ApplyUISettings()
{
    if (!prismaUI)
    {
        return;
    }
    if (Settings::PlayerUI.enabled && (runtimeEligible || currentUnblockable || (Settings::PlayerUI.editMode && runtimePreviewEligible)))
    {
        EnsureView();
    }
    SendSettings();
    SendDirection();
    SyncVisibility();
}

// ============================================================================
// PLAYER HUD - READ GAMEPLAY GRAPH STATE / UPDATE PRISMAUI
// InputTracking owns CameraMovementFHB. FHB::Events owns the combat flags.
// This is visual-only: no animation events, graph writes, timers, or new hooks.
// ============================================================================
void Prisma::UpdatePlayerHUD()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    std::int32_t direction = 0;
    std::int32_t allBlock = 0;
    std::int32_t unblockable = 0;
    std::int32_t parrySuccess = 0;
    if (player) {
        player->GetGraphVariableInt("CameraMovementFHB", direction);
        player->GetGraphVariableInt("AllBlockFHB", allBlock);
        player->GetGraphVariableInt("UnblockableFHB", unblockable);
        player->GetGraphVariableInt("ParrySuccessFHB", parrySuccess);
    }
    // Forward the existing gameplay states without changing their lifetimes.
    UpdateDirection(std::clamp(static_cast<int>(direction), 0, 3));
    UpdateAllBlock(allBlock == 1);
    UpdateUnblockable(unblockable == 1);
    UpdateParrySuccess(parrySuccess == 1);

    // Visual-only block state for the player's shield outer-edge highlight.
    // Do not alter animation variables or combat mechanics.
    const bool blocking = player && player->IsBlocking();
    if (currentBlocking != blocking) {
        currentBlocking = blocking;
        SendDirection();
    }
    // ========================================================================
    // PLAYER HUD - VISIBILITY / SCREEN POSITION
    // Preserve the old UI's first-person center and third-person torso anchor.
    // ========================================================================
    float resolutionScale = 1.0f;
    const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
    if (Settings::PlayerUI.scaleWithResolution && screen.height > 0) {
        resolutionScale = std::clamp(static_cast<float>(screen.height) / 1080.0f, 0.5f, 4.0f);
    }
    auto* ui = RE::UI::GetSingleton();
    const auto* actorState = player ? player->AsActorState() : nullptr;
    const auto* actorValues = player ? player->AsActorValueOwner() : nullptr;
    const float health = actorValues ? actorValues->GetActorValue(RE::ActorValue::kHealth) : 0.0f;
    const bool playerReady = player && std::isfinite(health) && health > 0.0f;
    const bool gameActive = ui && !ui->GameIsPaused() &&
                            !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
    const bool eligible = Settings::PlayerUI.enabled && playerReady && gameActive &&
                          actorState && actorState->IsWeaponDrawn();
    if (!playerReady || !gameActive) {
        UpdateRuntimeState(false, false, 50.0f, 50.0f, resolutionScale, false);
        return;
    }
    const auto* playerCamera = RE::PlayerCamera::GetSingleton();
    if (!playerCamera || playerCamera->IsInFirstPerson() || Settings::PlayerUI.centerInThirdPerson) {
        UpdateRuntimeState(eligible, true, 50.0f, 50.0f, resolutionScale, false);
        return;
    }
    auto* root = player->Get3D(false);
    if (!root) {
        UpdateRuntimeState(false, true, 50.0f, 50.0f, resolutionScale, false);
        return;
    }
    // The same torso-node order used by Honor Combat's original Events.cpp.
    static const std::array<RE::BSFixedString, 6> torsoNodes{
        RE::BSFixedString("NPC Spine1 [Spn1]"), RE::BSFixedString("NPC Spine2 [Spn2]"),
        RE::BSFixedString("NPC Spine [Spn0]"), RE::BSFixedString("NPC COM [COM ]"),
        RE::BSFixedString("Bip01 Spine1"), RE::BSFixedString("Bip01 Spine2")
    };
    RE::NiAVObject* torso = root;
    for (const auto& name : torsoNodes) {
        if (auto* node = root->GetObjectByName(name)) {
            torso = node;
            break;
        }
    }
    auto* camera = RE::Main::WorldRootCamera();
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
    if (!camera || !camera->WorldPtToScreenPt3(torso->world.translate, x, y, depth, 1e-5f) ||
        !std::isfinite(x) || !std::isfinite(y)) {
        UpdateRuntimeState(false, true, 50.0f, 50.0f, resolutionScale, false);
        return;
    }

    float xPercent = x * 100.0f;
    float yPercent = (1.0f - y) * 100.0f;
    if (screen.width > 0 && screen.height > 0) {
        xPercent += Settings::PlayerUI.attachOffsetXPixels * resolutionScale * 100.0f /
                    static_cast<float>(screen.width);
        yPercent -= Settings::PlayerUI.attachOffsetYPixels * resolutionScale * 100.0f /
                    static_cast<float>(screen.height);
    }
    UpdateRuntimeState(eligible, true, std::clamp(xPercent, 0.0f, 100.0f),
                       std::clamp(yPercent, 0.0f, 100.0f), resolutionScale, true);
}

void Prisma::UpdateDirection(int direction)
{
    direction = std::clamp(direction, 0, 3);
    if (currentDirection == direction) return;
    currentDirection = direction;
    SendDirection();
    SyncVisibility();
}

void Prisma::UpdateAllBlock(bool allBlock)
{
    if (currentAllBlock == allBlock)
    {
        return;
    }
    currentAllBlock = allBlock;
    SendDirection();
}

void Prisma::UpdateUnblockable(bool unblockable)
{
    if (currentUnblockable == unblockable)
    {
        return;
    }
    currentUnblockable = unblockable;
    SendDirection();
    SyncVisibility();
}

void Prisma::UpdateParrySuccess(bool success)
{
    if (currentParrySuccess == success)
    {
        return;
    }
    currentParrySuccess = success;
    SendDirection();
}

void Prisma::UpdateRuntimeState(bool eligible, bool previewEligible, float xPercent, float yPercent, float resolutionScale, bool attached)
{
    xPercent = std::clamp(xPercent, 0.0f, 100.0f);
    yPercent = std::clamp(yPercent, 0.0f, 100.0f);
    resolutionScale = std::clamp(resolutionScale, 0.5f, 4.0f);
    const bool changed = runtimeEligible != eligible || runtimePreviewEligible != previewEligible || runtimeAttached != attached || std::abs(runtimeXPercent - xPercent) >= 0.025f || std::abs(runtimeYPercent - yPercent) >= 0.025f ||
                         std::abs(runtimeResolutionScale - resolutionScale) >= 0.005f;
    runtimeEligible = eligible;
    runtimePreviewEligible = previewEligible;
    runtimeAttached = attached;
    runtimeXPercent = xPercent;
    runtimeYPercent = yPercent;
    runtimeResolutionScale = resolutionScale;
    if (changed)
    {
        SendRuntimeState();
    }
    SyncVisibility();
}

void Prisma::UpdateAttackWarnings(const std::vector<AttackWarningVisual>& warnings)
{
    const auto nearlyEqual = [](float left, float right, float threshold) { return std::abs(left - right) < threshold; };
    const bool changed = attackWarnings.size() != warnings.size() || !std::equal(attackWarnings.begin(), attackWarnings.end(), warnings.begin(), warnings.end(),
                                                                                 [&](const AttackWarningVisual& left, const AttackWarningVisual& right)
                                                                                 {
                                                                                     return left.id == right.id && left.direction == right.direction && nearlyEqual(left.xPercent, right.xPercent, 0.025f) &&
                                                                                            nearlyEqual(left.yPercent, right.yPercent, 0.025f) && nearlyEqual(left.resolutionScale, right.resolutionScale, 0.005f) &&
                                                                                            nearlyEqual(left.distanceScale, right.distanceScale, 0.005f) && left.hitFrameFlash == right.hitFrameFlash && left.unblockable == right.unblockable;
                                                                                 });
    if (!changed)
    {
        return;
    }
    attackWarnings = warnings;
    SendAttackWarnings();
    SyncVisibility();
}

void Prisma::Reset()
{
    currentDirection = 0;
    currentAllBlock = false;
    currentBlocking = false;
    currentUnblockable = false;
    currentParrySuccess = false;
    runtimeEligible = false;
    runtimePreviewEligible = false;
    runtimeAttached = false;
    runtimeXPercent = 50.0f;
    runtimeYPercent = 50.0f;
    runtimeResolutionScale = 1.0f;
    attackWarnings.clear();
    SendDirection();
    SendRuntimeState();
    SendAttackWarnings();
    SyncVisibility();
}

// ============================================================================
// PRISMAUI SETTINGS MENU - PLAYER HUD
// The renderer and settings UI live in this module. Menu.cpp only registers it.
// All changes persist using Settings::Save() and refresh the existing view.
// ============================================================================
namespace {
    bool RenderColorPicker(const char* label, std::array<float, 4>& color) {
        namespace ui = ImGuiMCP;
        constexpr ui::ImGuiColorEditFlags colorFlags =
            ui::ImGuiColorEditFlags_AlphaBar | ui::ImGuiColorEditFlags_AlphaPreviewHalf;
        return ui::ColorEdit4(label, color.data(), colorFlags);
    }

    void SaveAndRefreshIfChanged(bool changed) {
        if (!changed) return;
        Settings::Save();
        Prisma::ApplyUISettings();
    }
}

void Prisma::RenderPlayerSettings() {
    namespace ui = ImGuiMCP;
    auto& s = Settings::PlayerUI;
    bool changed = false;
    // ========================================================================
    // PLAYER HUD - VISIBILITY AND SCREEN POSITION
    // ========================================================================
    ui::Text("Player HUD - Three-Segment Shield");
    ui::Separator();
    changed |= ui::Checkbox("Enable Player HUD", &s.enabled);
    changed |= ui::Checkbox("Preview While Editing", &s.editMode);
    changed |= ui::Checkbox("Scale With Resolution", &s.scaleWithResolution);
    changed |= ui::Checkbox("Use Center-Screen Position In Third Person", &s.centerInThirdPerson);
    ui::Separator();
    ui::Text("First Person / Center-Screen Position");
    changed |= ui::SliderInt("Screen X (%)", &s.positionXPercent, 0, 100);
    changed |= ui::SliderInt("Screen Y (%)", &s.positionYPercent, 0, 100);
    ui::Separator();
    ui::Text("Third Person Chest Attachment");
    changed |= ui::SliderInt("Chest Horizontal Offset (px)", &s.attachOffsetXPixels, -1000, 1000);
    changed |= ui::SliderInt("Chest Vertical Offset (px, negative moves down)", &s.attachOffsetYPixels, -1000, 1000);
    ui::Text("Screen X/Y apply in first person or center-screen mode.");
    ui::Text("Chest offsets apply when attached to the third-person torso.");
    changed |= ui::SliderInt("Player Scale (%)", &s.scalePercent, 25, 300);
    changed |= ui::SliderInt("Player Shield Diameter (px)", &s.diameterPixels, 96, 800);
    // The shield background and active indicator must not share a parent alpha.
    changed |= ui::SliderInt("Player Shield Opacity (%)", &s.opacityPercent, 0, 100);
    changed |= ui::SliderInt("Player Active Indicator Opacity (%)", &s.activeOpacityPercent, 0, 100);
    changed |= ui::SliderInt("Player Rotation", &s.rotationDegrees, -180, 180);
    // ========================================================================
    // PLAYER HUD - CENTER PIECE
    // ========================================================================
    ui::Separator();
    ui::Text("Player Center Piece");
    changed |= ui::Checkbox("Show Player Center", &s.showCenter);
    changed |= ui::SliderInt("Player Center Size (%)", &s.centerPieceSizePercent, 10, 200);
    changed |= ui::Checkbox("Move Center With Direction", &s.moveCenterWithDirection);
    changed |= ui::SliderInt("Center Movement (px)", &s.centerMovementPixels, 0, 400);
    changed |= ui::SliderInt("Center Transition (ms)", &s.centerMovementDurationMs, 0, 1000);
    changed |= RenderColorPicker("Player Center Neutral", s.centerColor);
    changed |= RenderColorPicker("Player Center Active", s.centerActiveColor);
    // ========================================================================
    // PLAYER HUD - SEGMENT COLORS
    // ========================================================================
    ui::Separator();
    ui::Text("Player Shield Colors");
    changed |= RenderColorPicker("Player Active Indicator", s.activeColor);
    changed |= RenderColorPicker("Player Shield Background", s.inactiveColor);
    changed |= RenderColorPicker("Player Unblockable Indicator", s.unblockableColor);
    changed |= RenderColorPicker("Player Shield Border", s.borderColor);
    SaveAndRefreshIfChanged(changed);
    if (ui::Button("Reset Player HUD Defaults")) {
        s = Settings::PlayerUISettings{};
        Settings::Save();
        ApplyUISettings();
    }
}

void Prisma::RenderNPCSettings() {
    namespace ui = ImGuiMCP;
    auto& s = Settings::NPCUI;
    bool changed = false;
    // ========================================================================
    // NPC HUD - VISIBILITY, POSITION AND INDEPENDENT SCALING
    // ========================================================================
    ui::Text("NPC Attack Warnings - Three-Segment Shield");
    ui::Separator();
    changed |= ui::Checkbox("Enable NPC HUD", &s.enabled);
    changed |= ui::Checkbox("NPC Scale With Resolution", &s.scaleWithResolution);
    changed |= ui::Checkbox("NPC Scale With Distance", &s.scaleWithDistance);
    changed |= ui::Checkbox("Mirror NPC Attack Directions", &s.mirrorAttackDirections);
    // ========================================================================
    // NPC ATTACK DIRECTION CLASSIFIER - same threshold as original Honor Combat
    // ========================================================================
    changed |= ui::SliderFloat("NPC Top Threshold (game units)", &s.topThreshold, 0.0f, 200.0f, "%.1f");
    changed |= ui::SliderInt("NPC Offset X (px)", &s.offsetXPixels, -1000, 1000);
    changed |= ui::SliderInt("NPC Offset Y (px)", &s.offsetYPixels, -1000, 1000);
    changed |= ui::SliderInt("NPC Scale (%)", &s.scalePercent, 20, 200);
    changed |= ui::SliderInt("NPC Shield Diameter (px)", &s.diameterPixels, 96, 800);
    changed |= ui::SliderInt("NPC Shield Opacity (%)", &s.opacityPercent, 0, 100);
    changed |= ui::SliderInt("NPC Active Indicator Opacity (%)", &s.activeOpacityPercent, 0, 100);
    changed |= ui::SliderInt("NPC Rotation", &s.rotationDegrees, -180, 180);
    // ========================================================================
    // NPC HUD - CENTER PIECE (INDEPENDENT OF PLAYER)
    // ========================================================================
    ui::Separator();
    ui::Text("NPC Center Piece");
    changed |= ui::Checkbox("Show NPC Center", &s.showCenter);
    changed |= ui::SliderInt("NPC Center Size (%)", &s.centerPieceSizePercent, 10, 200);
    changed |= RenderColorPicker("NPC Center", s.centerColor);
    // ========================================================================
    // NPC HUD - SEGMENT COLORS
    // ========================================================================
    ui::Separator();
    ui::Text("NPC Shield Colors");
    changed |= RenderColorPicker("NPC Active Indicator", s.activeColor);
    changed |= RenderColorPicker("NPC Shield Background", s.inactiveColor);
    changed |= RenderColorPicker("NPC Unblockable Indicator", s.unblockableColor);
    changed |= RenderColorPicker("NPC Shield Border", s.borderColor);
    SaveAndRefreshIfChanged(changed);
    if (ui::Button("Reset NPC HUD Defaults")) {
        s = Settings::NPCUISettings{};
        Settings::Save();
        ApplyUISettings();
    }
}
