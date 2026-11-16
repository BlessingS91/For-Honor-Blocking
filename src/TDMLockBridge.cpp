#include "TDMLockBridge.h"
#include "TDM_API.h"
#include "Settings.h"

#include <RE/Skyrim.h>

#include <atomic>
#include <cstdint>
#include <Windows.h>

namespace {
    TDM_API::IVTDM5* g_tdm = nullptr;
    std::atomic_bool g_blockHeld{false};
    std::atomic_bool g_unlockRequested{false};
    constexpr std::uint64_t kUnlockDelayMs = 250;
    std::atomic<std::uint64_t> g_unlockDueMs{0};
    bool g_ownsDisable = false;
    bool g_warnedWrongThread = false;

    bool ReleaseOurDisable()
    {
        if (!g_ownsDisable || !g_tdm) return true;

        const auto result = g_tdm->ReleaseDisableTargetLock(SKSE::GetPluginHandle());
        if (result == TDM_API::APIResult::OK || result == TDM_API::APIResult::NotOwner) {
            g_ownsDisable = false;
            SKSE::log::info("[FHB TDM] Released forced target-lock disable");
            return true;
        }

        SKSE::log::warn("[FHB TDM] ReleaseDisableTargetLock returned {}", static_cast<int>(result));
        return false;
    }
}

namespace TDMLockBridge {
    void Initialize()
    {
        g_tdm = TDM_API::RequestPluginAPI();
        if (g_tdm) {
            SKSE::log::info("[FHB TDM] V5 API connected. Bind TDM target-lock key to the SAME physical key as SimpleBlock.");
        } else {
            SKSE::log::info("[FHB TDM] V5 API unavailable; hold-block lock-on disabled");
        }
    }

    void OnBlockPressed()
    {
        g_blockHeld.store(true, std::memory_order_release);
        g_unlockRequested.store(false, std::memory_order_release);
        g_unlockDueMs.store(0, std::memory_order_release);
        // TDM handles the key-down itself. Its public API cannot initiate a lock.
    }

    void OnBlockReleased()
    {
        // An attack cancel requires Block to be released and pressed again.
        // Delay only that release while a power attack is actually underway.
        // The second press cancels the pending unlock in OnBlockPressed().
        auto* player = RE::PlayerCharacter::GetSingleton();
        const bool powerAttackRelease = player && player->IsPowerAttacking();
        g_unlockDueMs.store(powerAttackRelease ? GetTickCount64() + kUnlockDelayMs : 0,
                            std::memory_order_release);
        g_blockHeld.store(false, std::memory_order_release);
        g_unlockRequested.store(true, std::memory_order_release);
        // Normal releases unlock at the next TDM Update; power attacks wait 250 ms.
    }

    void Update()
    {
        if (!g_tdm) return;
        // Disabling auto-unlock cancels pending requests; release any disable we own.
        if (!Settings::PlayerUI.unlockTDMOnBlockRelease) {
            g_unlockRequested.store(false, std::memory_order_release);
            g_unlockDueMs.store(0, std::memory_order_release);
        }
        if (!g_ownsDisable && !g_unlockRequested.load(std::memory_order_acquire)) return;

        if (GetCurrentThreadId() != g_tdm->GetTDMThreadId()) {
            if (!g_warnedWrongThread) {
                SKSE::log::warn("[FHB TDM] Player Update is not running on TDM thread; target-lock API request postponed");
                g_warnedWrongThread = true;
            }
            return;
        }

        if (!Settings::PlayerUI.unlockTDMOnBlockRelease) {
            ReleaseOurDisable();
            return;
        }

        if (g_blockHeld.load(std::memory_order_acquire)) {
            // Allow TDM's next normal key-down to acquire a target.
            ReleaseOurDisable();
            return;
        }

        if (!g_unlockRequested.load(std::memory_order_acquire)) {
            ReleaseOurDisable();
            return;
        }

        // The deadline is zero for ordinary Block releases; only power attacks wait.
        // Another Block press within the delay cancels the unlock in OnBlockPressed().
        if (GetTickCount64() < g_unlockDueMs.load(std::memory_order_acquire)) return;

        if (!g_tdm->GetTargetLockState()) {
            // The forced disable has done its job; don't interfere with other mods
            // or prevent ordinary TDM use while the block key is not held.
            if (ReleaseOurDisable()) {
                g_unlockRequested.store(false, std::memory_order_release);
                g_unlockDueMs.store(0, std::memory_order_release);
            }
            return;
        }

        if (g_ownsDisable) return; // Wait for TDM's normal update to end the lock.

        const auto result = g_tdm->RequestDisableTargetLock(SKSE::GetPluginHandle());
        if (result == TDM_API::APIResult::OK || result == TDM_API::APIResult::AlreadyGiven) {
            g_ownsDisable = true;
            SKSE::log::info("[FHB TDM] Requested unlock after releasing block");
        } else if (result != TDM_API::APIResult::BadThread) {
            // Another owner / TDM restriction. Do not steal or retry every frame.
            SKSE::log::warn("[FHB TDM] RequestDisableTargetLock returned {}", static_cast<int>(result));
            g_unlockRequested.store(false, std::memory_order_release);
        }
    }
}
