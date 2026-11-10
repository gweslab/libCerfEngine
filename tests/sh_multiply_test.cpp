#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <random>

namespace {

using cerfengine::kShNumGpr;
using cerfengine::ShCpuState;

enum class Kind { kMulL, kMulsW, kMuluW, kDmulsL, kDmuluL, kClrmac };

struct Insn {
    const char* name;
    Kind        kind;
    uint16_t    pattern;
    uint32_t    extra_cycles;
};

constexpr Insn kInsns[] = {
    {"MUL.L", Kind::kMulL, 0x0007u, 1u},     {"MULS.W", Kind::kMulsW, 0x200Fu, 0u},
    {"MULU.W", Kind::kMuluW, 0x200Eu, 0u},   {"DMULS.L", Kind::kDmulsL, 0x300Du, 1u},
    {"DMULU.L", Kind::kDmuluL, 0x3005u, 1u}, {"CLRMAC", Kind::kClrmac, 0x0028u, 0u},
};

constexpr uint32_t kEdges[] = {0x00000000u, 0x00000001u, 0x00000002u, 0x00005555u, 0x00007FFFu, 0x00008000u,
                               0x0000FFFFu, 0x00010000u, 0x12345678u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u,
                               0xFFFF0000u, 0xFFFF8000u, 0xFFFFFFFEu, 0xFFFFFFFFu};

void ApplyReference(Kind kind, uint32_t rn, uint32_t rm, ShCpuState& s) {
    switch (kind) {
    case Kind::kMulL: s.macl = rn * rm; return;
    case Kind::kMulsW:
        s.macl = static_cast<uint32_t>(static_cast<int32_t>(ShRigSignExtend(rn, 2u)) *
                                       static_cast<int32_t>(ShRigSignExtend(rm, 2u)));
        return;
    case Kind::kMuluW: s.macl = (rn & 0xFFFFu) * (rm & 0xFFFFu); return;
    case Kind::kDmulsL: {
        const uint64_t p = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(rn)) *
                                                 static_cast<int64_t>(static_cast<int32_t>(rm)));
        s.mach           = static_cast<uint32_t>(p >> 32);
        s.macl           = static_cast<uint32_t>(p);
        return;
    }
    case Kind::kDmuluL: {
        const uint64_t p = static_cast<uint64_t>(rn) * static_cast<uint64_t>(rm);
        s.mach           = static_cast<uint32_t>(p >> 32);
        s.macl           = static_cast<uint32_t>(p);
        return;
    }
    case Kind::kClrmac:
        s.mach = 0u;
        s.macl = 0u;
        return;
    }
    TestFail("unknown kind");
}

uint16_t Encode(const Insn& insn, uint32_t n, uint32_t m) {
    if (insn.kind == Kind::kClrmac) return insn.pattern;
    return static_cast<uint16_t>(insn.pattern | (n << 8) | (m << 4));
}

uint32_t RunCase(ShIrRig& rig, std::mt19937& rng, const Insn& insn, uint32_t n, uint32_t m, uint32_t rn,
                 uint32_t rm, const char* mode) {
    ShCpuState init = ShRigSeededState(rng);
    init.gpr[n]     = rn;
    init.gpr[m]     = rm;
    const uint16_t op   = Encode(insn, n, m);
    ShCpuState     want = init;
    ApplyReference(insn.kind, init.gpr[n], init.gpr[m], want);
    rig.Run({op}, init, insn.name);
    ShRigCheckState(rig.State(), want, mode, insn.name, op);
    return rig.State().guest_cycle_counter;
}

void RunManualExamples(ShIrRig& rig, std::mt19937& rng, const char* mode) {
    struct Example {
        uint16_t op;
        uint32_t r0;
        uint32_t r1;
        bool     mach_written;
        uint32_t mach;
        uint32_t macl;
    };
    constexpr Example kExamples[] = {
        {0x0107u, 0xFFFFFFFEu, 0x00005555u, false, 0u, 0xFFFF5556u},
        {0x210Fu, 0xFFFFFFFEu, 0x00005555u, false, 0u, 0xFFFF5556u},
        {0x210Eu, 0x00000002u, 0xFFFFAAAAu, false, 0u, 0x00015554u},
        {0x310Du, 0xFFFFFFFEu, 0x00005555u, true, 0xFFFFFFFFu, 0xFFFF5556u},
    };
    for (const Example& e : kExamples) {
        ShCpuState init = ShRigSeededState(rng);
        init.gpr[0]     = e.r0;
        init.gpr[1]     = e.r1;
        ShCpuState want = init;
        want.macl       = e.macl;
        if (e.mach_written) want.mach = e.mach;
        rig.Run({e.op}, init, "manual example");
        ShRigCheckState(rig.State(), want, mode, "manual example", e.op);
    }
}

void RunAll(bool dsp, bool verify, const char* mode) {
    std::mt19937 rng(0x4D554Cu);
    uint32_t     runs = 0;
    {
        ShIrRig rig(dsp, verify);
        RunManualExamples(rig, rng, mode);
    }
    for (const Insn& insn : kInsns) {
        ShIrRig rig(dsp, verify);
        rig.Run({kShRigNop}, ShRigSeededState(rng), "NOP");
        const uint32_t nop_cycles = rig.State().guest_cycle_counter;
        const uint32_t cycles     = RunCase(rig, rng, insn, 1u, 2u, 3u, 5u, mode);
        if (cycles != nop_cycles + insn.extra_cycles) {
            TestFail("%s %s: %u cycles, NOP %u, want %u more", mode, insn.name, cycles, nop_cycles, insn.extra_cycles);
        }
        for (uint32_t n = 0; n < kShNumGpr; ++n) {
            for (uint32_t m = 0; m < kShNumGpr; ++m) {
                for (uint32_t i = 0; i < 4u; ++i) {
                    RunCase(rig, rng, insn, n, m, kEdges[(n + i * 5u) % std::size(kEdges)],
                            kEdges[(m * 3u + i) % std::size(kEdges)], mode);
                    RunCase(rig, rng, insn, n, m, rng(), rng(), mode);
                    runs += 2u;
                }
            }
        }
        for (uint32_t a : kEdges) {
            for (uint32_t b : kEdges) {
                RunCase(rig, rng, insn, 4u, 6u, a, b, mode);
                ++runs;
            }
        }
    }
    std::printf("%s: %u SH multiply runs passed\n", mode, runs);
}

}

int main() {
    RunAll(false, false, "plain");
    RunAll(false, true, "verified IR");
    RunAll(true, false, "SH3-DSP");
    return 0;
}
