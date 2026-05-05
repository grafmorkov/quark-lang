#include "quark/parser.h"
#include "utils/logger.h"

using namespace utils::logger;

namespace quark::ps {

namespace {

ast::BinaryOp get_op_from_token(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:  return ast::BinaryOp::Add;
        case TOKEN_MINUS: return ast::BinaryOp::Sub;
        case TOKEN_STAR:  return ast::BinaryOp::Mul;
        case TOKEN_SLASH: return ast::BinaryOp::Div;
        case TOKEN_EQEQ:  return ast::BinaryOp::Eq;
        case TOKEN_NEQ:   return ast::BinaryOp::NotEq;
        case TOKEN_LT: return ast::BinaryOp::Lt;
        case TOKEN_LTE: return ast::BinaryOp::Lte;
        case TOKEN_GT: return ast::BinaryOp::Gt;
        case TOKEN_GTE: return ast::BinaryOp::Gte;
        default:
            return ast::BinaryOp::Add;
    }
}

int get_precedence(TokenType t) {
    switch (t) {
        case TOKEN_EQ: return 1;
        case TOKEN_EQEQ:
        case TOKEN_NEQ: return 2;
        case TOKEN_PLUS:
        case TOKEN_MINUS: return 3;
        case TOKEN_STAR:
        case TOKEN_SLASH: return 4;
        default: return -1;
    }
}

} // namespace

// Constructor

Parser::Parser(lx::Lexer& lex, CompilerContext& ctx_)
    : lexer(lex), ctx(ctx_) {
    current = lexer.next_token();
}

// Parse Entry

std::vector<std::unique_ptr<ast::Stmt>> Parser::parse() {
    std::vector<std::unique_ptr<ast::Stmt>> out;

    while (!check(TOKEN_EOF)) {
        out.push_back(std::make_unique<ast::Stmt>(parse_statement()));
    }

    return out;
}

// Token Utils

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
    while (buffer.size() <= n) {
        buffer.push_back(lexer.next_token());
    }
    return buffer[n];
}
// Stmts

ast::Stmt Parser::parse_statement() {
    if (match(TOKEN_RETURN)) return ast::Stmt{ parse_return() };
    if (match(TOKEN_IF))     return ast::Stmt{ parse_if() };
    if (match(TOKEN_WHILE))  return ast::Stmt{ parse_while() };
    if (match(TOKEN_FUNC))   return ast::Stmt{ parse_func() };
    if (match(TOKEN_STRUCT)) return ast::Stmt{ parse_struct_decl() };
    if (is_var_decl()) return ast::Stmt{ parse_var_decl() };

    ast::Expr expr = parse_expr(0);
    expect(TOKEN_SEMICOLON, "Expected ';' after expression");

    return ast::Stmt{
        ast::ExprStmt{
            std::make_unique<ast::Expr>(std::move(expr))
        }
    };
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

    Token name = expect(TOKEN_IDENT, "Expected the name of the variable");
    ret.name = name.text;

    ret.type = nullptr;
    ret.value.reset();

    if (match(TOKEN_COLON)) {
        ret.type = parse_type();

        if (match(TOKEN_EQ)) {
            ret.value = std::make_unique<ast::Expr>(parse_expr(0));
        }
    }

    ret.attributes = parse_attributes();
    expect(TOKEN_SEMICOLON, "Expected ';' after declaration");
    return ret;
}
ast::StructDecl Parser::parse_struct_decl() {
    ast::StructDecl ret;


    Token name = expect(TOKEN_IDENT, "Expected the name of the struct");
    ret.name = name.text;

    expect(TOKEN_LBRACE, "Expected '{' after struct name");

    while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {

        auto field = std::make_unique<ast::FieldDecl>();

        field->is_mut = match(TOKEN_MUT);

        Token field_name = expect(TOKEN_IDENT, "Expected field name");
        field->name = field_name.text;

        expect(TOKEN_COLON, "Expected ':' after field name");
        field->type = parse_type();

        field->default_value = nullptr;

        if (match(TOKEN_EQ)) {
            field->default_value =
                std::make_unique<ast::Expr>(parse_expr(0));
        }

        field->attributes = parse_attributes();

        expect(TOKEN_SEMICOLON, "Expected ';' after field");

        ret.fields.push_back(std::move(field));
    }

    expect(TOKEN_RBRACE, "Expected '}' after struct body");
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
    ret.condition = std::make_unique<ast::Expr>(parse_expr(0));
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.thenBranch = std::make_unique<ast::BlockExpr>(parse_block());

    if (match(TOKEN_ELSE)) {
        ret.elseBranch = std::make_unique<ast::BlockExpr>(parse_block());
    }

    return ret;
}

ast::WhileStmt Parser::parse_while() {
    ast::WhileStmt ret;

    expect(TOKEN_LPAREN, "Expected '('");
    ret.condition = std::make_unique<ast::Expr>(parse_expr(0));
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.body = std::make_unique<ast::BlockExpr>(parse_block());

    return ret;
}

ast::ReturnStmt Parser::parse_return() {
    ast::ReturnStmt ret;

    if (!check(TOKEN_SEMICOLON)) {
        ret.value = std::make_unique<ast::Expr>(parse_expr(0));
    }

    expect(TOKEN_SEMICOLON, "Expected ';'");

    return ret;
}

ast::FuncStmt Parser::parse_func() {
    ast::FuncStmt ret;

    Token name = expect(TOKEN_IDENT, "Expected function name");
    ret.name = name.text;

    expect(TOKEN_LPAREN, "Expected '('");
    ret.args = parse_func_args();
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.return_t = parse_type();
    ret.body = std::make_unique<ast::BlockExpr>(parse_block());

    return ret;
}

std::vector<ast::FuncArg> Parser::parse_func_args() {
    std::vector<ast::FuncArg> args;

    if (check(TOKEN_RPAREN)) return args;

    while (true) {
        bool is_mut = match(TOKEN_MUT);

        Token name = expect(TOKEN_IDENT, "Expected arg name");
        expect(TOKEN_COLON, "Expected ':' after argument name");

        const ast::Type* type = parse_type();

        args.push_back({
            std::string(name.text),
            type,
            is_mut
        });

        if (!match(TOKEN_COMMA)) break;
    }

    return args;
}
ast::BlockExpr Parser::parse_block() {
    expect(TOKEN_LBRACE, "Expected '{'");

    ast::BlockExpr block;

    while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
        block.statements.push_back(
            std::make_unique<ast::Stmt>(parse_statement())
        );
    }

    expect(TOKEN_RBRACE, "Expected '}'");

    return block;
}

// Expressions (Pratt)

ast::Expr Parser::parse_expr(int min_prec) {
    ast::Expr left = parse_prefix();

    if (std::holds_alternative<ast::NoneExpr>(left.kind)) {
        return left;
    }
    left = parse_postfix(std::move(left));

    while (true) {
        int prec = get_precedence(current.type);
        if (prec < min_prec) break;

        Token op = advance();

        if (op.type == TOKEN_EQ) {
            if (!std::holds_alternative<ast::VarExpr>(left.kind) &&
                !std::holds_alternative<ast::FieldAccessExpr>(left.kind)) {
                error(op.loc, "Invalid assignment target");
                return left;
            }

            ast::Expr right = parse_expr(0);

            ast::Expr e;
            e.kind = ast::AssignExpr{
                std::make_unique<ast::Expr>(std::move(left)),
                std::make_unique<ast::Expr>(std::move(right))
            };

            return e;
        }

        ast::Expr right = parse_expr(prec + 1);


        left = make_binary(std::move(left), std::move(right), op.type);
    }

    return left;
}

ast::Expr Parser::parse_prefix() {
    if (match(TOKEN_NUMBER)) {
        ast::Expr e;
        e.kind = ast::IntLit{ (int)previous.number };
        return e;
    }
    if (match(TOKEN_STRING)) {
        ast::Expr e;
        e.kind = ast::StringLit{ std::string(previous.text) };
        return e;
    }

    if (match(TOKEN_IDENT)) {
        ast::Expr e;
        e.kind = ast::VarExpr{ std::string(previous.text) };
        return e;
    }

    if (match(TOKEN_LPAREN)) {
        ast::Expr e = parse_expr(0);
        expect(TOKEN_RPAREN, "Expected ')'");
        return e;
    }

    error(current.loc, "Unexpected token");
    if (!check(TOKEN_EOF)) {
        advance();
    }
    return ast::Expr{ ast::NoneExpr{} };
}

ast::Expr Parser::parse_postfix(ast::Expr left) {
    while (true) {
        // a(b, c)
        if (match(TOKEN_LPAREN)) {
            std::vector<std::unique_ptr<ast::Expr>> args;

            if (!check(TOKEN_RPAREN)) {
                do {
                    args.push_back(std::make_unique<ast::Expr>(parse_expr(0)));
                } while (match(TOKEN_COMMA));
            }

            expect(TOKEN_RPAREN, "Expected ')'");

            ast::Expr e;
            e.kind = ast::CallExpr{
                std::make_unique<ast::Expr>(std::move(left)),
                std::move(args)
            };

            left = std::move(e);
            continue;
        }

        // a.b
        if (match(TOKEN_DOT)) {
            Token field = expect(TOKEN_IDENT, "Expected field name after '.'");

            ast::Expr e;
            e.kind = ast::FieldAccessExpr{
                std::make_unique<ast::Expr>(std::move(left)),
                std::string(field.text)
            };

            left = std::move(e);
            continue;
        }

        break;
    }

    return left;
}

// Helpers

ast::Expr Parser::make_binary(ast::Expr left, ast::Expr right, TokenType op) {
    ast::Expr e;
    e.loc = left.loc;

    e.kind = ast::BinaryExpr{
        std::make_unique<ast::Expr>(std::move(left)),
        std::make_unique<ast::Expr>(std::move(right)),
        get_op_from_token(op)
    };

    return e;
}

// Types

const ast::Type* Parser::parse_type() {
    if (match(TOKEN_INT)) return ctx.types.get_int();
    if (match(TOKEN_STRING)) return ctx.types.get_string();
    if (match(TOKEN_VOID)) return ctx.types.get_void();

    if (match(TOKEN_IDENT)) {
        return ctx.types.get_struct(std::string(previous.text));
    }

    error(current.loc, "Expected type");
    return ctx.types.get_int();
}

const ast::Type* Parser::get_type_from_token(Token t) {
    switch (t.type) {
        case TOKEN_INT: return ctx.types.get_int();
        case TOKEN_STRING: return ctx.types.get_string();
        case TOKEN_VOID: return ctx.types.get_void();
        default:
            error(t.loc, "Unknown type");
            return ctx.types.get_int();
    }
}

} // namespace quark::ps
