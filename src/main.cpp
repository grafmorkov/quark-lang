#include <filesystem>
#include <iostream>
#include <chrono>
#include <fstream>
#include <cstdlib>

#include "quark/frontend/lexer.h"
#include "quark/frontend/parser.h"
#include "quark/semantic/semantic.h"
#include "quark/support/compiler_context.h"

#include "utils/options.h"
#include "utils/logger.h"

#include "quark/ir/ir_gen.h"
#include "quark/backend/fasmcodegen.h"
#include "quark/backend/native_backend.h"

#include "quark/modules/module.h"
#include "quark/linker/linker.h"

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

        quark::CompilerContext ctx;

        {
            ctx.root_path = utils::io::get_executable_directory();
            // If binary is in build/ subdirectory, use parent (source root)
            auto parent = ctx.root_path.parent_path();
            if (std::filesystem::exists(parent / "std" / "io" / "io.qk")) {
                ctx.root_path = parent;
            }
        }

        quark::modules::ModuleManager mm(ctx);
        quark::linker::Linker linker(mm, ctx);

        auto* entry = mm.load_entry(opts.input_file);

        // Load std::attrs for runtime attribute lowering (e.g. @guard)
        {
            auto guard_path = ctx.root_path / "std" / "guard.qk";
            if (std::filesystem::exists(guard_path)) {
                auto* guard_mod = mm.load_module(guard_path);
                auto io_path = ctx.root_path / "std" / "io.qk";
                if (std::filesystem::exists(io_path)) {
                    auto* io_mod = mm.load_module(io_path);
                    guard_mod->dependencies.push_back(io_mod);
                }
            }
        }

        // Always compile the pure-Quark format runtime (used by `as str` casts)
        {
            auto format_path = ctx.root_path / "std" / "format" / "format.qk";
            if (std::filesystem::exists(format_path)) {
                mm.load_module(format_path);
            }
        }

        mm.build_graph(entry);
        if (ctx.errors.has_errors()) return 1;

        // Semantic analysis
        for (auto* mod : mm.ordered_modules()) {
            quark::sm::SemanticAnalyzer sem(
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
        quark::codegen::IRGenerator irgen(ctx);
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
            quark::codegen::FasmCodeGenerator fasmCodegen;
            std::string asm_code = fasmCodegen.generate(irgen.program);
            utils::logger::info("asm:");
            utils::logger::info(asm_code);
        }

        auto& root = ctx.root_path;

        // Build
        if (opts.build || opts.run) {
        #ifdef _WIN32
            auto exe_path = root / "out.exe";

            // Native backend: IR -> instruction selection -> machine code -> PE executable
            {
                quark::codegen::NativeBackend nativeBackend;
                auto pe_bytes = nativeBackend.generate(irgen.program);
                std::ofstream file(exe_path, std::ios::binary);
                file.write(
                    reinterpret_cast<const char*>(pe_bytes.data()),
                    static_cast<std::streamsize>(pe_bytes.size()));
            }
        #else
            auto obj_path = root / "out.o";
            auto exe_path = root / "out";

            // Native backend: IR -> instruction selection -> machine code -> ELF object
            {
                quark::codegen::NativeBackend nativeBackend;
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

        // Run
        if (opts.run) {
            #ifdef _WIN32
                auto run_path = root / "out.exe";
            #else
                auto run_path = root / "out";
            #endif
            std::system(run_path.string().c_str());
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
