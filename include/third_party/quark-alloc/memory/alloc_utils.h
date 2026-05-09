#pragma once

#include <utility>
#include <new>
#include "arena.h"

namespace quark::memory {

// Create object
template<typename T, typename... Args>
T* make(Arena& arena, Args&&... args) {
    void* mem = arena.alloc(sizeof(T), alignof(T));
    return new (mem) T(std::forward<Args>(args)...);
}

// Create default-constructed object
template<typename T>
T* make_default(Arena& arena) {
    void* mem = arena.alloc(sizeof(T), alignof(T));
    return new (mem) T();
}

// Allocate raw object memory (NO construction)
template<typename T>
T* alloc_raw(Arena& arena) {
    return static_cast<T*>(arena.alloc(sizeof(T), alignof(T)));
}

// Allocate array (RAW memory only)
template<typename T>
T* alloc_array_raw(Arena& arena, size_t count) {
    return static_cast<T*>(arena.alloc(sizeof(T) * count, alignof(T)));
}

// Allocate array + construct elements
template<typename T>
T* make_array(Arena& arena, size_t count) {
    T* mem = static_cast<T*>(
        arena.alloc(sizeof(T) * count, alignof(T))
    );

    for (size_t i = 0; i < count; i++) {
        new (&mem[i]) T();
    }

    return mem;
}

} // namespace quark::memory