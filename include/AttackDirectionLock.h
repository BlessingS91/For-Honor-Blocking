#pragma once

// Player attack animation guard for the existing CameraMovementFHB writer.
namespace AttackDirectionLock {
    void Lock();
    void Unlock();
    void Reset();
}
