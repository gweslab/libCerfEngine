#pragma once

#include "sh_ir_rig.h"

#include <cstdint>
#include <vector>

constexpr uint16_t kDspSxX0 = 0u;
constexpr uint16_t kDspSxX1 = 1u;
constexpr uint16_t kDspSxA0 = 2u;
constexpr uint16_t kDspSyY0 = 0u;
constexpr uint16_t kDspSyY1 = 1u;
constexpr uint16_t kDspSyM0 = 2u;

constexpr uint16_t kDspA1 = 0x5u;
constexpr uint16_t kDspA0 = 0x7u;
constexpr uint16_t kDspX0 = 0x8u;
constexpr uint16_t kDspX1 = 0x9u;
constexpr uint16_t kDspY1 = 0xBu;
constexpr uint16_t kDspM0 = 0xCu;

constexpr uint16_t kDspSeX0 = 0u;
constexpr uint16_t kDspSeX1 = 1u;
constexpr uint16_t kDspSfY0 = 0u;
constexpr uint16_t kDspSfY1 = 1u;
constexpr uint16_t kDspDgM0 = 0u;
constexpr uint16_t kDspDgA0 = 2u;
constexpr uint16_t kDspDuA0 = 2u;

constexpr uint32_t kDspSr = kShRigSrPrivileged | cerfengine::kShSrDsp;

constexpr uint16_t DspOp3(uint16_t hi, uint16_t sx, uint16_t sy, uint16_t dz) {
    return static_cast<uint16_t>((hi << 8) | (sx << 6) | (sy << 4) | dz);
}

constexpr uint16_t DspMultiply(uint16_t kind, uint16_t se, uint16_t sf, uint16_t sx, uint16_t sy, uint16_t dg,
                               uint16_t du) {
    return static_cast<uint16_t>((kind << 12) | (se << 10) | (sf << 8) | (sx << 6) | (sy << 4) | (dg << 2) | du);
}

constexpr uint16_t DspShiftImmediate(uint16_t kind, int32_t amount, uint16_t dz) {
    return static_cast<uint16_t>((kind << 11) | ((static_cast<uint32_t>(amount) & 0x7Fu) << 4) | dz);
}

inline std::vector<uint16_t> DspParallel(uint16_t field_a, uint16_t field_b) {
    return {static_cast<uint16_t>(0xF800u | field_a), field_b};
}

struct DspRegs {
    uint64_t a0   = 0;
    uint64_t a1   = 0;
    uint32_t x0   = 0;
    uint32_t x1   = 0;
    uint32_t y0   = 0;
    uint32_t y1   = 0;
    uint32_t m0   = 0;
    uint32_t m1   = 0;
    uint32_t dsr  = 0;
    uint32_t mach = 0;
    uint32_t macl = 0;
};

inline void DspLoad(cerfengine::ShCpuState& s, const DspRegs& r) {
    s.a0   = static_cast<uint32_t>(r.a0);
    s.a0g  = static_cast<uint32_t>(r.a0 >> 32) & 0xFFu;
    s.a1   = static_cast<uint32_t>(r.a1);
    s.a1g  = static_cast<uint32_t>(r.a1 >> 32) & 0xFFu;
    s.x0   = r.x0;
    s.x1   = r.x1;
    s.y0   = r.y0;
    s.y1   = r.y1;
    s.m0   = r.m0;
    s.m1   = r.m1;
    s.dsr  = r.dsr;
    s.mach = r.mach;
    s.macl = r.macl;
}

inline void DspExpect(const char* mode, const char* what, const cerfengine::ShCpuState& s, const DspRegs& want) {
    ShRigExpect(mode, what, "A0G", s.a0g, static_cast<uint32_t>(want.a0 >> 32) & 0xFFu);
    ShRigExpect(mode, what, "A0", s.a0, static_cast<uint32_t>(want.a0));
    ShRigExpect(mode, what, "A1G", s.a1g, static_cast<uint32_t>(want.a1 >> 32) & 0xFFu);
    ShRigExpect(mode, what, "A1", s.a1, static_cast<uint32_t>(want.a1));
    ShRigExpect(mode, what, "X0", s.x0, want.x0);
    ShRigExpect(mode, what, "X1", s.x1, want.x1);
    ShRigExpect(mode, what, "Y0", s.y0, want.y0);
    ShRigExpect(mode, what, "Y1", s.y1, want.y1);
    ShRigExpect(mode, what, "M0", s.m0, want.m0);
    ShRigExpect(mode, what, "M1", s.m1, want.m1);
    ShRigExpect(mode, what, "DSR", s.dsr, want.dsr);
    ShRigExpect(mode, what, "MACH", s.mach, want.mach);
    ShRigExpect(mode, what, "MACL", s.macl, want.macl);
}

constexpr uint32_t kDspXyPa    = 0x05000000u;
constexpr uint32_t kDspXyYHalf = 0x00010000u;

inline uint32_t DspXyRamOffset(bool x, uint32_t address) { return (x ? 0u : kDspXyYHalf) + address; }

inline void DspMapXyMemory(ShIrRig& rig) { rig.Host().MirrorRamAt(kDspXyPa); }

inline void DspSetXyPair(ShIrRig& rig, bool x, uint32_t address, uint16_t first, uint16_t second) {
    const uint32_t word = static_cast<uint32_t>(first) | (static_cast<uint32_t>(second) << 16);
    rig.Host().LoadWords(kShRigRamPa + DspXyRamOffset(x, address), &word, 1u);
}

inline uint16_t DspXyWord(ShIrRig& rig, bool x, uint32_t address) {
    return cerfengine::le::U16(&rig.Host().Ram()[DspXyRamOffset(x, address)]);
}
