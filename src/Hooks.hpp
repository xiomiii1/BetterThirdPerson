#pragma once

#include <cstdint>
#include <dlfcn.h>
#include <new>

#include <pl/memory/Hook.hpp>

namespace betterthirdperson::hooks {

struct State {
    void* target;
    void* detour;
};

using Handle = State*;

inline Handle install(
    void* target,
    void* detour,
    void** original
) {
    if (!target || !detour) {
        return nullptr;
    }

    if (
        pl::memory::hook(
            target,
            detour,
            original
        ) != 0
    ) {
        return nullptr;
    }

    auto* state =
        new (std::nothrow) State{
            target,
            detour
        };

    if (state) {
        return state;
    }

    pl::memory::unhook(
        target,
        detour
    );

    return nullptr;
}

inline void remove(
    Handle handle
) {
    if (!handle) {
        return;
    }

    pl::memory::unhook(
        handle->target,
        handle->detour
    );

    delete handle;
}

}
