#include "quant/backend/targets.h"

#include <algorithm>
#include <cctype>

namespace quant::codegen::mc {

namespace {

// Backends enabled in this compiler build. CMake writes the QUANT_BACKENDS
// list here at configure time (comma-separated canonical names). The macro
// only gates whole backends; per-target behavior stays in regular C++.
#ifndef QUANT_ENABLED_BACKENDS
// Fallback for builds that bypass CMake (all backend sources compiled).
#ifdef _WIN32
#define QUANT_ENABLED_BACKENDS "x86_64-windows"
#else
#define QUANT_ENABLED_BACKENDS "x86_64-linux"
#endif
#define QUANT_HAS_X86_64
#define QUANT_HAS_AARCH64
#define QUANT_HAS_PE
#define QUANT_HAS_ELF
#endif

std::vector<std::string> parse_enabled_list() {
    std::vector<std::string> out;
    const std::string csv = QUANT_ENABLED_BACKENDS;
    std::size_t start = 0;
    while (start <= csv.size()) {
        const std::size_t sep = csv.find(',', start);
        std::string item = csv.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        // trim whitespace
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        item.erase(item.begin(), std::find_if(item.begin(), item.end(), not_space));
        item.erase(std::find_if(item.rbegin(), item.rend(), not_space).base(), item.end());
        if (!item.empty()) out.push_back(item);
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return out;
}

// Whether the code generator for this architecture is compiled into this
// binary at all (CMake only builds the ISel of enabled architectures).
bool arch_compiled_in(TargetArch arch) {
    switch (arch) {
        case TargetArch::X86_64:
#ifdef QUANT_HAS_X86_64
            return true;
#else
            return false;
#endif
        case TargetArch::AARCH64:
#ifdef QUANT_HAS_AARCH64
            return true;
#else
            return false;
#endif
    }
    return false;
}

const std::vector<TargetInfo>& registry() {
    static const std::vector<TargetInfo> table = [] {
        const auto enabled = parse_enabled_list();
        const auto is_on = [&](const char* name) {
            return std::find(enabled.begin(), enabled.end(), name) != enabled.end();
        };
        std::vector<TargetInfo> v;
        const auto add = [&](const char* name, TargetArch arch, TargetOS os) {
            // A target is usable only if it is both listed in QUANT_BACKENDS
            // and its code generator was actually compiled into this binary.
            v.push_back({name, arch, os, is_on(name) && arch_compiled_in(arch)});
        };
        add("x86_64-linux", TargetArch::X86_64, TargetOS::Linux);
        add("aarch64-linux", TargetArch::AARCH64, TargetOS::Linux);
        add("x86_64-windows", TargetArch::X86_64, TargetOS::Windows);
        add("aarch64-zeropoint", TargetArch::AARCH64, TargetOS::ZeroPoint);
        return v;
    }();
    return table;
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Legacy CLI spellings kept for compatibility. Bare x86 names are
// host-relative: they select the host's native OS flavor (this preserves
// the historical behavior where the executable format followed the host).
// Bare AArch64 names always mean Linux; availability is decided by the
// enabled-backend set, not by the spelling.
std::optional<std::string> alias_to_canonical(const std::string& lower) {
#ifdef _WIN32
    constexpr bool kWindowsHost = true;
#else
    constexpr bool kWindowsHost = false;
#endif
    if (lower == "x86_64" || lower == "x86-64" || lower == "amd64") {
        return std::string(kWindowsHost ? "x86_64-windows" : "x86_64-linux");
    }
    if (lower == "aarch64" || lower == "arm64") return std::string("aarch64-linux");
    if (lower == "arm64-zeropoint") return std::string("aarch64-zeropoint");
    return std::nullopt;
}

} // namespace

const std::vector<TargetInfo>& known_targets() {
    return registry();
}

const TargetInfo* resolve_target(const std::string& spelling) {
    const std::string key = to_lower(spelling);
    for (const auto& t : registry()) {
        if (t.name == key) return &t;
    }
    const auto canon = alias_to_canonical(key);
    if (!canon) return nullptr;
    for (const auto& t : registry()) {
        if (t.name == *canon) return &t;
    }
    return nullptr;
}

std::string host_default_target_name() {
#ifdef QUANT_DEFAULT_TARGET
    // CMake-time override (validated to be one of QUANT_BACKENDS).
    return QUANT_DEFAULT_TARGET;
#elif defined(_WIN32)
    return "x86_64-windows";
#else
    return "x86_64-linux";
#endif
}

std::string enabled_targets_csv() {
    std::string out;
    for (const auto& t : registry()) {
        if (!t.enabled) continue;
        if (!out.empty()) out += ", ";
        out += t.name;
    }
    return out;
}

} // namespace quant::codegen::mc
