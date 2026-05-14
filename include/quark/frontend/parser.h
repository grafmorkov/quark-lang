#pragma once

#include <iostream>
#include <vector>
#include <queue>

#include "token.h"
#include "lexer.h"
#include "ast.h"

namespace quark::ps {

    class Parser {
        public:
            Parser(lx::Lexer& lex, CompilerContext& ctx);

            std::vector<ast::Stmt*> parse();

        private:
            lx::Lexer& lexer;
            CompilerContext& ctx;

            Token current;
            Token previous;

            std::deque<Token> buffer;

        private:
            // core token control
            Token advance();
            bool check(TokenType type);
            bool match(TokenType type);
            Token expect(TokenType type, const char* msg);
            Token peek(int offset);

            // statements
            ast::Stmt parse_statement();
            ast::VarDecl parse_var_decl();
            ast::StructDecl parse_struct_decl();
            ast::Block* parse_block();
            ast::NamespaceStmt parse_namespace_stmt();
            ast::IfStmt parse_if();
            std::vector<ast::Attribute> parse_attributes();
            ast::WhileStmt parse_while();
            ast::ReturnStmt parse_return();
            ast::FuncStmt parse_func();
            std::vector<ast::FuncArg> parse_func_args();
            
            // expressions(Pratt)
            ast::Expr* parse_expr(int precedence = 0);
            ast::Expr* parse_prefix();
            ast::Expr* parse_postfix(ast::Expr* left);

            // helpers
            ast::Expr* make_binary(ast::Expr* left, ast::Expr* right, TokenType op);
            const ast::Type* parse_type();
            bool is_var_decl();
        };

    enum class Precedence {
        Lowest = 0,
        Assignment = 1,
        NullCoalesce = 2,
        Equality = 3,
        Additive = 4,
        Multiplicative = 5,
        Prefix = 6,
        Call = 7
    };
}
