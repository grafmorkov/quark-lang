#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quant/backend/mc.h"

namespace quant::codegen::aarch64 {

// 64-bit general-purpose registers.
enum Reg : uint8_t {
    X0 = 0, X1, X2, X3, X4, X5, X6, X7,
    X8, X9, X10, X11, X12, X13, X14, X15,
    X16, X17, X18, X19, X20, X21, X22, X23,
    X24, X25, X26, X27, X28, X29, X30,
    XZR = 31,  // zero register (read-only: always 0)
    SP  = 31,  // stack pointer (write context only)
};

// 32-bit general-purpose registers (lower halves).
enum WReg : uint8_t {
    W0 = 0, W1, W2, W3, W4, W5, W6, W7,
    W8, W9, W10, W11, W12, W13, W14, W15,
    W16, W17, W18, W19, W20, W21, W22, W23,
    W24, W25, W26, W27, W28, W29, W30,
    WZR = 31,
};

// 64-bit FP/NEON registers (double-precision).
enum DReg : uint8_t {
    D0 = 0, D1, D2, D3, D4, D5, D6, D7,
};

// 32-bit FP registers (single-precision).
enum SReg : uint8_t {
    S0 = 0, S1, S2, S3, S4, S5, S6, S7,
};

// Condition codes for B.cond, CSET, CSEL, etc.
enum Cond : uint8_t {
    COND_EQ = 0x0,  // equal (Z=1)
    COND_NE = 0x1,  // not equal (Z=0)
    COND_CS = 0x2,  // carry set (unsigned >=)
    COND_CC = 0x3,  // carry clear (unsigned <)
    COND_MI = 0x4,  // minus (negative)
    COND_PL = 0x5,  // plus (positive or zero)
    COND_VS = 0x6,  // overflow
    COND_VC = 0x7,  // no overflow
    COND_HI = 0x8,  // unsigned higher (C=1, Z=0)
    COND_LS = 0x9,  // unsigned lower or same
    COND_GE = 0xA,  // signed >=
    COND_LT = 0xB,  // signed <
    COND_GT = 0xC,  // signed >
    COND_LE = 0xD,  // signed <=
    COND_AL = 0xE,  // always
    COND_NV = 0xF,  // always (reserved)
};

// Invert a condition (for branch swapping).
inline Cond invert_cond(Cond c) {
    return static_cast<Cond>(static_cast<uint8_t>(c) ^ 1);
}

// Memory operand for LDR/STR with immediate offset.
struct Mem {
    Reg base = X0;
    int64_t offset = 0;  // byte offset (unscaled: -256..255 for LDUR/STUR; scaled 0..32760 for LDR/STR)
};

inline Mem mem(Reg base, int64_t offset = 0) {
    return Mem{base, offset};
}

// Float memory operand (same as Mem, just typed for clarity).
struct FMem {
    Reg base = X0;
    int64_t offset = 0;
};

inline FMem fmem(Reg base, int64_t offset = 0) {
    return FMem{base, offset};
}

class Emitter {
public:
    std::vector<uint8_t> code;
    std::vector<mc::Relocation> relocs;

    void emit32(uint32_t instruction);
    void reloc(const std::string& sym, mc::RelType type, int64_t addend);

    // Move (immediate)
    void mov_imm64(Reg dst, uint64_t imm); // Load 64-bit immediate into Xd (up to 4 MOVZ+MOVK instructions).
    // Load 16-bit immediate with optional LSL #0/#16/#32/#48.
    void movz(Reg dst, uint16_t imm, uint8_t shift = 0);
    void movk(Reg dst, uint16_t imm, uint8_t shift = 0);
    // MOV (register alias): ORR Xd, XZR, Xm
    void mov_reg(Reg dst, Reg src);

    // Arithmetic (immediate)
    void add_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift = 0);  // shift: 0 or 12
    void sub_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift = 0);
    void adds_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift = 0);
    void subs_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift = 0);
    // SP/FP relative: ADD Xd, SP, #imm or SUB Xd, SP, #imm
    void add_sp_imm(Reg dst, uint16_t imm, uint8_t shift = 0);
    void sub_sp(uint16_t imm, uint8_t shift = 0);

    // Arithmetic (register)
    void add_reg(Reg dst, Reg a, Reg b);
    void sub_reg(Reg dst, Reg a, Reg b);
    void adds_reg(Reg dst, Reg a, Reg b);
    void subs_reg(Reg dst, Reg a, Reg b);
    void mul(Reg dst, Reg a, Reg b);
    void sdiv(Reg dst, Reg a, Reg b);
    void udiv(Reg dst, Reg a, Reg b);
    // MADD/SUB: dst = a * b + c / a * b - c
    void madd(Reg dst, Reg a, Reg b, Reg c);
    void msub(Reg dst, Reg a, Reg b, Reg c);

    // Logical (register)
    void and_reg(Reg dst, Reg a, Reg b);
    void orr_reg(Reg dst, Reg a, Reg b);
    void eor_reg(Reg dst, Reg a, Reg b);
    void and_imm(Reg dst, Reg src, uint64_t imm, uint8_t immr, uint8_t imms);
    void orr_imm(Reg dst, Reg src, uint64_t imm, uint8_t immr, uint8_t imms);

    // Shift (register)
    void lsl_reg(Reg dst, Reg src, Reg amount);  // LSL Xd, Xn, Xm
    void lsr_reg(Reg dst, Reg src, Reg amount);
    void asr_reg(Reg dst, Reg src, Reg amount);
    void lsl_imm(Reg dst, Reg src, uint8_t amount);
    void lsr_imm(Reg dst, Reg src, uint8_t amount);
    void asr_imm(Reg dst, Reg src, uint8_t amount);

    // Compare / Test
    void cmp_reg(Reg a, Reg b);   // SUBS XZR, Xn, Xm
    void cmp_imm(Reg a, uint16_t imm, uint8_t shift = 0);
    void cmn_reg(Reg a, Reg b);   // ADDS XZR, Xn, Xm
    void tst_reg(Reg a, Reg b);   // ANDS XZR, Xn, Xm
    void tst_imm(Reg a, uint64_t imm, uint8_t immr, uint8_t imms);

    // Conditional select / set
    void cset(Reg dst, Cond c);   // CSET Xd, cond  => CINC Xd, XZR, invert(cond)
    void cinc(Reg dst, Reg src, Cond c);

    // Branches
    void b(int32_t offset);                        // unconditional branch (±128MB)
    void bl(const std::string& sym);               // branch-and-link (call) with reloc
    void bl_reg(Reg target);                       // BLR Xn
    void ret(Reg xn = X30);                        // RET {Xn}
    void cbz(Reg src, int32_t offset);             // compare and branch if zero
    void cbnz(Reg src, int32_t offset);            // compare and branch if not zero
    void b_cond(Cond cond, int32_t offset);        // conditional branch

    // Load / Store (unsigned offset)
    void ldr_imm(Reg dst, Reg base, int16_t offset);      // LDR Xd, [Xn, #offset] (scaled/unscaled)
    void ldr32_imm(WReg dst, Reg base, int16_t offset);   // LDR Wd, [Xn, #offset] (scaled/unscaled)
    void ldrb(Reg dst, Reg base, uint16_t offset);         // LDRB Xd, [Xn, #offset]
    void ldrh(Reg dst, Reg base, uint16_t offset);         // LDRH Xd, [Xn, #offset]
    void str_imm(Reg src, Reg base, int16_t offset);      // STR Xn, [Xm, #offset]
    void str32_imm(WReg src, Reg base, int16_t offset);   // STR Wn, [Xm, #offset]
    void strb(Reg src, Reg base, uint16_t offset);         // STRB Xn, [Xm, #offset]
    void strh(Reg src, Reg base, uint16_t offset);         // STRH Xn, [Xm, #offset]

    // Load / Store (signed offset, unscaled)
    void ldur(Reg dst, Reg base, int16_t offset);
    void stur(Reg src, Reg base, int16_t offset);

    // Load / Store pair
    void ldp(Reg r1, Reg r2, Reg base, int16_t offset);   // signed, ±512 (8-byte)
    void stp(Reg r1, Reg r2, Reg base, int16_t offset);
    void stp_pre(Reg r1, Reg r2, Reg base, int16_t offset);  // pre-index: [base, #offset]!
    void ldp_post(Reg r1, Reg r2, Reg base, int16_t offset); // post-index: [base], #offset
    void ldp32(WReg r1, WReg r2, Reg base, int16_t offset); // signed, ±256 (4-byte)
    void stp32(WReg r1, WReg r2, Reg base, int16_t offset);

    // Load (register offset)
    void ldr_reg(Reg dst, Reg base, Reg index, uint8_t scale = 3); // LDR Xd, [Xn, Xm, LSL #scale]

    // ADRP / ADR
    void adrp(Reg dst, int32_t page_offset);  // ADRP Xd, #page_offset
    void adr(Reg dst, int32_t offset);         // ADR Xd, #offset

    // Sign/Zero extend
    void sxtw(Reg dst, WReg src);   // SXTW Xd, Wn (= SBFM Xd, Xn, #0, #31)
    void sxth(Reg dst, WReg src);   // SXTH Xd, Wn (= SBFM Xd, Xn, #0, #15)
    void sxtb(Reg dst, WReg src);   // SXTB Xd, Wn (= SBFM Xd, Xn, #0, #7)
    void uxtb(Reg dst, WReg src);   // UXTB Wd, Wn (= UBFM Wd, Wn, #0, #7)
    void uxth(Reg dst, WReg src);   // UXTH Wd, Wn (= UBFM Wd, Wn, #0, #15)

    // FP
    void fmov_d(DReg dst, DReg src);
    void fmov_s(SReg dst, SReg src);
    void fadd_d(DReg dst, DReg a, DReg b);
    void fsub_d(DReg dst, DReg a, DReg b);
    void fmul_d(DReg dst, DReg a, DReg b);
    void fdiv_d(DReg dst, DReg a, DReg b);
    void fadd_s(SReg dst, SReg a, SReg b);
    void fsub_s(SReg dst, SReg a, SReg b);
    void fmul_s(SReg dst, SReg a, SReg b);
    void fdiv_s(SReg dst, SReg a, SReg b);
    void fcmp_d(DReg a, DReg b);
    void fcmp_s(SReg a, SReg b);
    // FP load/store
    void ldr_d(DReg dst, Reg base, int16_t offset);
    void str_d(DReg src, Reg base, int16_t offset);
    void ldr_s(SReg dst, Reg base, int16_t offset);
    void str_s(SReg src, Reg base, int16_t offset);

    // FP conversion
    void scvtf_d(Reg src, DReg dst);     // int64 -> double
    void scvtf_s(Reg src, SReg dst);     // int64 -> float
    void fcvtzs_d(DReg src, Reg dst);    // double -> int64
    void fcvtzs_s(SReg src, Reg dst);    // float -> int64
    void fcvt_d_s(SReg src, DReg dst);   // float -> double
    void fcvt_s_d(DReg src, SReg dst);   // double -> float
    void fmov_d_reg(Reg src, DReg dst);  // Xn -> Dd
    void fmov_reg_d(DReg src, Reg dst);  // Dd -> Xn
    void fmov_s_reg(WReg src, SReg dst); // Wn -> Sd
    void fmov_reg_s(SReg src, WReg dst); // Sd -> Wn

    // System
    void svc(uint16_t imm = 0);          // SVC #imm (syscall)
    void wfe();                          // WFE (wait for event)
    void nop();
};

} // namespace quant::codegen::aarch64
