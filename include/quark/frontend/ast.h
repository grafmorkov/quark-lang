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
        int value;
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

    struct TypeExpr {
        const Type* type;
    };

    struct IndexExpr {
        Expr* base;
        Expr* index;
    };

    using ExprKind = std::variant<
        IntExpr,
        BoolExpr,
        FloatExpr,
        StringExpr,
        CharExpr,
        VarExpr,
        BinaryExpr,
        AssignExpr,
        CallExpr,
        FieldExpr,
        NamespaceExpr,
        CastExpr,
        TypeExpr,
        IndexExpr
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
	std::vector<std::string> type_params;
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
    struct RegionStmt{
        std::string name;
        Block* body;
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
        RegionStmt,
        ModuleDecl,
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
