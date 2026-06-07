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
            if (std::filesystem::exists(parent / "std" / "io.qk")) {
                ctx.root_path = parent;
            }
        }

        quark::modules::ModuleManager mm(ctx);
        quark::linker::Linker linker(mm);

        auto* entry = mm.load_entry(opts.input_file);
        mm.build_graph(entry);

        // Semantic analysis
        for (auto* mod : mm.ordered_modules()) {
            quark::sm::SemanticAnalyzer sem(
                ctx,
                mod->namespace_path
            );

            sem.analyze(mod->ast, mod);
            mod->analyzed = true;
        }
        // Linker validation
        linker.validate();

        // IRGen
        quark::codegen::IRGenerator irgen(ctx);
        irgen.gen_program(mm.ordered_modules());

        if (opts.emit_ir) {
             utils::logger::info("IR");
        }
        if(opts.no_compile){
            return 0;
        }
        // Codegen
        quark::codegen::FasmCodeGenerator fasmCodegen;
        std::string asm_code;

        asm_code = fasmCodegen.generate(irgen.program);

        if (opts.emit_asm) {
            utils::logger::info("asm:");
            utils::logger::info(asm_code);
        }
        // Write Asm File
        auto& root = ctx.root_path;
        auto asm_path = root / "out.S";

        {
            std::ofstream file(asm_path);
            file << asm_code;
        }

        // Build fasm
        if (opts.build || opts.run) {
        #ifdef _WIN32
            auto fasm_path = root / "fasm" / "fasm.exe";
            auto exe_path = root / "out.exe";

            std::string build_cmd = fasm_path.string() + " " + asm_path.string() + " " + exe_path.string();

            if (std::system(build_cmd.c_str()) != 0) {
                utils::logger::error("build failed\n");
                return 1;
            }
        #else
            auto fasm_path = root / "fasm" / "fasm";
            auto obj_path = root / "out.o";
            auto exe_path = root / "out.exe";

            // Assemble runtime files
            struct RuntimeAsm {
                std::filesystem::path src;
                std::string obj_name;
            };
            RuntimeAsm runtime_files[] = {
                { root / "qkrt" / "linux" / "io.asm", "qkrt_io.o" },
                { root / "qkrt" / "linux" / "file.asm", "qkrt_file.o" },
                { root / "qkrt" / "linux" / "format.asm", "qkrt_format.o" },
            };

            std::string link_objs;
            for (const auto& r : runtime_files) {
                auto obj = root / r.obj_name;
                std::string cmd = fasm_path.string() + " " + r.src.string() + " " + obj.string();
                if (std::system(cmd.c_str()) != 0) {
                    utils::logger::error("build failed: runtime assembly\n");
                    return 1;
                }
                if (!link_objs.empty()) link_objs += " ";
                link_objs += obj.string();
            }

            // Assemble generated code
            std::string asm_cmd = fasm_path.string() + " " + asm_path.string() + " " + obj_path.string();
            if (std::system(asm_cmd.c_str()) != 0) {
                utils::logger::error("build failed\n");
                return 1;
            }

            // Link with ld
            std::string link_cmd = "ld -o " + exe_path.string() + " " + obj_path.string() + " " + link_objs;
            if (std::system(link_cmd.c_str()) != 0) {
                utils::logger::error("link failed\n");
                return 1;
            }

            // Cleanup object files
            for (const auto& r : runtime_files) {
                std::filesystem::remove(root / r.obj_name);
            }
            std::filesystem::remove(obj_path);
        #endif
            std::filesystem::remove(asm_path);
        }

        // Run
        if (opts.run) {
            auto run_path = root / "out.exe";
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
