#include "utils/options.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace utils::options{
	Options parse_args(int argc, char** argv) {
        	Options opts;

        	std::unordered_map<std::string, Flag> flag_map = {
            	{"--emit-ir", Flag::EmitIR},
            	{"--emit-asm", Flag::EmitAsm},
            	{"--no-compile", Flag::NoCompile},
            	{"--time", Flag::Time},
        };

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "-o") {
                if (i + 1 >= argc) {
                    throw std::runtime_error("Option '-o' requires an output file");
                }
                opts.output_file = argv[++i];
                opts.has_output = true;
                continue;
            }

            auto it = flag_map.find(arg);
            if (it != flag_map.end()) {
                switch (it->second) {
                    case Flag::EmitIR: opts.emit_ir = true; break;
                    case Flag::EmitAsm: opts.emit_asm = true; break;
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
