#pragma once

// Player attack animation guard for the existing blockDirectionFHB writer.
namespace AttackDirectionLock {
    void Lock();
    void Unlock();
    void Reset();
}
