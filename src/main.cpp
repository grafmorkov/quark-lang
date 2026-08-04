#include <filesystem>
#include <iostream>
#include <chrono>
#include <fstream>
#include <cstdlib>

#include "quanta/frontend/lexer.h"
#include "quanta/frontend/parser.h"
#include "quanta/semantic/semantic.h"
#include "quanta/support/compiler_context.h"

#include "utils/options.h"
#include "utils/logger.h"

#include "quanta/ir/ir_gen.h"
#include "quanta/backend/fasmcodegen.h"
#include "quanta/backend/native_backend.h"

#include "quanta/modules/module.h"
#include "quanta/linker/linker.h"

int main(int argc, char **argv)
{
    try{
        using namespace std::chrono;

        auto opts = utils::options::parse_args(argc, argv);

        if (opts.input_file.empty()) {
            utils::logger::error("No input file provided");
            return 1;
        }

        auto start = high_resolution_clock::now();

        quanta::CompilerContext ctx;

        {
            ctx.root_path = utils::io::get_executable_directory();
            // If binary is in build/ subdirectory, use parent (source root)
            auto parent = ctx.root_path.parent_path();
            if (std::filesystem::exists(parent / "std" / "io" / "io.qu")) {
                ctx.root_path = parent;
            }
        }

        quanta::modules::ModuleManager mm(ctx);
        quanta::linker::Linker linker(mm, ctx);

        auto* entry = mm.load_entry(opts.input_file);

        // Always compile the pure-Quanta format runtime (used by `as str` casts).
        // It ships embedded in the binary; fall back to the source tree in dev builds.
        if (mm.load_embedded("std::format") == nullptr) {
            auto format_path = ctx.root_path / "std" / "format" / "format.qu";
            if (std::filesystem::exists(format_path)) {
                mm.load_module(format_path);
            }
        }

        mm.build_graph(entry);
        if (ctx.errors.has_errors()) return 1;

        // Semantic analysis
        for (auto* mod : mm.ordered_modules()) {
            quanta::sm::SemanticAnalyzer sem(
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
        quanta::codegen::IRGenerator irgen(ctx);
        irgen.gen_program(mm.ordered_modules());
        if (ctx.errors.has_errors()) return 1;

        if (opts.emit_ir) {
             utils::logger::info("IR");
        }
        if(opts.no_compile){
            return 0;
        }
        // Codegen
        if (opts.emit_asm) {
            quanta::codegen::FasmCodeGenerator fasmCodegen;
            std::string asm_code = fasmCodegen.generate(irgen.program);
            utils::logger::info("asm:");
            utils::logger::info(asm_code);
        }

        // Build
        if (opts.has_output) {
            std::filesystem::path exe_path = opts.output_file;
        #ifdef _WIN32
            if (!exe_path.has_extension()) {
                exe_path += ".exe";
            }
            
            // Native backend: IR -> instruction selection -> machine code -> PE executable
            {
                quanta::codegen::NativeBackend nativeBackend;
                auto pe_bytes = nativeBackend.generate(irgen.program);
                std::ofstream file(exe_path, std::ios::binary);
                file.write(
                    reinterpret_cast<const char*>(pe_bytes.data()),
                    static_cast<std::streamsize>(pe_bytes.size()));
            }
        #else
            std::filesystem::path obj_path = exe_path.string() + ".o";

            // Native backend: IR -> instruction selection -> machine code -> ELF object
            {
                quanta::codegen::NativeBackend nativeBackend;
                auto elf_bytes = nativeBackend.generate(irgen.program);
                std::ofstream file(obj_path, std::ios::binary);
                file.write(
                    reinterpret_cast<const char*>(elf_bytes.data()),
                    static_cast<std::streamsize>(elf_bytes.size()));
            }

            // Link with ld
            std::string link_cmd = "ld -o " + exe_path.string() + " " + obj_path.string();
            if (std::system(link_cmd.c_str()) != 0) {
                utils::logger::error("link failed\n");
                return 1;
            }

            std::filesystem::remove(obj_path);
        #endif
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
