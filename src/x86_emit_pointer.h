#pragma once

#include "x86_emit_encoding.h"

namespace cerfengine {

namespace x86 {

/* MOV r64, r/m64 - REX.W 8B /r, register-direct (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovPtrRegReg(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    EmitRex(p, true, dst, src);
    Emit8(p, 0x8B);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* MOV r64, [base + disp32] - REX.W 8B /r mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovPtrRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                                    int32_t disp) {
    uint8_t* p = c;
    EmitRex(p, true, reg, base);
    Emit8(p, 0x8B);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* ADD r64, r/m64 - REX.W 03 /r, register-direct (SDM Vol. 2A 3-32 ADD). */
CERFENGINE_FORCEINLINE void EmitAddPtrRegReg(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    EmitRex(p, true, dst, src);
    Emit8(p, 0x03);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* ADD r64, [base + disp32] - REX.W 03 /r mod=10 (SDM Vol. 2A 3-32 ADD). */
CERFENGINE_FORCEINLINE void EmitAddPtrRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                                    int32_t disp) {
    uint8_t* p = c;
    EmitRex(p, true, reg, base);
    Emit8(p, 0x03);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* ADD r/m64, imm32 - REX.W 81 /0 id; SUB r/m64, imm32 - REX.W 81 /5 id; the
   immediate is sign-extended (SDM Vol. 2A 3-32 ADD, Vol. 2B 4-681 SUB). */
CERFENGINE_FORCEINLINE void EmitAddPtrRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    EmitRex(p, true, 0, reg);
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 0);
    Emit32(p, imm);
    c = p;
}

CERFENGINE_FORCEINLINE void EmitSubPtrRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    EmitRex(p, true, 0, reg);
    Emit8(p, 0x81);
    EmitModRmReg(p, 3, reg, 5);
    Emit32(p, imm);
    c = p;
}

/* TEST r/m64, r64 - REX.W 85 /r, register-direct (SDM Vol. 2B 4-711 TEST). */
CERFENGINE_FORCEINLINE void EmitTestPtrRegReg(uint8_t*& c, uint8_t a, uint8_t b) {
    uint8_t* p = c;
    EmitRex(p, true, b, a);
    Emit8(p, 0x85);
    EmitModRmReg(p, 3, a, b);
    c = p;
}

/* SHR r/m64, imm8 - REX.W C1 /5 ib (SDM Vol. 2B 4-600 SHR). */
CERFENGINE_FORCEINLINE void EmitShrReg64Imm(uint8_t*& c, uint8_t reg, uint8_t imm8) {
    uint8_t* p = c;
    EmitRex(p, true, 0, reg);
    Emit8(p, 0xC1);
    EmitModRmReg(p, 3, reg, 5);
    Emit8(p, imm8);
    c = p;
}

/* MOV r32, imm32 - B8+rd id zero-extends into the 64-bit register (SDM Vol. 1
   3-13); a larger host address takes MOV r64, imm64 - REX.W B8+rd io
   (Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovPtrRegImm(uint8_t*& c, uint8_t reg, const void* address) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    uint8_t*        p     = c;
    if constexpr (kX64) {
        if (value > 0xFFFFFFFFu) {
            EmitMovReg64Imm64(p, reg, value);
            c = p;
            return;
        }
    }
    EmitRex(p, false, 0, reg);
    Emit8(p, static_cast<uint8_t>(0xB8 + (reg & 7u)));
    Emit32(p, static_cast<uint32_t>(value));
    c = p;
}

/* LEA r64, [RIP + disp32] - REX.W 8D /r with mod=00 r/m=101 (SDM Vol. 2A 3-594
   LEA; RIP-relative addressing, Vol. 2A 2.2.1.6, p. 2-12). Returns the disp32
   field; the displacement counts from the end of the instruction. */
CERFENGINE_FORCEINLINE uint8_t* EmitLeaRipRelative(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    EmitRex(p, true, reg, 0);
    Emit8(p, 0x8D);
    EmitModRmDisp32(p, reg);
    uint8_t* const disp = p;
    Emit32(p, 0u);
    c = p;
    return disp;
}

}

}
