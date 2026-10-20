#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include "x86_emit.h"
#include "x86_emit_compare.h"

namespace cerfengine {

namespace x86 {

/* ADD r/m32, imm32 - 81 /0 id, register-direct (SDM Vol. 2A 3-32 ADD). */
CERFENGINE_FORCEINLINE void EmitAddRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 0);
    Emit32(p, imm);
    c = p;
}

/* ADD r32, r/m32 - 03 /r, register-direct (SDM Vol. 2A 3-32 ADD). */
CERFENGINE_FORCEINLINE void EmitAddReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x03);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* ADD r32, [base + disp32] - 03 /r mod=10 (SDM Vol. 2A 3-32 ADD). */
CERFENGINE_FORCEINLINE void EmitAddRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x03);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* ADD r/m32, imm8 (sign-extended) - 83 /0 ib mod=10
   (SDM Vol. 2A 3-32 ADD). */
CERFENGINE_FORCEINLINE void EmitAddBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0x83);
    EmitModRmReg(p, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit8(p, imm8);
    c = p;
}

/* ADD r/m32, imm32 - 81 /0 id mod=10 (SDM Vol. 2A 3-32 ADD). */
CERFENGINE_FORCEINLINE void EmitAddBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit32(p, imm);
    c = p;
}

/* ADC r/m32, imm8 (sign-extended) - 83 /2 ib mod=10
   (SDM Vol. 2A 3-27 ADC). */
CERFENGINE_FORCEINLINE void EmitAdcBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0x83);
    EmitModRmReg(p, 2, base, 2);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit8(p, imm8);
    c = p;
}

/* ADC r/m32, imm32 - 81 /2 id, register-direct (SDM Vol. 2A 3-27 ADC). */
CERFENGINE_FORCEINLINE void EmitAdcRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 2);
    Emit32(p, imm);
    c = p;
}

/* ADC r32, r/m32 - 13 /r, register-direct (SDM Vol. 2A 3-27 ADC). */
CERFENGINE_FORCEINLINE void EmitAdcReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x13);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* ADC r32, [base + disp32] - 13 /r mod=10 (SDM Vol. 2A 3-27 ADC). */
CERFENGINE_FORCEINLINE void EmitAdcRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x13);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* SUB r/m32, imm32 - 81 /5 id, register-direct (SDM Vol. 2B 4-681 SUB). */
CERFENGINE_FORCEINLINE void EmitSubRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 5);
    Emit32(p, imm);
    c = p;
}

/* SBB r/m32, imm32 - 81 /3 id, register-direct (SDM Vol. 2B 4-608 SBB). */
CERFENGINE_FORCEINLINE void EmitSbbRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 3);
    Emit32(p, imm);
    c = p;
}

/* SBB r32, r/m32 - 1B /r, register-direct (SDM Vol. 2B 4-608 SBB). */
CERFENGINE_FORCEINLINE void EmitSbbReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x1B);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* SUB r32, r/m32 - 2B /r, register-direct (SDM Vol. 2B 4-681 SUB). */
CERFENGINE_FORCEINLINE void EmitSubReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x2B);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* BSR r32, r/m32 - 0F BD /r, register-direct; "If the content source operand
   is 0, the content of the destination operand is undefined" (SDM Vol. 2A 3-127 BSR). */
CERFENGINE_FORCEINLINE void EmitBsrReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBD);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* SUB r32, [base + disp32] - 2B /r mod=10 (SDM Vol. 2B 4-681 SUB). */
CERFENGINE_FORCEINLINE void EmitSubRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x2B);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* SBB r32, [base + disp32] - 1B /r mod=10 (SDM Vol. 2B 4-608 SBB). */
CERFENGINE_FORCEINLINE void EmitSbbRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x1B);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* AND r/m32, imm32 - 81 /4 id, register-direct (SDM Vol. 2A 3-78 AND). */
CERFENGINE_FORCEINLINE void EmitAndRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 4);
    Emit32(p, imm);
    c = p;
}

/* AND r32, r/m32 - 23 /r, register-direct (SDM Vol. 2A 3-78 AND). */
CERFENGINE_FORCEINLINE void EmitAndReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x23);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* AND r32, r/m32 - 23 /r mod=10 (SDM Vol. 2A 3-78 AND). */
CERFENGINE_FORCEINLINE void EmitAndRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x23);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* AND r/m32, imm32 - 81 /4 id, mod=10 (SDM Vol. 2A 3-78 AND). */
CERFENGINE_FORCEINLINE void EmitAndBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                   uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 2, base, 4);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit32(p, imm);
    c = p;
}

/* OR r32, r/m32 - 0B /r, register-direct (SDM Vol. 2B 4-172 OR). */
CERFENGINE_FORCEINLINE void EmitOrReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x0B);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* OR r32, [base + disp32] - 0B /r mod=10 (SDM Vol. 2B 4-172 OR). */
CERFENGINE_FORCEINLINE void EmitOrRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x0B);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* OR r/m32, imm32 - 81 /1 id, mod=10 (SDM Vol. 2B 4-172 OR). */
CERFENGINE_FORCEINLINE void EmitOrBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 2, base, 1);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit32(p, imm);
    c = p;
}

/* OR r/m32, imm32 - 81 /1 id, register-direct (SDM Vol. 2B 4-172 OR). */
CERFENGINE_FORCEINLINE void EmitOrRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 1);
    Emit32(p, imm);
    c = p;
}

/* XOR r32, r/m32 - 33 /r, register-direct (SDM Vol. 2D 6-36 XOR). */
CERFENGINE_FORCEINLINE void EmitXorRegReg(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x33);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* XOR r32, r/m32 - 33 /r mod=10 (SDM Vol. 2D 6-36 XOR). */
CERFENGINE_FORCEINLINE void EmitXorRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x33);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* XOR r/m32, imm32 - 81 /6 id, register-direct (SDM Vol. 2D 6-36 XOR). */
CERFENGINE_FORCEINLINE void EmitXorRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 6);
    Emit32(p, imm);
    c = p;
}

/* MOV r/m8, imm8 - C6 /0 ib, mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovByteBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                      uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xC6);
    EmitModRmReg(p, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit8(p, imm8);
    c = p;
}

/* MOV r8, r/m8 - 8A /r, mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovByteRegBaseDisp32(uint8_t*& c, uint8_t reg8, uint8_t base,
                                     int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x8A);
    EmitModRmReg(p, 2, base, reg8);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* CMP r/m8, imm8 - 80 /7 ib, mod=10 (SDM Vol. 2A 3-179 CMP). */
CERFENGINE_FORCEINLINE void EmitCmpByteBaseDisp32Imm8(uint8_t*& c, uint8_t base, int32_t disp,
                                      uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0x80);
    EmitModRmReg(p, 2, base, 7);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit8(p, imm8);
    c = p;
}

/* CMP r/m8, imm8 - 80 /7 ib, register-direct (SDM Vol. 2A 3-179 CMP). */
CERFENGINE_FORCEINLINE void EmitCmpReg8Imm8(uint8_t*& c, uint8_t reg8, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0x80);
    EmitModRmReg(p, 3, reg8, 7);
    Emit8(p, imm8);
    c = p;
}

/* XOR r8, r/m8 - 32 /r, mod=10 (SDM Vol. 2D 6-36 XOR). */
CERFENGINE_FORCEINLINE void EmitXorByteRegBaseDisp32(uint8_t*& c, uint8_t reg8, uint8_t base,
                                     int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x32);
    EmitModRmReg(p, 2, base, reg8);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* SUB r8, r/m8 - 2A /r, mod=10 (SDM Vol. 2B 4-681 SUB). */
CERFENGINE_FORCEINLINE void EmitSubByteRegBaseDisp32(uint8_t*& c, uint8_t reg8, uint8_t base,
                                     int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x2A);
    EmitModRmReg(p, 2, base, reg8);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* OR r8, r/m8 - 0A /r, mod=10 (SDM Vol. 2B 4-172 OR). */
CERFENGINE_FORCEINLINE void EmitOrByteRegBaseDisp32(uint8_t*& c, uint8_t reg8, uint8_t base,
                                    int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x0A);
    EmitModRmReg(p, 2, base, reg8);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* BSWAP r32 - 0F C8+rd (SDM Vol. 2A 3-129 BSWAP). */
CERFENGINE_FORCEINLINE void EmitBswapReg32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, static_cast<uint8_t>(0xC8 + reg));
    c = p;
}

/* NOT r/m32 - F7 /2, mod=10 (SDM Vol. 2B 4-170 NOT). */
CERFENGINE_FORCEINLINE void EmitNotBaseDisp32(uint8_t*& c, uint8_t base, int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 2, base, 2);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* NOT r/m32 - F7 /2, register-direct, "Flags Affected: None"
   (SDM Vol. 2B 4-170 NOT). */
CERFENGINE_FORCEINLINE void EmitNotReg32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 3, reg, 2);
    c = p;
}

/* NEG r/m32 - F7 /3, register-direct: "The CF flag set to 0 if the source
   operand is 0; otherwise it is set to 1" (SDM Vol. 2B 4-167 NEG). */
CERFENGINE_FORCEINLINE void EmitNegReg32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 3, reg, 3);
    c = p;
}

/* MUL r/m32 - F7 /4, register-direct, EDX:EAX := EAX * r/m32 unsigned
   (SDM Vol. 2B 4-150 MUL). */
CERFENGINE_FORCEINLINE void EmitMulReg32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 3, reg, 4);
    c = p;
}

/* IMUL r/m32 - F7 /5, register-direct, EDX:EAX := EAX * r/m32 signed
   (SDM Vol. 2A 3-500 IMUL). */
CERFENGINE_FORCEINLINE void EmitImulReg32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 3, reg, 5);
    c = p;
}

/* DIV r/m32 - F7 /6, register-direct, EAX := EDX:EAX / r/m32 unsigned, EDX :=
   remainder (SDM Vol. 2A 3-321 DIV). */
CERFENGINE_FORCEINLINE void EmitDivReg32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 3, reg, 6);
    c = p;
}

/* IDIV r/m32 - F7 /7, register-direct, EAX := EDX:EAX / r/m32 signed, EDX :=
   remainder (SDM Vol. 2A 3-497 IDIV). */
CERFENGINE_FORCEINLINE void EmitIdivReg32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xF7);
    EmitModRmReg(p, 3, reg, 7);
    c = p;
}

/* IMUL r32, r/m32 - 0F AF /r, register-direct, dst := low doubleword of
   the product (SDM Vol. 2A 3-500 IMUL). */
CERFENGINE_FORCEINLINE void EmitImulReg32Reg32(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xAF);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* IMUL r32, r/m32, imm32 - 69 /r id, mod=11 register-direct; operand
   encoding RMI is ModRM:reg (destination), ModRM:r/m (source), imm32, and
   the product is truncated to the destination width (SDM Vol. 2A 3-500). */
CERFENGINE_FORCEINLINE void EmitImulReg32Reg32Imm32(uint8_t*& c, uint8_t dst, uint8_t src,
                                    uint32_t imm32) {
    uint8_t* p = c;
    Emit8(p, 0x69);
    EmitModRmReg(p, 3, src, dst);
    Emit32(p, imm32);
    c = p;
}

/* CDQ - 99, EDX:EAX = sign-extend of EAX (SDM Vol. 2A 3-314 CWD/CDQ/CQO). */
CERFENGINE_FORCEINLINE void EmitCdq(uint8_t*& c) {
    uint8_t* p = c;
    Emit8(p, 0x99);
    c = p;
}

/* CMC - F5, CF := NOT CF (SDM Vol. 2A 3-174 CMC). */
CERFENGINE_FORCEINLINE void EmitCmc(uint8_t*& c) {
    uint8_t* p = c;
    Emit8(p, 0xF5);
    c = p;
}

}

}
