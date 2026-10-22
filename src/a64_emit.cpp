#include "a64_emit.h"

#include <bit>
#include <cstdio>

#include "a64_emit_alu.h"
#include "a64_emit_memory.h"

namespace cerfengine::a64 {

void BranchRangeFault(const char* what) {
    char text[128];
    std::snprintf(text, sizeof(text), "A64 emitter: %s does not reach its target", what);
    throw EmitFault(text);
}

void ImmediateFault(const char* what, uint64_t value) {
    char text[128];
    std::snprintf(text, sizeof(text), "A64 emitter: %s has no encoding for 0x%llX", what,
                  static_cast<unsigned long long>(value));
    throw EmitFault(text);
}

void MemoryOffsetFault(int64_t offset) {
    char text[128];
    std::snprintf(text, sizeof(text), "A64 emitter: a memory offset of %lld has no encoding",
                  static_cast<long long>(offset));
    throw EmitFault(text);
}

/* Inverse of ARM DDI 0487 M.d J1.2.3.19 DecodeBitMasks(): an element of esize bits holding
   s + 1 ones rotated right by r, replicated across the register. */
bool EncodeLogicalImmediate(uint64_t value, bool x64, uint32_t& n_immr_imms) {
    const uint32_t width = x64 ? 64u : 32u;
    const uint64_t all   = x64 ? ~uint64_t{0} : uint64_t{0xFFFFFFFFu};
    value &= all;
    if (value == 0u || value == all) return false;
    uint32_t esize = width;
    while (esize > 2u) {
        const uint32_t half = esize / 2u;
        const uint64_t mask = (uint64_t{1} << half) - 1u;
        if (((value >> half) & mask) != (value & mask)) break;
        esize = half;
    }
    const uint64_t emask = esize == 64u ? ~uint64_t{0} : (uint64_t{1} << esize) - 1u;
    const uint64_t elem  = value & emask;
    const uint32_t ones  = static_cast<uint32_t>(std::popcount(elem));
    const uint64_t low   = (uint64_t{1} << ones) - 1u;
    for (uint32_t r = 0; r < esize; ++r) {
        const uint64_t rotated = r == 0u ? elem : (((elem << r) | (elem >> (esize - r))) & emask);
        if (rotated != low) continue;
        const uint32_t n    = esize == 64u ? 1u : 0u;
        const uint32_t imms = ((~(esize * 2u - 1u)) & 0x3Fu) | (ones - 1u);
        n_immr_imms         = (n << 12) | (r << 6) | imms;
        return true;
    }
    return false;
}

void EmitMovImm32(uint8_t*& c, uint8_t rd, uint32_t value) {
    const uint32_t lo = value & 0xFFFFu;
    const uint32_t hi = value >> 16;
    if (hi == 0u) {
        EmitMovz(c, rd, lo, 0u, false);
        return;
    }
    if (lo == 0u) {
        EmitMovz(c, rd, hi, 1u, false);
        return;
    }
    if (hi == 0xFFFFu) {
        EmitMovn(c, rd, ~lo & 0xFFFFu, 0u, false);
        return;
    }
    if (lo == 0xFFFFu) {
        EmitMovn(c, rd, ~hi & 0xFFFFu, 1u, false);
        return;
    }
    uint32_t fields = 0u;
    if (EncodeLogicalImmediate(value, false, fields)) {
        Emit32(c, 0x32000000u | ((fields >> 12) << 22) | (((fields >> 6) & 0x3Fu) << 16) |
                      ((fields & 0x3Fu) << 10) | (kZr << 5) | rd);
        return;
    }
    EmitMovz(c, rd, lo, 0u, false);
    EmitMovk(c, rd, hi, 1u, false);
}

void EmitMovImm64(uint8_t*& c, uint8_t rd, uint64_t value) {
    uint32_t zeros = 0u;
    uint32_t ffffs = 0u;
    for (uint32_t hw = 0; hw < 4u; ++hw) {
        const uint32_t part = static_cast<uint32_t>(value >> (hw * 16u)) & 0xFFFFu;
        if (part == 0u) ++zeros;
        if (part == 0xFFFFu) ++ffffs;
    }
    const uint32_t best   = zeros > ffffs ? zeros : ffffs;
    uint32_t       fields = 0u;
    if (best < 3u && EncodeLogicalImmediate(value, true, fields)) {
        Emit32(c, 0xB2000000u | ((fields >> 12) << 22) | (((fields >> 6) & 0x3Fu) << 16) |
                      ((fields & 0x3Fu) << 10) | (kZr << 5) | rd);
        return;
    }
    const bool     inverted = ffffs > zeros;
    const uint32_t skip     = inverted ? 0xFFFFu : 0u;
    bool           first    = true;
    for (uint32_t hw = 0; hw < 4u; ++hw) {
        const uint32_t part = static_cast<uint32_t>(value >> (hw * 16u)) & 0xFFFFu;
        if (part == skip) continue;
        if (first) {
            if (inverted) {
                EmitMovn(c, rd, ~part & 0xFFFFu, hw, true);
            } else {
                EmitMovz(c, rd, part, hw, true);
            }
            first = false;
        } else {
            EmitMovk(c, rd, part, hw, true);
        }
    }
    if (first) {
        if (inverted) {
            EmitMovn(c, rd, 0u, 0u, true);
        } else {
            EmitMovz(c, rd, 0u, 0u, true);
        }
    }
}

}
