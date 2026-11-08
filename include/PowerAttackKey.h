#pragma once
#include <string_view>

namespace PowerAttackKey {
    void Initialize();
    void OnDataLoaded();
    bool IsBlockExitPending();

    // Called by FHB::Events for the player's animation graph events.
    void OnPlayerAnimationEvent(std::string_view tag);
    void OnGameLoaded();
}
