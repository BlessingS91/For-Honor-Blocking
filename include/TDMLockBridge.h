#pragma once

namespace TDMLockBridge {
    // kDataLoaded: optional TDM dependency; FHB works without it.
    void Initialize();

    // Called only on the physical BlockKey button transitions.
    void OnBlockPressed();
    void OnBlockReleased();

    // Called once on PlayerCharacter::Update (the same hook SPA already uses).
    void Update();
}
