#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include "x86_emit.h"

namespace cerfengine {

namespace x86 {

/* CMP r/m32, imm8 (sign-extended) - 83 /7 ib, register-direct
   (SDM Vol. 2A 3-179 CMP). */
CERFENGINE_FORCEINLINE void EmitCmpRegImm8(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0x83);
    EmitModRmReg(p, 3, reg, 7);
    Emit8(p, imm8);
    c = p;
}

/* CMP r/m32, imm32 - 81 /7 id, register-direct (SDM Vol. 2A 3-179 CMP). */
CERFENGINE_FORCEINLINE void EmitCmpRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 7);
    Emit32(p, imm);
    c = p;
}

/* CMP r32, r/m32 - 3B /r, register-direct (SDM Vol. 2A 3-179 CMP). */
CERFENGINE_FORCEINLINE void EmitCmpReg32Reg32(uint8_t*& c, uint8_t a, uint8_t b) {
    uint8_t* p = c;
    Emit8(p, 0x3B);
    EmitModRmReg(p, 3, b, a);
    c = p;
}

/* CMP r32, r/m32 - 3B /r mod=10 (SDM Vol. 2A 3-179 CMP). */
CERFENGINE_FORCEINLINE void EmitCmpRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x3B);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* CMP r32, [disp32] - 3B /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2A 3-179 CMP; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitCmpRegDwordPtr(uint8_t*& c, uint8_t reg, const void* addr) {
    uint8_t* p = c;
    Emit8(p, 0x3B);
    EmitModRmDisp32(p, reg);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    c = p;
}

/* CMP r8, r/m8 - 3A /r mod=10 (SDM Vol. 2A 3-179 CMP). */
CERFENGINE_FORCEINLINE void EmitCmpReg8BaseDisp32(uint8_t*& c, uint8_t reg8, uint8_t base,
                                  int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x3A);
    EmitModRmReg(p, 2, base, reg8);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* TEST r/m32, r32 - 85 /r, register-direct (SDM Vol. 2B 4-711 TEST). */
CERFENGINE_FORCEINLINE void EmitTestRegReg(uint8_t*& c, uint8_t a, uint8_t b) {
    uint8_t* p = c;
    Emit8(p, 0x85);
    EmitModRmReg(p, 3, a, b);
    c = p;
}

/* TEST r/m8, r8 - 84 /r, register-direct (SDM Vol. 2B 4-711 TEST). */
CERFENGINE_FORCEINLINE void EmitTestReg8Reg8(uint8_t*& c, uint8_t a, uint8_t b) {
    uint8_t* p = c;
    Emit8(p, 0x84);
    EmitModRmReg(p, 3, a, b);
    c = p;
}

/* TEST r/m32, imm32 - F7 /0 id, register-direct (SDM Vol. 2B 4-711 TEST). */
CERFENGINE_FORCEINLINE void EmitTestRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 3, reg, 0);
    Emit32(p, imm);
    c = p;
}

/* TEST r/m32, imm32 - F7 /0 id mod=10 (SDM Vol. 2B 4-711 TEST). */
CERFENGINE_FORCEINLINE void EmitTestDwordBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                         uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit32(p, imm);
    c = p;
}

/* TEST r/m8, imm8 - F6 /0 ib mod=10 (SDM Vol. 2B 4-711 TEST). */
CERFENGINE_FORCEINLINE void EmitTestByteBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                       uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xF6);
    EmitModRmReg(p, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit8(p, imm8);
    c = p;
}

/* TEST r/m32, imm32 - F7 /0 id, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-711 TEST; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitTestDwordPtrImm32(uint8_t*& c, const void* addr, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmDisp32(p, 0);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    Emit32(p, imm);
    c = p;
}

/* TEST r/m8, imm8 - F6 /0 ib, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-711 TEST; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitTestBytePtrImm8(uint8_t*& c, const void* addr, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xF6);
    EmitModRmDisp32(p, 0);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    Emit8(p, imm8);
    c = p;
}

/* CMOVcc r32, r/m32 - 0F 40+cc /r, register-direct (SDM Vol. 2A 3-175 CMOVcc). */
CERFENGINE_FORCEINLINE void EmitCmovccRegReg(uint8_t*& c, uint8_t cc, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, static_cast<uint8_t>(0x40u + cc));
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* BT r/m32, r32 - 0F A3 /r, mod=00 [base] (SDM Vol. 2A 3-130 BT). */
CERFENGINE_FORCEINLINE void EmitBtMemReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xA3);
    EmitModRmReg(p, 0, base, reg);
    c = p;
}

/* BTS r/m32, r32 - 0F AB /r, mod=00 [base] (SDM Vol. 2A 3-136 BTS). */
CERFENGINE_FORCEINLINE void EmitBtsMemReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xAB);
    EmitModRmReg(p, 0, base, reg);
    c = p;
}

/* BT r/m32, imm8 - 0F BA /4 ib, register-direct; a register bit base
   takes the offset modulo 32 (SDM Vol. 2A 3-130 BT). */
CERFENGINE_FORCEINLINE void EmitBtRegImm8(uint8_t*& c, uint8_t reg, uint8_t bit) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBA);
    EmitModRmReg(p, 3, reg, 4);
    Emit8(p, bit);
    c = p;
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
CERFENGINE_FORCEINLINE void EmitSetccReg8(uint8_t*& c, uint8_t setcc_opcode, uint8_t reg8) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, setcc_opcode);
    EmitModRmReg(p, 3, reg8, 0);
    c = p;
}

/* SETcc r/m8 - 0F 90+cc, mod=10 [base+disp32]. Operand encoding M is
   ModRM:r/m alone, so the reg field is unused, and "the destination operand
   points to a byte register or a byte in memory" (SDM Vol. 2B 4-619). */
CERFENGINE_FORCEINLINE void EmitSetccBaseDisp32(uint8_t*& c, uint8_t setcc_opcode,
                                uint8_t base, int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, setcc_opcode);
    EmitModRmReg(p, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

}

}
