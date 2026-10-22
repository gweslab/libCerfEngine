#pragma once

#include <cstdint>

#include "a64_emit.h"

namespace cerfengine::a64 {

enum MemSize : uint8_t { kByte = 0, kHalf = 1, kWord = 2, kDouble = 3 };

enum MemOpc : uint8_t {
    kStore        = 0,
    kLoad         = 1,
    kLoadSigned64 = 2,
    kLoadSigned32 = 3,
};

[[noreturn]] void MemoryOffsetFault(int64_t offset);

/* ARM DDI 0487 M.d C4.1 "Load/store register (unsigned immediate)": size 111 VR 01 opc imm12 Rn Rt,
   imm12 scaled by the access size (C6.2.216 LDR (immediate), C6.2.415 STR (immediate)). */
CERFENGINE_FORCEINLINE bool ScaledOffsetFits(MemSize size, int64_t offset) {
    return offset >= 0 && (offset & ((int64_t{1} << size) - 1)) == 0 && (offset >> size) < 4096;
}

/* ARM DDI 0487 M.d C6.2.259 LDUR, C6.2.446 STUR: size 111 VR 00 opc 0 imm9 00 Rn Rt. */
CERFENGINE_FORCEINLINE bool UnscaledOffsetFits(int64_t offset) {
    return offset >= -256 && offset <= 255;
}

CERFENGINE_FORCEINLINE bool OffsetFits(MemSize size, int64_t offset) {
    return ScaledOffsetFits(size, offset) || UnscaledOffsetFits(offset);
}

CERFENGINE_FORCEINLINE void EmitLoadStoreImm(uint8_t*& c, MemSize size, MemOpc opc, uint8_t rt, uint8_t rn,
                                             int64_t offset) {
    const uint32_t head = (uint32_t{size} << 30) | (uint32_t{opc} << 22) | (uint32_t{rn} << 5) | rt;
    if (ScaledOffsetFits(size, offset)) {
        Emit32(c, 0x39000000u | head | (static_cast<uint32_t>(offset >> size) << 10));
        return;
    }
    if (UnscaledOffsetFits(offset)) {
        Emit32(c, 0x38000000u | head | ((static_cast<uint32_t>(offset) & 0x1FFu) << 12));
        return;
    }
    MemoryOffsetFault(offset);
}

/* ARM DDI 0487 M.d C4.1 "Load/store register (register offset)": size 111 VR 00 opc 1 Rm option S 10
   Rn Rt; option 011 adds the 64-bit Rm, and S 1 shifts it left by the access size
   (C6.2.218 LDR (register): "shift = if S == '1' then scale else 0"). */
CERFENGINE_FORCEINLINE void EmitLoadStoreIndexed(uint8_t*& c, MemSize size, MemOpc opc, uint8_t rt,
                                                 uint8_t rn, uint8_t rm, bool scaled = false) {
    Emit32(c, 0x38206800u | (uint32_t{size} << 30) | (uint32_t{opc} << 22) | (uint32_t{rm} << 16) |
                  (scaled ? 0x1000u : 0u) | (uint32_t{rn} << 5) | rt);
}

CERFENGINE_FORCEINLINE void EmitLoadStore(uint8_t*& c, MemSize size, MemOpc opc, uint8_t rt, uint8_t rn,
                                          int64_t offset) {
    if (OffsetFits(size, offset)) {
        EmitLoadStoreImm(c, size, opc, rt, rn, offset);
        return;
    }
    if (offset < INT32_MIN || offset > INT32_MAX) MemoryOffsetFault(offset);
    EmitMovImm64(c, kIp1, static_cast<uint64_t>(offset));
    EmitLoadStoreIndexed(c, size, opc, rt, rn, kIp1);
}

CERFENGINE_FORCEINLINE void EmitLdrW(uint8_t*& c, uint8_t rt, uint8_t rn, int64_t offset) {
    EmitLoadStore(c, kWord, kLoad, rt, rn, offset);
}
CERFENGINE_FORCEINLINE void EmitStrW(uint8_t*& c, uint8_t rt, uint8_t rn, int64_t offset) {
    EmitLoadStore(c, kWord, kStore, rt, rn, offset);
}
CERFENGINE_FORCEINLINE void EmitLdrX(uint8_t*& c, uint8_t rt, uint8_t rn, int64_t offset) {
    EmitLoadStore(c, kDouble, kLoad, rt, rn, offset);
}
CERFENGINE_FORCEINLINE void EmitStrX(uint8_t*& c, uint8_t rt, uint8_t rn, int64_t offset) {
    EmitLoadStore(c, kDouble, kStore, rt, rn, offset);
}
CERFENGINE_FORCEINLINE void EmitLdrb(uint8_t*& c, uint8_t rt, uint8_t rn, int64_t offset) {
    EmitLoadStore(c, kByte, kLoad, rt, rn, offset);
}
CERFENGINE_FORCEINLINE void EmitStrb(uint8_t*& c, uint8_t rt, uint8_t rn, int64_t offset) {
    EmitLoadStore(c, kByte, kStore, rt, rn, offset);
}

/* ARM DDI 0487 M.d C6.2.414 STP, C6.2.214 LDP (64-bit): opc 10 101 0 class L imm7 Rt2 Rn Rt;
   class 001 post-index, 010 signed offset, 011 pre-index; imm7 is the byte offset / 8. */
CERFENGINE_FORCEINLINE void EmitPair(uint8_t*& c, uint32_t base, uint8_t rt, uint8_t rt2, uint8_t rn,
                                     int32_t offset) {
    if ((offset & 7) != 0 || offset < -512 || offset > 504) MemoryOffsetFault(offset);
    Emit32(c, base | ((static_cast<uint32_t>(offset / 8) & 0x7Fu) << 15) | (uint32_t{rt2} << 10) |
                  (uint32_t{rn} << 5) | rt);
}
CERFENGINE_FORCEINLINE void EmitStpPre(uint8_t*& c, uint8_t rt, uint8_t rt2, uint8_t rn, int32_t offset) {
    EmitPair(c, 0xA9800000u, rt, rt2, rn, offset);
}
CERFENGINE_FORCEINLINE void EmitLdpPost(uint8_t*& c, uint8_t rt, uint8_t rt2, uint8_t rn, int32_t offset) {
    EmitPair(c, 0xA8C00000u, rt, rt2, rn, offset);
}

/* ARM DDI 0487 M.d C6.2.214 LDP, C6.2.414 STP, signed offset 32-bit variant: opc 00 101 0 010 L
   imm7 Rt2 Rn Rt; the offset is a multiple of 4 in -256..252, encoded as offset / 4. */
CERFENGINE_FORCEINLINE bool PairWOffsetFits(int64_t offset) {
    return (offset & 3) == 0 && offset >= -256 && offset <= 252;
}
CERFENGINE_FORCEINLINE void EmitPairW(uint8_t*& c, uint32_t base, uint8_t rt, uint8_t rt2, uint8_t rn,
                                      int32_t offset) {
    if (!PairWOffsetFits(offset)) MemoryOffsetFault(offset);
    Emit32(c, base | ((static_cast<uint32_t>(offset / 4) & 0x7Fu) << 15) | (uint32_t{rt2} << 10) |
                  (uint32_t{rn} << 5) | rt);
}
CERFENGINE_FORCEINLINE void EmitStpWOffset(uint8_t*& c, uint8_t rt, uint8_t rt2, uint8_t rn, int32_t offset) {
    EmitPairW(c, 0x29000000u, rt, rt2, rn, offset);
}
CERFENGINE_FORCEINLINE void EmitLdpWOffset(uint8_t*& c, uint8_t rt, uint8_t rt2, uint8_t rn, int32_t offset) {
    EmitPairW(c, 0x29400000u, rt, rt2, rn, offset);
}

}
