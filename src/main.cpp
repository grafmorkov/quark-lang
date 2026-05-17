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
        memory::Arena arena;

        quark::CompilerContext ctx;
        // Lexer
        quark::lx::Lexer lex(opts.input_file.c_str(), ctx);

        // Parser
        quark::ps::Parser parser(lex, ctx);
        auto ast = parser.parse();
        
        // Semantic
        quark::sm::SemanticAnalyzer sem(ctx);
        sem.analyze(ast);

        // IRGen
        quark::codegen::IRGenerator irgen(ctx);
        irgen.gen_program(ast);

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

        #ifdef _WIN32
            std::string output = "out.exe";
            std::string cmd = "fasm out.S " + output;
        #else
            std::string output = "out";
            std::string cmd = "fasm out.S " + output;
        #endif

            if (std::system(cmd.c_str()) != 0) {
                utils::logger::error("Fasm build failed\n");
                return 1;
            }

        #ifndef _WIN32
            std::string chmod_cmd = "chmod +x " + output;

            if (std::system(chmod_cmd.c_str()) != 0) {
                utils::logger::error("chmod failed\n");
                return 1;
            }
        #endif
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