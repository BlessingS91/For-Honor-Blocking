#include "AttackCancel.h"
#include "Settings.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <SKSEMenuFramework.h>
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace {
    using Clock = std::chrono::steady_clock;

    // Animation-driven attack movement can linger after a block cancel. Zero
    // only its horizontal component for a short bounded period. This is not a
    // player-movement lock: ordinary input movement remains available.
    constexpr std::uint64_t kCancelMotionWindowMs = 500;
    std::atomic<std::uint64_t> g_cancelMotionUntilMs{0};

    void ClearCancelMotion() noexcept
    {
        g_cancelMotionUntilMs.store(0, std::memory_order_release);
    }

    void BeginCancelMotion() noexcept
    {
        g_cancelMotionUntilMs.store(GetTickCount64() + kCancelMotionWindowMs,
                                    std::memory_order_release);
    }

    bool IsCancelMotionActive() noexcept
    {
        const auto until = g_cancelMotionUntilMs.load(std::memory_order_acquire);
        return until != 0 && GetTickCount64() < until;
    }

    // Motion-processing call hook. It modifies the output of Skyrim's own
    // ProcessMotionData call, rather than changing actor movement velocity.
    // Based on the motion correction mechanism used by Block Cancel Fix.
    class CancelMotionHook final {
    public:
        static void Install()
        {
            // MotionUpdate hook site for SE / AE (and CommonLib's VR offset).
            REL::Relocation<std::uintptr_t> target{
                RELOCATION_ID(41160, 42246),
                REL::VariantOffset(0x111, 0xFF, 0x111)
            };

            SKSE::AllocTrampoline(14);
            original_ = SKSE::GetTrampoline().write_call<5>(target.address(), Thunk);
        }

    private:
        static bool Thunk(RE::TESObjectREFR* reference,
                          float deltaTime,
                          RE::NiPoint3* translation,
                          RE::NiPoint3* rotation,
                          bool* result)
        {
            const bool succeeded = original_(reference, deltaTime,
                                             translation, rotation, result);
            if (reference && reference->IsPlayerRef() && translation &&
                IsCancelMotionActive()) {
                translation->x = 0.0f;
                translation->y = 0.0f;
                // Leave Z and rotational motion untouched.
            }
            return succeeded;
        }

        static inline REL::Relocation<decltype(Thunk)> original_;
    };

    // Restrict this feature to melee swings, not bashes or bows.
    bool IsMeleeAttack(RE::ATTACK_STATE_ENUM state)
    {
        return state >= RE::ATTACK_STATE_ENUM::kDraw &&
               state <= RE::ATTACK_STATE_ENUM::kFollowThrough;
    }

    bool CanCancelState(RE::ATTACK_STATE_ENUM state)
    {
        return state >= RE::ATTACK_STATE_ENUM::kDraw &&
               state <= RE::ATTACK_STATE_ENUM::kNextAttack;
    }

    bool CanProcessPlayer(RE::PlayerCharacter* player)
    {
        auto* ui = RE::UI::GetSingleton();
        const auto* controls = RE::ControlMap::GetSingleton();
        return player && !player->IsDead() && ui && !ui->GameIsPaused() &&
               controls && controls->IsFightingControlsEnabled();
    }

    class AttackCancelGraphSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static AttackCancelGraphSink& GetSingleton()
        {
            static AttackCancelGraphSink singleton;
            return singleton;
        }

        void Update(RE::PlayerCharacter* player)
        {
            if (!AttackCancel::GetConfig().enabled || !CanProcessPlayer(player)) {
                Reset();
                return;
            }
            // The player's animation graph may not exist when Install() runs.
            // Retry registration during the existing player update instead.
            EnsureAnimationSink(player);
            TrackAttack(*player);
        }

        // Called by BlockKey.cpp for a fresh press of the configured
        // Attack Cancel binding. No connection to Block or TDM input.
        void OnCancelPressed(RE::PlayerCharacter* player)
        {
            if (!AttackCancel::GetConfig().enabled || !CanProcessPlayer(player)) return;
            EnsureAnimationSink(player);
            // Input can arrive before the update that tracks the attack start.
            TrackAttack(*player);
            TryCancel(*player);
        }

        void Reset()
        {
            lastAttackState_ = RE::ATTACK_STATE_ENUM::kNone;
            windowActive_ = false;
            canceledThisAttack_ = false;
            ClearCancelMotion();
        }

    private:
        RE::ATTACK_STATE_ENUM lastAttackState_{RE::ATTACK_STATE_ENUM::kNone};
        Clock::time_point attackBegan_{};
        bool windowActive_{false};
        bool canceledThisAttack_{false};
        bool animationSinkRegistered_{false};

        void EnsureAnimationSink(RE::PlayerCharacter* player)
        {
            if (!animationSinkRegistered_ && player) {
                animationSinkRegistered_ = player->AddAnimationGraphEventSink(this);
            }
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!event || !player || event->holder != player ||
                !AttackCancel::GetConfig().enabled) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const std::string_view tag{event->tag.c_str()};
            if (tag == "blockStartOut") {
                // Also cover block cancels initiated outside this input sink.
                // This follows the behavior-graph signal used by Block Cancel Fix.
                bool isAttacking = false;
                if (player->GetGraphVariableBool("IsAttacking", isAttacking) &&
                    isAttacking) {
                    BeginCancelMotion();
                }
            } else if (tag == "attackStart" || tag == "MCO_PowerAttackInitiate") {
                // Don't strip root motion from a new follow-up attack.
                ClearCancelMotion();
            }

            if (AttackCancel::GetConfig().useWeaponSwingWindow &&
                (tag == "weaponSwing" || tag == "weaponLeftSwing")) {
                // Synchronize first in case the attack event precedes Update().
                TrackAttack(*player);
                windowActive_ = false;
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        void TrackAttack(RE::PlayerCharacter& player)
        {
            auto* actorState = player.AsActorState();
            if (!actorState) {
                Reset();
                return;
            }

            const auto state = actorState->GetAttackState();
            const bool active = IsMeleeAttack(state);
            const bool wasActive = IsMeleeAttack(lastAttackState_);
            const bool newDraw = state == RE::ATTACK_STATE_ENUM::kDraw &&
                                 lastAttackState_ != RE::ATTACK_STATE_ENUM::kDraw;

            // Prefer kDraw; first observed melee state is a fallback if skipped.
            if (newDraw || (active && !wasActive)) {
                ClearCancelMotion();
                attackBegan_ = Clock::now();
                windowActive_ = true;
                canceledThisAttack_ = false;
            } else if (!active) {
                windowActive_ = false;
                canceledThisAttack_ = false;
            }

            lastAttackState_ = state;
        }

        void TryCancel(RE::PlayerCharacter& player)
        {
            const auto& config = AttackCancel::GetConfig();
            if (!windowActive_ || canceledThisAttack_) return;

            const auto* state = player.AsActorState();
            if (!state || !CanCancelState(state->GetAttackState()) || !player.IsPowerAttacking()) return;

            if (config.useWeaponSwingWindow) {
                // Never leave an unlimited cancel window if the graph listener
                // could not be installed. This mode requires swing events.
                if (!animationSinkRegistered_) return;
            } else {
                const int windowMs = std::clamp(config.windowMs, 0, 1000);
                if (windowMs == 0) return;
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    Clock::now() - attackBegan_).count();
                if (elapsed < 0 || elapsed > windowMs) return;
            }

            auto* values = player.AsActorValueOwner();
            if (!values) return;

            const float cost = RE::PlayerCharacter::IsGodMode() ? 0.0f :
                               std::clamp(config.staminaCost, 0.0f, 100.0f);
            if (cost > 0.0f && values->GetActorValue(RE::ActorValue::kStamina) < cost) {
                RE::HUDMenu::FlashMeter(RE::ActorValue::kStamina);
                return;
            }

            // Cancel only when the animation graph accepts the event.
            if (!player.NotifyAnimationGraph("attackStop")) return;

            // Arm AFTER attackStop, which may synchronously dispatch its own
            // animation events. Otherwise an attackStop listener could clear
            // our movement correction before the next motion update.
            BeginCancelMotion();
            canceledThisAttack_ = true;
            if (cost > 0.0f) {
                values->DamageActorValue(RE::ActorValue::kStamina, cost);
            }
        }
    };
}

AttackCancel::Config& AttackCancel::GetConfig()
{
    static Config config{};
    return config;
}

void AttackCancel::Install()
{
    static bool installed = false;
    if (installed) return;

    // Cancel inputs are forwarded by BlockKey.cpp on separate bindings;
    // there is no competing vanilla-input listener.
    CancelMotionHook::Install();
    installed = true;
}

void AttackCancel::OnCancelPressed()
{
    AttackCancelGraphSink::GetSingleton().OnCancelPressed(RE::PlayerCharacter::GetSingleton());
}

void AttackCancel::Update()
{
    AttackCancelGraphSink::GetSingleton().Update(RE::PlayerCharacter::GetSingleton());
}

void AttackCancel::Reset()
{
    AttackCancelGraphSink::GetSingleton().Reset();
}

void AttackCancel::RenderSettings()
{
    namespace ui = ImGuiMCP;
    auto& config = GetConfig();

    ui::Text("Attack Cancel");
    ui::Separator();
    bool changed = false;
    changed |= ui::Checkbox("Enable Attack Cancel", &config.enabled);
    changed |= ui::Checkbox("End Cancel Window at Weapon Swing", &config.useWeaponSwingWindow);
    if (config.useWeaponSwingWindow) {
        ui::Text("Cancel until weaponSwing or weaponLeftSwing fires.");
    } else {
        changed |= ui::SliderInt("Cancel Window (ms)", &config.windowMs, 0, 1000);
        ui::Text("Timed mode: 0 ms disables cancellation.");
    }
    changed |= ui::SliderFloat("Cancel Stamina Cost", &config.staminaCost, 0.0f, 100.0f, "%.1f");
    ui::Text("Press your assigned Attack Cancel key during a power attack.");
    ui::Text("0 stamina makes cancellation free in either mode.");

    if (ui::Button("Reset Attack Cancel Defaults")) {
        config = Config{};
        changed = true;
    }

    if (changed) {
        Settings::Save();
    }
}
