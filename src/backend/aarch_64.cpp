#include "quant/backend/aarch_64.h"

namespace quant::codegen::aarch64 {

// primitives

void Emitter::emit32(uint32_t instruction) {
    code.push_back(static_cast<uint8_t>(instruction));
    code.push_back(static_cast<uint8_t>(instruction >> 8));
    code.push_back(static_cast<uint8_t>(instruction >> 16));
    code.push_back(static_cast<uint8_t>(instruction >> 24));
}

void Emitter::reloc(const std::string& sym, mc::RelType type, int64_t addend) {
    relocs.push_back({static_cast<uint32_t>(code.size()), sym, type, addend});
}

// move (immediate)

void Emitter::movz(Reg dst, uint16_t imm, uint8_t shift) {
    uint32_t hw = (shift / 16) & 3u;
    emit32(0xD2800000u | (hw << 21) | (imm << 5) | dst);
}

void Emitter::movk(Reg dst, uint16_t imm, uint8_t shift) {
    uint32_t hw = (shift / 16) & 3u;
    emit32(0xF2800000u | (hw << 21) | (imm << 5) | dst);
}

void Emitter::mov_imm64(Reg dst, uint64_t imm) {
    movz(dst, static_cast<uint16_t>(imm), 0);
    if ((imm & ~0xFFFFull) != 0) movk(dst, static_cast<uint16_t>(imm >> 16), 16);
    if ((imm & ~0xFFFFFFFFull) != 0) movk(dst, static_cast<uint16_t>(imm >> 32), 32);
    if ((imm & ~0xFFFFFFFFFFFFull) != 0) movk(dst, static_cast<uint16_t>(imm >> 48), 48);
}

void Emitter::mov_reg(Reg dst, Reg src) {
    // MOV Xd, Xm = ORR Xd, XZR, Xm
    emit32(0xAA0003E0u | (src << 16) | dst);
}

// Arithmetic (immediate)

void Emitter::add_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift) {
    uint32_t sh = (shift == 12) ? 1u : 0u;
    emit32(0x91000000u | (sh << 22) | (imm << 10) | (src << 5) | dst);
}

void Emitter::sub_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift) {
    uint32_t sh = (shift == 12) ? 1u : 0u;
    emit32(0xD1000000u | (sh << 22) | (imm << 10) | (src << 5) | dst);
}

void Emitter::adds_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift) {
    uint32_t sh = (shift == 12) ? 1u : 0u;
    emit32(0xB1000000u | (sh << 22) | (imm << 10) | (src << 5) | dst);
}

void Emitter::subs_imm(Reg dst, Reg src, uint16_t imm, uint8_t shift) {
    uint32_t sh = (shift == 12) ? 1u : 0u;
    emit32(0xF1000000u | (sh << 22) | (imm << 10) | (src << 5) | dst);
}

void Emitter::add_sp_imm(Reg dst, uint16_t imm, uint8_t shift) {
    uint32_t sh = (shift == 12) ? 1u : 0u;
    // ADD Xd, SP, #imm uses SP encoding (reg 31) as Rn
    emit32(0x91000000u | (sh << 22) | (imm << 10) | (static_cast<uint32_t>(SP) << 5) | dst);
}

void Emitter::sub_sp(uint16_t imm, uint8_t shift) {
    uint32_t sh = (shift == 12) ? 1u : 0u;
    emit32(0xD1000000u | (sh << 22) | (imm << 10) | (static_cast<uint32_t>(SP) << 5) | SP);
}

// Arithmetic (register)

void Emitter::add_reg(Reg dst, Reg a, Reg b) {
    emit32(0x8B000000u | (b << 16) | (a << 5) | dst);
}

void Emitter::sub_reg(Reg dst, Reg a, Reg b) {
    emit32(0xCB000000u | (b << 16) | (a << 5) | dst);
}

void Emitter::adds_reg(Reg dst, Reg a, Reg b) {
    emit32(0xAB000000u | (b << 16) | (a << 5) | dst);
}

void Emitter::subs_reg(Reg dst, Reg a, Reg b) {
    emit32(0xEB000000u | (b << 16) | (a << 5) | dst);
}

void Emitter::mul(Reg dst, Reg a, Reg b) {
    // MUL Xd, Xn, Xm = MADD Xd, Xn, Xm, XZR
    // Encoding: [4:0]=Rd, [9:5]=Rn, [14:10]=Ra, [20:16]=Rm, [31:21]=opcode
    emit32(0x9B007C00u | (b << 16) | (a << 5) | dst);
}

void Emitter::sdiv(Reg dst, Reg a, Reg b) {
    emit32(0x9AC00C00u | (b << 16) | (a << 5) | dst);
}

void Emitter::udiv(Reg dst, Reg a, Reg b) {
    emit32(0x9AC00800u | (b << 16) | (a << 5) | dst);
}

void Emitter::madd(Reg dst, Reg a, Reg b, Reg c) {
    // MADD Xd, Xn, Xm, XRa
    emit32(0x9B000000u | (b << 16) | (c << 10) | (a << 5) | dst);
}

void Emitter::msub(Reg dst, Reg a, Reg b, Reg c) {
    emit32(0x9B008000u | (b << 16) | (c << 10) | (a << 5) | dst);
}

// Logical (register)

void Emitter::and_reg(Reg dst, Reg a, Reg b) {
    emit32(0x8A000000u | (b << 16) | (a << 5) | dst);
}

void Emitter::orr_reg(Reg dst, Reg a, Reg b) {
    emit32(0xAA000000u | (b << 16) | (a << 5) | dst);
}

void Emitter::eor_reg(Reg dst, Reg a, Reg b) {
    emit32(0xCA000000u | (b << 16) | (a << 5) | dst);
}

void Emitter::and_imm(Reg dst, Reg src, uint64_t imm, uint8_t immr, uint8_t imms) {
    uint32_t N = 1u; // 64-bit
    emit32(0x92000000u | (N << 22) | (immr << 16) | (imms << 10) | (src << 5) | dst);
}

void Emitter::orr_imm(Reg dst, Reg src, uint64_t imm, uint8_t immr, uint8_t imms) {
    uint32_t N = 1u;
    emit32(0xB2000000u | (N << 22) | (immr << 16) | (imms << 10) | (src << 5) | dst);
}

// Shift (register)

void Emitter::lsl_reg(Reg dst, Reg src, Reg amount) {
    // LSL Xd, Xn, Xm = LSLV Xd, Xn, Xm  (alias, shift type=00 is LSL in hardware)
    // LSLV: 0x9AC02000 | Rm<<16 | Rn<<5 | Rd
    emit32(0x9AC02000u | (amount << 16) | (src << 5) | dst);
}

void Emitter::lsr_reg(Reg dst, Reg src, Reg amount) {
    // LSRV: 0x9AC02400
    emit32(0x9AC02400u | (amount << 16) | (src << 5) | dst);
}

void Emitter::asr_reg(Reg dst, Reg src, Reg amount) {
    // ASRV: 0x9AC02800
    emit32(0x9AC02800u | (amount << 16) | (src << 5) | dst);
}

void Emitter::lsl_imm(Reg dst, Reg src, uint8_t amount) {
    // LSL Xd, Xn, #imm = UBFM Xd, Xn, #(64-imm), #(63-imm)
    uint8_t immr = 64 - amount;
    uint8_t imms = 63 - amount;
    emit32(0xD3400000u | (immr << 16) | (imms << 10) | (src << 5) | dst);
}

void Emitter::lsr_imm(Reg dst, Reg src, uint8_t amount) {
    // LSR Xd, Xn, #imm = UBFM Xd, Xn, #amount, #63
    uint8_t imms = 63;
    emit32(0xD3400000u | (amount << 16) | (imms << 10) | (src << 5) | dst);
}

void Emitter::asr_imm(Reg dst, Reg src, uint8_t amount) {
    // ASR Xd, Xn, #imm = SBFM Xd, Xn, #amount, #63
    uint8_t imms = 63;
    emit32(0x93400000u | (amount << 16) | (imms << 10) | (src << 5) | dst);
}

// Compare / test

void Emitter::cmp_reg(Reg a, Reg b) {
    // SUBS XZR, Xn, Xm
    emit32(0xEB00001Fu | (b << 16) | (a << 5));
}

void Emitter::cmp_imm(Reg a, uint16_t imm, uint8_t shift) {
    uint32_t sh = (shift == 12) ? 1u : 0u;
    emit32(0xF100001Fu | (sh << 22) | (imm << 10) | (a << 5));
}

void Emitter::cmn_reg(Reg a, Reg b) {
    // ADDS XZR, Xn, Xm
    emit32(0xAB00001Fu | (b << 16) | (a << 5));
}

void Emitter::tst_reg(Reg a, Reg b) {
    // ANDS XZR, Xn, Xm
    emit32(0xEA00001Fu | (b << 16) | (a << 5));
}

void Emitter::tst_imm(Reg a, uint64_t imm, uint8_t immr, uint8_t imms) {
    uint32_t N = 1u;
    emit32(0xF200001Fu | (N << 22) | (immr << 16) | (imms << 10) | (a << 5));
}

// Сonditional select / set

void Emitter::cset(Reg dst, Cond c) {
    // CSET Xd, cond = CINC Xd, XZR, invert(cond)
    // CINC: ADD Xd, XZR, Xm{, cond} => 0x1A800400 | Rm<<16 | cond<<12 | 0x1F<<5 | Rd
    uint32_t inv = invert_cond(c);
    emit32(0x1A9F07E0u | (static_cast<uint32_t>(XZR) << 16) | (inv << 12) | dst);
}

void Emitter::cinc(Reg dst, Reg src, Cond c) {
    uint32_t inv = invert_cond(c);
    emit32(0x1A800400u | (src << 16) | (inv << 12) | (static_cast<uint32_t>(XZR) << 5) | dst);
}

// branches

void Emitter::b(int32_t offset) {
    // B: imm26 * 4 = offset  =>  imm26 = offset >> 2
    uint32_t imm26 = static_cast<uint32_t>(offset >> 2) & 0x03FFFFFFu;
    emit32(0x14000000u | imm26);
}

void Emitter::bl(const std::string& sym) {
    // BL with placeholder; linker patches the displacement.
    // For now, emit B-like encoding; the fixup will patch imm26.
    reloc(sym, mc::RelType::AARCH64_CALL26, 0);
    emit32(0x94000000u); // imm26 = 0, patched later
}

void Emitter::bl_reg(Reg target) {
    // BLR Xn
    emit32(0xD63F0000u | (target << 5));
}

void Emitter::ret(Reg xn) {
    emit32(0xD65F0000u | (xn << 5));
}

void Emitter::cbz(Reg src, int32_t offset) {
    uint32_t imm19 = static_cast<uint32_t>(offset >> 2) & 0x7FFFFu;
    emit32(0xB4000000u | (imm19 << 5) | src);
}

void Emitter::cbnz(Reg src, int32_t offset) {
    uint32_t imm19 = static_cast<uint32_t>(offset >> 2) & 0x7FFFFu;
    emit32(0xB5000000u | (imm19 << 5) | src);
}

void Emitter::b_cond(Cond cond, int32_t offset) {
    uint32_t imm19 = static_cast<uint32_t>(offset >> 2) & 0x7FFFFu;
    emit32(0x54000000u | (imm19 << 5) | static_cast<uint32_t>(cond));
}

// Load / store (unsigned offset)

void Emitter::ldr_imm(Reg dst, Reg base, int16_t offset) {
    if (offset < 0) {
        ldur(dst, base, offset);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 8;
        emit32(0xF9400000u | (scaled << 10) | (base << 5) | dst);
    }
}

void Emitter::ldr32_imm(WReg dst, Reg base, int16_t offset) {
    if (offset < 0) {
        // LDUR Wd, [Xn, #offset]  (unscaled, 4-byte)
        uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
        emit32(0xB8400000u | (imm9 << 12) | (base << 5) | dst);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 4;
        emit32(0xB9400000u | (scaled << 10) | (base << 5) | dst);
    }
}

void Emitter::ldrb(Reg dst, Reg base, uint16_t offset) {
    // LDRB Xd, [Xn, #imm]
    emit32(0x39400000u | (offset << 10) | (base << 5) | dst);
}

void Emitter::ldrh(Reg dst, Reg base, uint16_t offset) {
    // LDRH Xd, [Xn, #imm]  (scaled, 2-byte)
    uint16_t scaled = offset / 2;
    emit32(0x79400000u | (scaled << 10) | (base << 5) | dst);
}

void Emitter::str_imm(Reg src, Reg base, int16_t offset) {
    if (offset < 0) {
        stur(src, base, offset);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 8;
        emit32(0xF9000000u | (scaled << 10) | (base << 5) | src);
    }
}

void Emitter::str32_imm(WReg src, Reg base, int16_t offset) {
    if (offset < 0) {
        // STUR Wn, [Xm, #offset]  (unscaled, 4-byte)
        uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
        emit32(0xB8000000u | (imm9 << 12) | (base << 5) | src);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 4;
        emit32(0xB9000000u | (scaled << 10) | (base << 5) | src);
    }
}

void Emitter::strb(Reg src, Reg base, uint16_t offset) {
    emit32(0x39000000u | (offset << 10) | (base << 5) | src);
}

void Emitter::strh(Reg src, Reg base, uint16_t offset) {
    uint16_t scaled = offset / 2;
    emit32(0x79000000u | (scaled << 10) | (base << 5) | src);
}

// Load / store (signed offset, unscaled)

void Emitter::ldur(Reg dst, Reg base, int16_t offset) {
    // LDUR Xd, [Xn, #offset]
    uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
    emit32(0xF8400000u | (imm9 << 12) | (base << 5) | dst);
}

void Emitter::stur(Reg src, Reg base, int16_t offset) {
    uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
    emit32(0xF8000000u | (imm9 << 12) | (base << 5) | src);
}

// load / store pair

void Emitter::ldp(Reg r1, Reg r2, Reg base, int16_t offset) {
    // LDP X1, X2, [Xn, #offset]  (signed, ±512, 8-byte scaled)
    int16_t scaled = offset / 8;
    uint32_t imm7 = static_cast<uint32_t>(scaled) & 0x7Fu;
    emit32(0xA9400000u | (imm7 << 15) | (r2 << 10) | (base << 5) | r1);
}

void Emitter::stp(Reg r1, Reg r2, Reg base, int16_t offset) {
    int16_t scaled = offset / 8;
    uint32_t imm7 = static_cast<uint32_t>(scaled) & 0x7Fu;
    emit32(0xA9000000u | (imm7 << 15) | (r2 << 10) | (base << 5) | r1);
}

void Emitter::stp_pre(Reg r1, Reg r2, Reg base, int16_t offset) {
    int16_t scaled = offset / 8;
    uint32_t imm7 = static_cast<uint32_t>(scaled) & 0x7Fu;
    emit32(0xA9800000u | (imm7 << 15) | (r2 << 10) | (base << 5) | r1);
}

void Emitter::ldp_post(Reg r1, Reg r2, Reg base, int16_t offset) {
    int16_t scaled = offset / 8;
    uint32_t imm7 = static_cast<uint32_t>(scaled) & 0x7Fu;
    emit32(0xA8C00000u | (imm7 << 15) | (r2 << 10) | (base << 5) | r1);
}

void Emitter::ldp32(WReg r1, WReg r2, Reg base, int16_t offset) {
    int16_t scaled = offset / 4;
    uint32_t imm7 = static_cast<uint32_t>(scaled) & 0x7Fu;
    emit32(0x29400000u | (imm7 << 15) | (r2 << 10) | (base << 5) | r1);
}

void Emitter::stp32(WReg r1, WReg r2, Reg base, int16_t offset) {
    int16_t scaled = offset / 4;
    uint32_t imm7 = static_cast<uint32_t>(scaled) & 0x7Fu;
    emit32(0x29000000u | (imm7 << 15) | (r2 << 10) | (base << 5) | r1);
}

// Load (register offset)

void Emitter::ldr_reg(Reg dst, Reg base, Reg index, uint8_t scale) {
    // LDR Xd, [Xn, Xm, LSL #scale]
    // opc=10, V=0, size=11 => LDR 64-bit
    // Rm=index(16), option=011 (LSL), S=scale(12), Rn=base(5), Rt=dst(0)
    uint32_t S = (scale >= 3) ? 1u : 0u;
    emit32(0xF8600800u | (index << 16) | (S << 12) | (base << 5) | dst);
}

// ADRP / ADR

void Emitter::adrp(Reg dst, int32_t page_offset) {
    // ADRP Xd, #page_offset
    // page_offset is already the offset to the target page from current page
    uint32_t immhi = static_cast<uint32_t>((page_offset >> 2) >> 19) & 0x7FFFFu;
    uint32_t immlo = static_cast<uint32_t>((page_offset >> 2)) & 0x3u;
    emit32(0x90000000u | (immhi << 5) | (immlo << 29) | dst);
}

void Emitter::adr(Reg dst, int32_t offset) {
    // ADR Xd, #offset
    uint32_t immhi = static_cast<uint32_t>(offset >> 2 >> 19) & 0x7FFFFu;
    uint32_t immlo = static_cast<uint32_t>(offset >> 2) & 0x3u;
    emit32(0x10000000u | (immhi << 5) | (immlo << 29) | dst);
}

// Sign / zero extend

void Emitter::sxtw(Reg dst, WReg src) {
    // SXTW Xd, Wn = SBFM Xd, Xn, #0, #31
    emit32(0x93407C00u | (src << 5) | dst);
}

void Emitter::sxth(Reg dst, WReg src) {
    // SXTH Xd, Wn = SBFM Xd, Xn, #0, #15
    emit32(0x93403C00u | (src << 5) | dst);
}

void Emitter::sxtb(Reg dst, WReg src) {
    // SXTB Xd, Wn = SBFM Xd, Xn, #0, #7
    emit32(0x93401C00u | (src << 5) | dst);
}

void Emitter::uxtb(Reg dst, WReg src) {
    // UXTB Wd, Wn = UBFM Wd, Wn, #0, #7
    emit32(0x53001C00u | (src << 5) | dst);
}

void Emitter::uxth(Reg dst, WReg src) {
    // UXTH Wd, Wn = UBFM Wd, Wn, #0, #15
    emit32(0x53003C00u | (src << 5) | dst);
}

// FP

void Emitter::fmov_d(DReg dst, DReg src) {
    emit32(0x1E602000u | (src << 16) | dst);
}

void Emitter::fmov_s(SReg dst, SReg src) {
    emit32(0x1E202000u | (src << 16) | dst);
}

void Emitter::fadd_d(DReg dst, DReg a, DReg b) {
    emit32(0x1E602800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fsub_d(DReg dst, DReg a, DReg b) {
    emit32(0x1E603800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fmul_d(DReg dst, DReg a, DReg b) {
    emit32(0x1E600800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fdiv_d(DReg dst, DReg a, DReg b) {
    emit32(0x1E601800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fadd_s(SReg dst, SReg a, SReg b) {
    emit32(0x1E202800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fsub_s(SReg dst, SReg a, SReg b) {
    emit32(0x1E203800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fmul_s(SReg dst, SReg a, SReg b) {
    emit32(0x1E200800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fdiv_s(SReg dst, SReg a, SReg b) {
    emit32(0x1E201800u | (b << 16) | (a << 10) | dst);
}

void Emitter::fcmp_d(DReg a, DReg b) {
    // FCMP Dn, Dm
    emit32(0x1E602010u | (b << 16) | (a << 5));
}

void Emitter::fcmp_s(SReg a, SReg b) {
    emit32(0x1E202010u | (b << 16) | (a << 5));
}

void Emitter::ldr_d(DReg dst, Reg base, int16_t offset) {
    if (offset < 0) {
        // LDUR Dt, [Xn, #offset]  (unscaled)
        uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
        emit32(0xFC400000u | (imm9 << 12) | (base << 5) | dst);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 8;
        emit32(0xFD400000u | (scaled << 10) | (base << 5) | dst);
    }
}

void Emitter::str_d(DReg src, Reg base, int16_t offset) {
    if (offset < 0) {
        uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
        emit32(0xFC000000u | (imm9 << 12) | (base << 5) | src);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 8;
        emit32(0xFD000000u | (scaled << 10) | (base << 5) | src);
    }
}

void Emitter::ldr_s(SReg dst, Reg base, int16_t offset) {
    if (offset < 0) {
        // LDUR St, [Xn, #offset]  (unscaled)
        uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
        emit32(0xBC400000u | (imm9 << 12) | (base << 5) | dst);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 4;
        emit32(0xBD400000u | (scaled << 10) | (base << 5) | dst);
    }
}

void Emitter::str_s(SReg src, Reg base, int16_t offset) {
    if (offset < 0) {
        uint32_t imm9 = static_cast<uint32_t>(offset) & 0x1FFu;
        emit32(0xBC000000u | (imm9 << 12) | (base << 5) | src);
    } else {
        uint16_t scaled = static_cast<uint16_t>(offset) / 4;
        emit32(0xBD000000u | (scaled << 10) | (base << 5) | src);
    }
}

// FP conversion

void Emitter::scvtf_d(Reg src, DReg dst) {
    // SCVTF Dd, Xn  (integer to double)
    emit32(0x9E220000u | (src << 5) | dst);
}

void Emitter::scvtf_s(Reg src, SReg dst) {
    // SCVTF Sd, Xn  (integer to float)
    emit32(0x9E210000u | (src << 5) | dst);
}

void Emitter::fcvtzs_d(DReg src, Reg dst) {
    // FCVTZS Xd, Dn  (double to integer)
    emit32(0x9E780000u | (src << 5) | dst);
}

void Emitter::fcvtzs_s(SReg src, Reg dst) {
    // FCVTZS Xd, Sn  (float to integer)
    emit32(0x9E380000u | (src << 5) | dst);
}

void Emitter::fcvt_d_s(SReg src, DReg dst) {
    // FCVT Dd, Sn  (float to double)
    emit32(0x1E22C000u | (src << 5) | dst);
}

void Emitter::fcvt_s_d(DReg src, SReg dst) {
    // FCVT Sd, Dn  (double to float)
    emit32(0x1E624000u | (src << 5) | dst);
}

void Emitter::fmov_d_reg(Reg src, DReg dst) {
    // FMOV Dd, Xn
    emit32(0x9E670000u | (src << 5) | dst);
}

void Emitter::fmov_reg_d(DReg src, Reg dst) {
    // FMOV Xd, Dn
    emit32(0x9E660000u | (src << 5) | dst);
}

void Emitter::fmov_s_reg(WReg src, SReg dst) {
    // FMOV Sd, Wn
    emit32(0x1E270000u | (src << 5) | dst);
}

void Emitter::fmov_reg_s(SReg src, WReg dst) {
    // FMOV Wd, Sn
    emit32(0x1E260000u | (src << 5) | dst);
}

// System

void Emitter::svc(uint16_t imm) {
    emit32(0xD4000001u | (imm << 5));
}

void Emitter::nop() {
    emit32(0xD503201Fu);
}

void Emitter::wfe() {
    // WFE = HINT #2
    emit32(0xD503205Fu);
}

} // namespace quant::codegen::aarch64
