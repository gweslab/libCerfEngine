#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include "x86_emit_branch.h"
#include "x86_emit_encoding.h"

namespace cerfengine {

namespace x86 {

/* MOV r32, imm32 - B8+rd id (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    EmitRex(p, false, 0, reg);
    Emit8(p, static_cast<uint8_t>(0xB8 + (reg & 7u)));
    Emit32(p, imm);
    c = p;
}

/* MOV r32, r/m32 - 8B /r, register-direct (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegReg(uint8_t*& c, uint8_t dst, uint8_t src) {
    EmitOpModRm(c, 0x8B, 3, src, dst);
}

/* MOV r32, [base + disp32] - 8B /r mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    EmitOpModRm(p, 0x8B, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOV [base + disp32], r32 - 89 /r mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseDisp32Reg(uint8_t*& c, uint8_t base, int32_t disp,
                                 uint8_t reg) {
    uint8_t* p = c;
    EmitOpModRm(p, 0x89, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOV r/m32, imm32 - C7 /0 id, mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                   uint32_t imm) {
    uint8_t* p = c;
    EmitOpModRm(p, 0xC7, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit32(p, imm);
    c = p;
}

/* MOV r/m8, r8 - 88 /r mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseDisp32Byte(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint8_t reg8) {
    uint8_t* p = c;
    EmitOpModRm(p, 0x88, 2, base, reg8);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOV r32, [base] - 8B /r mod=00 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegBase(uint8_t*& c, uint8_t reg, uint8_t base) {
    EmitOpModRm(c, 0x8B, 0, base, reg);
}

/* MOV [base], r32 - 89 /r mod=00 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    EmitOpModRm(c, 0x89, 0, base, reg);
}

/* MOV [base], r8 - 88 /r mod=00 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseReg8(uint8_t*& c, uint8_t base, uint8_t reg8) {
    EmitOpModRm(c, 0x88, 0, base, reg8);
}

/* MOVZX r32, byte [base] - 0F B6 /r mod=00 (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxRegByteBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    EmitOp0FModRm(c, 0xB6, 0, base, dst);
}

/* MOVZX r32, word [base] - 0F B7 /r mod=00 (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxRegWordBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    EmitOp0FModRm(c, 0xB7, 0, base, dst);
}

/* MOVSX r32, byte [base] - 0F BE /r mod=00 (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxRegByteBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    EmitOp0FModRm(c, 0xBE, 0, base, dst);
}

/* MOVSX r32, word [base] - 0F BF /r mod=00 (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxRegWordBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    EmitOp0FModRm(c, 0xBF, 0, base, dst);
}

/* MOV [base], r16 - 66 89 /r mod=00: MOV r/m16, r16 (SDM Vol. 2B 4-35) under
   the operand-size override prefix 66H (SDM Vol. 2A 2-2). */
CERFENGINE_FORCEINLINE void EmitMovBaseReg16(uint8_t*& c, uint8_t base, uint8_t reg16) {
    uint8_t* p = c;
    Emit8(p, 0x66);
    EmitOpModRm(p, 0x89, 0, base, reg16);
    c = p;
}

/* MOV [disp32], r32 - 89 /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovDwordPtrReg(uint8_t*& c, const void* addr, uint8_t reg) {
    EmitOpHostAddress(c, 0x89, reg, addr);
}

/* MOV [disp32], imm32 - C7 /0 id, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovDwordPtrImm32(uint8_t*& c, const void* addr, uint32_t imm) {
    uint8_t* p = c;
    EmitOpHostAddress(p, 0xC7, 0, addr);
    Emit32(p, imm);
    c = p;
}

/* MOV r32, [disp32] - 8B /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovRegDwordPtr(uint8_t*& c, uint8_t reg, const void* addr) {
    EmitOpHostAddress(c, 0x8B, reg, addr);
}

/* MOVSX r32, r/m8 - 0F BE /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-130 MOVSX; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovsxRegBytePtr(uint8_t*& c, uint8_t reg, const void* addr) {
    uint8_t* p = c;
    EmitHostAddressPrefix(p, addr);
    EmitHostAddressRex(p, false, reg);
    Emit8(p, 0x0F);
    Emit8(p, 0xBE);
    EmitHostAddressModRm(p, reg, addr);
    c = p;
}

/* MOV r/m8, r8 - 88 /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovBytePtrReg8(uint8_t*& c, const void* addr, uint8_t reg8) {
    EmitOpHostAddress(c, 0x88, reg8, addr);
}

/* MOV r/m8, imm8 - C6 /0 ib, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovBytePtrImm8(uint8_t*& c, const void* addr, uint8_t imm8) {
    uint8_t* p = c;
    EmitOpHostAddress(p, 0xC6, 0, addr);
    Emit8(p, imm8);
    c = p;
}

/* MOVSX r32, r/m8 - 0F BE /r mod=10 (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxByteRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                       int32_t disp) {
    uint8_t* p = c;
    EmitOp0FModRm(p, 0xBE, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOVSX r32, r/m8 - 0F BE /r, register-direct (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxReg32Reg8(uint8_t*& c, uint8_t dst, uint8_t src8) {
    EmitOp0FModRm(c, 0xBE, 3, src8, dst);
}

/* MOVSX r32, r/m16 - 0F BF /r, register-direct (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxReg32Reg16(uint8_t*& c, uint8_t dst, uint8_t src16) {
    EmitOp0FModRm(c, 0xBF, 3, src16, dst);
}

/* MOVZX r32, r/m8 - 0F B6 /r, register-direct (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxReg32Reg8(uint8_t*& c, uint8_t dst, uint8_t src8) {
    EmitOp0FModRm(c, 0xB6, 3, src8, dst);
}

/* MOVZX r32, r/m8 - 0F B6 /r mod=10, "Flags Affected: None"
   (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxRegByteBaseDisp32(uint8_t*& c, uint8_t dst, uint8_t base,
                                       int32_t disp) {
    uint8_t* p = c;
    EmitOp0FModRm(p, 0xB6, 2, base, dst);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOVZX r32, r/m16 - 0F B7 /r, register-direct (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxReg32Reg16(uint8_t*& c, uint8_t dst, uint8_t src16) {
    EmitOp0FModRm(c, 0xB7, 3, src16, dst);
}

/* LEA r32, m - 8D /r mod=10 (SDM Vol. 2A 3-594 LEA). */
CERFENGINE_FORCEINLINE void EmitLeaRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    EmitOpModRm(p, 0x8D, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* PUSH imm32 - 68 id (SDM Vol. 2B 4-521 PUSH). */
CERFENGINE_FORCEINLINE void EmitPush32(uint8_t*& c, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0x68);
    Emit32(p, imm);
    c = p;
}

/* PUSH r32 - 50+rd (SDM Vol. 2B 4-521 PUSH). */
CERFENGINE_FORCEINLINE void EmitPushReg(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    EmitRex(p, false, 0, reg);
    Emit8(p, static_cast<uint8_t>(0x50 + (reg & 7u)));
    c = p;
}

/* PUSH r/m32 - FF /6 mod=10 (SDM Vol. 2B 4-521 PUSH). */
CERFENGINE_FORCEINLINE void EmitPushBaseDisp32(uint8_t*& c, uint8_t base, int32_t disp) {
    uint8_t* p = c;
    EmitOpModRm(p, 0xFF, 2, base, 6);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* PUSH r/m32 - FF /6, mod=00 r/m=101 = bare disp32 (SDM Vol. 2B 4-521 PUSH;
   Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitPushDwordPtr(uint8_t*& c, const void* addr) {
    if constexpr (kX64) {
        throw EmitFault("x86 emit: a 64-bit push of a host word reads 8 bytes");
    } else {
        EmitOpHostAddress(c, 0xFF, 6, addr);
    }
}

/* POP r32 - 58+rd (SDM Vol. 2B 4-398 POP). */
CERFENGINE_FORCEINLINE void EmitPopReg(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    EmitRex(p, false, 0, reg);
    Emit8(p, static_cast<uint8_t>(0x58 + (reg & 7u)));
    c = p;
}

}

}
