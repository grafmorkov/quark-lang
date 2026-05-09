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

    struct ASTVisitor;

    struct Type {
        enum Kind {
            Int,
            Float,
            String,
            Void,
            Struct,
        } kind;
        std::string struct_name; // if struct
    };
    
    struct IntLit {
        int value;
    };

    struct StringLit {
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
        NotEq,
        Lt,
        Lte,
        Gt,
        Gte
        // ...
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
    struct NoneExpr{};
    struct CallExpr {
        Expr* callee;
        std::vector<Expr*> args;
    };

    struct BlockExpr {
        std::vector<Stmt*> statements;
    };
    struct Attribute {
        std::string name; // TODO: Checks in semantic
    }; 
    struct FieldAccessExpr {
        Expr* base;
        std::string field;
    };

    using ExprKind = std::variant<
        IntLit,
        StringLit,
        VarExpr,
        BinaryExpr,
        CallExpr,
        NoneExpr,
        FieldAccessExpr,
        AssignExpr
    >;

    struct Expr {
        ExprKind kind;
        SourceLocation loc{};

        Expr() = default;
        Expr(const Expr&) = delete;
        Expr& operator=(const Expr&) = delete;

        Expr(Expr&&) = default;
        Expr& operator=(Expr&&) = default;
    };

    struct ExprStmt {
        Expr* expr;
    };
    struct IfStmt {
        Expr* condition;
        BlockExpr* thenBranch;
        BlockExpr* elseBranch;
    };

    struct WhileStmt {
        Expr* condition;
        BlockExpr* body;
    };

    struct ReturnStmt {
        Expr* value;
    };
    struct FuncArg {
        std::string name;
        const Type* type;
        bool is_mut;
    };
    struct FuncStmt {
        std::string name;
        BlockExpr* body;
        std::vector<FuncArg> args;
        const Type* return_t;
    };
    struct Decl {
        virtual ~Decl() = default;
        std::string name;
        std::vector<Attribute> attributes;
    };

    struct VarDecl : Decl {
        const Type* type;
        Expr* value;
        bool is_mut;
    };
    struct FieldDecl : Decl {
        const Type* type;
        Expr* default_value;
        bool is_mut;
    };

    struct StructDecl : Decl {
        std::vector<FieldDecl*> fields;
        bool is_mut;
    };
    struct StructLayout {
        std::vector<const ast::Type*> field_types;
        std::unordered_map<std::string, int> field_index;
    };
    using StmtKind = std::variant<
        VarDecl,
        StructDecl,
        FieldDecl,
        IfStmt,
        WhileStmt,
        ReturnStmt,
        FuncStmt,
        ExprStmt
    >;

    struct Stmt {
        StmtKind kind;
        SourceLocation loc{};

        Stmt() = default;
        explicit Stmt(StmtKind k) : kind(std::move(k)) {}
    };
}
