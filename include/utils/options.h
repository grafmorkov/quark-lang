#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <stdexcept>

#include "logger.h"

using namespace utils::logger;

namespace utils::options {

    struct Options {
        bool emit_ir = false;
        bool emit_asm = false;
        bool build = false;
        bool run = false;
        bool no_compile = false;
        bool time = false;
        std::string input_file;
    };

    enum class Flag {
        EmitIR,
        EmitAsm,
        Build,
        Run,
        NoCompile,
        Time
    };

    Options parse_args(int argc, char** argv) {
        Options opts;

        std::unordered_map<std::string, Flag> flag_map = {
            {"--emit-ir", Flag::EmitIR},
            {"--emit-asm", Flag::EmitAsm},
            {"--build", Flag::Build},
            {"--run", Flag::Run},
            {"--no-compile", Flag::NoCompile},
            {"--time", Flag::Time},
        };

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            auto it = flag_map.find(arg);
            if (it != flag_map.end()) {
                switch (it->second) {
                    case Flag::EmitIR: opts.emit_ir = true; break;
                    case Flag::EmitAsm: opts.emit_asm = true; break;
                    case Flag::Build: opts.build = true; break;
                    case Flag::Run: opts.run = true; break;
                    case Flag::NoCompile: opts.no_compile = true; break;
                    case Flag::Time: opts.time = true; break;
                }
            } else {
                if (!opts.input_file.empty()) {
                    fatal("Multiple input files are not supported: '" 
                        + opts.input_file + "' and '" + arg + "'");
                }
                opts.input_file = arg;
            }
        }

        if (opts.input_file.empty()) {
            throw std::runtime_error("No input file provided");
        }

        return opts;
    }

}