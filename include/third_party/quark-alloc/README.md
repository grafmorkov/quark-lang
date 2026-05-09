# Quark Allocator

This memory allocator library was developed for Quark programming language.

Simple allocator, that is fast, simple and direct without any abstraction.

---

## Features

It has:

* **Arena (Region) Allocation**

  * fast linear memory allocation
  * allocation memory is deallocated at once (arena reset/destroy)

* **Allocation Utilities**

  * allocate in 'make<T>' fashion
  * utilities to allocate array
  * allocating zeroed memory region

* **Memory Allocation Using Block Scheme**

  * automatically grows memory block when allocating space fails
  * uses several memory blocks instead of reallocating space

* **Debugging Utilities**

  * basic statistics (used memory, peak memory, amount of allocations)
  * filling memory with certain pattern (debug mode)

---
## Usage

### Step 1. Clone the Repository

```bash
git clone https://github.com/grafmorkov/quark-alloc.git
```

---

### Step 2. Add to Your Project

```cpp
#include "quark-alloc/memory/alloc.h"
```

### 3. Example usage

```cpp
#include "quark-alloc/memory/alloc.h"

int main() {
    quark::memory::Arena arena(1024 * 1024);

    int* value = quark::memory::make<int>(arena);
    *value = 42;

    float* arr = quark::memory::alloc_array<float>(arena, 10);

    arena.reset(); // free all memory at once
}
```
---

## Summary

This allocator does not perform frequent allocations and de-allocations. Rather, it allocates/deallocates space using arenas.

Allocated memory is released when an arena is reset or destroyed. Thus this allocator becomes:

* extremely fast
* predictable
* simple implementation

---
## Why use this allocator

Use it:

* when you work with a compiler (like Quark).
* when creating ASTs or IR objects.
* memory allocation should be really fast.
* you do not need manual deallocations everywhere.

There is no need for ownership, just lifetime.

---

## Concept

The main idea is pretty clear:

> Fast allocation, batch de-allocation.

No deallocation of individual objects, no dependencies of their ownership.

Only allocate -> use -> reset.

---

## Remarks

* Library is not thread safe.
* Library is aimed at compiler and runtime applications.
* Not focused on flexibility but on simplicity and performance