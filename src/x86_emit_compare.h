#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include "x86_emit.h"

namespace cerfengine {

namespace x86 {

/* CMP r/m32, imm8 (sign-extended) - 83 /7 ib, register-direct
   (SDM Vol. 2A 3-179 CMP). */
inline void EmitCmpRegImm8(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    Emit8(c, 0x83);
    EmitModRmReg(c, 3, reg, 7);
    Emit8(c, imm8);
}

/* CMP r/m32, imm32 - 81 /7 id, register-direct (SDM Vol. 2A 3-179 CMP). */
inline void EmitCmpRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    Emit8(c, 0x81);
    EmitModRmReg(c, 3, reg, 7);
    Emit32(c, imm);
}

/* CMP r32, r/m32 - 3B /r mod=10 (SDM Vol. 2A 3-179 CMP). */
inline void EmitCmpRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    Emit8(c, 0x3B);
    EmitModRmReg(c, 2, base, reg);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* CMP r32, [disp32] - 3B /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2A 3-179 CMP; Vol. 2A Table 2-2 note 2). */
inline void EmitCmpRegDwordPtr(uint8_t*& c, uint8_t reg, const void* addr) {
    Emit8(c, 0x3B);
    EmitModRmDisp32(c, reg);
    Emit32(c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
}

/* CMP r8, r/m8 - 3A /r mod=10 (SDM Vol. 2A 3-179 CMP). */
inline void EmitCmpReg8BaseDisp32(uint8_t*& c, uint8_t reg8, uint8_t base,
                                  int32_t disp) {
    Emit8(c, 0x3A);
    EmitModRmReg(c, 2, base, reg8);
    Emit32(c, static_cast<uint32_t>(disp));
}

/* TEST r/m32, r32 - 85 /r, register-direct (SDM Vol. 2B 4-711 TEST). */
inline void EmitTestRegReg(uint8_t*& c, uint8_t a, uint8_t b) {
    Emit8(c, 0x85);
    EmitModRmReg(c, 3, a, b);
}

/* TEST r/m32, imm32 - F7 /0 id, register-direct (SDM Vol. 2B 4-711 TEST). */
inline void EmitTestRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    Emit8(c, 0xF7);
    EmitModRmReg(c, 3, reg, 0);
    Emit32(c, imm);
}

/* TEST r/m32, imm32 - F7 /0 id mod=10 (SDM Vol. 2B 4-711 TEST). */
inline void EmitTestDwordBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                         uint32_t imm) {
    Emit8(c, 0xF7);
    EmitModRmReg(c, 2, base, 0);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit32(c, imm);
}

/* TEST r/m8, imm8 - F6 /0 ib mod=10 (SDM Vol. 2B 4-711 TEST). */
inline void EmitTestByteBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                       uint8_t imm8) {
    Emit8(c, 0xF6);
    EmitModRmReg(c, 2, base, 0);
    Emit32(c, static_cast<uint32_t>(disp));
    Emit8(c, imm8);
}

/* BT r/m32, r32 - 0F A3 /r, mod=00 [base] (SDM Vol. 2A 3-130 BT). */
inline void EmitBtMemReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    Emit8(c, 0x0F);
    Emit8(c, 0xA3);
    EmitModRmReg(c, 0, base, reg);
}

/* BTS r/m32, r32 - 0F AB /r, mod=00 [base] (SDM Vol. 2A 3-136 BTS). */
inline void EmitBtsMemReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    Emit8(c, 0x0F);
    Emit8(c, 0xAB);
    EmitModRmReg(c, 0, base, reg);
}

/* BT r/m32, imm8 - 0F BA /4 ib, register-direct; a register bit base
   takes the offset modulo 32 (SDM Vol. 2A 3-130 BT). */
inline void EmitBtRegImm8(uint8_t*& c, uint8_t reg, uint8_t bit) {
    Emit8(c, 0x0F);
    Emit8(c, 0xBA);
    EmitModRmReg(c, 3, reg, 4);
    Emit8(c, bit);
}

/* SETcc second opcode bytes - SDM Vol. 2B 4-618/4-619 SETcc. */
constexpr uint8_t kSetO  = 0x90;
constexpr uint8_t kSetC  = 0x92;
constexpr uint8_t kSetNc = 0x93;
constexpr uint8_t kSetZ  = 0x94;
constexpr uint8_t kSetNz = 0x95;
constexpr uint8_t kSetS  = 0x98;

/* SETcc r8 - 0F 90+cc, mod=11 register-direct; operand encoding M is
   ModRM:r/m alone, so the reg field is unused (SDM Vol. 2B 4-619). */
inline void EmitSetccReg8(uint8_t*& c, uint8_t setcc_opcode, uint8_t reg8) {
    Emit8(c, 0x0F);
    Emit8(c, setcc_opcode);
    EmitModRmReg(c, 3, reg8, 0);
}

/* SETcc r/m8 - 0F 90+cc, mod=10 [base+disp32]. Operand encoding M is
   ModRM:r/m alone, so the reg field is unused, and "the destination operand
   points to a byte register or a byte in memory" (SDM Vol. 2B 4-619). */
inline void EmitSetccBaseDisp32(uint8_t*& c, uint8_t setcc_opcode,
                                uint8_t base, int32_t disp) {
    Emit8(c, 0x0F);
    Emit8(c, setcc_opcode);
    EmitModRmReg(c, 2, base, 0);
    Emit32(c, static_cast<uint32_t>(disp));
}

}

}
