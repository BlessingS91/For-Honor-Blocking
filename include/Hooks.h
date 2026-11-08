#pragma once

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

namespace Hooks
{
    void Install();

    struct InputHook
    {
        static void thunk(
            RE::BSTEventSource<RE::InputEvent*>* a_dispatcher,
            RE::InputEvent* const* a_events);

        static inline REL::Relocation<decltype(thunk)> func;
    };
}
