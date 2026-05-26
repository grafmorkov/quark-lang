#pragma once

#include <variant>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <optional>

#include "utils/logger.h"

namespace quark::ast {

    struct Expr;
    struct Stmt;
    struct Block;
    struct Attribute;

    // Types

    enum class TypeKind {
        Void,
        Bool,

        I8,
        I16,
        I32,
        I64,

        U8,
        U16,
        U32,
        U64,

        F32,
        F64,

        String,

        Struct,
        Pointer,

        Count
    };

    struct Type {
        TypeKind kind;
        std::string struct_name; // struct only
        const Type* pointed; // ptr only
    };

    // Expressions

    struct IntExpr {
        int value;
    };

    struct FloatExpr {
        double value;
    };

    struct StringExpr {
        std::string value;
    };

    struct VarExpr {
        std::string name;
    };

    enum class BinaryOp {
        Add,
        Sub,
        Mul,
        Div,
        Eq,
        Neq,
        Lt,
        Lte,
        Gt,
        Gte
    };

    struct BinaryExpr {
        Expr* lhs;
        Expr* rhs;
        BinaryOp op;
    };

    struct AssignExpr {
        Expr* target;
        Expr* value;
    };

    struct CallExpr {
        Expr* callee;
        std::vector<Expr*> args;
    };

    struct FieldExpr {
        Expr* base;
        std::string field;
    };

    struct NamespaceExpr {
        Expr* left;
        Expr* right;
    };
    enum class CastKind {
        ValueCast,   // as
        Bitcast      // as!
    };
    struct CastExpr {
        Expr* value;
        const Type* target;
        CastKind kind;
    };

    using ExprKind = std::variant<
        IntExpr,
        FloatExpr,
        StringExpr,
        VarExpr,
        BinaryExpr,
        AssignExpr,
        CallExpr,
        FieldExpr,
        NamespaceExpr,
        CastExpr
    >;

    struct Expr {
        ExprKind kind;
        SourceLocation loc;
        const Type* resolved_type = nullptr;
    };

    // Statements

    struct ExprStmt {
        Expr* expr;
    };

    struct ReturnStmt {
        Expr* value;
    };

    struct IfStmt {
        Expr* condition;
        Block* then_block;
        Block* else_block;
    };

    struct WhileStmt {
        Expr* condition;
        Block* body;
    };

    struct FuncArg {
        std::string name;
        const Type* type;
        bool is_mut;
    };

    struct StructField {
        std::string name;
        const Type* type;
        bool is_mut = false;
        Expr* default_value = nullptr;
        std::vector<Attribute> attributes;
    };

    struct VarDecl {
        std::string name;
        const Type* type = nullptr;
        Expr* value = nullptr;
        bool is_mut = false;
        std::vector<Attribute> attributes;
    };

    struct StructDecl {
        std::string name;
        std::vector<StructField> fields;
        std::vector<Attribute> attributes;
    };

    struct FuncStmt {
        std::string name;
        std::vector<FuncArg> args;
        const Type* return_type;

        bool is_extern;
        bool is_forward;
        bool is_entry;
        bool has_body;

        Block* body;
    };
    struct Attribute { 
        std::string name; // TODO: Checks in semantic 
    };
    struct NamespaceStmt { 
        std::string name; 
        Block* body; 
    };
    struct LoadStmt {
        std::string module;
    };

    using StmtKind = std::variant<
        ExprStmt,
        ReturnStmt,
        IfStmt,
        WhileStmt,
        VarDecl,
        StructDecl,
        FuncStmt,
        NamespaceStmt,
        LoadStmt
    >;

    struct Stmt {
        StmtKind kind;
        SourceLocation loc;
    };

    struct Block {
        std::vector<Stmt*> stmts;
    };

}
