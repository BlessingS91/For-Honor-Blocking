#pragma once

namespace BlockKey {
    // Register the SimpleBlock settings section and input callbacks after kDataLoaded.
    void OnDataLoaded();

    // Clear per-session button/modifier state when starting or loading a game.
    void OnGameLoaded();
}
