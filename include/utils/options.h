#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <stdexcept>

#include "logger.h"
#include "quant/backend/mc.h"
#include "quant/backend/targets.h"

using namespace utils::logger;

namespace utils::options {

    using quant::codegen::mc::TargetArch;
    using quant::codegen::mc::TargetOS;

    struct Options {
        bool emit_ir = false;
        bool emit_asm = false;
        bool no_compile = false;
        bool time = false;
        std::string input_file;
        std::string output_file;
        bool has_output = false;

        // Resolved compilation target. Defaults to the host's native backend
        // if it is enabled in this build; otherwise --target is mandatory.
        std::string target_name;   // canonical, e.g. "aarch64-zeropoint"
        TargetArch target_arch = TargetArch::X86_64;
        TargetOS target_os = TargetOS::Linux;
    };

    enum class Flag {
        EmitIR,
        EmitAsm,
        NoCompile,
        Time
    };

    Options parse_args(int argc, char** argv); 
}
