#include <iostream>
#include <vector>
#include <algorithm>
#include <cctype>

#include "quark/frontend/lexer.h"
#include "quark/frontend/token.h"

constexpr unsigned int str_hash(const char* str, int h = 0){
        return !str[h] ? 5381 : (str_hash(str, h + 1) * 33) ^ str[h];
}

namespace quark::lx {

    char Lexer::peek() {
        if (pos >= buffer.size()) return '\0';
        return buffer[pos];
    }
    char Lexer::advance() {
        char c = peek();

        if (c == '\0') return c;

        pos++;

        if (c == '\n') {
            ctx.srcloc.line++;
            ctx.srcloc.column = 1;
        } else {
            ctx.srcloc.column++;
        }

        return c;
    }
    bool Lexer::match(char expected) {
        if (peek() != expected) return false;

        advance();
        return true;
    }
    bool Lexer::is_at_end() {
        return peek() == '\0';
    }
    Token Lexer::next_token() {
        while (std::isspace((unsigned char)peek())) {
            advance();
        }

        start = pos;
        token_line = ctx.srcloc.line;
        token_column = ctx.srcloc.column;

        char c = advance();

        if (std::isdigit((unsigned char)c)) {
            return number();
        } else if (std::isalpha((unsigned char)c) || c == '_') {
            return identifier();
        } else if (c == '"') {
            return string();
        }

        switch (c) {
            case '(': return make_token(TOKEN_LPAREN);
            case ')': return make_token(TOKEN_RPAREN);
            case '{': return make_token(TOKEN_LBRACE);
            case '}': return make_token(TOKEN_RBRACE);
            case '[': return make_token(TOKEN_LBRACKET);
            case ']': return make_token(TOKEN_RBRACKET);
            case ',': return make_token(TOKEN_COMMA);
            case '=': 
                if(match('=')){
                    return make_token(TOKEN_EQEQ);
                }
                else if(match('>')){
                    return make_token(TOKEN_EQA);
                }
                else{
                    return make_token(TOKEN_EQ);
                }

            case '!': return make_token(match('=') ? TOKEN_NEQ : TOKEN_NOT);
            case '<': return make_token(match('=') ? TOKEN_LTE : TOKEN_LT);
            case '>': return make_token(match('=') ? TOKEN_GTE : TOKEN_GT);
            case '+': return make_token(TOKEN_PLUS);
            case '-': return make_token(TOKEN_MINUS);
            case '*': return make_token(TOKEN_STAR);
            case ';': return make_token(TOKEN_SEMICOLON);
            case '@': return make_token(TOKEN_AT);
            case '.': return make_token(TOKEN_DOT);
            case '?': 
                if(match('?')){
                    return make_token(TOKEN_QUESTION_QUESTION);
                }
                return make_token(TOKEN_QUESTION);
            case ':': return make_token(match(':') ? TOKEN_COLON_COLON: TOKEN_COLON);
            case '/':
                if (match('/')) {
                    while (peek() != '\n' && !is_at_end()) advance();
                    return next_token();
                } else if (match('*')) {
                    while (!is_at_end()) {
                        if (peek() == '*' && pos + 1 < buffer.size() && buffer[pos + 1] == '/') {
                            advance();
                            advance();
                            break;
                        }
                        if (peek() == '\n') {
                            ctx.srcloc.line++;
                            ctx.srcloc.column = 1;
                        }
                        advance();
                    }
                    return next_token();
                }
                return make_token(TOKEN_SLASH);
            case '\0': return make_token(TOKEN_EOF);
            default: return make_token(TOKEN_ILLEGAL);
        }
    }
    Token Lexer::make_token(TokenType type) {
        return Token{
            type,
            std::string_view(buffer.data() + start, pos - start),
            {},
            {ctx.srcloc.file, token_line, token_column}
        };
    }
    Token Lexer::make_number() {
        Token tok = make_token(TOKEN_NUMBER);
        tok.number = std::stod(
            std::string(buffer.data() + start, pos - start)
        );
        return tok;
    }
    Token Lexer::number() {
        while (std::isdigit((unsigned char)peek())) {
            advance();
        }

        if (peek() == '.' && pos + 1 < buffer.size() &&
            std::isdigit((unsigned char)buffer[pos + 1])) {
            advance();

            while (std::isdigit((unsigned char)peek())) {
                advance();
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            advance();

            if (peek() == '+' || peek() == '-') {
                advance();
            }

            while (std::isdigit((unsigned char)peek())) {
                advance();
            }
        }

        return make_number();
    }
    Token Lexer::identifier() {
        while (std::isalnum((unsigned char)peek()) || peek() == '_') {
            advance();
        }

        std::string text(buffer.data() + start, pos - start);

        switch(str_hash(text.c_str())) {
            case str_hash("if"): return make_token(TOKEN_IF);
            case str_hash("else"): return make_token(TOKEN_ELSE);
            case str_hash("while"):return make_token(TOKEN_WHILE);
            case str_hash("return"):return make_token(TOKEN_RETURN);
            case str_hash("func"): return make_token(TOKEN_FUNC);
            case str_hash("var"): return make_token(TOKEN_VAR);

            case str_hash("mut"): return make_token(TOKEN_MUT);
            case str_hash("struct"): return make_token(TOKEN_STRUCT);
            case str_hash("namespace"): return make_token(TOKEN_NAMESPACE);
            case str_hash("load"): return make_token(TOKEN_LOAD);
            case str_hash("extern"): return make_token(TOKEN_EXTERN);

            // Types
            case str_hash("void"): return make_token(TOKEN_VOID);
            case str_hash("bool"): return make_token(TOKEN_BOOL);
            case str_hash("i8"):   return make_token(TOKEN_I8);
            case str_hash("i16"):  return make_token(TOKEN_I16);
            case str_hash("i32"):  return make_token(TOKEN_I32);
            case str_hash("i64"):  return make_token(TOKEN_I64);
            case str_hash("u8"):   return make_token(TOKEN_U8);
            case str_hash("u16"):  return make_token(TOKEN_U16);
            case str_hash("u32"):  return make_token(TOKEN_U32);
            case str_hash("u64"):  return make_token(TOKEN_U64);
            case str_hash("f32"):  return make_token(TOKEN_F32);
            case str_hash("f64"):  return make_token(TOKEN_F64);
            case str_hash("str"):  return make_token(TOKEN_STR_TYPE);
            case str_hash("as"): return make_token(TOKEN_AS);
            case str_hash("region"): return make_token(TOKEN_REGION);
        }
        return make_token(TOKEN_IDENT);
    }
    Token Lexer::string() {
        while (peek() != '"' && !is_at_end()) {
            if (peek() == '\n') {
                ctx.srcloc.line++;
                ctx.srcloc.column = 1;
            }
            advance();
        }
        if (is_at_end()) {
            return make_token(TOKEN_ILLEGAL);
        }
        advance();
        return Token{
            TOKEN_STRING,
            std::string_view(buffer.data() + start + 1, pos - start - 2),
            {},
            {ctx.srcloc.file, token_line, token_column}
        };
    }
}