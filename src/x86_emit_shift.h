#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include "x86_emit.h"

namespace cerfengine {

namespace x86 {

/* SHL r/m32, imm8 - C1 /4 ib, register-direct (SDM Vol. 2B 4-600 SHL). */
CERFENGINE_FORCEINLINE void EmitShlReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xC1);
    EmitModRmReg(p, 3, reg, 4);
    Emit8(p, imm8);
    c = p;
}

/* SHL r/m32, CL - D3 /4, register-direct; count masked to 5 bits, and
   "If the count is 0, the flags are not affected" (SDM Vol. 2B 4-600,
   4-602 SHL). */
CERFENGINE_FORCEINLINE void EmitShlReg32Cl(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xD3);
    EmitModRmReg(p, 3, reg, 4);
    c = p;
}

/* SHR r/m32, CL - D3 /5, register-direct (SDM Vol. 2B 4-600 SHR). */
CERFENGINE_FORCEINLINE void EmitShrReg32Cl(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xD3);
    EmitModRmReg(p, 3, reg, 5);
    c = p;
}

/* SAR r/m32, CL - D3 /7, register-direct (SDM Vol. 2B 4-599 SAR). */
CERFENGINE_FORCEINLINE void EmitSarReg32Cl(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xD3);
    EmitModRmReg(p, 3, reg, 7);
    c = p;
}

/* SHR r/m32, imm8 - C1 /5 ib, register-direct (SDM Vol. 2B 4-600 SHR). */
CERFENGINE_FORCEINLINE void EmitShrReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xC1);
    EmitModRmReg(p, 3, reg, 5);
    Emit8(p, imm8);
    c = p;
}

/* SAR r/m32, imm8 - C1 /7 ib, register-direct (SDM Vol. 2B 4-599 SAR). */
CERFENGINE_FORCEINLINE void EmitSarReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xC1);
    EmitModRmReg(p, 3, reg, 7);
    Emit8(p, imm8);
    c = p;
}

/* ROR r/m32, imm8 - C1 /1 ib, register-direct
   (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
CERFENGINE_FORCEINLINE void EmitRorReg32Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xC1);
    EmitModRmReg(p, 3, reg, 1);
    Emit8(p, imm8);
    c = p;
}

/* ROR r/m32, CL - D3 /1, register-direct
   (SDM Vol. 2B 4-533 RCL/RCR/ROL/ROR). */
CERFENGINE_FORCEINLINE void EmitRorReg32Cl(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xD3);
    EmitModRmReg(p, 3, reg, 1);
    c = p;
}

/* SHRD r/m32, r32, imm8 - 0F AC /r ib, register-direct: shift dst right
   imm8 places, filling from src on the left (SDM Vol. 2B 4-638 SHRD). */
CERFENGINE_FORCEINLINE void EmitShrdReg32Reg32Imm8(uint8_t*& c, uint8_t dst, uint8_t src,
                                   uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xAC);
    EmitModRmReg(p, 3, dst, src);
    Emit8(p, imm8);
    c = p;
}

/* RCR r/m32, 1 - D1 /3, register-direct, "Rotate 33 bits (CF, r/m32)
   right once" (SDM Vol. 2B 4-532 RCL/RCR/ROL/ROR). */
CERFENGINE_FORCEINLINE void EmitRcrReg32By1(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xD1);
    EmitModRmReg(p, 3, reg, 3);
    c = p;
}

}

}
