#pragma once

/* Encodings: Intel SDM Vol. 2; ModR/M forms per Vol. 2A Table 2-2 (p. 2-6). */

#include <cstddef>
#include <cstdint>

#include <cerfengine/byte_order.h>

#include "emit_fault.h"
#include <cerfengine/force_inline.h>

namespace cerfengine {

namespace x86 {

#if defined(_M_X64) || defined(__x86_64__)
inline constexpr bool kX64 = true;
#else
inline constexpr bool kX64 = false;
#endif

[[noreturn]] void ThrowModRmNeedsSib(uint8_t mod);
[[noreturn]] void ThrowModRmBareDisp32();
[[noreturn]] void ThrowRel8FixupRange(uint8_t opcode, ptrdiff_t disp);

/* r32 / r8 field encodings - SDM Vol. 2A Table 2-2; 8-15 take REX.R or REX.B
   (Vol. 2A Table 2-4, p. 2-9). */
constexpr uint8_t kEax = 0;
constexpr uint8_t kEcx = 1;
constexpr uint8_t kEdx = 2;
constexpr uint8_t kEbx = 3;
constexpr uint8_t kEsp = 4;
constexpr uint8_t kEbp = 5;
constexpr uint8_t kEsi = 6;
constexpr uint8_t kEdi = 7;
constexpr uint8_t kR8  = 8;
constexpr uint8_t kR9  = 9;
constexpr uint8_t kR10 = 10;
constexpr uint8_t kR11 = 11;
constexpr uint8_t kR12 = 12;
constexpr uint8_t kR13 = 13;

constexpr uint8_t kStateReg   = kX64 ? kEbx : kEsi;
constexpr uint8_t kMmuReg     = kX64 ? kEbp : kEbx;
constexpr uint8_t kSavedRegA  = kX64 ? kR12 : kEdi;
constexpr uint8_t kSavedRegB  = kX64 ? kR13 : kEbp;
constexpr uint8_t kAddressReg = kR11;

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

CERFENGINE_FORCEINLINE void Emit64(uint8_t*& c, uint64_t v) {
    uint8_t* p = c;
    le::Put32(p, static_cast<uint32_t>(v));
    le::Put32(p + 4, static_cast<uint32_t>(v >> 32));
    p += 8;
    c = p;
}

/* REX = 0100WRXB, placed after any mandatory prefix and right before the
   opcode or the 0FH escape (SDM Vol. 2A 2.2.1, pp. 2-8/2-9, Table 2-4). */
CERFENGINE_FORCEINLINE void EmitRex(uint8_t*& c, bool w, uint8_t reg, uint8_t rm) {
    if constexpr (kX64) {
        const uint8_t rex = static_cast<uint8_t>(0x40u | (w ? 0x08u : 0u) | ((reg & 8u) >> 1) |
                                                 ((rm & 8u) >> 3));
        if (rex != 0x40u) Emit8(c, rex);
    }
}

/* ModR/M byte = (mod << 6) | (reg << 3) | r/m - SDM Vol. 2A Table 2-2. */
CERFENGINE_FORCEINLINE void EmitModRmByte(uint8_t*& c, uint8_t mod, uint8_t rm, uint8_t reg) {
    uint8_t* p = c;
    Emit8(p, static_cast<uint8_t>((mod << 6) | ((reg & 7u) << 3) | (rm & 7u)));
    c = p;
}

/* SDM Vol. 2A Table 2-2 notes 1-2 (SIB at r/m = 100b, disp32 at mod = 00b r/m = 101b);
   64-bit mode: R12 takes a SIB byte, RBP/R13 without displacement take mod = 01b
   disp8 0 (Vol. 2A Table 2-5, p. 2-11). */
CERFENGINE_FORCEINLINE void EmitModRmReg(uint8_t*& c, uint8_t mod, uint8_t rm, uint8_t reg) {
    uint8_t* p = c;
    if constexpr (kX64) {
        if (mod != 3u && (rm & 7u) == kEsp) {
            EmitModRmByte(p, mod, kEsp, reg);
            Emit8(p, 0x24);
        } else if (mod == 0u && (rm & 7u) == kEbp) {
            EmitModRmByte(p, 1, kEbp, reg);
            Emit8(p, 0x00);
        } else {
            EmitModRmByte(p, mod, rm, reg);
        }
    } else {
        if (mod != 3u && rm == kEsp) ThrowModRmNeedsSib(mod);
        if (mod == 0u && rm == kEbp) ThrowModRmBareDisp32();
        EmitModRmByte(p, mod, rm, reg);
    }
    c = p;
}

/* mod = 00b, r/m = 101b - bare disp32 (SDM Vol. 2A Table 2-2 note 2). */
CERFENGINE_FORCEINLINE void EmitModRmDisp32(uint8_t*& c, uint8_t reg) {
    uint8_t* p = c;
    EmitModRmByte(p, 0, kEbp, reg);
    c = p;
}

CERFENGINE_FORCEINLINE void EmitOpModRm(uint8_t*& c, uint8_t opcode, uint8_t mod, uint8_t rm,
                                        uint8_t reg) {
    uint8_t* p = c;
    EmitRex(p, false, reg, rm);
    Emit8(p, opcode);
    EmitModRmReg(p, mod, rm, reg);
    c = p;
}

CERFENGINE_FORCEINLINE void EmitOp0FModRm(uint8_t*& c, uint8_t opcode, uint8_t mod, uint8_t rm,
                                          uint8_t reg) {
    uint8_t* p = c;
    EmitRex(p, false, reg, rm);
    Emit8(p, 0x0F);
    Emit8(p, opcode);
    EmitModRmReg(p, mod, rm, reg);
    c = p;
}

/* MOV r64, imm64 - REX.W B8+rd io (SDM Vol. 2A 2.2.1.5, p. 2-11). */
CERFENGINE_FORCEINLINE void EmitMovReg64Imm64(uint8_t*& c, uint8_t reg, uint64_t imm) {
    uint8_t* p = c;
    EmitRex(p, true, 0, reg);
    Emit8(p, static_cast<uint8_t>(0xB8 + (reg & 7u)));
    Emit64(p, imm);
    c = p;
}

/* A host address operand: bare disp32 on x86-32; in 64-bit mode mod = 00b
   r/m = 101b is RIP + disp32 (SDM Vol. 2A 2.2.1.6, p. 2-12), so the address is
   loaded into the address register and used as [R11]. */
CERFENGINE_FORCEINLINE void EmitHostAddressPrefix(uint8_t*& c, const void* addr) {
    if constexpr (kX64) EmitMovReg64Imm64(c, kAddressReg, reinterpret_cast<uintptr_t>(addr));
}

CERFENGINE_FORCEINLINE void EmitHostAddressRex(uint8_t*& c, bool w, uint8_t reg) {
    EmitRex(c, w, reg, kX64 ? kAddressReg : 0u);
}

CERFENGINE_FORCEINLINE void EmitHostAddressModRm(uint8_t*& c, uint8_t reg, const void* addr) {
    uint8_t* p = c;
    if constexpr (kX64) {
        EmitModRmReg(p, 0, kAddressReg, reg);
    } else {
        EmitModRmDisp32(p, reg);
        Emit32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(addr)));
    }
    c = p;
}

CERFENGINE_FORCEINLINE void EmitOpHostAddress(uint8_t*& c, uint8_t opcode, uint8_t reg,
                                              const void* addr) {
    uint8_t* p = c;
    EmitHostAddressPrefix(p, addr);
    EmitHostAddressRex(p, false, reg);
    Emit8(p, opcode);
    EmitHostAddressModRm(p, reg, addr);
    c = p;
}

}

}
