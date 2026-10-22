#pragma once

#include <cstddef>
#include <cstdint>

#include <cerfengine/byte_order.h>
#include <cerfengine/force_inline.h>

#include "emit_fault.h"

namespace cerfengine::a64 {

inline constexpr uint8_t kX0  = 0;
inline constexpr uint8_t kX1  = 1;
inline constexpr uint8_t kX2  = 2;
inline constexpr uint8_t kX3  = 3;
inline constexpr uint8_t kX4  = 4;
inline constexpr uint8_t kX5  = 5;
inline constexpr uint8_t kX6  = 6;
inline constexpr uint8_t kX7  = 7;
inline constexpr uint8_t kX8  = 8;
inline constexpr uint8_t kX9  = 9;
inline constexpr uint8_t kX10 = 10;
inline constexpr uint8_t kX11 = 11;
inline constexpr uint8_t kX12 = 12;
inline constexpr uint8_t kX13 = 13;
inline constexpr uint8_t kX14 = 14;
inline constexpr uint8_t kX15 = 15;
inline constexpr uint8_t kIp0 = 16;
inline constexpr uint8_t kIp1 = 17;
inline constexpr uint8_t kX19 = 19;
inline constexpr uint8_t kX20 = 20;
inline constexpr uint8_t kX21 = 21;
inline constexpr uint8_t kX22 = 22;
inline constexpr uint8_t kX23 = 23;
inline constexpr uint8_t kX24 = 24;
inline constexpr uint8_t kX25 = 25;
inline constexpr uint8_t kX26 = 26;
inline constexpr uint8_t kX27 = 27;
inline constexpr uint8_t kX28 = 28;
inline constexpr uint8_t kFp  = 29;
inline constexpr uint8_t kLr  = 30;
inline constexpr uint8_t kZr  = 31;
inline constexpr uint8_t kSp  = 31;

inline constexpr uint8_t kStateReg = kX28;
inline constexpr uint8_t kMmuReg   = kX27;

/* ARM DDI 0487 M.d C1.2.4 Table C1-1 Condition codes. */
enum Cond : uint8_t {
    kEq = 0x0,
    kNe = 0x1,
    kCs = 0x2,
    kCc = 0x3,
    kMi = 0x4,
    kPl = 0x5,
    kVs = 0x6,
    kVc = 0x7,
    kHi = 0x8,
    kLs = 0x9,
    kGe = 0xA,
    kLt = 0xB,
    kGt = 0xC,
    kLe = 0xD,
    kAl = 0xE,
};

CERFENGINE_FORCEINLINE constexpr uint8_t InvertCond(uint8_t cond) {
    return static_cast<uint8_t>(cond ^ 1u);
}

CERFENGINE_FORCEINLINE void Emit32(uint8_t*& c, uint32_t insn) {
    le::Put32(c, insn);
    c += 4;
}

CERFENGINE_FORCEINLINE constexpr uint32_t Sf(bool x64) { return x64 ? 0x80000000u : 0u; }

[[noreturn]] void BranchRangeFault(const char* what);

CERFENGINE_FORCEINLINE int64_t BranchDelta(const uint8_t* from, const void* to) {
    return reinterpret_cast<intptr_t>(to) - reinterpret_cast<intptr_t>(from);
}

CERFENGINE_FORCEINLINE bool BranchReaches26(const uint8_t* from, const void* to) {
    const int64_t delta = BranchDelta(from, to);
    return (delta & 3) == 0 && delta >= -(int64_t{1} << 27) && delta < (int64_t{1} << 27);
}

CERFENGINE_FORCEINLINE uint32_t Imm26(const uint8_t* from, const void* to) {
    if (!BranchReaches26(from, to)) BranchRangeFault("a 26-bit branch");
    return static_cast<uint32_t>(BranchDelta(from, to) >> 2) & 0x03FFFFFFu;
}

CERFENGINE_FORCEINLINE uint32_t Imm19(const uint8_t* from, const void* to) {
    const int64_t delta = BranchDelta(from, to);
    if ((delta & 3) != 0 || delta < -(int64_t{1} << 20) || delta >= (int64_t{1} << 20)) {
        BranchRangeFault("a 19-bit branch");
    }
    return static_cast<uint32_t>(delta >> 2) & 0x7FFFFu;
}

CERFENGINE_FORCEINLINE uint32_t Imm14(const uint8_t* from, const void* to) {
    const int64_t delta = BranchDelta(from, to);
    if ((delta & 3) != 0 || delta < -(int64_t{1} << 15) || delta >= (int64_t{1} << 15)) {
        BranchRangeFault("a 14-bit branch");
    }
    return static_cast<uint32_t>(delta >> 2) & 0x3FFFu;
}

/* ARM DDI 0487 M.d C6.2.35 B, C6.2.43 BL, C6.2.34 B.cond, C6.2.78 CBZ, C6.2.77 CBNZ,
   C6.2.479 TBZ, C6.2.478 TBNZ. */
CERFENGINE_FORCEINLINE void EmitB(uint8_t*& c, const void* to) { Emit32(c, 0x14000000u | Imm26(c, to)); }
CERFENGINE_FORCEINLINE void EmitBl(uint8_t*& c, const void* to) { Emit32(c, 0x94000000u | Imm26(c, to)); }

CERFENGINE_FORCEINLINE uint8_t* EmitBLabel(uint8_t*& c) {
    uint8_t* const site = c;
    Emit32(c, 0x14000000u);
    return site;
}

CERFENGINE_FORCEINLINE uint8_t* EmitBCondLabel(uint8_t*& c, uint8_t cond) {
    uint8_t* const site = c;
    Emit32(c, 0x54000000u | cond);
    return site;
}

CERFENGINE_FORCEINLINE uint8_t* EmitCbzLabel(uint8_t*& c, uint8_t rt, bool nonzero, bool x64) {
    uint8_t* const site = c;
    Emit32(c, (nonzero ? 0x35000000u : 0x34000000u) | Sf(x64) | rt);
    return site;
}

CERFENGINE_FORCEINLINE uint8_t* EmitTbzLabel(uint8_t*& c, uint8_t rt, uint32_t bit, bool nonzero) {
    uint8_t* const site = c;
    Emit32(c, (nonzero ? 0x37000000u : 0x36000000u) | ((bit >> 5) << 31) | ((bit & 31u) << 19) | rt);
    return site;
}

CERFENGINE_FORCEINLINE void FixupLabel(uint8_t* site, const void* to) {
    uint32_t insn = le::U32(site);
    if ((insn & 0xFC000000u) == 0x14000000u) {
        insn = (insn & 0xFC000000u) | Imm26(site, to);
    } else if ((insn & 0xFF000010u) == 0x54000000u) {
        insn = (insn & 0xFF00001Fu) | (Imm19(site, to) << 5);
    } else if ((insn & 0x7E000000u) == 0x34000000u) {
        insn = (insn & 0xFF00001Fu) | (Imm19(site, to) << 5);
    } else if ((insn & 0x7E000000u) == 0x36000000u) {
        insn = (insn & 0xFFF8001Fu) | (Imm14(site, to) << 5);
    } else {
        BranchRangeFault("a label fixup on a word that is not a branch");
    }
    le::Put32(site, insn);
}

/* ARM DDI 0487 M.d C6.2.46 BR, C6.2.44 BLR, C6.2.338 RET. */
CERFENGINE_FORCEINLINE void EmitBr(uint8_t*& c, uint8_t rn) { Emit32(c, 0xD61F0000u | (rn << 5)); }
CERFENGINE_FORCEINLINE void EmitBlr(uint8_t*& c, uint8_t rn) { Emit32(c, 0xD63F0000u | (rn << 5)); }
CERFENGINE_FORCEINLINE void EmitRet(uint8_t*& c, uint8_t rn = kLr) { Emit32(c, 0xD65F0000u | (rn << 5)); }

/* ARM DDI 0487 M.d C6.2.12 ADR. */
CERFENGINE_FORCEINLINE void EmitAdr(uint8_t*& c, uint8_t rd, const void* to) {
    const int64_t delta = BranchDelta(c, to);
    if (delta < -(int64_t{1} << 20) || delta >= (int64_t{1} << 20)) BranchRangeFault("an ADR");
    const uint32_t imm = static_cast<uint32_t>(delta) & 0x1FFFFFu;
    Emit32(c, 0x10000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | rd);
}

/* ARM DDI 0487 M.d C6.2.299 NOP. */
CERFENGINE_FORCEINLINE void EmitNop(uint8_t*& c) { Emit32(c, 0xD503201Fu); }

/* ARM DDI 0487 M.d C6.2.285 MOVZ, C6.2.284 MOVN, C6.2.283 MOVK. */
CERFENGINE_FORCEINLINE void EmitMovz(uint8_t*& c, uint8_t rd, uint32_t imm16, uint32_t hw, bool x64) {
    Emit32(c, 0x52800000u | Sf(x64) | (hw << 21) | (imm16 << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitMovn(uint8_t*& c, uint8_t rd, uint32_t imm16, uint32_t hw, bool x64) {
    Emit32(c, 0x12800000u | Sf(x64) | (hw << 21) | (imm16 << 5) | rd);
}
CERFENGINE_FORCEINLINE void EmitMovk(uint8_t*& c, uint8_t rd, uint32_t imm16, uint32_t hw, bool x64) {
    Emit32(c, 0x72800000u | Sf(x64) | (hw << 21) | (imm16 << 5) | rd);
}

bool EncodeLogicalImmediate(uint64_t value, bool x64, uint32_t& n_immr_imms);

void EmitMovImm32(uint8_t*& c, uint8_t rd, uint32_t value);
void EmitMovImm64(uint8_t*& c, uint8_t rd, uint64_t value);

CERFENGINE_FORCEINLINE void EmitMovPtr(uint8_t*& c, uint8_t rd, const void* p) {
    EmitMovImm64(c, rd, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)));
}

CERFENGINE_FORCEINLINE void EmitCall(uint8_t*& c, const void* fn) {
    if (BranchReaches26(c, fn)) {
        EmitBl(c, fn);
        return;
    }
    EmitMovPtr(c, kIp0, fn);
    EmitBlr(c, kIp0);
}

}
