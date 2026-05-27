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

            sem.analyze(mod->ast);
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
        // Write C File
        std::ofstream file("out.S");
        file << asm_code;
        file.close();

        // Build fasm
        if (opts.build || opts.run) {
            auto root = std::filesystem::absolute(QUARK_ROOT);

        #ifdef _WIN32
            std::string build_cmd = "fasm out.S out.exe";
        #else
            std::string build_cmd = "fasm out.asm   out.o";
        #endif

            if (std::system(build_cmd.c_str()) != 0) {
                utils::logger::error("build failed\n");
                return 1;
            }
        }

        // Run
        if (opts.run) {
            std::system("./out");
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