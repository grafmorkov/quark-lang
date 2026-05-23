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
        Int,
        String,
        Void,
        Struct
    };

    struct Type {
        TypeKind kind;
        std::string struct_name;
    };

    // Expressions

    struct IntExpr {
        int value;
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
        std::string ns;
        std::string name;
    };

    using ExprKind = std::variant<
        IntExpr,
        StringExpr,
        VarExpr,
        BinaryExpr,
        AssignExpr,
        CallExpr,
        FieldExpr,
        NamespaceExpr
    >;

    struct Expr {
        ExprKind kind;
        SourceLocation loc;
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
