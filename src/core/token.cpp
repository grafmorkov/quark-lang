#include <iostream>

#include "quark/token.h"

namespace quark{

    std::string token_type_to_string(TokenType type) {
        switch (type) {
            // special
            case TOKEN_EOF: return "EOF";
            case TOKEN_ILLEGAL: return "ILLEGAL";

            // identifiers & literals
            case TOKEN_IDENT: return "IDENTIFIER";
            case TOKEN_NUMBER: return "NUMBER";
            case TOKEN_STRING: return "STRING";

            // keywords
            case TOKEN_IF: return "IF";
            case TOKEN_ELSE: return "ELSE";
            case TOKEN_WHILE: return "WHILE";
            case TOKEN_RETURN: return "RETURN";
            case TOKEN_FUNC: return "FUNC";
            case TOKEN_VAR: return "VAR";
            case TOKEN_MUT: return "MUT";
            case TOKEN_STRUCT: return "STRUCT";
            case TOKEN_AT: return "@";
            case TOKEN_DOT: return ".";

            // types
            case TOKEN_VOID: return "VOID";
            case TOKEN_INT: return "INT";
            case TOKEN_FLOAT: return "FLOAT";

            // operators
            case TOKEN_PLUS: return "+";
            case TOKEN_MINUS: return "-";
            case TOKEN_STAR: return "*";
            case TOKEN_SLASH: return "/";
            case TOKEN_NOT: return "!";
            case TOKEN_COLON: return ":";

            case TOKEN_EQ: return "=";
            case TOKEN_EQEQ: return "==";
            case TOKEN_NEQ: return "!=";
            case TOKEN_EQA: return "=>";

            case TOKEN_LT: return "<";
            case TOKEN_LTE: return "<=";
            case TOKEN_GT: return ">";
            case TOKEN_GTE: return ">=";
            case TOKEN_QUESTION_QUESTION: return "??";

            // delimiters
            case TOKEN_LPAREN: return "(";
            case TOKEN_RPAREN: return ")";
            case TOKEN_LBRACE: return "{";
            case TOKEN_RBRACE: return "}";
            case TOKEN_LBRACKET: return "[";
            case TOKEN_RBRACKET: return "]";
            case TOKEN_COMMA: return ",";
            case TOKEN_SEMICOLON: return ";";

            default: return "UNKNOWN";
        }
    }
}