#pragma once

#include <cstdint>

#include "a64_emit.h"

namespace cerfengine::a64 {

enum Shift : uint8_t { kLsl = 0, kLsr = 1, kAsr = 2, kRor = 3 };

[[noreturn]] void ImmediateFault(const char* what, uint64_t value);

/* ARM DDI 0487 M.d C6.2.5 ADD (immediate), C6.2.457 SUB (immediate), C6.2.10 ADDS (immediate),
   C6.2.464 SUBS (immediate): sf op S 100010 sh imm12 Rn Rd. */
CERFENGINE_FORCEINLINE bool AddSubImmFits(uint32_t imm) {
    return imm < 0x1000u || ((imm & 0xFFFu) == 0u && imm < 0x1000000u);
}

CERFENGINE_FORCEINLINE void EmitAddSubImm(uint8_t*& c, uint32_t base, uint8_t rd, uint8_t rn,
                                          uint32_t imm, bool x64) {
    if (!AddSubImmFits(imm)) ImmediateFault("an add/sub immediate", imm);
    const uint32_t sh = imm < 0x1000u ? 0u : 1u;
    Emit32(c, base | Sf(x64) | (sh << 22) | ((sh != 0u ? imm >> 12 : imm) << 10) | (rn << 5) | rd);
}

CERFENGINE_FORCEINLINE void EmitAddImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t imm, bool x64 = false) {
    EmitAddSubImm(c, 0x11000000u, rd, rn, imm, x64);
}
CERFENGINE_FORCEINLINE void EmitSubImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t imm, bool x64 = false) {
    EmitAddSubImm(c, 0x51000000u, rd, rn, imm, x64);
}
CERFENGINE_FORCEINLINE void EmitAddsImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t imm, bool x64 = false) {
    EmitAddSubImm(c, 0x31000000u, rd, rn, imm, x64);
}
CERFENGINE_FORCEINLINE void EmitSubsImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t imm, bool x64 = false) {
    EmitAddSubImm(c, 0x71000000u, rd, rn, imm, x64);
}
CERFENGINE_FORCEINLINE void EmitCmpImm(uint8_t*& c, uint8_t rn, uint32_t imm, bool x64 = false) {
    EmitSubsImm(c, kZr, rn, imm, x64);
}

/* ARM DDI 0487 M.d C6.2.6 ADD (shifted register), C6.2.458 SUB (shifted register),
   C6.2.11 ADDS (shifted register), C6.2.465 SUBS (shifted register):
   sf op S 01011 shift 0 Rm imm6 Rn Rd. */
CERFENGINE_FORCEINLINE void EmitAddSubReg(uint8_t*& c, uint32_t base, uint8_t rd, uint8_t rn,
                                          uint8_t rm, Shift shift, uint32_t amount, bool x64) {
    if (shift == kRor || amount >= (x64 ? 64u : 32u)) ImmediateFault("an add/sub register shift", amount);
    Emit32(c, base | Sf(x64) | (uint32_t{shift} << 22) | (uint32_t{rm} << 16) | (amount << 10) |
                  (rn << 5) | rd);
}

CERFENGINE_FORCEINLINE void EmitAddReg(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm,
                                       Shift shift = kLsl, uint32_t amount = 0u, bool x64 = false) {
    EmitAddSubReg(c, 0x0B000000u, rd, rn, rm, shift, amount, x64);
}
CERFENGINE_FORCEINLINE void EmitSubReg(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm,
                                       Shift shift = kLsl, uint32_t amount = 0u, bool x64 = false) {
    EmitAddSubReg(c, 0x4B000000u, rd, rn, rm, shift, amount, x64);
}
CERFENGINE_FORCEINLINE void EmitAddsReg(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm,
                                        Shift shift = kLsl, uint32_t amount = 0u, bool x64 = false) {
    EmitAddSubReg(c, 0x2B000000u, rd, rn, rm, shift, amount, x64);
}
CERFENGINE_FORCEINLINE void EmitSubsReg(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm,
                                        Shift shift = kLsl, uint32_t amount = 0u, bool x64 = false) {
    EmitAddSubReg(c, 0x6B000000u, rd, rn, rm, shift, amount, x64);
}
CERFENGINE_FORCEINLINE void EmitCmpReg(uint8_t*& c, uint8_t rn, uint8_t rm, bool x64 = false) {
    EmitSubsReg(c, kZr, rn, rm, kLsl, 0u, x64);
}
CERFENGINE_FORCEINLINE void EmitNeg(uint8_t*& c, uint8_t rd, uint8_t rm, bool x64 = false) {
    EmitSubReg(c, rd, kZr, rm, kLsl, 0u, x64);
}

/* ARM DDI 0487 M.d C6.2.4 ADD (extended register): sf 0 0 01011 00 1 Rm option imm3 Rn Rd;
   option 010 is UXTW. */
CERFENGINE_FORCEINLINE void EmitAddUxtw(uint8_t*& c, uint8_t xd, uint8_t xn, uint8_t wm) {
    Emit32(c, 0x8B200000u | (uint32_t{wm} << 16) | (0x2u << 13) | (uint32_t{xn} << 5) | xd);
}

/* ARM DDI 0487 M.d C6.2.2 ADC, C6.2.3 ADCS, C6.2.352 SBC, C6.2.353 SBCS:
   sf op S 11010000 Rm 000000 Rn Rd. */
CERFENGINE_FORCEINLINE void EmitCarryOp(uint8_t*& c, uint32_t base, uint8_t rd, uint8_t rn,
                                        uint8_t rm, bool x64) {
    Emit32(c, base | Sf(x64) | (uint32_t{rm} << 16) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitAdc(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, bool x64 = false) {
    EmitCarryOp(c, 0x1A000000u, rd, rn, rm, x64);
}
CERFENGINE_FORCEINLINE void EmitAdcs(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, bool x64 = false) {
    EmitCarryOp(c, 0x3A000000u, rd, rn, rm, x64);
}
CERFENGINE_FORCEINLINE void EmitSbc(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, bool x64 = false) {
    EmitCarryOp(c, 0x5A000000u, rd, rn, rm, x64);
}
CERFENGINE_FORCEINLINE void EmitSbcs(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, bool x64 = false) {
    EmitCarryOp(c, 0x7A000000u, rd, rn, rm, x64);
}

/* ARM DDI 0487 M.d C6.2.15 AND, C6.2.302 ORR, C6.2.156 EOR, C6.2.17 ANDS, C6.2.41 BIC,
   C6.2.42 BICS, C6.2.300 ORN, C6.2.154 EON (shifted register): sf opc 01010 shift N Rm imm6 Rn Rd. */
enum LogicOp : uint32_t {
    kAnd  = 0x0A000000u,
    kOrr  = 0x2A000000u,
    kEor  = 0x4A000000u,
    kAnds = 0x6A000000u,
    kBic  = 0x0A200000u,
    kBics = 0x6A200000u,
    kOrn  = 0x2A200000u,
    kEon  = 0x4A200000u,
};

CERFENGINE_FORCEINLINE void EmitLogicReg(uint8_t*& c, LogicOp op, uint8_t rd, uint8_t rn, uint8_t rm,
                                         Shift shift = kLsl, uint32_t amount = 0u, bool x64 = false) {
    if (amount >= (x64 ? 64u : 32u)) ImmediateFault("a logical register shift", amount);
    Emit32(c, uint32_t{op} | Sf(x64) | (uint32_t{shift} << 22) | (uint32_t{rm} << 16) |
                  (amount << 10) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitMovReg(uint8_t*& c, uint8_t rd, uint8_t rm, bool x64 = false) {
    EmitLogicReg(c, kOrr, rd, kZr, rm, kLsl, 0u, x64);
}
CERFENGINE_FORCEINLINE void EmitMvn(uint8_t*& c, uint8_t rd, uint8_t rm, bool x64 = false) {
    EmitLogicReg(c, kOrn, rd, kZr, rm, kLsl, 0u, x64);
}
CERFENGINE_FORCEINLINE void EmitTstReg(uint8_t*& c, uint8_t rn, uint8_t rm, bool x64 = false) {
    EmitLogicReg(c, kAnds, kZr, rn, rm, kLsl, 0u, x64);
}

/* ARM DDI 0487 M.d C6.2.14 AND, C6.2.301 ORR, C6.2.155 EOR, C6.2.16 ANDS (immediate):
   sf opc 100100 N immr imms Rn Rd. The op argument is the register form; the immediate
   form keeps its opc and moves to class 100100. */
CERFENGINE_FORCEINLINE bool LogicImmFits(uint64_t value, bool x64) {
    uint32_t fields = 0u;
    return EncodeLogicalImmediate(value, x64, fields);
}

CERFENGINE_FORCEINLINE void EmitLogicImm(uint8_t*& c, LogicOp op, uint8_t rd, uint8_t rn,
                                         uint64_t value, bool x64 = false) {
    uint32_t fields = 0u;
    if ((uint32_t{op} & 0x00200000u) != 0u || !EncodeLogicalImmediate(value, x64, fields)) {
        ImmediateFault("a logical immediate", value);
    }
    const uint32_t opc = (uint32_t{op} >> 29) & 3u;
    Emit32(c, 0x12000000u | Sf(x64) | (opc << 29) | ((fields >> 12) << 22) |
                  (((fields >> 6) & 0x3Fu) << 16) | ((fields & 0x3Fu) << 10) | (rn << 5) | rd);
}

/* ARM DDI 0487 M.d C6.2.271 LSLV, C6.2.274 LSRV, C6.2.21 ASRV, C6.2.349 RORV:
   sf 0 0 11010110 Rm 0010 op2 Rn Rd. */
CERFENGINE_FORCEINLINE void EmitShiftReg(uint8_t*& c, Shift shift, uint8_t rd, uint8_t rn, uint8_t rm,
                                         bool x64 = false) {
    Emit32(c, 0x1AC02000u | Sf(x64) | (uint32_t{rm} << 16) | (uint32_t{shift} << 10) | (rn << 5) | rd);
}

/* ARM DDI 0487 M.d C6.2.487 UBFM, C6.2.355 SBFM, C6.2.39 BFM: sf opc 100110 N immr imms Rn Rd;
   C6.2.160 EXTR: sf 00 100111 N 0 Rm imms Rn Rd. */
CERFENGINE_FORCEINLINE void EmitBitfield(uint8_t*& c, uint32_t base, uint8_t rd, uint8_t rn,
                                         uint32_t immr, uint32_t imms, bool x64) {
    Emit32(c, base | Sf(x64) | (x64 ? 0x00400000u : 0u) | (immr << 16) | (imms << 10) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitUbfm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t immr, uint32_t imms,
                                     bool x64 = false) {
    EmitBitfield(c, 0x53000000u, rd, rn, immr, imms, x64);
}
CERFENGINE_FORCEINLINE void EmitSbfm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t immr, uint32_t imms,
                                     bool x64 = false) {
    EmitBitfield(c, 0x13000000u, rd, rn, immr, imms, x64);
}
CERFENGINE_FORCEINLINE void EmitExtr(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, uint32_t lsb,
                                     bool x64 = false) {
    Emit32(c, 0x13800000u | Sf(x64) | (x64 ? 0x00400000u : 0u) | (uint32_t{rm} << 16) | (lsb << 10) |
                  (rn << 5) | rd);
}

CERFENGINE_FORCEINLINE void EmitLslImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t n, bool x64 = false) {
    const uint32_t size = x64 ? 64u : 32u;
    if (n == 0u || n >= size) ImmediateFault("a left shift amount", n);
    EmitUbfm(c, rd, rn, size - n, size - 1u - n, x64);
}
CERFENGINE_FORCEINLINE void EmitLsrImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t n, bool x64 = false) {
    const uint32_t size = x64 ? 64u : 32u;
    if (n >= size) ImmediateFault("a right shift amount", n);
    EmitUbfm(c, rd, rn, n, size - 1u, x64);
}
CERFENGINE_FORCEINLINE void EmitAsrImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t n, bool x64 = false) {
    const uint32_t size = x64 ? 64u : 32u;
    if (n >= size) ImmediateFault("an arithmetic shift amount", n);
    EmitSbfm(c, rd, rn, n, size - 1u, x64);
}
CERFENGINE_FORCEINLINE void EmitRorImm(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t n, bool x64 = false) {
    if (n >= (x64 ? 64u : 32u)) ImmediateFault("a rotate amount", n);
    EmitExtr(c, rd, rn, rn, n, x64);
}
CERFENGINE_FORCEINLINE void EmitUbfx(uint8_t*& c, uint8_t rd, uint8_t rn, uint32_t lsb, uint32_t width,
                                     bool x64 = false) {
    EmitUbfm(c, rd, rn, lsb, lsb + width - 1u, x64);
}
CERFENGINE_FORCEINLINE void EmitUxtb(uint8_t*& c, uint8_t rd, uint8_t rn) { EmitUbfm(c, rd, rn, 0u, 7u); }
CERFENGINE_FORCEINLINE void EmitUxth(uint8_t*& c, uint8_t rd, uint8_t rn) { EmitUbfm(c, rd, rn, 0u, 15u); }
CERFENGINE_FORCEINLINE void EmitSxtb(uint8_t*& c, uint8_t rd, uint8_t rn) { EmitSbfm(c, rd, rn, 0u, 7u); }
CERFENGINE_FORCEINLINE void EmitSxth(uint8_t*& c, uint8_t rd, uint8_t rn) { EmitSbfm(c, rd, rn, 0u, 15u); }

/* ARM DDI 0487 M.d C6.2.91 CLZ, C6.2.342 REV (32-bit). */
CERFENGINE_FORCEINLINE void EmitClz(uint8_t*& c, uint8_t rd, uint8_t rn) { Emit32(c, 0x5AC01000u | (rn << 5) | rd); }
CERFENGINE_FORCEINLINE void EmitRev(uint8_t*& c, uint8_t rd, uint8_t rn) { Emit32(c, 0x5AC00800u | (rn << 5) | rd); }

/* ARM DDI 0487 M.d C6.2.275 MADD, C6.2.291 MSUB: sf 00 11011 000 Rm o0 Ra Rn Rd;
   C6.2.369 SMADDL, C6.2.491 UMADDL: 1 00 11011 U 01 Rm 0 Ra Rn Rd. */
CERFENGINE_FORCEINLINE void EmitMadd(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, uint8_t ra,
                                     bool x64 = false) {
    Emit32(c, 0x1B000000u | Sf(x64) | (uint32_t{rm} << 16) | (uint32_t{ra} << 10) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitMsub(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, uint8_t ra,
                                     bool x64 = false) {
    Emit32(c, 0x1B008000u | Sf(x64) | (uint32_t{rm} << 16) | (uint32_t{ra} << 10) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitMul(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, bool x64 = false) {
    EmitMadd(c, rd, rn, rm, kZr, x64);
}
CERFENGINE_FORCEINLINE void EmitSmull(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm) {
    Emit32(c, 0x9B200000u | (uint32_t{rm} << 16) | (uint32_t{kZr} << 10) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitUmull(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm) {
    Emit32(c, 0x9BA00000u | (uint32_t{rm} << 16) | (uint32_t{kZr} << 10) | (rn << 5) | rd);
}

/* ARM DDI 0487 M.d C6.2.490 UDIV, C6.2.357 SDIV: "Dividing by zero writes the value zero". */
CERFENGINE_FORCEINLINE void EmitUdiv(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, bool x64 = false) {
    Emit32(c, 0x1AC00800u | Sf(x64) | (uint32_t{rm} << 16) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitSdiv(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, bool x64 = false) {
    Emit32(c, 0x1AC00C00u | Sf(x64) | (uint32_t{rm} << 16) | (rn << 5) | rd);
}

/* ARM DDI 0487 M.d C6.2.138 CSEL, C6.2.141 CSINC, C6.2.142 CSINV, C6.2.143 CSNEG:
   sf op 0 11010100 Rm cond 0 o2 Rn Rd. */
CERFENGINE_FORCEINLINE void EmitCondSelect(uint8_t*& c, uint32_t base, uint8_t rd, uint8_t rn, uint8_t rm,
                                           uint8_t cond, bool x64) {
    Emit32(c, base | Sf(x64) | (uint32_t{rm} << 16) | (uint32_t{cond} << 12) | (rn << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitCsel(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, uint8_t cond,
                                     bool x64 = false) {
    EmitCondSelect(c, 0x1A800000u, rd, rn, rm, cond, x64);
}
CERFENGINE_FORCEINLINE void EmitCsinc(uint8_t*& c, uint8_t rd, uint8_t rn, uint8_t rm, uint8_t cond,
                                      bool x64 = false) {
    EmitCondSelect(c, 0x1A800400u, rd, rn, rm, cond, x64);
}
CERFENGINE_FORCEINLINE void EmitCset(uint8_t*& c, uint8_t rd, uint8_t cond) {
    EmitCsinc(c, rd, kZr, kZr, InvertCond(cond));
}

/* ARM DDI 0487 M.d C6.2.82 CCMP (register): sf 1 1 11010010 Rm cond 0 0 Rn 0 nzcv. */
CERFENGINE_FORCEINLINE void EmitCcmpReg(uint8_t*& c, uint8_t rn, uint8_t rm, uint32_t nzcv, uint8_t cond,
                                        bool x64 = false) {
    Emit32(c, 0x7A400000u | Sf(x64) | (uint32_t{rm} << 16) | (uint32_t{cond} << 12) | (rn << 5) | nzcv);
}

}
