#include "quark/frontend/parser.h"
#include "utils/logger.h"

#include <utility>

using namespace utils::logger;

namespace quark::ps {

namespace {

ast::BinaryOp get_op_from_token(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:  return ast::BinaryOp::Add;
        case TOKEN_MINUS: return ast::BinaryOp::Sub;
        case TOKEN_STAR:  return ast::BinaryOp::Mul;
        case TOKEN_SLASH: return ast::BinaryOp::Div;
        case TOKEN_EQEQ:   return ast::BinaryOp::Eq;
        case TOKEN_NEQ:    return ast::BinaryOp::Neq;
        case TOKEN_LT:     return ast::BinaryOp::Lt;
        case TOKEN_LTE:    return ast::BinaryOp::Lte;
        case TOKEN_GT:     return ast::BinaryOp::Gt;
        case TOKEN_GTE:    return ast::BinaryOp::Gte;
        default:           return ast::BinaryOp::Add;
    }
}

int get_precedence(TokenType t) {
    switch (t) {
        case TOKEN_EQ:
            return 1; 

        case TOKEN_EQEQ:
        case TOKEN_NEQ:
        case TOKEN_LT:
        case TOKEN_LTE:
        case TOKEN_GT:
        case TOKEN_GTE:
            return 2;

        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return 3;

        case TOKEN_STAR:
        case TOKEN_SLASH:
            return 4;

        default:
            return -1;
    }
}

template <class T>
ast::Expr* make_expr(CompilerContext& ctx, T&& kind, SourceLocation loc = {}) {
    auto* e = memory::make_default<ast::Expr>(ctx.ast_arena);
    e->kind = std::forward<T>(kind);
    e->loc = loc;
    return e;
}

} // namespace

Parser::Parser(lx::Lexer& lex, CompilerContext& ctx_)
    : lexer(lex), ctx(ctx_) {
    current = lexer.next_token();
}

std::vector<ast::Stmt*> Parser::parse() {
    std::vector<ast::Stmt*> out;

    while (!check(TOKEN_EOF)) {
        out.push_back(memory::make<ast::Stmt>(ctx.ast_arena, parse_statement()));
    }

    return out;
}

Token Parser::advance() {
    previous = current;

    if (!buffer.empty()) {
        current = buffer.front();
        buffer.pop_front();
    } else {
        current = lexer.next_token();
    }

    return previous;
}

bool Parser::check(TokenType type) {
    return current.type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

Token Parser::expect(TokenType type, const char* msg) {
    if (!check(type)) {
        error(current.loc, msg);

        Token bad = current;
        if (!check(TOKEN_EOF)) {
            advance();
        }
        return bad;
    }

    return advance();
}

Token Parser::peek(int n) {
    while (buffer.size() <= (size_t)n) {
        buffer.push_back(lexer.next_token());
    }
    return buffer[n];
}

// Statements

ast::Stmt Parser::parse_statement() {
    if (match(TOKEN_RETURN)) {
        return ast::Stmt{ ast::ReturnStmt{ parse_return() } };
    }

    if (match(TOKEN_IF)) {
        return ast::Stmt{ ast::IfStmt{ parse_if() } };
    }

    if (match(TOKEN_WHILE)) {
        return ast::Stmt{ ast::WhileStmt{ parse_while() } };
    }

    if (match(TOKEN_EXTERN)) {
        expect(TOKEN_FUNC, "Expected 'func' after extern");
        return ast::Stmt{ ast::FuncStmt{ parse_func(true) } };
    }

    if (match(TOKEN_FUNC)) {
        return ast::Stmt{ ast::FuncStmt{ parse_func(false) } };
    }

    if (match(TOKEN_STRUCT)) {
        return ast::Stmt{ ast::StructDecl{ parse_struct_decl() } };
    }

    if (match(TOKEN_NAMESPACE)) {
        return ast::Stmt{ ast::NamespaceStmt{ parse_namespace_stmt() } };
    }

    if (match(TOKEN_LOAD)) {
        return ast::Stmt{ ast::LoadStmt{ parse_load() } };
    }

    if (is_var_decl()) {
        return ast::Stmt{ parse_var_decl() };
    }

    ast::Expr* expr = parse_expr(0);
    expect(TOKEN_SEMICOLON, "Expected ';' after expression");
    return ast::Stmt{ ast::ExprStmt{ expr } };
}

bool Parser::is_var_decl() {
    if (check(TOKEN_MUT)) {
        return peek(0).type == TOKEN_IDENT && peek(1).type == TOKEN_COLON;
    }
    return check(TOKEN_IDENT) && peek(0).type == TOKEN_COLON;
}

ast::VarDecl Parser::parse_var_decl() {
    ast::VarDecl ret;
    ret.is_mut = match(TOKEN_MUT);

    Token name = expect(TOKEN_IDENT, "Expected variable name");
    ret.name = name.text;

    ret.type = nullptr;
    ret.value = nullptr;

    if (match(TOKEN_COLON)) {
        ret.type = parse_type();

        if (match(TOKEN_EQ)) {
            ret.value = parse_expr(0);
        }
    }

    ret.attributes = parse_attributes();
    expect(TOKEN_SEMICOLON, "Expected ';' after declaration");

    return ret;
}

ast::StructDecl Parser::parse_struct_decl() {
    ast::StructDecl ret;

    Token name = expect(TOKEN_IDENT, "Expected struct name");
    ret.name = name.text;

    expect(TOKEN_LBRACE, "Expected '{' after struct name");

    while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
        ast::StructField field;

        field.is_mut = match(TOKEN_MUT);

        Token field_name = expect(TOKEN_IDENT, "Expected field name");
        field.name = field_name.text;

        expect(TOKEN_COLON, "Expected ':' after field name");
        field.type = parse_type();

        field.default_value = nullptr;
        if (match(TOKEN_EQ)) {
            field.default_value = parse_expr(0);
        }

        field.attributes = parse_attributes();

        expect(TOKEN_SEMICOLON, "Expected ';' after field");

        ret.fields.push_back(std::move(field));
    }

    expect(TOKEN_RBRACE, "Expected '}' after struct body");
    ret.attributes = parse_attributes();
    expect(TOKEN_SEMICOLON, "Expected ';' after struct body");

    return ret;
}

std::vector<ast::Attribute> Parser::parse_attributes() {
    std::vector<ast::Attribute> attrs;

    while (match(TOKEN_AT)) {
        Token name = expect(TOKEN_IDENT, "Expected attribute name");

        ast::Attribute attr;
        attr.name = name.text;

        attrs.push_back(std::move(attr));
    }

    return attrs;
}

ast::IfStmt Parser::parse_if() {
    ast::IfStmt ret;

    expect(TOKEN_LPAREN, "Expected '('");
    ret.condition = parse_expr(0);
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.then_block = parse_block();

    if (match(TOKEN_ELSE)) {
        ret.else_block = parse_block();
    } else {
        ret.else_block = nullptr;
    }

    return ret;
}

ast::WhileStmt Parser::parse_while() {
    ast::WhileStmt ret;

    expect(TOKEN_LPAREN, "Expected '('");
    ret.condition = parse_expr(0);
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.body = parse_block();

    return ret;
}

ast::ReturnStmt Parser::parse_return() {
    ast::ReturnStmt ret;
    ret.value = nullptr;

    if (!check(TOKEN_SEMICOLON)) {
        ret.value = parse_expr(0);
    }

    expect(TOKEN_SEMICOLON, "Expected ';'");

    return ret;
}

ast::FuncStmt Parser::parse_func(bool is_extern) {
    ast::FuncStmt ret;

    Token name = expect(TOKEN_IDENT, "Expected function name");
    ret.name = name.text;
    ret.is_extern = is_extern;
    ret.has_body = false;
    ret.body = nullptr;

    expect(TOKEN_LPAREN, "Expected '('");
    ret.args = parse_func_args();
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.return_type = parse_type(true);

    if (check(TOKEN_LBRACE)) {
        ret.body = parse_block();   
        ret.has_body = true;
    } else {
        expect(TOKEN_SEMICOLON, "Expected ';' after function declaration");
    }

    return ret;
}

std::vector<ast::FuncArg> Parser::parse_func_args() {
    std::vector<ast::FuncArg> args;

    if (check(TOKEN_RPAREN)) {
        return args;
    }

    while (true) {
        bool is_mut = match(TOKEN_MUT);

        Token name = expect(TOKEN_IDENT, "Expected argument name");
        expect(TOKEN_COLON, "Expected ':' after argument name");

        const ast::Type* type = parse_type(true);

        args.push_back({
            std::string(name.text),
            type,
            is_mut
        });

        if (!match(TOKEN_COMMA)) {
            break;
        }
    }

    return args;
}

ast::NamespaceStmt Parser::parse_namespace_stmt() {
    ast::NamespaceStmt ret;
    ret.name = expect(TOKEN_IDENT, "Expected namespace name").text;
    ret.body = parse_block();
    return ret;
}

ast::Block* Parser::parse_block() {
    expect(TOKEN_LBRACE, "Expected '{'");

    auto* block = memory::make_default<ast::Block>(ctx.ast_arena);

    while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
        block->stmts.push_back(
            memory::make<ast::Stmt>(ctx.ast_arena, parse_statement())
        );
    }

    expect(TOKEN_RBRACE, "Expected '}'");

    return block;
}
ast::LoadStmt Parser::parse_load() {
    ast::LoadStmt ret;

    std::string_view raw = expect(TokenType::TOKEN_STRING, "Expected module").text;

    // temporary fix
    if (!raw.empty() && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
    }

    ret.module = raw;

    expect(TOKEN_SEMICOLON, "Expected ';' after load");
    return ret;
}
// Expressions

ast::Expr* Parser::parse_expr(int min_prec) {
    ast::Expr* left = parse_prefix();
    if (!left) return nullptr;

    left = parse_postfix(left);

    while (true) {
        int prec = get_precedence(current.type);
        if (prec < min_prec) break;

        Token op = advance();

        if (op.type == TOKEN_EQ) {
            if (!std::holds_alternative<ast::VarExpr>(left->kind) &&
                !std::holds_alternative<ast::FieldExpr>(left->kind)) {
                error(op.loc, "Invalid assignment target");
                (void)parse_expr(prec);
                return left;
            }

            ast::Expr* right = parse_expr(prec);
            return make_expr(ctx, ast::AssignExpr{ left, right }, left->loc);
        }

        ast::Expr* right = parse_expr(prec + 1);
        left = make_binary(left, right, op.type);
    }

    return left;
}

ast::Expr* Parser::parse_prefix() {
    if (match(TOKEN_NUMBER)) {
        return make_expr(ctx, ast::IntExpr{ (int)previous.number }, previous.loc);
    }

    if (match(TOKEN_STRING)) {
        return make_expr(ctx, ast::StringExpr{ std::string(previous.text) }, previous.loc);
    }

    if (match(TOKEN_IDENT)) {
        ast::Expr* expr =
            make_expr(ctx,
                ast::VarExpr{
                    std::string(previous.text)
                },
                previous.loc
            );

        while (match(TOKEN_COLON_COLON)) {
            Token name = expect(TOKEN_IDENT, "Expected identifier after ::");

            ast::Expr* rhs = make_expr(ctx, ast::VarExpr{
                        std::string(name.text)
                    },
                    name.loc
                );

            expr = make_expr(ctx,
                ast::NamespaceExpr{
                    expr,
                    rhs
                },
                expr->loc
            );
        }

        return expr;
    }

    if (match(TOKEN_LPAREN)) {
        ast::Expr* e = parse_expr(0);
        expect(TOKEN_RPAREN, "Expected ')'");
        return e;
    }

    error(current.loc, "Unexpected token");
    if (!check(TOKEN_EOF)) {
        advance();
    }
    return nullptr;
}

ast::Expr* Parser::parse_postfix(ast::Expr* left) {
    while (true) {
        if (match(TOKEN_LPAREN)) {
            std::vector<ast::Expr*> args;

            if (!check(TOKEN_RPAREN)) {
                do {
                    args.push_back(parse_expr(0));
                } while (match(TOKEN_COMMA));
            }

            expect(TOKEN_RPAREN, "Expected ')'");

            left = make_expr(ctx, ast::CallExpr{ left, args }, left ? left->loc : SourceLocation{});
            continue;
        }

        if (match(TOKEN_DOT)) {
            Token field = expect(TOKEN_IDENT, "Expected field name after '.'");

            left = make_expr(ctx, ast::FieldExpr{
                left,
                std::string(field.text)
            }, left ? left->loc : field.loc);

            continue;
        }

        break;
    }

    return left;
}

ast::Expr* Parser::make_binary(ast::Expr* left, ast::Expr* right, TokenType op) {
    SourceLocation loc = left ? left->loc : SourceLocation{};
    return make_expr(ctx, ast::BinaryExpr{
        left,
        right,
        get_op_from_token(op)
    }, loc);
}

const ast::Type* Parser::parse_type(bool allow_implicit_void) {
    
    if (match(TOKEN_VOID)) {
        return ctx.types.get_void();
    }

    if (match(TOKEN_INT)) {
        return ctx.types.get_int();
    }

    if (match(TOKEN_STR_TYPE)) {
        return ctx.types.get_string();
    }

    if (match(TOKEN_IDENT)) {
        return ctx.types.get_struct(std::string(previous.text));
    }

    if (allow_implicit_void) {
        return ctx.types.get_void();
    }

    error(current.loc, "Expected type");
    return nullptr;
}
} // namespace quark::ps