#pragma once

// For Honor Blocking: MCO/BFCO attack animation playback control.
// Header-only deliberately: Menu.cpp is already compiled by the FHB target,
// so no CMakeLists.txt changes or extra plugin.cpp hook calls are necessary.
// Include this header ONLY from src/Menu.cpp.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <SKSEMenuFramework.h>
#include <SimpleIni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace FHBAttackSpeed {
    namespace detail {
        inline constexpr std::string_view kIniFile = "Data/SKSE/Plugins/ForHonorBlocking_AttackSpeed.ini";
        inline constexpr const char* kIniSection = "AttackSpeed";
        inline std::atomic<float> playerMultiplier{1.0f};
        inline std::atomic<float> npcMultiplier{1.0f};
        inline std::atomic<bool> applyWeaponSpeedMult{false};
        inline std::atomic<bool> useGraphWeaponSpeed{true};
        inline std::atomic<bool> averageDualWield{false};
        inline std::atomic<bool> includeBFCOBashes{false};
        inline std::atomic<bool> installed{false};

        inline bool StartsWithIgnoreCase(std::string_view value, std::string_view prefix) noexcept {
            if (value.size() < prefix.size()) return false;
            for (std::size_t i = 0; i < prefix.size(); ++i) {
                const unsigned char left = static_cast<unsigned char>(value[i]);
                const unsigned char right = static_cast<unsigned char>(prefix[i]);
                const auto lower = [](unsigned char c) noexcept -> unsigned char {
                    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c - 'A' + 'a') : c;
                };
                if (lower(left) != lower(right)) return false;
            }
            return true;
        }

        inline bool IsTargetAttack(std::string_view fullPath) noexcept {
            const auto offset = fullPath.find_last_of("/\\");
            const auto name = offset == std::string_view::npos ? fullPath : fullPath.substr(offset + 1);
            // Deliberately do NOT match every MCO_ animation: that would also slow
            // MCO dodges and other non-attack clips.
            static constexpr std::array<std::string_view, 11> prefixes = {
                "MCO_attack",       "MCO_powerattack",   "MCO_sprintattack", "MCO_sprintpowerattack",
                "MCO_weaponart",    "BFCO_attack",       "BFCO_powerattack", "BFCO_specialattack",
                "BFCO_rangeattack", "BFCO_sprintattack", "BFCO_jumpattack"};
            for (const auto prefix : prefixes) {
                if (StartsWithIgnoreCase(name, prefix)) return true;
            }
            return includeBFCOBashes.load(std::memory_order_relaxed) && StartsWithIgnoreCase(name, "BFCO_bash");
        }

        inline float NormalizeWeaponSpeed(float value) noexcept {
            // Malformed/zero graph variables must never freeze an animation.
            if (!std::isfinite(value) || value <= 0.0f) return 1.0f;
            return std::clamp(value, 0.1f, 4.0f);
        }

        inline bool IsDualWielding(RE::Actor* actor) {
            if (!actor) return false;
            const auto* rightForm = actor->GetEquippedObject(false);
            const auto* leftForm = actor->GetEquippedObject(true);
            const auto* right = rightForm ? rightForm->As<RE::TESObjectWEAP>() : nullptr;
            const auto* left = leftForm ? leftForm->As<RE::TESObjectWEAP>() : nullptr;
            if (!right || !left) return false;
            const auto rightType = right->GetWeaponType();
            const auto leftType = left->GetWeaponType();
            return rightType >= RE::WeaponTypes::kOneHandSword && rightType <= RE::WeaponTypes::kOneHandMace &&
                   leftType >= RE::WeaponTypes::kOneHandSword && leftType <= RE::WeaponTypes::kOneHandMace;
        }

        inline float WeaponSpeedFactor(RE::Actor* actor) {
            if (!actor || !applyWeaponSpeedMult.load(std::memory_order_relaxed)) return 1.0f;

            if (useGraphWeaponSpeed.load(std::memory_order_relaxed)) {
                static const RE::BSFixedString rightVariable("weaponSpeedMult");
                static const RE::BSFixedString leftVariable("leftWeaponSpeedMult");
                float right = 1.0f;
                if (actor->GetGraphVariableFloat(rightVariable, right)) {
                    float result = NormalizeWeaponSpeed(right);
                    if (averageDualWield.load(std::memory_order_relaxed) && IsDualWielding(actor)) {
                        float left = 1.0f;
                        if (actor->GetGraphVariableFloat(leftVariable, left)) {
                            result = 0.5f * (result + NormalizeWeaponSpeed(left));
                        }
                    }
                    return result;
                }
                // Missing graph variable: fall back to actor value.
            }
            auto* owner = actor->AsActorValueOwner();
            return owner ? NormalizeWeaponSpeed(owner->GetActorValue(RE::ActorValue::kWeaponSpeedMult)) : 1.0f;
        }

        inline float SafeMultiplier(float input) noexcept {
            return std::clamp(std::isfinite(input) ? input : 1.0f, 0.5f, 1.2f);
        }

        // Restoring the clip speed after the original Update prevents geometric
        // compounding (0.85 * 0.85 * 0.85...) across subsequent frames.
        struct PlaybackSpeedScope {
            RE::hkbClipGenerator* clip;
            float original;
            explicit PlaybackSpeedScope(RE::hkbClipGenerator* a_clip, float multiplier) noexcept
                : clip(a_clip), original(a_clip->playbackSpeed) {
                clip->playbackSpeed = original * multiplier;
            }
            ~PlaybackSpeedScope() { clip->playbackSpeed = original; }
            PlaybackSpeedScope(const PlaybackSpeedScope&) = delete;
            PlaybackSpeedScope& operator=(const PlaybackSpeedScope&) = delete;
        };

        struct ClipUpdateHook {
            static void Update(RE::hkbClipGenerator* clip, const RE::hkbContext& context, float timestep) {
                if (!clip || !context.character || !std::isfinite(clip->playbackSpeed) || clip->playbackSpeed <= 0.0f) {
                    return original(clip, context, timestep);
                }
                const char* rawName = clip->animationName.c_str();
                if (!rawName || !IsTargetAttack(rawName)) {
                    return original(clip, context, timestep);
                }

                // hkbCharacter is embedded at +0xC0 in BShkbAnimationGraph.
                // This is CommonLibSSE-NG's 1.6.1170 layout, not an arbitrary
                // pointer scan. The holder is the graph's Actor.
                auto* graph = SKSE::stl::adjust_pointer<RE::BShkbAnimationGraph>(context.character, -0xC0);
                RE::Actor* actor = graph ? graph->holder : nullptr;
                if (!actor) {
                    return original(clip, context, timestep);
                }

                const float actorMultiplier = actor->IsPlayerRef() ? playerMultiplier.load(std::memory_order_relaxed)
                                                                   : npcMultiplier.load(std::memory_order_relaxed);
                const float combined =
                    std::clamp(SafeMultiplier(actorMultiplier) * WeaponSpeedFactor(actor), 0.15f, 3.5f);
                if (std::abs(combined - 1.0f) < 0.0001f || !std::isfinite(clip->playbackSpeed * combined)) {
                    return original(clip, context, timestep);
                }

                PlaybackSpeedScope speedOverride(clip, combined);
                return original(clip, context, timestep);
            }

            static inline REL::Relocation<decltype(&Update)> original;
        };

        inline std::filesystem::path IniPath() { return std::filesystem::path{kIniFile}; }
    }

    inline void Load() {
        CSimpleIniA ini;
        ini.SetUnicode();
        const auto path = detail::IniPath();
        if (ini.LoadFile(path.string().c_str()) < 0) return;
        detail::playerMultiplier.store(detail::SafeMultiplier(static_cast<float>(
                                           ini.GetDoubleValue(detail::kIniSection, "PlayerMultiplier", 1.0))),
                                       std::memory_order_relaxed);
        detail::npcMultiplier.store(
            detail::SafeMultiplier(static_cast<float>(ini.GetDoubleValue(detail::kIniSection, "NPCMultiplier", 1.0))),
            std::memory_order_relaxed);
        detail::applyWeaponSpeedMult.store(ini.GetBoolValue(detail::kIniSection, "ApplyWeaponSpeedMult", false),
                                           std::memory_order_relaxed);
        detail::useGraphWeaponSpeed.store(ini.GetBoolValue(detail::kIniSection, "UseGraphWeaponSpeed", true),
                                          std::memory_order_relaxed);
        detail::averageDualWield.store(ini.GetBoolValue(detail::kIniSection, "AverageDualWield", false),
                                       std::memory_order_relaxed);
        detail::includeBFCOBashes.store(ini.GetBoolValue(detail::kIniSection, "IncludeBFCOBashes", false),
                                        std::memory_order_relaxed);
    }

    inline void Save() {
        const auto path = detail::IniPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            SKSE::log::warn("[FHB Attack Speed] Could not create INI directory: {}", error.message());
            return;
        }
        CSimpleIniA ini;
        ini.SetUnicode();
        ini.LoadFile(path.string().c_str());  // Keep unrelated INI keys intact.
        ini.SetDoubleValue(detail::kIniSection, "PlayerMultiplier",
                           detail::playerMultiplier.load(std::memory_order_relaxed));
        ini.SetDoubleValue(detail::kIniSection, "NPCMultiplier", detail::npcMultiplier.load(std::memory_order_relaxed));
        ini.SetBoolValue(detail::kIniSection, "ApplyWeaponSpeedMult",
                         detail::applyWeaponSpeedMult.load(std::memory_order_relaxed));
        ini.SetBoolValue(detail::kIniSection, "UseGraphWeaponSpeed",
                         detail::useGraphWeaponSpeed.load(std::memory_order_relaxed));
        ini.SetBoolValue(detail::kIniSection, "AverageDualWield",
                         detail::averageDualWield.load(std::memory_order_relaxed));
        ini.SetBoolValue(detail::kIniSection, "IncludeBFCOBashes",
                         detail::includeBFCOBashes.load(std::memory_order_relaxed));
        if (ini.SaveFile(path.string().c_str()) < 0) {
            SKSE::log::warn("[FHB Attack Speed] Could not save {}", path.string());
        }
    }

    inline void Install() {
        if (detail::installed.exchange(true, std::memory_order_acq_rel)) return;
        Load();
        REL::Relocation<std::uintptr_t> vtable(RE::VTABLE_hkbClipGenerator[0]);
        detail::ClipUpdateHook::original = vtable.write_vfunc(0x05, &detail::ClipUpdateHook::Update);
        SKSE::log::info("[FHB Attack Speed] hkbClipGenerator Update hook installed; player={} NPC={} WSM={}",
                        detail::playerMultiplier.load(), detail::npcMultiplier.load(),
                        detail::applyWeaponSpeedMult.load());
    }

    inline void RenderSettings() {
        namespace ui = ImGuiMCP;
        ui::Text("Attack animation speed (MCO/BFCO)");
        ui::Text("100%% = original animation speed. Lower values make attacks slower.");
        ui::Text("Only attack clips are affected; dodges, movement and blocking are excluded.");
        ui::Separator();

        bool changed = false;
        float player = detail::playerMultiplier.load(std::memory_order_relaxed) * 100.0f;
        if (ui::SliderFloat("Player Attack Speed", &player, 50.0f, 120.0f, "%.0f%%")) {
            detail::playerMultiplier.store(detail::SafeMultiplier(player / 100.0f), std::memory_order_relaxed);
            changed = true;
        }
        float npc = detail::npcMultiplier.load(std::memory_order_relaxed) * 100.0f;
        if (ui::SliderFloat("NPC Attack Speed", &npc, 50.0f, 120.0f, "%.0f%%")) {
            detail::npcMultiplier.store(detail::SafeMultiplier(npc / 100.0f), std::memory_order_relaxed);
            changed = true;
        }

        ui::Separator();
        ui::Text("Optional WeaponSpeedMult compatibility");
        ui::Text("Leave off if another mod already applies weapon speed to MCO/BFCO.");
        bool useWSM = detail::applyWeaponSpeedMult.load(std::memory_order_relaxed);
        if (ui::Checkbox("Apply WeaponSpeedMult", &useWSM)) {
            detail::applyWeaponSpeedMult.store(useWSM, std::memory_order_relaxed);
            changed = true;
        }
        if (useWSM) {
            bool graph = detail::useGraphWeaponSpeed.load(std::memory_order_relaxed);
            if (ui::Checkbox("Use graph weapon speed (includes weapon base speed)", &graph)) {
                detail::useGraphWeaponSpeed.store(graph, std::memory_order_relaxed);
                changed = true;
            }
            if (graph) {
                bool average = detail::averageDualWield.load(std::memory_order_relaxed);
                if (ui::Checkbox("Average dual-wield weapon speeds", &average)) {
                    detail::averageDualWield.store(average, std::memory_order_relaxed);
                    changed = true;
                }
            }
        }

        ui::Separator();
        bool bfcoBashes = detail::includeBFCOBashes.load(std::memory_order_relaxed);
        if (ui::Checkbox("Include BFCO bash clips", &bfcoBashes)) {
            detail::includeBFCOBashes.store(bfcoBashes, std::memory_order_relaxed);
            changed = true;
        }

        if (ui::Button("Reset Attack Speed Settings")) {
            detail::playerMultiplier.store(1.0f);
            detail::npcMultiplier.store(1.0f);
            detail::applyWeaponSpeedMult.store(false);
            detail::useGraphWeaponSpeed.store(true);
            detail::averageDualWield.store(false);
            detail::includeBFCOBashes.store(false);
            changed = true;
        }
        if (changed) Save();
    }
}  // namespace FHBAttackSpeed
