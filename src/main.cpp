#include <filesystem>
#include <iostream>
#include <chrono>
#include <fstream>
#include <cstdlib>

#include "quant/frontend/lexer.h"
#include "quant/frontend/parser.h"
#include "quant/semantic/semantic.h"
#include "quant/support/compiler_context.h"

#include "utils/options.h"
#include "utils/logger.h"

#include "quant/ir/ir_gen.h"
#include "quant/backend/fasmcodegen.h"
#include "quant/backend/native_backend.h"

#include "quant/modules/module.h"
#include "quant/linker/linker.h"

int main(int argc, char **argv)
{
    try{
        using namespace std::chrono;

        auto opts = utils::options::parse_args(argc, argv);

        if (opts.input_file.empty()) {
            utils::logger::error("No input file provided");
            return 1;
        }

        // Target selection is fully resolved by parse_args against the
        // backends enabled at CMake configure time (QUANT_BACKENDS).

        auto start = high_resolution_clock::now();

        quant::CompilerContext ctx;
        ctx.target_os = opts.target_os;

        {
            ctx.root_path = utils::io::get_executable_directory();
            // If binary is in build/ subdirectory, use parent (source root)
            auto parent = ctx.root_path.parent_path();
            if (std::filesystem::exists(parent / "std" / "io" / "io.qu")) {
                ctx.root_path = parent;
            }
        }

        quant::modules::ModuleManager mm(ctx);
        quant::linker::Linker linker(mm, ctx);

        if (!std::filesystem::path(opts.input_file).has_extension())
        {
            opts.input_file += ".qu";
        }

        auto* entry = mm.load_entry(opts.input_file);

        // Always compile the pure-Quant format runtime (used by `as str` casts).
        // It ships embedded in the binary; fall back to the source tree in dev builds.
        // Skipped on ZeroPoint: format depends on std::heap, and the ZeroPoint
        // ABI has no allocation syscalls yet (kernel malloc is TODO), so the
        // runtime is unusable there and its mmap syscalls are invalid.
        if (opts.target_os != quant::codegen::mc::TargetOS::ZeroPoint &&
            mm.load_embedded("std::format") == nullptr) {
            auto format_path = ctx.root_path / "std" / "format" / "format.qu";
            if (std::filesystem::exists(format_path)) {
                mm.load_module(format_path);
            }
        }

        mm.build_graph(entry);
        if (ctx.errors.has_errors()) return 1;

        // Semantic analysis
        for (auto* mod : mm.ordered_modules()) {
            quant::sm::SemanticAnalyzer sem(
                ctx,
                mod->namespace_path
            );

            sem.analyze(mod->ast, mod);
            if (ctx.errors.has_errors()) break;
            mod->analyzed = true;
        }
        if (ctx.errors.has_errors()) return 1;

        // Linker validation
        linker.validate();
        if (ctx.errors.has_errors()) return 1;

        // IRGen
        quant::codegen::IRGenerator irgen(ctx);
        irgen.gen_program(mm.ordered_modules());
        if (ctx.errors.has_errors()) return 1;

        if (opts.emit_ir) {
             irgen.program.dump();
        }
        if(opts.no_compile){
            return 0;
        }
        // Codegen
        if (opts.emit_asm) {
            quant::codegen::FasmCodeGenerator fasmCodegen;
            fasmCodegen.target_os = opts.target_os;
            std::string asm_code = fasmCodegen.generate(irgen.program);
            utils::logger::info("asm:");
            utils::logger::info(asm_code);
        }
        // Build
        std::filesystem::path exe_path = "out";

        if(opts.has_output) exe_path = opts.output_file;

        // Create the output directory if it does not exist yet; otherwise the
        // ofstream below would silently fail and produce no executable.
        if (exe_path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(exe_path.parent_path(), ec);
        }

        auto write_output = [](const std::filesystem::path& path,
                               const std::vector<uint8_t>& bytes) {
            std::ofstream file(path, std::ios::binary);
            file.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!file) {
                utils::logger::error("failed to write output file: " + path.string());
                return false;
            }
            return true;
        };

        if (opts.target_os == quant::codegen::mc::TargetOS::Windows) {
            if (!exe_path.has_extension()) {
                exe_path += ".exe";
            }

            // Native backend: IR -> instruction selection -> machine code -> PE executable
            {
                quant::codegen::NativeBackend nativeBackend;
                auto pe_bytes = nativeBackend.generate(irgen.program, opts.target_arch, opts.target_os);
                if (!write_output(exe_path, pe_bytes)) return 1;
            }
        } else {
            std::filesystem::path obj_path = exe_path.string() + ".o";

            // Native backend: IR -> instruction selection -> machine code -> ELF object
            {
                quant::codegen::NativeBackend nativeBackend;
                auto elf_bytes = nativeBackend.generate(irgen.program, opts.target_arch, opts.target_os);
                if (!write_output(obj_path, elf_bytes)) return 1;
            }

            // Link with ld (x86-64) or ld.lld (AArch64).
            const char* ld_name = (opts.target_arch == quant::codegen::mc::TargetArch::AARCH64)
                ? "ld.lld" : "ld";
            std::string link_cmd = std::string(ld_name) + " -o " + exe_path.string() + " " + obj_path.string();
            if (opts.target_os == quant::codegen::mc::TargetOS::ZeroPoint) {
                // ZeroPoint: position-independent ET_DYN, no interpreter.
                // (lld has no GNU-style -static-pie; -pie on a fully static
                // input produces the same static-PIE image.)
                // Stock load address is 0x40000000; PIC keeps it movable.
                link_cmd += " -pie --image-base=0x40000000";
            }
            if (std::system(link_cmd.c_str()) != 0) {
                utils::logger::error("link failed\n");
                return 1;
            }

            //std::filesystem::remove(obj_path);
        }

        auto end = std::chrono::high_resolution_clock::now();

        if (opts.time) {
            std::chrono::duration<double, std::milli> duration = end - start;
            std::cout << "\nCompilation took: " << duration.count() << " ms\n";
        }

        return 0;
    }
    catch (const std::exception& e) {
        utils::logger::error(std::string(e.what()));
        return 1;
    }
}
