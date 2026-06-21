#include "quark/frontend/parser.h"
#include "quark/frontend/ast.h"
#include "quark/frontend/token.h"
#include "utils/logger.h"

#include <utility>

using namespace utils::logger;

namespace quark::ps {

namespace {

bool is_type_token(TokenType t) {
    switch (t) {
        case TOKEN_VOID: case TOKEN_BOOL:
        case TOKEN_I8: case TOKEN_I16: case TOKEN_I32: case TOKEN_I64:
        case TOKEN_U8: case TOKEN_U16: case TOKEN_U32: case TOKEN_U64:
        case TOKEN_F32: case TOKEN_F64:
        case TOKEN_STR_TYPE:
        case TOKEN_CHAR_TYPE:
        case TOKEN_STAR:
            return true;
        default:
            return false;
    }
}

std::string process_escapes(const std::string& raw, SourceLocation loc) {
    std::string result;
    result.reserve(raw.size());

    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\') {
            if (i + 1 >= raw.size()) {
                error(loc, "Invalid escape sequence at end of string");
                result += '\\';
                continue;
            }
            char c = raw[++i]; // consume the escape char
            switch (c) {
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"':  result += '"';  break;
                case '\'': result += '\''; break;
                case '0':  result += '\0'; break;
                default:
                    error(loc, "Invalid escape sequence: \\" + std::string(1, c));
                    result += c;
                    break;
            }
        } else {
            result += raw[i];
        }
    }

    return result;
}

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
        case TOKEN_AMP:    return ast::BinaryOp::BitAnd;
        case TOKEN_PIPE:   return ast::BinaryOp::BitOr;
        case TOKEN_AMP_AMP: return ast::BinaryOp::LogicAnd;
        case TOKEN_PIPE_PIPE: return ast::BinaryOp::LogicOr;
        default:           return ast::BinaryOp::Add;
    }
}

int get_precedence(TokenType t) {
    switch (t) {
        case TOKEN_EQ:
        case TOKEN_PLUS_EQ:
        case TOKEN_MINUS_EQ:
        case TOKEN_STAR_EQ:
        case TOKEN_SLASH_EQ:
        case TOKEN_AMP_EQ:
        case TOKEN_PIPE_EQ:
            return 1;

        case TOKEN_PIPE_PIPE:
            return 2;

        case TOKEN_AMP_AMP:
            return 3;

        case TOKEN_PIPE:
            return 4;

        case TOKEN_AMP:
            return 5;

        case TOKEN_EQEQ:
        case TOKEN_NEQ:
            return 6;

        case TOKEN_LT:
        case TOKEN_LTE:
        case TOKEN_GT:
        case TOKEN_GTE:
            return 7;

        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return 8;

        case TOKEN_STAR:
        case TOKEN_SLASH:
            return 9;

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
    std::vector<ast::Attribute> attrs;
    if (check(TOKEN_AT)) {
        attrs = parse_attributes();
    }

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
        auto func = parse_func(true);
        func.attributes = std::move(attrs);
        return ast::Stmt{ std::move(func) };
    }

    if (match(TOKEN_FUNC)) {
        if (match(TOKEN_OPERATOR)) {
            auto func = parser_operator_func();
            func.attributes = std::move(attrs);
            return ast::Stmt{ std::move(func) };
        }
        auto func = parse_func(false);
        func.attributes = std::move(attrs);
        return ast::Stmt{ std::move(func) };
    }

    if (match(TOKEN_STRUCT)) {
        auto decl = parse_struct_decl();
        decl.attributes = std::move(attrs);
        return ast::Stmt{ std::move(decl) };
    }

    if (match(TOKEN_NAMESPACE)) {
        return ast::Stmt{ ast::NamespaceStmt{ parse_namespace_stmt() } };
    }

    if (match(TOKEN_MODULE)) {
        auto decl = parse_module_decl();
        decl.attributes = std::move(attrs);
        return ast::Stmt{ std::move(decl) };
    }

    if (match(TOKEN_LOAD)) {
        return ast::Stmt{ ast::LoadStmt{ parse_load() } };
    }

    if (match(TOKEN_REGION)){
        return ast::Stmt{ast::RegionStmt{ parse_region() } };
    }

    if (is_var_decl()) {
        auto var = parse_var_decl();
        var.attributes = std::move(attrs);
        return ast::Stmt{ std::move(var) };
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

    expect(TOKEN_SEMICOLON, "Expected ';' after declaration");

    return ret;
}

ast::StructDecl Parser::parse_struct_decl() {
    ast::StructDecl ret;

    ret.name = std::string(expect(TOKEN_IDENT, "Expected struct name").text);

    if(match(TOKEN_LT)){
        do{
            Token param = expect(TOKEN_IDENT, "Expected type parameter name");
            ret.type_params.push_back(std::string(param.text));
        } while(match(TOKEN_COMMA));
        expect(TOKEN_GT, "Expected '>' after type parameters");
    }
    expect(TOKEN_LBRACE, "Expected '{' after struct name");

    while (!check(TOKEN_RBRACE) && !check(TOKEN_EOF)) {
        ast::StructField field;

        field.attributes = parse_attributes();
        field.is_mut = match(TOKEN_MUT);

        Token field_name = expect(TOKEN_IDENT, "Expected field name");
        field.name = field_name.text;

        expect(TOKEN_COLON, "Expected ':' after field name");
        field.type = parse_type(false, &ret.type_params);

        field.default_value = nullptr;
        if (match(TOKEN_EQ)) {
            field.default_value = parse_expr(0);
        }

        expect(TOKEN_SEMICOLON, "Expected ';' after field");

        ret.fields.push_back(std::move(field));
    }

    expect(TOKEN_RBRACE, "Expected '}' after struct body");
    expect(TOKEN_SEMICOLON, "Expected ';' after struct body");

    return ret;
}

std::vector<ast::Attribute> Parser::parse_attributes() {
    std::vector<ast::Attribute> attrs;
    while (check(TOKEN_AT)) {
        advance();
        ast::Attribute attr;
        attr.name = expect(TOKEN_IDENT, "Expected attribute name").text;
        if (match(TOKEN_LPAREN)) {
            while (!check(TOKEN_RPAREN) && !check(TOKEN_EOF)){
                attr.args.push_back(parse_expr(0));
                if (!check(TOKEN_RPAREN)) {
                    expect(TOKEN_COMMA, "Expected ',' after attribute argument");
                }
            }
            expect(TOKEN_RPAREN, "Expected ')' after attribute arguments");
        }
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

    if(match(TOKEN_LT)){
        do{
            Token param = expect(TOKEN_IDENT, "Expected type parameter name");
            ret.type_params.push_back(std::string(param.text));
        } while(match(TOKEN_COMMA));
        expect(TOKEN_GT, "Expected '>' after type parameters");
    }

    ret.is_extern = is_extern;
    ret.has_body = false;
    ret.body = nullptr;

    const auto* saved_type_params = current_type_params;
    current_type_params = ret.type_params.empty() ? nullptr : &ret.type_params;

    expect(TOKEN_LPAREN, "Expected '('");
    ret.args = parse_func_args(current_type_params);
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.return_type = parse_type(true, current_type_params);

    if (check(TOKEN_LBRACE)) {
        ret.body = parse_block();
        ret.has_body = true;
    } else {
        expect(TOKEN_SEMICOLON, "Expected ';' after function declaration");
    }

    current_type_params = saved_type_params;

    return ret;
}

ast::FuncStmt Parser::parser_operator_func() {
    ast::FuncStmt ret;

    Token op_token = advance();
    std::string op_text;
    switch (op_token.type) {
        case TOKEN_PLUS:  op_text = "operator+"; break;
        case TOKEN_MINUS: op_text = "operator-"; break;
        case TOKEN_STAR:  op_text = "operator*"; break;
        case TOKEN_SLASH: op_text = "operator/"; break;
        case TOKEN_EQEQ:  op_text = "operator=="; break;
        case TOKEN_NEQ:   op_text = "operator!="; break;
        case TOKEN_LT:    op_text = "operator<";  break;
        case TOKEN_LTE:   op_text = "operator<="; break;
        case TOKEN_GT:    op_text = "operator>";  break;
        case TOKEN_GTE:   op_text = "operator>="; break;
        case TOKEN_NOT:   op_text = "operator!";  break;
        case TOKEN_AMP:    op_text = "operator&";  break;
        case TOKEN_PIPE:   op_text = "operator|";  break;
        case TOKEN_AMP_AMP:  op_text = "operator&&"; break;
        case TOKEN_PIPE_PIPE: op_text = "operator||"; break;
        default:
            error(op_token.loc, "Expected operator token after 'operator' keyword");
            ret.name = "operator_";
            return ret;
    }
    ret.name = op_text;
    ret.is_extern = false;
    ret.has_body = false;
    ret.body = nullptr;

    const auto* saved_type_params = current_type_params;
    current_type_params = nullptr;

    if(match(TOKEN_LT)){
        do{
            Token param = expect(TOKEN_IDENT, "Expected type parameter name");
            ret.type_params.push_back(std::string(param.text));
        } while(match(TOKEN_COMMA));
        expect(TOKEN_GT, "Expected '>' after type parameters");
    }

    current_type_params = ret.type_params.empty() ? nullptr : &ret.type_params;

    expect(TOKEN_LPAREN, "Expected '(' after operator");
    ret.args = parse_func_args(current_type_params);
    expect(TOKEN_RPAREN, "Expected ')'");

    ret.return_type = parse_type(true, current_type_params);

    if (check(TOKEN_LBRACE)) {
        ret.body = parse_block();
        ret.has_body = true;
    } else {
        expect(TOKEN_SEMICOLON, "Expected ';' after operator declaration");
    }

    current_type_params = saved_type_params;

    return ret;
}

ast::RegionStmt Parser::parse_region(){
    ast::RegionStmt ret;

    ret.name = expect(TOKEN_IDENT, "Expected region name").text;
    ret.body = parse_block();

    return ret;
}
std::vector<ast::FuncArg> Parser::parse_func_args(const std::vector<std::string>* type_params) {
    std::vector<ast::FuncArg> args;

    if (check(TOKEN_RPAREN)) {
        return args;
    }

    while (true) {
        bool is_mut = match(TOKEN_MUT);

        Token name = expect(TOKEN_IDENT, "Expected argument name");
        expect(TOKEN_COLON, "Expected ':' after argument name");

        const ast::Type* type = parse_type(true, type_params);

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
ast::ModuleDecl Parser::parse_module_decl() {
    ast::ModuleDecl ret;
    std::string_view raw = expect(TokenType::TOKEN_STRING, "Expected module name").text;
    ret.name = raw;
    expect(TOKEN_SEMICOLON, "Expected ';' after module");
    return ret;
}

ast::LoadStmt Parser::parse_load() {
    ast::LoadStmt ret;

    std::string_view raw = expect(TokenType::TOKEN_STRING, "Expected module").text;

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
                !std::holds_alternative<ast::FieldExpr>(left->kind) &&
                !std::holds_alternative<ast::IndexExpr>(left->kind)) {
                error(op.loc, "Invalid assignment target");
                (void)parse_expr(prec);
                return left;
            }

            ast::Expr* right = parse_expr(prec);
            return make_expr(ctx, ast::AssignExpr{ left, right }, left->loc);
        }

        if (op.type == TOKEN_PLUS_EQ || op.type == TOKEN_MINUS_EQ ||
            op.type == TOKEN_STAR_EQ || op.type == TOKEN_SLASH_EQ ||
            op.type == TOKEN_AMP_EQ || op.type == TOKEN_PIPE_EQ) {
            ast::Expr* rhs = parse_expr(prec);
            TokenType base_op;
            switch (op.type) {
                case TOKEN_PLUS_EQ:  base_op = TOKEN_PLUS;  break;
                case TOKEN_MINUS_EQ: base_op = TOKEN_MINUS; break;
                case TOKEN_STAR_EQ:  base_op = TOKEN_STAR;  break;
                case TOKEN_SLASH_EQ: base_op = TOKEN_SLASH; break;
                case TOKEN_AMP_EQ:   base_op = TOKEN_AMP;   break;
                case TOKEN_PIPE_EQ:  base_op = TOKEN_PIPE;  break;
                default:             base_op = TOKEN_PLUS;  break;
            }
            auto* bin = make_binary(left, rhs, base_op);
            return make_expr(ctx, ast::AssignExpr{ left, bin }, left->loc);
        }

        ast::Expr* right = parse_expr(prec + 1);
        left = make_binary(left, right, op.type);
    }

    return left;
}

ast::Expr* Parser::parse_prefix() {
    if (match(TOKEN_NUMBER)) {
        std::string_view text = previous.text;
        bool is_float = text.find('.') != std::string_view::npos ||
                        text.find('e') != std::string_view::npos ||
                        text.find('E') != std::string_view::npos;
        if (is_float) {
            return make_expr(ctx, ast::FloatExpr{ previous.number }, previous.loc);
        }
        return make_expr(ctx, ast::IntExpr{ static_cast<int>(previous.number) }, previous.loc);
    }

    if (match(TOKEN_STRING)) {
        return make_expr(ctx, ast::StringExpr{ process_escapes(std::string(previous.text), previous.loc) }, previous.loc);
    }

    if (match(TOKEN_CHAR_LITERAL)) {
        return make_expr(ctx, ast::CharExpr{ previous.char_val }, previous.loc);
    }

    if (match(TOKEN_TRUE)) {
        return make_expr(ctx, ast::BoolExpr{ true }, previous.loc);
    }

    if (match(TOKEN_FALSE)) {
        return make_expr(ctx, ast::BoolExpr{ false }, previous.loc);
    }
    if (match(TOKEN_NOT)){
        auto* operand = parse_expr(10);
        return make_expr(ctx, UnaryExpr{operand, ast::UnaryOp::Not}, previous.loc);
    }
    if (match(TOKEN_MINUS)){
        auto* operand = parse_expr(10);
        return make_expr(ctx, UnaryExpr{operand, ast::UnaryOp::Neg}, previous.loc);
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

    if (is_type_token(current.type)) {
        const ast::Type* type = parse_type(false);
        if (type) {
            return make_expr(ctx, ast::TypeExpr{ type }, previous.loc);
        }
    }

    error(current.loc, "Unexpected token");
    if (!check(TOKEN_EOF)) {
        advance();
    }
    return nullptr;
}

ast::Expr* Parser::parse_postfix(ast::Expr* left) {
    while (true) {
        if (check(TOKEN_LT)) {
            auto n1 = peek(0);
            if (n1.type == TOKEN_IDENT || is_type_token(n1.type)) {
                auto n2 = peek(1);
                if (n2.type == TOKEN_GT || n2.type == TOKEN_COMMA) {
                    advance(); // consume <
                    std::vector<const ast::Type*> type_args;
                    do {
                        type_args.push_back(parse_type(false, current_type_params));
                    } while (match(TOKEN_COMMA));
                    expect(TOKEN_GT, "Expected '>' after generic type arguments");
                    expect(TOKEN_LPAREN, "Expected '(' after generic function name");
                    std::vector<ast::Expr*> args;
                    if (!check(TOKEN_RPAREN)) {
                        do {
                            args.push_back(parse_expr(0));
                        } while (match(TOKEN_COMMA));
                    }
                    expect(TOKEN_RPAREN, "Expected ')'");
                    left = make_expr(ctx, ast::CallExpr{ left, args, type_args }, left->loc);
                    continue;
                }
            }
        }

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

        if (match(TOKEN_LBRACKET)) {
            ast::Expr* index = parse_expr(0);
            expect(TOKEN_RBRACKET, "Expected ']'");

            left = make_expr(ctx, ast::IndexExpr{ left, index }, left ? left->loc : SourceLocation{});
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
        if (match(TOKEN_AS)) {
            ast::CastKind kind = ast::CastKind::ValueCast;

            if (match(TOKEN_NOT)) { // !
                kind = ast::CastKind::Bitcast;
            }

            const ast::Type* target = parse_type();

            if (!target) {
                error(current.loc, "Expected type after cast");
                return left;
            }

            left = make_cast(left, target, kind);
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
ast::Expr* Parser::make_cast(ast::Expr* value, const ast::Type* target, ast::CastKind kind) {
    SourceLocation loc = value ? value->loc : SourceLocation{};

    return make_expr(ctx,
        ast::CastExpr{
            value,
            target,
            kind
        },
        loc
    );
}

const ast::Type* Parser::parse_type(bool allow_implicit_void, const std::vector<std::string>* type_params) {

    if (match(TOKEN_STAR)) {
        const ast::Type* base = parse_type();
        if (!base) return nullptr;
        return ctx.types.get_pointer(base);
    }

    if (match(TOKEN_VOID))    return ctx.types.get_builtin(TypeKind::Void);
    if (match(TOKEN_BOOL))    return ctx.types.get_builtin(TypeKind::Bool);

    if (match(TOKEN_I8))      return ctx.types.get_builtin(TypeKind::I8);
    if (match(TOKEN_I16))     return ctx.types.get_builtin(TypeKind::I16);
    if (match(TOKEN_I32))     return ctx.types.get_builtin(TypeKind::I32);
    if (match(TOKEN_I64))     return ctx.types.get_builtin(TypeKind::I64);

    if (match(TOKEN_U8))      return ctx.types.get_builtin(TypeKind::U8);
    if (match(TOKEN_U16))     return ctx.types.get_builtin(TypeKind::U16);
    if (match(TOKEN_U32))     return ctx.types.get_builtin(TypeKind::U32);
    if (match(TOKEN_U64))     return ctx.types.get_builtin(TypeKind::U64);

    if (match(TOKEN_F32))     return ctx.types.get_builtin(TypeKind::F32);
    if (match(TOKEN_F64))     return ctx.types.get_builtin(TypeKind::F64);

    if (match(TOKEN_STR_TYPE)) return ctx.types.get_builtin(TypeKind::String);
    if (match(TOKEN_CHAR_TYPE)) return ctx.types.get_builtin(TypeKind::U8);

    if (match(TOKEN_IDENT)) {
        std::string name(previous.text);

        while (match(TOKEN_COLON_COLON)) {
            name += "::";
            name += expect(TOKEN_IDENT, "Expected identifier after ::").text;
        }

        if (type_params) {
            for (const auto& p : *type_params) {
                if (name == p) {
                    return ctx.types.get_generic_param(std::string(p));
                }
            }
        }

        if (match(TOKEN_LT)) {
            std::vector<const ast::Type*> args;
            do {
                args.push_back(parse_type(false, type_params));
            } while (match(TOKEN_COMMA));
            expect(TOKEN_GT, "Expected '>' after type arguments");
            return ctx.types.get_deferred_generic(name, args);
        }

        return ctx.types.get_struct(name);
    }

    if (allow_implicit_void) {
        return ctx.types.get_builtin(TypeKind::Void);
    }

    error(current.loc, "Expected type");
    return nullptr;
}
} // namespace quark::ps
