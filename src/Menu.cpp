#include "Menu.h"

#include <SKSE/SKSE.h>
#include <SKSEMenuFramework.h>

#include <cstring>

#include "Settings.h"

namespace {
    namespace ui = ImGuiMCP;

    constexpr auto kSection = "For Honor Blocking";
    constexpr auto kBlockingPage = "Blocking Settings";

    void RenderBlockingSettings() {
        ui::Text("For Honor Blocking");
        ui::Separator();

        ui::Text("Damage Reduction from Block Skill");
        ui::Text("Controls how much damage is reduced based on the defender's Block skill.");

        const float previousLightReduction = Settings::lightAttackReductionPerLevel;

        ui::SliderFloat("Light Attack Reduction per Block level", &Settings::lightAttackReductionPerLevel, 0.0f, 1.0f,
                        "%.2f", 0.01f);

        if (Settings::lightAttackReductionPerLevel != previousLightReduction) {
            Settings::Save();
        }

        const float previousPowerReduction = Settings::powerAttackReductionPerLevel;

        ui::SliderFloat("Power Attack Reduction Block level.", &Settings::powerAttackReductionPerLevel, 0.0f, 1.0f,
                        "%.2f", 0.01f);

        if (Settings::powerAttackReductionPerLevel != previousPowerReduction) {
            Settings::Save();
        }

        ui::Separator();

        ui::Text("Block Stagger");
        ui::Text("Controls the stagger magnitude applied to the attacker and defender.");

        const float previousLightAttackerStagger = Settings::lightAttackerStagger;

        ui::SliderFloat("Light Block - Attacker Stagger", &Settings::lightAttackerStagger, 0.0f, 100.0f, "%.1f");

        if (Settings::lightAttackerStagger != previousLightAttackerStagger) {
            Settings::Save();
        }

        const float previousLightDefenderStagger = Settings::lightDefenderStagger;

        ui::SliderFloat("Light Block - Defender Stagger", &Settings::lightDefenderStagger, 0.0f, 100.0f, "%.1f");

        if (Settings::lightDefenderStagger != previousLightDefenderStagger) {
            Settings::Save();
        }

        const float previousPowerAttackerStagger = Settings::powerAttackerStagger;

        ui::SliderFloat("Power Block - Attacker Stagger", &Settings::powerAttackerStagger, 0.0f, 100.0f, "%.1f");

        if (Settings::powerAttackerStagger != previousPowerAttackerStagger) {
            Settings::Save();
        }

        const float previousPowerDefenderStagger = Settings::powerDefenderStagger;

        ui::SliderFloat("Power Block - Defender Stagger", &Settings::powerDefenderStagger, 0.0f, 100.0f, "%.1f");

        if (Settings::powerDefenderStagger != previousPowerDefenderStagger) {
            Settings::Save();
        }

        ui::Separator();

        ui::Text("Superior Block Perk");
        ui::Text("Defenders with this perk perform a large recoil instead of a normal recoil.");

        const bool previousEmpoweredBlockRequired = Settings::empoweredBlockRequired;

        ui::Checkbox("Require Empowered Block Perk", &Settings::empoweredBlockRequired);

        if (Settings::empoweredBlockRequired != previousEmpoweredBlockRequired) {
            Settings::Save();
        }

        char superiorBlockPerk[256]{};
        std::strncpy(superiorBlockPerk, Settings::superiorBlockPerk.c_str(), sizeof(superiorBlockPerk) - 1);

        if (ui::InputText("Perk Editor ID", superiorBlockPerk, sizeof(superiorBlockPerk))) {
            Settings::superiorBlockPerk = superiorBlockPerk;
            Settings::Save();
        }

        ui::Separator();

        ui::Text("Block Stamina Consumption");
        ui::Text("Controls stamina consumed when blocking an incoming attack.");

#define STAMINA_SLIDER(label, setting)                                    \
    do {                                                                  \
        const float previous = Settings::setting;                         \
        ui::SliderFloat(label, &Settings::setting, 0.0f, 100.0f, "%.1f"); \
        if (Settings::setting != previous) {                              \
            Settings::Save();                                             \
        }                                                                 \
    } while (false)

        STAMINA_SLIDER("One-Handed Sword", staminaOneHandSword);
        STAMINA_SLIDER("Dagger", staminaDagger);
        STAMINA_SLIDER("One-Handed Axe", staminaOneHandAxe);
        STAMINA_SLIDER("Mace", staminaMace);
        STAMINA_SLIDER("Two-Handed Sword", staminaTwoHandSword);
        STAMINA_SLIDER("Two-Handed Axe", staminaTwoHandAxe);
        STAMINA_SLIDER("Bow", staminaBow);
        STAMINA_SLIDER("Crossbow", staminaCrossbow);
        STAMINA_SLIDER("Staff", staminaStaff);
        STAMINA_SLIDER("Unarmed", staminaUnarmed);
        STAMINA_SLIDER("Other", staminaOther);

#undef STAMINA_SLIDER

        ui::Separator();

        ui::Text("Debug");

        const bool previousDebugLogging = Settings::debugLogging;

        ui::Checkbox("Debug Logging", &Settings::debugLogging);

        if (Settings::debugLogging != previousDebugLogging) {
            Settings::Save();
        }

        ui::Separator();

        if (ui::Button("Save Settings")) {
            Settings::Save();
        }

        ui::SameLine();

        if (ui::Button("Reset to Defaults")) {
            Settings::ResetToDefaults();
            Settings::Save();
        }
    }
}

namespace Menu {

    void Install() {
        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::info(
                "[menu] SKSE Menu Framework not loaded, "
                "settings page unavailable");
            return;
        }

        SKSEMenuFramework::SetSection(kSection);

        SKSEMenuFramework::AddSectionItem(kBlockingPage, RenderBlockingSettings);

        SKSE::log::info("[menu] registered {}/{}", kSection, kBlockingPage);
    }

}