#pragma once

// Bridge to the external arena-alloc library (github.com/grafmorkov/quark-alloc).
// The library hard-codes namespace quark::memory; alias it so the rest of the
// codebase can keep referring to `memory` without knowing about the external name.
#include "memory/alloc.h"

namespace memory = quark::memory;
