#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <array>
#include <optional>
#include "quark/frontend/ast.h"

using namespace quark::ast;

namespace quark::types {

struct GenericStructDef {
    std::vector<std::string> params;
    std::vector<ast::StructField> fields;
};

class TypeContext {
public:
    const Type* get_builtin(TypeKind kind);
    const Type* get_struct(const std::string& name);
    const Type* get_pointer(const Type* base);
    const Type* get_generic_param(const std::string& name);
    const Type* get_generic_instantiation(const std::string& name, const std::vector<const Type*>& args);
    const Type* get_deferred_generic(const std::string& name, const std::vector<const Type*>& args);
    bool is_mangled_name(const std::string& mangled, std::string& out_base) const;
    bool try_instantiate(const std::string& mangled, const std::vector<const Type*>& type_args);

    void register_struct(const std::string& name, 
        const std::vector<std::pair<std::string, const Type*>>& fields);

    void register_generic_struct(const std::string& name, const GenericStructDef& def);

    const std::vector<std::pair<std::string, const Type*>>* get_struct_fields(const std::string& name) const;

    int get_field_index(const std::string& struct_name, const std::string& field) const;

    const Type* get_field_type(const std::string& struct_name, const std::string& field) const;

    TypeContext();

private:
    std::array<Type, (size_t)TypeKind::Count> builtin_types;
    std::unordered_map<std::string, Type> struct_types;
    std::unordered_map<const Type*, Type> pointer_cache;
    std::unordered_map<std::string, Type> generic_param_types;

    std::unordered_map<
        std::string,
        std::vector<std::pair<std::string, const Type*>>
    > structs;

    std::unordered_map<std::string, GenericStructDef> generic_defs;
    std::unordered_map<std::string, std::string> mangled_to_base;
};

}