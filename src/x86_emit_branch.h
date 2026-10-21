#pragma once

#include "x86_emit_encoding.h"

namespace cerfengine {

namespace x86 {

CERFENGINE_FORCEINLINE bool Rel32Reaches(const uint8_t* next, const void* target) {
    const ptrdiff_t disp = reinterpret_cast<const uint8_t*>(target) - next;
    return disp >= INT32_MIN && disp <= INT32_MAX;
}

/* rel32 - displacement relative to the next instruction, sign-extended to 64 bits
   in 64-bit mode: SDM Vol. 2A 3-139 CALL, 3-552 JMP. */
CERFENGINE_FORCEINLINE void EmitOpcodeRel32(uint8_t*& c, uint8_t opcode, const void* target) {
    uint8_t* p = c;
    Emit8(p, opcode);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) -
                                    reinterpret_cast<uintptr_t>(p + 4)));
    c = p;
}

/* CALL r/m32 / r/m64 - FF /2 (SDM Vol. 2A 3-139 CALL). */
CERFENGINE_FORCEINLINE void EmitCallReg(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    EmitOpModRm(p, 0xFF, 3, reg, 2);
    c = p;
}

/* JMP r/m64 - FF /4 (SDM Vol. 2A 3-552 JMP). */
CERFENGINE_FORCEINLINE void EmitJmpReg(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    EmitOpModRm(p, 0xFF, 3, reg, 4);
    c = p;
}

/* CALL rel32 - E8 cd (SDM Vol. 2A 3-139 CALL). */
CERFENGINE_FORCEINLINE void EmitCall(uint8_t*& c, const void* target) {
    uint8_t* p = c;
    if constexpr (kX64) {
        if (!Rel32Reaches(p + 5, target)) {
            EmitMovReg64Imm64(p, kAddressReg, reinterpret_cast<uintptr_t>(target));
            EmitCallReg(p, kAddressReg);
            c = p;
            return;
        }
    }
    EmitOpcodeRel32(p, 0xE8, target);
    c = p;
}

/* JMP rel32 - E9 cd (SDM Vol. 2A 3-552 JMP). */
CERFENGINE_FORCEINLINE void EmitJmp32(uint8_t*& c, const void* target) {
    uint8_t* p = c;
    if constexpr (kX64) {
        if (!Rel32Reaches(p + 5, target)) {
            EmitMovReg64Imm64(p, kAddressReg, reinterpret_cast<uintptr_t>(target));
            EmitJmpReg(p, kAddressReg);
            c = p;
            return;
        }
    }
    EmitOpcodeRel32(p, 0xE9, target);
    c = p;
}

/* RET - C3, Op/En ZO (SDM Vol. 2B 4-564 RET). */
CERFENGINE_FORCEINLINE void EmitRet(uint8_t*& c) {
    uint8_t* p = c;
    Emit8(p, 0xC3);
    c = p;
}

CERFENGINE_FORCEINLINE uint8_t* EmitRel8Label(uint8_t*& c, uint8_t opcode) {
    uint8_t* p = c;
    Emit8(p, opcode);
    uint8_t* label = p;
    Emit8(p, 0);
    c = p;
    return label;
}

CERFENGINE_FORCEINLINE uint8_t* EmitRel32Label(uint8_t*& c, uint8_t opcode0F) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, opcode0F);
    uint8_t* label = p;
    Emit32(p, 0);
    c = p;
    return label;
}

/* JMP rel8 - EB cb (SDM Vol. 2A 3-552 JMP). */
CERFENGINE_FORCEINLINE uint8_t* EmitJmpLabel(uint8_t*& c) { return EmitRel8Label(c, 0xEB); }

/* JMP rel32 - E9 cd (SDM Vol. 2A 3-552 JMP). */
CERFENGINE_FORCEINLINE uint8_t* EmitJmpLabel32(uint8_t*& c) {
    uint8_t* p = c;
    Emit8(p, 0xE9);
    uint8_t* label = p;
    Emit32(p, 0);
    c = p;
    return label;
}

/* Jcc rel8 - 7x cb; Jcc rel32 - 0F 8x cd (SDM Vol. 2A 3-547..3-549 Jcc). */
CERFENGINE_FORCEINLINE uint8_t* EmitJzLabel(uint8_t*& c)    { return EmitRel8Label(c, 0x74); }
CERFENGINE_FORCEINLINE uint8_t* EmitJnzLabel(uint8_t*& c)   { return EmitRel8Label(c, 0x75); }
CERFENGINE_FORCEINLINE uint8_t* EmitJnoLabel(uint8_t*& c)   { return EmitRel8Label(c, 0x71); }
CERFENGINE_FORCEINLINE uint8_t* EmitJgeLabel(uint8_t*& c)   { return EmitRel8Label(c, 0x7D); }
CERFENGINE_FORCEINLINE uint8_t* EmitJleLabel(uint8_t*& c)   { return EmitRel8Label(c, 0x7E); }
CERFENGINE_FORCEINLINE uint8_t* EmitJzLabel32(uint8_t*& c)  { return EmitRel32Label(c, 0x84); }
CERFENGINE_FORCEINLINE uint8_t* EmitJnzLabel32(uint8_t*& c) { return EmitRel32Label(c, 0x85); }
CERFENGINE_FORCEINLINE uint8_t* EmitJsLabel32(uint8_t*& c)  { return EmitRel32Label(c, 0x88); }
CERFENGINE_FORCEINLINE uint8_t* EmitJnsLabel32(uint8_t*& c) { return EmitRel32Label(c, 0x89); }
CERFENGINE_FORCEINLINE uint8_t* EmitJaeLabel32(uint8_t*& c) { return EmitRel32Label(c, 0x83); }
CERFENGINE_FORCEINLINE uint8_t* EmitJncLabel32(uint8_t*& c) { return EmitJaeLabel32(c); }
CERFENGINE_FORCEINLINE uint8_t* EmitJbLabel32(uint8_t*& c)  { return EmitRel32Label(c, 0x82); }

/* rel8 - sign-extended 8-bit displacement: SDM Vol. 2A 3-552 JMP (EB cb),
   3-547 Jcc (7x cb). */
CERFENGINE_FORCEINLINE void FixupLabel(uint8_t* label, uint8_t* cursor) {
    const ptrdiff_t disp = cursor - (label + 1);
    if (disp < -128 || disp > 127) ThrowRel8FixupRange(label[-1], disp);
    label[0] = static_cast<uint8_t>(disp);
}

CERFENGINE_FORCEINLINE void FixupLabel32(uint8_t* label, uint8_t* cursor) {
    le::Put32(label, static_cast<uint32_t>(cursor - (label + 4)));
}

}

}
