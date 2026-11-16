#include "Menu.h"

#include <SKSE/SKSE.h>
#include <SKSEMenuFramework.h>

#include "AttackCancel.h"
#include "AttackSpeed.h"
#include "Blocking.h"
#include "InputTracking.h"
#include "Prisma.h"

// Each module owns its input settings and INI persistence.
// Menu.cpp only composes the sections and one shared Save action.
namespace BlockKey {
    void RenderBlockingSettings();
    void RenderAttackCancelBindings();
    bool HasUnsavedInputChanges();
    bool HasInputBindingConflict();
    bool SaveInputBindings();
    void DiscardInputBindings();
}
namespace PowerAttackKey {
    void RenderInputSettings();
    bool HasUnsavedInputChanges();
    bool SaveInputBindings();
    void DiscardInputBindings();
}

namespace {
    namespace ui = ImGuiMCP;

    constexpr auto kSection = "For Honor Combat";
    constexpr auto kBlockingPage = "Blocking Settings";
    constexpr auto kInputsPage = "Input Bindings";
    constexpr auto kInputTrackingPage = "Input Tracking";
    constexpr auto kAttackSpeedPage = "Attack Speed";
    constexpr auto kAttackCancelPage = "Attack Cancel";
    constexpr auto kPlayerHudPage = "Player HUD";
    constexpr auto kNpcHudPage = "NPC HUD";

    void RenderInputs() {
        static bool lastSaveFailed = false;

        ui::Text("BLOCKING");
        ui::TextWrapped("Hold to guard. Match TDM's lock-on key to Block for targeting.");
        BlockKey::RenderBlockingSettings();

        ui::Separator();
        ui::Text("POWER ATTACK");
        ui::TextWrapped("Separate right, left and dual attacks. KB modifiers are optional.");
        PowerAttackKey::RenderInputSettings();

        ui::Separator();
        ui::Text("ATTACK CANCEL");
        ui::TextWrapped("Feint a power attack within its cancel window, without pressing Block.");
        BlockKey::RenderAttackCancelBindings();

        ui::Separator();
        const bool blockChanges = BlockKey::HasUnsavedInputChanges();
        const bool powerChanges = PowerAttackKey::HasUnsavedInputChanges();
        const bool conflict = BlockKey::HasInputBindingConflict();
        if (conflict) {
            ui::TextWrapped("Block and Attack Cancel cannot share a key on the same device.");
        } else if (blockChanges || powerChanges) {
            ui::Text("Unsaved changes");
        }
        if (lastSaveFailed) {
            ui::TextWrapped("Some bindings could not be saved. Check the SKSE log.");
        }

        if (ui::Button("Save Input Bindings")) {
            if (conflict) {
                lastSaveFailed = false;  // The conflict explanation is already visible above.
            } else {
                const bool blockSaved = !blockChanges || BlockKey::SaveInputBindings();
                const bool powerSaved = !powerChanges || PowerAttackKey::SaveInputBindings();
                lastSaveFailed = !(blockSaved && powerSaved);
            }
        }
        ui::SameLine();
        if (ui::Button("Discard Changes")) {
            BlockKey::DiscardInputBindings();
            PowerAttackKey::DiscardInputBindings();
            lastSaveFailed = false;
        }
    }
}

namespace Menu {
    void Install() {
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
        SKSEMenuFramework::AddSectionItem(kAttackCancelPage, AttackCancel::RenderSettings);
        SKSEMenuFramework::AddSectionItem(kPlayerHudPage, Prisma::RenderPlayerSettings);
        SKSEMenuFramework::AddSectionItem(kNpcHudPage, Prisma::RenderNPCSettings);

        SKSE::log::info("[menu] registered {} with shared Input Bindings save action", kSection);
    }
}
