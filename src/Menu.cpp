#include "Menu.h"

#include <SKSE/SKSE.h>
#include <SKSEMenuFramework.h>

#include "AttackSpeed.h"
#include "Blocking.h"
#include "InputTracking.h"
#include "Prisma.h"

// The existing binding modules provide their own settings renderers.
// Only their UI entry points are used here; gameplay input stays in each module.
namespace BlockKey { void RenderInputSettings(); }
namespace PowerAttackKey { void RenderInputSettings(); }

namespace {
    namespace ui = ImGuiMCP;

    // ============================================================================
    // MENU TAB NAMES
    // Page registration is centralized here; controls remain in their modules.
    // ============================================================================
    constexpr auto kSection = "For Honor Combat";
    constexpr auto kBlockingPage = "Blocking Settings";
    constexpr auto kInputsPage = "Inputs";
    constexpr auto kInputTrackingPage = "Input Tracking";
    constexpr auto kAttackSpeedPage = "Attack Speed";
    constexpr auto kPlayerHudPage = "Player HUD";
    constexpr auto kNpcHudPage = "NPC HUD";

    // ============================================================================
    // SHARED INPUT BINDINGS TAB
    // Combines existing Block Key and Power Attack Key UI; no input logic here.
    // ============================================================================
    void RenderInputs() {
        ui::Text("Block Bindings");
        ui::Separator();
        BlockKey::RenderInputSettings();

        ui::Separator();
        ui::Text("Power Attack Bindings");
        ui::Separator();
        PowerAttackKey::RenderInputSettings();
    }
}

namespace Menu {
    // ============================================================================
    // REGISTER FHB MENU TABS
    // Each feature owns its settings UI; Menu.cpp only registers pages.
    // ============================================================================
    void Install() {
        // Install gameplay hook even when Menu Framework is unavailable.
        FHBAttackSpeed::Install();

        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::info("[menu] SKSE Menu Framework not loaded, settings page unavailable");
            return;
        }

        SKSEMenuFramework::SetSection(kSection);
        SKSEMenuFramework::AddSectionItem(kBlockingPage, Blocking::RenderSettings);
        SKSEMenuFramework::AddSectionItem(kInputsPage, RenderInputs);
        SKSEMenuFramework::AddSectionItem(kInputTrackingPage, InputTrackingTuning::RenderSettings);
        SKSEMenuFramework::AddSectionItem(kAttackSpeedPage, FHBAttackSpeed::RenderSettings);
        SKSEMenuFramework::AddSectionItem(kPlayerHudPage, Prisma::RenderPlayerSettings);
        SKSEMenuFramework::AddSectionItem(kNpcHudPage, Prisma::RenderNPCSettings);
        SKSE::log::info("[menu] registered {} with blocking, input, attack speed, and player/NPC HUD tabs", kSection);
    }
}
