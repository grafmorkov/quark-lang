#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <limits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "quant/ir/opt.h"

namespace quant::codegen {

namespace {

using ast::TypeKind;

bool skip_pass(const char* name) {
    const char* skip = std::getenv("QUANT_SKIP");
    return skip && std::string(skip).find(name) != std::string::npos;
}

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

bool is_signed_int_kind(TypeKind kind) {
    return kind >= TypeKind::I8 && kind <= TypeKind::I64;
}

bool is_scalar_int_kind(TypeKind kind) {
    return is_signed_int_kind(kind) ||
           kind == TypeKind::U8 || kind == TypeKind::U16 ||
           kind == TypeKind::U32 || kind == TypeKind::U64 ||
           kind == TypeKind::Bool;
}

bool is_float_kind(TypeKind kind) {
    return kind == TypeKind::F32 || kind == TypeKind::F64;
}

bool op_is_compare(IRBinaryOp op) {
    switch (op) {
        case IRBinaryOp::Eq:
        case IRBinaryOp::NotEq:
        case IRBinaryOp::Lt:
        case IRBinaryOp::Lte:
        case IRBinaryOp::Gt:
        case IRBinaryOp::Gte:
            return true;
        default:
            return false;
    }
}

struct Attr {
    bool has_def = false;
    Reg def = 0;
    std::vector<Reg> uses;
    bool keep = false;
};

Attr classify(const IRInst& inst) {
    return std::visit(Overloaded{
        [](const IRLoadConst& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const IRLoadFloatConst& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const IRLoadString& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const IRLoadLocal& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const IRStoreLocal& x) {
            return Attr{false, 0, {x.src}, true};
        },
        [](const IRAddrOf& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const IRBinary& x) {
            return Attr{true, x.dst, {x.lhs, x.rhs}, false};
        },
        [](const IRCall& x) {
            return Attr{true, x.dst, x.args, true};
        },
        [](const IRReturn& x) {
            return Attr{false, 0, {x.value}, true};
        },
        [](const IRJump&) {
            return Attr{false, 0, {}, true};
        },
        [](const IRBranch& x) {
            return Attr{false, 0, {x.cond}, true};
        },
        [](const IRLabel&) {
            return Attr{false, 0, {}, true};
        },
        [](const IRGetField& x) {
            return Attr{true, x.dst, {x.base}, false};
        },
        [](const IRSetField& x) {
            return Attr{false, 0, {x.base, x.value}, true};
        },
        [](const IRCast& x) {
            return Attr{true, x.dst, {x.src}, false};
        },
        [](const IRLoadElement& x) {
            return Attr{true, x.dst, {x.base, x.index}, false};
        },
        [](const IRStoreElement& x) {
            return Attr{false, 0, {x.base, x.index, x.value}, true};
        },
        [](const IRRegionBegin&) {
            return Attr{false, 0, {}, true};
        },
        [](const IRRegionAlloc& x) {
            return Attr{true, x.dst, {x.size}, true};
        },
        [](const IRRegionEnd&) {
            return Attr{false, 0, {}, true};
        },
        [](const IRAlloca& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const IRLoadGlobal& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const IRStoreGlobal& x) {
            return Attr{false, 0, {x.src}, true};
        },
        [](const IRLoadGlobalAddr& x) {
            return Attr{true, x.dst, {}, false};
        },
        [](const auto&) {
            return Attr{};
        }
    }, inst);
}

void compact_body(std::vector<IRInst>& body, const std::vector<uint8_t>& dead) {
    if (body.size() != dead.size()) return;

    size_t write = 0;
    for (size_t read = 0; read < body.size(); ++read) {
        if (dead[read]) continue;
        if (write != read) body[write] = std::move(body[read]);
        ++write;
    }

    body.resize(write);
}

bool remove_dead_instructions(IRFunction& fn) {
    const size_t size = fn.body.size();
    if (size == 0) return false;

    std::vector<uint8_t> live(fn.temp_count);
    std::vector<uint8_t> dead(size);

    for (size_t i = size; i-- > 0;) {
        const Attr attr = classify(fn.body[i]);
        const bool removable =
            !attr.keep &&
            attr.has_def &&
            attr.def < live.size() &&
            !live[attr.def];

        if (removable) {
            dead[i] = 1;
            continue;
        }

        for (Reg use : attr.uses) {
            if (use < live.size()) live[use] = 1;
        }
    }

    if (std::none_of(dead.begin(), dead.end(), [](uint8_t value) {
            return value != 0;
        })) {
        return false;
    }

    compact_body(fn.body, dead);
    return true;
}

bool remove_dead_locals(IRFunction& fn) {
    const size_t size = fn.body.size();
    if (size == 0 || fn.local_count == 0) return false;

    const size_t local_count = static_cast<size_t>(fn.local_count);
    std::vector<uint8_t> escaped(local_count);

    for (const auto& inst : fn.body) {
        std::visit(Overloaded{
            [&](const IRAddrOf& addr) {
                if (addr.local < local_count) escaped[addr.local] = 1;
            },
            [](const auto&) {}
        }, inst);
    }

    std::vector<uint32_t> leaders{0};
    std::unordered_map<uint32_t, uint32_t> block_of_label;

    for (size_t i = 0; i < size; ++i) {
        std::visit(Overloaded{
            [&](const IRLabel& label) {
                if (i == leaders.back()) {
                    block_of_label[label.id] = static_cast<uint32_t>(leaders.size() - 1);
                } else {
                    block_of_label[label.id] = static_cast<uint32_t>(leaders.size());
                    leaders.push_back(static_cast<uint32_t>(i));
                }
            },
            [](const auto&) {}
        }, fn.body[i]);
    }

    const uint32_t block_count = static_cast<uint32_t>(leaders.size());
    const auto block_start = [&](uint32_t block) { return leaders[block]; };
    const auto block_end = [&](uint32_t block) {
        return block + 1 < block_count ? leaders[block + 1] : static_cast<uint32_t>(size);
    };

    std::vector<std::vector<uint32_t>> successors(block_count);
    const auto successor = [&](uint32_t label) -> uint32_t {
        const auto it = block_of_label.find(label);
        return it == block_of_label.end() ? block_count : it->second;
    };

    for (uint32_t block = 0; block < block_count; ++block) {
        const size_t last = block_end(block) - 1;
        if (last >= size || block_start(block) >= block_end(block)) std::abort();

        std::visit(Overloaded{
            [&](const IRJump& jump) {
                const uint32_t target = successor(jump.target);
                if (target != block_count) successors[block].push_back(target);
            },
            [&](const IRBranch& branch) {
                const uint32_t then_block = successor(branch.then_label);
                if (then_block != block_count) successors[block].push_back(then_block);

                const uint32_t else_block = successor(branch.else_label);
                if (else_block != block_count && else_block != then_block) {
                    successors[block].push_back(else_block);
                }
            },
            [](const IRReturn&) {},
            [&](const auto&) {
                if (block + 1 < block_count) successors[block].push_back(block + 1);
            }
        }, fn.body[last]);
    }

    auto scan_block = [&](uint32_t block,
                          const std::vector<uint8_t>& live_out,
                          std::vector<uint8_t>& live,
                          std::vector<uint32_t>& dead_indices) {
        live = live_out;

        for (size_t i = block_end(block); i-- > block_start(block);) {
            std::visit(Overloaded{
                [&](const IRStoreLocal& store) {
                    const Local local = store.local;
                    if (local >= local_count || escaped[local]) return;

                    if (live[local]) {
                        live[local] = 0;
                    } else {
                        dead_indices.push_back(static_cast<uint32_t>(i));
                    }
                },
                [&](const IRLoadLocal& load) {
                    if (load.local < local_count) live[load.local] = 1;
                },
                [&](const IRAddrOf& addr) {
                    if (addr.local < local_count) live[addr.local] = 1;
                },
                [&](const IRRegionBegin& region) {
                    if (region.region_local < local_count) live[region.region_local] = 1;
                },
                [&](const IRRegionAlloc& region) {
                    if (region.region_local < local_count) live[region.region_local] = 1;
                },
                [&](const IRRegionEnd& region) {
                    if (region.region_local < local_count) live[region.region_local] = 1;
                },
                [](const auto&) {}
            }, fn.body[i]);
        }
    };

    std::vector<std::vector<uint8_t>> live_in(
        block_count, std::vector<uint8_t>(local_count));

    bool converged = false;
    for (int iteration = 0; iteration < 512 && !converged; ++iteration) {
        converged = true;

        for (uint32_t block = 0; block < block_count; ++block) {
            std::vector<uint8_t> live_out(local_count);
            for (uint32_t successor_block : successors[block]) {
                const auto& successor_live = live_in[successor_block];
                for (size_t local = 0; local < local_count; ++local) {
                    live_out[local] |= successor_live[local];
                }
            }

            std::vector<uint8_t> input(local_count);
            std::vector<uint32_t> ignored;
            scan_block(block, live_out, input, ignored);

            if (input != live_in[block]) {
                live_in[block] = std::move(input);
                converged = false;
            }
        }
    }

    std::vector<uint8_t> dead(size);
    for (uint32_t block = 0; block < block_count; ++block) {
        std::vector<uint8_t> live_out(local_count);
        for (uint32_t successor_block : successors[block]) {
            const auto& successor_live = live_in[successor_block];
            for (size_t local = 0; local < local_count; ++local) {
                live_out[local] |= successor_live[local];
            }
        }

        std::vector<uint8_t> live(local_count);
        std::vector<uint32_t> dead_indices;
        scan_block(block, live_out, live, dead_indices);
        for (uint32_t index : dead_indices) dead[index] = 1;
    }

    if (std::none_of(dead.begin(), dead.end(), [](uint8_t value) { return value != 0; })) {
        return false;
    }

    compact_body(fn.body, dead);
    return true;
}

struct FoldResult {
    bool valid = false;
    int64_t value = 0;
};

FoldResult fold_int(IRBinaryOp op, int64_t lhs, int64_t rhs, TypeKind kind) {
    if (!is_scalar_int_kind(kind)) return {};

    const uint64_t a = static_cast<uint64_t>(lhs);
    const uint64_t b = static_cast<uint64_t>(rhs);
    const bool signed_op = is_signed_int_kind(kind);

    switch (op) {
        case IRBinaryOp::Add:
            return {true, static_cast<int64_t>(a + b)};
        case IRBinaryOp::Sub:
            return {true, static_cast<int64_t>(a - b)};
        case IRBinaryOp::Mul:
            return {true, static_cast<int64_t>(a * b)};
        case IRBinaryOp::Div:
            if (b == 0) return {};
            if (signed_op &&
                a == static_cast<uint64_t>(std::numeric_limits<int64_t>::min()) &&
                b == static_cast<uint64_t>(-1)) {
                return {};
            }
            return signed_op
                ? FoldResult{true, static_cast<int64_t>(a) / static_cast<int64_t>(b)}
                : FoldResult{true, static_cast<int64_t>(a / b)};
        case IRBinaryOp::Eq:
            return {true, a == b};
        case IRBinaryOp::NotEq:
            return {true, a != b};
        case IRBinaryOp::Lt:
            return signed_op
                ? FoldResult{true, static_cast<int64_t>(static_cast<int64_t>(a) < static_cast<int64_t>(b))}
                : FoldResult{true, static_cast<int64_t>(a < b)};
        case IRBinaryOp::Lte:
            return signed_op
                ? FoldResult{true, static_cast<int64_t>(static_cast<int64_t>(a) <= static_cast<int64_t>(b))}
                : FoldResult{true, static_cast<int64_t>(a <= b)};
        case IRBinaryOp::Gt:
            return signed_op
                ? FoldResult{true, static_cast<int64_t>(static_cast<int64_t>(a) > static_cast<int64_t>(b))}
                : FoldResult{true, static_cast<int64_t>(a > b)};
        case IRBinaryOp::Gte:
            return signed_op
                ? FoldResult{true, static_cast<int64_t>(static_cast<int64_t>(a) >= static_cast<int64_t>(b))}
                : FoldResult{true, static_cast<int64_t>(a >= b)};
        case IRBinaryOp::BitAnd:
            return {true, static_cast<int64_t>(a & b)};
        case IRBinaryOp::BitOr:
            return {true, static_cast<int64_t>(a | b)};
        case IRBinaryOp::LogicAnd:
            return {true, a != 0 && b != 0};
        case IRBinaryOp::LogicOr:
            return {true, a != 0 || b != 0};
    }

    return {};
}

bool fold_float_arith(IRBinaryOp op, double lhs, double rhs, double& out) {
    switch (op) {
        case IRBinaryOp::Add:
            out = lhs + rhs;
            return true;
        case IRBinaryOp::Sub:
            out = lhs - rhs;
            return true;
        case IRBinaryOp::Mul:
            out = lhs * rhs;
            return true;
        case IRBinaryOp::Div:
            if (rhs == 0.0) return false;
            out = lhs / rhs;
            return true;
        default:
            return false;
    }
}

bool fold_float_compare(IRBinaryOp op, double lhs, double rhs, int64_t& out) {
    switch (op) {
        case IRBinaryOp::Eq:
            out = lhs == rhs;
            return true;
        case IRBinaryOp::NotEq:
            out = lhs != rhs;
            return true;
        case IRBinaryOp::Lt:
            out = lhs < rhs;
            return true;
        case IRBinaryOp::Lte:
            out = lhs <= rhs;
            return true;
        case IRBinaryOp::Gt:
            out = lhs > rhs;
            return true;
        case IRBinaryOp::Gte:
            out = lhs >= rhs;
            return true;
        default:
            return false;
    }
}

void rename_reg_in_inst(IRInst& inst, Reg from, Reg to) {
    if (from == to) return;

    std::visit(Overloaded{
        [&](IRBinary& x) {
            if (x.lhs == from) x.lhs = to;
            if (x.rhs == from) x.rhs = to;
        },
        [&](IRCall& x) {
            for (Reg& reg : x.args) {
                if (reg == from) reg = to;
            }
        },
        [&](IRReturn& x) {
            if (x.value == from) x.value = to;
        },
        [&](IRBranch& x) {
            if (x.cond == from) x.cond = to;
        },
        [&](IRGetField& x) {
            if (x.base == from) x.base = to;
        },
        [&](IRSetField& x) {
            if (x.base == from) x.base = to;
            if (x.value == from) x.value = to;
        },
        [&](IRCast& x) {
            if (x.src == from) x.src = to;
        },
        [&](IRLoadElement& x) {
            if (x.base == from) x.base = to;
            if (x.index == from) x.index = to;
        },
        [&](IRStoreElement& x) {
            if (x.base == from) x.base = to;
            if (x.index == from) x.index = to;
            if (x.value == from) x.value = to;
        },
        [&](IRStoreLocal& x) {
            if (x.src == from) x.src = to;
        },
        [&](IRStoreGlobal& x) {
            if (x.src == from) x.src = to;
        },
        [&](IRRegionAlloc& x) {
            if (x.size == from) x.size = to;
        },
        [](auto&) {}
    }, inst);
}

template <class Map>
bool lookup_constant(const Map& map, Reg reg, typename Map::mapped_type& value) {
    const auto it = map.find(reg);
    if (it == map.end()) return false;
    value = it->second;
    return true;
}

bool fold_constants(IRFunction& fn) {
    std::unordered_map<Reg, int64_t> integer_constants;
    std::unordered_map<Reg, double> float_constants;

    for (const auto& inst : fn.body) {
        std::visit(Overloaded{
            [&](const IRLoadConst& x) {
                integer_constants[x.dst] = x.value;
            },
            [&](const IRLoadFloatConst& x) {
                float_constants[x.dst] = x.value;
            },
            [](const auto&) {}
        }, inst);
    }

    bool changed = false;
    std::vector<uint8_t> erase(fn.body.size());

    for (size_t i = 0; i < fn.body.size(); ++i) {
        IRBinary* binary = nullptr;
        std::visit(Overloaded{
            [&](IRBinary& value) {
                binary = &value;
            },
            [](auto&) {}
        }, fn.body[i]);

        if (!binary) continue;

        const TypeKind kind = binary->type_kind;

        if (is_float_kind(kind)) {
            const auto lhs = float_constants.find(binary->lhs);
            const auto rhs = float_constants.find(binary->rhs);

            if (lhs != float_constants.end() && rhs != float_constants.end()) {
                if (op_is_compare(binary->op)) {
                    int64_t result = 0;
                    if (fold_float_compare(binary->op, lhs->second, rhs->second, result)) {
                        fn.body[i] = IRLoadConst{binary->dst, result};
                        changed = true;
                        continue;
                    }
                } else {
                    double result = 0;
                    if (fold_float_arith(binary->op, lhs->second, rhs->second, result)) {
                        fn.body[i] = IRLoadFloatConst{binary->dst, result, kind};
                        changed = true;
                        continue;
                    }
                }
            }

            continue;
        }

        if (!is_scalar_int_kind(kind)) continue;

        int64_t lhs = 0;
        int64_t rhs = 0;
        const bool has_lhs = lookup_constant(integer_constants, binary->lhs, lhs);
        const bool has_rhs = lookup_constant(integer_constants, binary->rhs, rhs);

        if (has_lhs && has_rhs) {
            const FoldResult result = fold_int(binary->op, lhs, rhs, kind);
            if (result.valid) {
                fn.body[i] = IRLoadConst{binary->dst, result.value};
                changed = true;
                continue;
            }
        }

        const Reg dst = binary->dst;

        if (binary->op == IRBinaryOp::Mul &&
            ((has_lhs && lhs == 0) || (has_rhs && rhs == 0))) {
            fn.body[i] = IRLoadConst{dst, 0};
            changed = true;
            continue;
        }

        if (binary->op == IRBinaryOp::BitAnd &&
            ((has_lhs && lhs == 0) || (has_rhs && rhs == 0))) {
            fn.body[i] = IRLoadConst{dst, 0};
            changed = true;
            continue;
        }

        Reg keep = 0;
        bool has_keep = false;

        switch (binary->op) {
            case IRBinaryOp::Add:
                if (has_rhs && rhs == 0) {
                    keep = binary->lhs;
                    has_keep = true;
                } else if (has_lhs && lhs == 0) {
                    keep = binary->rhs;
                    has_keep = true;
                }
                break;
            case IRBinaryOp::Sub:
                if (has_rhs && rhs == 0) {
                    keep = binary->lhs;
                    has_keep = true;
                }
                break;
            case IRBinaryOp::Mul:
                if (has_rhs && rhs == 1) {
                    keep = binary->lhs;
                    has_keep = true;
                } else if (has_lhs && lhs == 1) {
                    keep = binary->rhs;
                    has_keep = true;
                }
                break;
            case IRBinaryOp::Div:
                if (has_rhs && rhs == 1) {
                    keep = binary->lhs;
                    has_keep = true;
                }
                break;
            case IRBinaryOp::BitOr:
                if (has_rhs && rhs == 0) {
                    keep = binary->lhs;
                    has_keep = true;
                } else if (has_lhs && lhs == 0) {
                    keep = binary->rhs;
                    has_keep = true;
                }
                break;
            default:
                break;
        }

        if (has_keep && keep != dst) {
            for (auto& inst : fn.body) {
                rename_reg_in_inst(inst, dst, keep);
            }

            erase[i] = 1;
            changed = true;
        }
    }

    if (std::any_of(erase.begin(), erase.end(), [](uint8_t value) {
            return value != 0;
        })) {
        compact_body(fn.body, erase);
    }

    return changed;
}

bool is_entry_root(const IRFunction& fn) {
    return fn.is_entry ||
           fn.is_extern ||
           fn.syscall_number >= 0 ||
           !fn.export_name.empty() ||
           !fn.import_dll.empty() ||
           fn.name.ends_with("main");
}

void remove_unused_functions(IRProgram& program) {
    const size_t size = program.functions.size();
    if (size == 0) return;

    std::vector<uint8_t> keep(size);
    std::vector<size_t> work;

    for (size_t i = 0; i < size; ++i) {
        if (!is_entry_root(program.functions[i])) continue;
        keep[i] = 1;
        work.push_back(i);
    }

    while (!work.empty()) {
        const size_t index = work.back();
        work.pop_back();

        for (const auto& inst : program.functions[index].body) {
            std::visit(Overloaded{
                [&](const IRCall& call) {
                    if (call.func_id < size && !keep[call.func_id]) {
                        keep[call.func_id] = 1;
                        work.push_back(call.func_id);
                    }
                },
                [](const auto&) {}
            }, inst);
        }
    }

    if (std::all_of(keep.begin(), keep.end(), [](uint8_t value) {
            return value != 0;
        })) {
        return;
    }

    std::vector<uint32_t> remap(size);
    std::vector<IRFunction> functions;
    functions.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        if (!keep[i]) continue;

        remap[i] = static_cast<uint32_t>(functions.size());
        IRFunction fn = std::move(program.functions[i]);
        fn.id = remap[i];
        functions.push_back(std::move(fn));
    }

    for (auto& fn : functions) {
        for (auto& inst : fn.body) {
            std::visit(Overloaded{
                [&](IRCall& call) {
                    call.func_id = remap[call.func_id];
                },
                [](auto&) {}
            }, inst);
        }
    }

    program.functions = std::move(functions);
}

void compact_strings(IRProgram& program) {
    const size_t size = program.strings.size();
    if (size == 0) return;

    std::vector<uint8_t> used(size);

    for (const auto& fn : program.functions) {
        for (const auto& inst : fn.body) {
            std::visit(Overloaded{
                [&](const IRLoadString& load) {
                    if (load.string_id < size) used[load.string_id] = 1;
                },
                [](const auto&) {}
            }, inst);
        }
    }

    if (std::all_of(used.begin(), used.end(), [](uint8_t value) {
            return value != 0;
        })) {
        return;
    }

    std::vector<uint32_t> remap(size);
    std::vector<IRString> strings;
    strings.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        if (!used[i]) continue;

        remap[i] = static_cast<uint32_t>(strings.size());
        IRString string = program.strings[i];
        string.id = remap[i];
        strings.push_back(std::move(string));
    }

    for (auto& fn : program.functions) {
        for (auto& inst : fn.body) {
            std::visit(Overloaded{
                [&](IRLoadString& load) {
                    load.string_id = remap[load.string_id];
                },
                [](auto&) {}
            }, inst);
        }
    }

    program.strings = std::move(strings);
}

void compact_globals(IRProgram& program) {
    const size_t size = program.globals.size();
    if (size == 0) return;

    std::vector<uint8_t> used(size);

    for (const auto& fn : program.functions) {
        for (const auto& inst : fn.body) {
            std::visit(Overloaded{
                [&](const IRLoadGlobal& load) {
                    if (load.global_id < size) used[load.global_id] = 1;
                },
                [&](const IRStoreGlobal& store) {
                    if (store.global_id < size) used[store.global_id] = 1;
                },
                [&](const IRLoadGlobalAddr& load) {
                    if (load.global_id < size) used[load.global_id] = 1;
                },
                [](const auto&) {}
            }, inst);
        }
    }

    if (std::all_of(used.begin(), used.end(), [](uint8_t value) {
            return value != 0;
        })) {
        return;
    }

    std::vector<uint32_t> remap(size);
    std::vector<IRGlobal> globals;
    globals.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        if (!used[i]) continue;

        remap[i] = static_cast<uint32_t>(globals.size());
        globals.push_back(std::move(program.globals[i]));
    }

    for (auto& fn : program.functions) {
        for (auto& inst : fn.body) {
            std::visit(Overloaded{
                [&](IRLoadGlobal& load) {
                    load.global_id = remap[load.global_id];
                },
                [&](IRStoreGlobal& store) {
                    store.global_id = remap[store.global_id];
                },
                [&](IRLoadGlobalAddr& load) {
                    load.global_id = remap[load.global_id];
                },
                [](auto&) {}
            }, inst);
        }
    }

    program.globals = std::move(globals);
}

void compact_labels(IRProgram& program) {
    for (auto& fn : program.functions) {
        std::vector<uint8_t> referenced;

        for (const auto& inst : fn.body) {
            std::visit(Overloaded{
                [&](const IRJump& jump) {
                    if (jump.target >= referenced.size()) {
                        referenced.resize(jump.target + 1);
                    }
                    referenced[jump.target] = 1;
                },
                [&](const IRBranch& branch) {
                    if (branch.then_label >= referenced.size()) {
                        referenced.resize(branch.then_label + 1);
                    }
                    referenced[branch.then_label] = 1;

                    if (branch.else_label >= referenced.size()) {
                        referenced.resize(branch.else_label + 1);
                    }
                    referenced[branch.else_label] = 1;
                },
                [](const auto&) {}
            }, inst);
        }

        std::vector<uint8_t> dead(fn.body.size());
        bool dropped = false;

        for (size_t i = 0; i < fn.body.size(); ++i) {
            std::visit(Overloaded{
                [&](const IRLabel& label) {
                    if (label.id >= referenced.size() || !referenced[label.id]) {
                        dead[i] = 1;
                        dropped = true;
                    }
                },
                [](const auto&) {}
            }, fn.body[i]);
        }

        if (dropped) compact_body(fn.body, dead);
    }
}

}

void optimize(IRProgram& program, OptLevel level, bool keep_all_functions) {
    if (level == OptLevel::O0) return;

    for (int round = 0; round < 4; ++round) {
        bool changed = false;

        for (auto& fn : program.functions) {
            if (fn.is_extern || fn.body.empty()) continue;

            if (!skip_pass("fold")) changed |= fold_constants(fn);
            if (!skip_pass("dead")) changed |= remove_dead_instructions(fn);
            if (!skip_pass("dse")) changed |= remove_dead_locals(fn);
            if (!skip_pass("dead")) changed |= remove_dead_instructions(fn);
        }

        if (!changed) break;
    }

    if (level == OptLevel::O1) return;

    if (!keep_all_functions && !skip_pass("prune")) {
        remove_unused_functions(program);
    }

    if (!skip_pass("prune")) {
        compact_strings(program);
        compact_globals(program);
        compact_labels(program);
    }
}

}
