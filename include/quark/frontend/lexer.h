#pragma once

#include <iostream>
#include <string>
#include <fstream> 
#include <string> 
#include <vector>

#include "token.h"
#include "quark/support/compiler_context.h"

#include "utils/logger.h"

namespace quark::lx{

    class Lexer {
        private:
            std::string buffer;
            size_t pos;
            int start;

            CompilerContext& ctx;
            int token_line;
            int token_column;

            char peek();
            char advance();
            bool match(char expected);
            bool is_at_end();

            Token make_token(TokenType);
            Token make_number();

            Token number();
            Token identifier();
            Token string();

        public:
            Token next_token();
            
        Lexer(const char *fileName, CompilerContext& _ctx): ctx(_ctx){
            std::ifstream f(fileName, std::ios::binary); 

            if (!f) 
                utils::logger::fatal("Failed to read the file while creating lexer!"); 

            buffer = std::string( 
                (std::istreambuf_iterator<char>(f)), 
                std::istreambuf_iterator<char>() );
            pos = 0;
            start = 0;
            ctx.srcloc.line = 1;
            ctx.srcloc.column = 1;
            ctx.srcloc.file = fileName;
        }
    };
}
