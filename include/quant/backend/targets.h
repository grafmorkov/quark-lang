#pragma once

#include <optional>
#include <string>
#include <vector>

#include "quant/backend/mc.h"

namespace quant::codegen::mc {

// One entry of the target registry: a canonical CLI name plus its
// (arch, OS) pair and whether this compiler build can select it.
struct TargetInfo {
    std::string name;   // canonical spelling, e.g. "x86_64-linux"
    TargetArch arch;
    TargetOS os;
    bool enabled;
};

// All targets known to the compiler (enabled or not), in registry order.
// The enabled set is fixed at CMake configure time via QUANT_BACKENDS.
const std::vector<TargetInfo>& known_targets();

// Resolve a CLI spelling (canonical name or legacy alias like "x86_64",
// "arm64") to a registry entry. Returns nullptr for unknown spellings.
// A resolved-but-disabled entry is returned as-is so callers can tell
// "unknown target" apart from "known but not enabled".
const TargetInfo* resolve_target(const std::string& spelling);

// Canonical name of the target used when no --target is passed:
// the host-native backend ("x86_64-linux" on POSIX hosts, "x86_64-windows"
// on Windows), or the QUANT_DEFAULT_TARGET override chosen at CMake time.
std::string host_default_target_name();

// Comma-separated list of enabled backend names (for diagnostics).
std::string enabled_targets_csv();

} // namespace quant::codegen::mc
