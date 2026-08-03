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
    struct ElseIfStmt;

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
        Reference,

        Generic,

        Count
    };

    struct Type {
        TypeKind kind;
        std::string struct_name; // struct only
        const Type* pointed; // ptr only
        std::vector<const Type*> type_args;
    };

    // Expressions

    struct IntExpr {
        int64_t value;
    };

    struct BoolExpr {
        bool value;
    };

    struct FloatExpr {
        double value;
    };

    struct StringExpr {
        std::string value;
    };

    struct CharExpr {
        uint8_t value;
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
        Gte,
        BitAnd,
        BitOr,
        LogicAnd,
        LogicOr
    };

    struct BinaryExpr {
        Expr* lhs;
        Expr* rhs;
        BinaryOp op;
    };
    enum class UnaryOp { Neg, Not, AddrOf };

    struct UnaryExpr{
        Expr* operand;
        UnaryOp op;
    };

    struct AssignExpr {
        Expr* target;
        Expr* value;
    };

    struct CallExpr {
        Expr* callee;
        std::vector<Expr*> args;
        std::vector<const Type*> type_args;
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

    struct TypeExpr {
        const Type* type;
    };

    struct IndexExpr {
        Expr* base;
        Expr* index;
    };

    struct StructInitExpr {
        Expr* type_ref;                          // VarExpr, NamespaceExpr, TypeExpr, or nullptr
        std::vector<const Type*> type_args;      // Generic type arguments (empty if non-generic)
        std::vector<Expr*> args;                 // Positional field values
    };

    struct SizeofExpr {
        const Type* type;
    };

    using ExprKind = std::variant<
        IntExpr,
        BoolExpr,
        FloatExpr,
        StringExpr,
        CharExpr,
        VarExpr,
        BinaryExpr,
        UnaryExpr,
        AssignExpr,
        CallExpr,
        FieldExpr,
        NamespaceExpr,
        CastExpr,
        TypeExpr,
        IndexExpr,
        StructInitExpr,
        SizeofExpr
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

    struct BreakStmt {};

    struct ContinueStmt {};

    struct IfStmt {
        Expr* condition;
        Block* then_block;
        ElseIfStmt* else_if;  // first else-if branch in the chain (nullptr if none)
        Block* else_block;
    };

    struct ElseIfStmt {
        Expr* condition;
        Block* then_block;
        Block* else_block;  // plain else block (nullptr if the chain continues)
        ElseIfStmt* next;   // next else-if branch in the chain (nullptr if none)
    };

    struct WhileStmt {
        Expr* condition;
        Block* body;
    };

    struct CaseStmt {
        std::vector<Expr*> values;                            // one or more constant case values sharing one body
        Block* body;
        std::vector<std::optional<int64_t>> const_values;     // constant values, filled by semantic analysis
    };

    struct SwitchStmt {
        Expr* condition;
        std::vector<CaseStmt> cases;
        Block* default_block;   // nullptr if no default
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
	    std::vector<std::string> type_params;
    };

    struct FuncStmt {
        std::string name;
        std::vector<FuncArg> args;
        const Type* return_type;
        std::vector<std::string> type_params;

        bool is_extern;
        bool is_forward;
        bool is_entry;
        bool has_body;

        Block* body;
        std::vector<Attribute> attributes;
    };
    struct Attribute {
        std::string name;
        std::vector<Expr*> args;
    };
    struct NamespaceStmt {
        std::string name;
        Block* body;
    };
    struct ModuleDecl {
        std::string name;
        std::vector<Attribute> attributes;
    };
    struct LoadStmt {
        std::string module;
    };
    struct UsingStmt {
        std::vector<std::string> path;
    };
    struct RegionStmt{
        std::string name;
        Block* body;
    };

    struct EnumDecl {
        std::string name;
        std::vector<std::string> variants;
        std::vector<Attribute> attributes;
    };

    using StmtKind = std::variant<
        ExprStmt,
        ReturnStmt,
        BreakStmt,
        ContinueStmt,
        IfStmt,
        WhileStmt,
        SwitchStmt,
        VarDecl,
        StructDecl,
        FuncStmt,
        NamespaceStmt,
        RegionStmt,
        EnumDecl,
        ModuleDecl,
        LoadStmt,
        UsingStmt
    >;

    struct Stmt {
        StmtKind kind;
        SourceLocation loc;
    };

    struct Block {
        std::vector<Stmt*> stmts;
    };

}
