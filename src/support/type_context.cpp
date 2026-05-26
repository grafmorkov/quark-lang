#include "quark/support/type_context.h"

namespace quark::types {
    TypeContext::TypeContext() {
        builtin_types[(size_t)TypeKind::Void].kind = TypeKind::Void;
        builtin_types[(size_t)TypeKind::Bool].kind = TypeKind::Bool;

        builtin_types[(size_t)TypeKind::I8].kind  = TypeKind::I8;
        builtin_types[(size_t)TypeKind::I16].kind = TypeKind::I16;
        builtin_types[(size_t)TypeKind::I32].kind = TypeKind::I32;
        builtin_types[(size_t)TypeKind::I64].kind = TypeKind::I64;

        builtin_types[(size_t)TypeKind::U8].kind  = TypeKind::U8;
        builtin_types[(size_t)TypeKind::U16].kind = TypeKind::U16;
        builtin_types[(size_t)TypeKind::U32].kind = TypeKind::U32;
        builtin_types[(size_t)TypeKind::U64].kind = TypeKind::U64;

        builtin_types[(size_t)TypeKind::F32].kind = TypeKind::F32;
        builtin_types[(size_t)TypeKind::F64].kind = TypeKind::F64;

        builtin_types[(size_t)TypeKind::String].kind = TypeKind::String;
    }
    const Type* TypeContext::get_builtin(TypeKind kind) {
        return &builtin_types[(int)kind];
    }

    const Type* TypeContext::get_struct(const std::string& name) {
        auto it = struct_types.find(name);
        if (it != struct_types.end())
            return &it->second;

        Type t;
        t.kind = TypeKind::Struct;
        t.struct_name = name;

        auto [iter, _] = struct_types.emplace(name, std::move(t));
        return &iter->second;
    }

    void TypeContext::register_struct(
        const std::string& name,
        const std::vector<std::pair<std::string, const Type*>>& fields
    ) {
        structs[name] = fields;
    }

    const std::vector<std::pair<std::string, const Type*>>* TypeContext::get_struct_fields(const std::string& name) const{
        auto it = structs.find(name);
        if (it == structs.end())
            return nullptr;
        return &it->second;
    }

    int TypeContext::get_field_index(const std::string& struct_name, const std::string& field) const {
        auto fields = get_struct_fields(struct_name);
        if (!fields) return -1;

        for (int i = 0; i < (int)fields->size(); i++) {
            if ((*fields)[i].first == field)
                return i;
        }
        return -1;
    }

    const Type* TypeContext::get_field_type(const std::string& struct_name, const std::string& field) const {
        auto fields = get_struct_fields(struct_name);
        if (!fields) return nullptr;

        for (auto& [name, type] : *fields) {
            if (name == field)
                return type;
        }
        return nullptr;
    }
    const Type* TypeContext::get_pointer(const Type* base) {
        auto it = pointer_cache.find(base);
        if (it != pointer_cache.end())
            return &it->second;

        Type t;
        t.kind = TypeKind::Pointer;
        t.pointed = base;

        auto [iter, _] = pointer_cache.emplace(base, std::move(t));
        return &iter->second;
    }

}
