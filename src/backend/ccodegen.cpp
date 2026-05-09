#include "quark/backend/ccodegen.h"

namespace quark::codegen{
    namespace {

        std::string type_to_string(const Type* type) {
            if (!type) return "void";

            switch (type->kind) {
                case Type::Int:    return "int";
                case Type::String: return "char*";
                case Type::Void:   return "void";
                case Type::Struct: return type->struct_name;
                default:           return "void";
            }
        }

        std::string op_to_string(IRBinaryOp op) {
            switch (op) {
                case IRBinaryOp::Add:   return "+";
                case IRBinaryOp::Sub:   return "-";
                case IRBinaryOp::Mul:   return "*";
                case IRBinaryOp::Div:   return "/";
                case IRBinaryOp::Eq:    return "==";
                case IRBinaryOp::NotEq: return "!=";
                case IRBinaryOp::Lt:    return "<";
                case IRBinaryOp::Lte:   return "<=";
                case IRBinaryOp::Gt:    return ">";
                case IRBinaryOp::Gte:   return ">=";
                default:                return "?";
            }
        }

        std::string field_name_for_index(size_t idx) {
            return "field" + std::to_string(idx);
        }
    }
    std::string CGenerator::generate(const IRBuilder& builder) {
            this->builder = &builder;
            for (const auto& block_ptr : builder.blocks) {
                if (!block_ptr->terminated) {
                    crash("Block not terminated: " + block_ptr->name);
                }
            }

            out << "#include <stdint.h>\n";
            out << "#include <stdbool.h>\n\n";

            emit_struct_defs(builder);

            out << "\nint main() {\n";
            for (const auto& block_ptr : builder.blocks) {
                emit_block(*block_ptr);
            }
            out << "}\n";

            return out.str();
        }

        void CGenerator::emit_struct_defs(const IRBuilder& builder) {
            for (const auto& [name, layout] : builder.struct_layouts) {
                out << "typedef struct " << name << " {\n";

                for (size_t i = 0; i < layout.field_types.size(); ++i) {
                    out << "    " << type_to_string(layout.field_types[i]) << " "
                        << field_name_for_index(i) << ";\n";
                }

                out << "} " << name << ";\n";
            }
        }

        void CGenerator::emit_block(const IRBlock& block) {
            out << block.name << ":\n";

            for (const auto& inst : block.inst) {
                std::visit([this](auto& node) {
                    emit_inst(node);
                }, inst);
            }
        }

        template<typename T>
        void CGenerator::emit_inst(const T&) {
            static_assert(sizeof(T) == 0, "Unhandled IR node");
        }

        void CGenerator::emit_inst(const IRBinary& node) {
            out << "    " << type_to_string(node.dst.type) << " "
                << node.dst.name << " = "
                << node.lhs.name << " "
                << op_to_string(node.op) << " "
                << node.rhs.name << ";\n";
        }

        void CGenerator::emit_inst(const IRStore& node) {
            out << "    " << node.target.name
                << " = " << node.value.name << ";\n";
        }

        void CGenerator::emit_inst(const IRReturn& node) {
            out << "    return " << node.value.name << ";\n";
        }

        void CGenerator::emit_inst(const IRJump& node) {
            out << "    goto " << node.target->name << ";\n";
        }

        void CGenerator::emit_inst(const IRBranch& node) {
            out << "    if (" << node.cond.name << ") goto "
                << node.then_block->name << "; else goto "
                << node.else_block->name << ";\n";
        }

        void CGenerator::emit_inst(const IRCall& node) {
            if (node.dst.type && node.dst.type->kind != Type::Void) {
                out << "    " << type_to_string(node.dst.type) << " "
                    << node.dst.name << " = ";
            } else {
                out << "    ";
            }

            out << node.callee.name << "(";
            for (size_t i = 0; i < node.args.size(); ++i) {
                if (i > 0) out << ", ";
                out << node.args[i].name;
            }
            out << ");\n";
        }

        void CGenerator::emit_inst(const IRAlloc& node) {
            out << "    " << type_to_string(node.type) << " "
                << node.name << ";\n";
        }

        void CGenerator::emit_inst(const IRGetField& node) {
            if (!node.base.type || node.base.type->kind != Type::Struct) {
                crash("IRGetField base is not a struct");
            }

            auto it = builder->struct_layouts.find(node.base.type->struct_name);
            if (it == builder->struct_layouts.end()) {
                crash("Unknown struct type: " + node.base.type->struct_name);
            }

            const auto& layout = it->second;
            if (node.index < 0 || static_cast<size_t>(node.index) >= layout.field_types.size()) {
                crash("IRGetField index out of range");
            }

            out << "    " << type_to_string(node.dst.type) << " "
                << node.dst.name << " = "
                << node.base.name << "."
                << field_name_for_index(static_cast<size_t>(node.index))
                << ";\n";
        }

        void CGenerator::emit_inst(const IRSetField& node) {
            if (!node.base.type || node.base.type->kind != Type::Struct) {
                crash("IRSetField base is not a struct");
            }

            auto it = builder->struct_layouts.find(node.base.type->struct_name);
            if (it == builder->struct_layouts.end()) {
                crash("Unknown struct type: " + node.base.type->struct_name);
            }

            const auto& layout = it->second;
            if (node.index < 0 || static_cast<size_t>(node.index) >= layout.field_types.size()) {
                crash("IRSetField index out of range");
            }

            out << "    " << node.base.name << "."
                << field_name_for_index(static_cast<size_t>(node.index))
                << " = " << node.value.name << ";\n";
        }
}