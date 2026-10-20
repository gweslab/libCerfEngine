#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include <cstddef>
#include <cstdint>

#include <cerfengine/byte_order.h>

#include "emit_fault.h"
#include <cerfengine/force_inline.h>

namespace cerfengine {

namespace x86 {

[[noreturn]] void ThrowModRmNeedsSib(uint8_t mod);
[[noreturn]] void ThrowModRmBareDisp32();
[[noreturn]] void ThrowRel8FixupRange(uint8_t opcode, ptrdiff_t disp);

/* r32 / r8 field encodings - SDM Vol. 2A Table 2-2. */
constexpr uint8_t kEax = 0;
constexpr uint8_t kEcx = 1;
constexpr uint8_t kEdx = 2;
constexpr uint8_t kEbx = 3;
constexpr uint8_t kEsp = 4;
constexpr uint8_t kEbp = 5;
constexpr uint8_t kEsi = 6;
constexpr uint8_t kEdi = 7;

constexpr uint8_t kMmuReg   = kEbx;
constexpr uint8_t kStateReg = kEsi;

constexpr uint8_t kAl = 0;
constexpr uint8_t kCl = 1;
constexpr uint8_t kDl = 2;

CERFENGINE_FORCEINLINE void Emit8(uint8_t*& c, uint8_t v) {
    uint8_t* p = c;
    *p++ = v;
    c = p;
}

CERFENGINE_FORCEINLINE void Emit32(uint8_t*& c, uint32_t v) {
    uint8_t* p = c;
    le::Put32(p, v);
    p += 4;
    c = p;
}

/* ModR/M byte = (mod << 6) | (reg << 3) | r/m - SDM Vol. 2A Table 2-2. */
CERFENGINE_FORCEINLINE void EmitModRmByte(uint8_t*& c, uint8_t mod, uint8_t rm, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, static_cast<uint8_t>((mod << 6) | (reg << 3) | rm));
    c = p;
}

/* Per Table 2-2 notes 1-2: in the memory forms, r/m = 100b means a SIB
   byte follows, and mod = 00b with r/m = 101b means bare disp32. */
CERFENGINE_FORCEINLINE void EmitModRmReg(uint8_t*& c, uint8_t mod, uint8_t rm, uint8_t reg) {
    uint8_t* p = c;
    if (mod != 3u && rm == kEsp) ThrowModRmNeedsSib(mod);
    if (mod == 0u && rm == kEbp) ThrowModRmBareDisp32();
    EmitModRmByte(p, mod, rm, reg);
    c = p;
}

/* mod = 00b, r/m = 101b - bare disp32 (SDM Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitModRmDisp32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    EmitModRmByte(p, 0, kEbp, reg);
    c = p;
}

/* rel32 - displacement relative to the next instruction:
   SDM Vol. 2A 3-139 CALL, 3-552 JMP. */
CERFENGINE_FORCEINLINE void EmitOpcodeRel32(uint8_t*& c, uint8_t opcode, const void* target) {
    uint8_t* p = c;
    Emit8(p, opcode);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) -
                                    reinterpret_cast<uintptr_t>(p + 4)));
    c = p;
}

/* CALL rel32 - E8 cd (SDM Vol. 2A 3-139 CALL). */
CERFENGINE_FORCEINLINE void EmitCall(uint8_t*& c, const void* target) {
    uint8_t* p = c;
    EmitOpcodeRel32(p, 0xE8, target);
    c = p;
}

/* CALL r/m32 - FF /2 (SDM Vol. 2A 3-139 CALL). */
CERFENGINE_FORCEINLINE void EmitCallReg(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0xFF);
    EmitModRmByte(p, 3, reg, 2);
    c = p;
}

/* JMP rel32 - E9 cd (SDM Vol. 2A 3-552 JMP). */
CERFENGINE_FORCEINLINE void EmitJmp32(uint8_t*& c, const void* target) {
    uint8_t* p = c;
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

/* MOV r32, imm32 - B8+rd id (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegImm32(uint8_t*& c, uint8_t reg, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, static_cast<uint8_t>(0xB8 + reg));
    Emit32(p, imm);
    c = p;
}

/* MOV r32, r/m32 - 8B /r, register-direct (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegReg(uint8_t*& c, uint8_t dst, uint8_t src) {
    uint8_t* p = c;
    Emit8(p, 0x8B);
    EmitModRmReg(p, 3, src, dst);
    c = p;
}

/* MOV r32, [base + disp32] - 8B /r mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x8B);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOV [base + disp32], r32 - 89 /r mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseDisp32Reg(uint8_t*& c, uint8_t base, int32_t disp,
                                 uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0x89);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOV r/m32, imm32 - C7 /0 id, mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseDisp32Imm32(uint8_t*& c, uint8_t base, int32_t disp,
                                   uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0xC7);
    EmitModRmReg(p, 2, base, 0);
    Emit32(p, static_cast<uint32_t>(disp));
    Emit32(p, imm);
    c = p;
}

/* MOV r/m8, r8 - 88 /r mod=10 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseDisp32Byte(uint8_t*& c, uint8_t base, int32_t disp,
                                  uint8_t reg8) {
    uint8_t* p = c;
    Emit8(p, 0x88);
    EmitModRmReg(p, 2, base, reg8);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOV r32, [base] - 8B /r mod=00 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovRegBase(uint8_t*& c, uint8_t reg, uint8_t base) {
    uint8_t* p = c;
    Emit8(p, 0x8B);
    EmitModRmReg(p, 0, base, reg);
    c = p;
}

/* MOV [base], r32 - 89 /r mod=00 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseReg(uint8_t*& c, uint8_t base, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0x89);
    EmitModRmReg(p, 0, base, reg);
    c = p;
}

/* MOV [base], r8 - 88 /r mod=00 (SDM Vol. 2B 4-35 MOV). */
CERFENGINE_FORCEINLINE void EmitMovBaseReg8(uint8_t*& c, uint8_t base, uint8_t reg8) {
    uint8_t* p = c;
    Emit8(p, 0x88);
    EmitModRmReg(p, 0, base, reg8);
    c = p;
}

/* MOVZX r32, byte [base] - 0F B6 /r mod=00 (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxRegByteBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xB6);
    EmitModRmReg(p, 0, base, dst);
    c = p;
}

/* MOVZX r32, word [base] - 0F B7 /r mod=00 (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxRegWordBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xB7);
    EmitModRmReg(p, 0, base, dst);
    c = p;
}

/* MOVSX r32, byte [base] - 0F BE /r mod=00 (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxRegByteBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBE);
    EmitModRmReg(p, 0, base, dst);
    c = p;
}

/* MOVSX r32, word [base] - 0F BF /r mod=00 (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxRegWordBase(uint8_t*& c, uint8_t dst, uint8_t base) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBF);
    EmitModRmReg(p, 0, base, dst);
    c = p;
}

/* MOV [base], r16 - 66 89 /r mod=00: MOV r/m16, r16 (SDM Vol. 2B 4-35) under
   the operand-size override prefix 66H (SDM Vol. 2A 2-2). */
CERFENGINE_FORCEINLINE void EmitMovBaseReg16(uint8_t*& c, uint8_t base, uint8_t reg16) {
    uint8_t* p = c;
    Emit8(p, 0x66);
    Emit8(p, 0x89);
    EmitModRmReg(p, 0, base, reg16);
    c = p;
}

/* MOV [disp32], r32 - 89 /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovDwordPtrReg(uint8_t*& c, const void* addr, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, 0x89);
    EmitModRmDisp32(p, reg);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    c = p;
}

/* MOV [disp32], imm32 - C7 /0 id, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovDwordPtrImm32(uint8_t*& c, const void* addr, uint32_t imm) {
    uint8_t* p = c;
    Emit8(p, 0xC7);
    EmitModRmDisp32(p, 0);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    Emit32(p, imm);
    c = p;
}

/* MOV r32, [disp32] - 8B /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovRegDwordPtr(uint8_t*& c, uint8_t reg, const void* addr) {
    uint8_t* p = c;
    Emit8(p, 0x8B);
    EmitModRmDisp32(p, reg);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    c = p;
}

/* MOVSX r32, r/m8 - 0F BE /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-130 MOVSX; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovsxRegBytePtr(uint8_t*& c, uint8_t reg, const void* addr) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBE);
    EmitModRmDisp32(p, reg);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    c = p;
}

/* MOV r/m8, r8 - 88 /r, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovBytePtrReg8(uint8_t*& c, const void* addr, uint8_t reg8) {
    uint8_t* p = c;
    Emit8(p, 0x88);
    EmitModRmDisp32(p, reg8);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    c = p;
}

/* MOV r/m8, imm8 - C6 /0 ib, mod=00 r/m=101 = bare disp32
   (SDM Vol. 2B 4-35 MOV; Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitMovBytePtrImm8(uint8_t*& c, const void* addr, uint8_t imm8) {
    uint8_t* p = c;
    Emit8(p, 0xC6);
    EmitModRmDisp32(p, 0);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    Emit8(p, imm8);
    c = p;
}

/* MOVSX r32, r/m8 - 0F BE /r mod=10 (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxByteRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                       int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBE);
    EmitModRmReg(p, 2, base, reg);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOVSX r32, r/m8 - 0F BE /r, register-direct (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxReg32Reg8(uint8_t*& c, uint8_t dst, uint8_t src8) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBE);
    EmitModRmReg(p, 3, src8, dst);
    c = p;
}

/* MOVSX r32, r/m16 - 0F BF /r, register-direct (SDM Vol. 2B 4-130 MOVSX). */
CERFENGINE_FORCEINLINE void EmitMovsxReg32Reg16(uint8_t*& c, uint8_t dst, uint8_t src16) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xBF);
    EmitModRmReg(p, 3, src16, dst);
    c = p;
}

/* MOVZX r32, r/m8 - 0F B6 /r, register-direct (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxReg32Reg8(uint8_t*& c, uint8_t dst, uint8_t src8) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xB6);
    EmitModRmReg(p, 3, src8, dst);
    c = p;
}

/* MOVZX r32, r/m8 - 0F B6 /r mod=10, "Flags Affected: None"
   (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxRegByteBaseDisp32(uint8_t*& c, uint8_t dst, uint8_t base,
                                       int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xB6);
    EmitModRmReg(p, 2, base, dst);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* MOVZX r32, r/m16 - 0F B7 /r, register-direct (SDM Vol. 2B 4-140 MOVZX). */
CERFENGINE_FORCEINLINE void EmitMovzxReg32Reg16(uint8_t*& c, uint8_t dst, uint8_t src16) {
    uint8_t* p = c;
    Emit8(p, 0x0F);
    Emit8(p, 0xB7);
    EmitModRmReg(p, 3, src16, dst);
    c = p;
}

/* LEA r32, m - 8D /r mod=10 (SDM Vol. 2A 3-594 LEA). */
CERFENGINE_FORCEINLINE void EmitLeaRegBaseDisp32(uint8_t*& c, uint8_t reg, uint8_t base,
                                 int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0x8D);
    EmitModRmReg(p, 2, base, reg);
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
    Emit8(p, static_cast<uint8_t>(0x50 + reg));
    c = p;
}

/* PUSH r/m32 - FF /6 mod=10 (SDM Vol. 2B 4-521 PUSH). */
CERFENGINE_FORCEINLINE void EmitPushBaseDisp32(uint8_t*& c, uint8_t base, int32_t disp) {
    uint8_t* p = c;
    Emit8(p, 0xFF);
    EmitModRmReg(p, 2, base, 6);
    Emit32(p, static_cast<uint32_t>(disp));
    c = p;
}

/* PUSH r/m32 - FF /6, mod=00 r/m=101 = bare disp32 (SDM Vol. 2B 4-521 PUSH;
   Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitPushDwordPtr(uint8_t*& c, const void* addr) {
    uint8_t* p = c;
    Emit8(p, 0xFF);
    EmitModRmDisp32(p, 6);
    Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    c = p;
}

/* POP r32 - 58+rd (SDM Vol. 2B 4-398 POP). */
CERFENGINE_FORCEINLINE void EmitPopReg(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, static_cast<uint8_t>(0x58 + reg));
    c = p;
}

}

}
