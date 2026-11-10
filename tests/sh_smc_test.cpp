#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

using cerfengine::ShCpuState;

constexpr uint16_t kMovR0R5   = 0x6503u;
constexpr uint16_t kMovWR1R2  = 0x2211u;
constexpr uint16_t kMovOneR0  = 0xE001u;
constexpr uint16_t kMovTwoR0  = 0xE002u;
constexpr uint32_t kP1Base    = 0x80000000u;
constexpr uint32_t kP2Base    = 0xA0000000u;
constexpr uint32_t kMirror    = kP2Base | kShRigRamSize;
constexpr uint32_t kBesideGap = 0x20u;

const std::vector<uint16_t> kCallStoreCall = {kShRigJsrR3,   kShRigNop, kShRigJsrR3, kShRigNop,
                                              kMovR0R5, kMovWR1R2, kShRigJsrR3, kShRigNop};

struct Case {
    const char* name;
    uint32_t    code_pa;
    uint32_t    fetch_base;
    uint32_t    store_offset;
    uint32_t    store_base;
    uint16_t    store_value;
    uint32_t    want_r0;
};

uint32_t RunCase(ShIrRig& rig, std::mt19937& rng, const Case& c, const char* mode) {
    rig.LoadCode(c.code_pa, {kMovOneR0, kShRigRts, kShRigNop});
    const uint32_t target_va = c.code_pa | c.fetch_base;
    const uint32_t store_va  = (c.code_pa + c.store_offset) | c.store_base;
    const uint32_t before    = rig.BlocksCompiled();
    rig.Run(kCallStoreCall, ShRigSeededState(rng), c.name, [&](ShCpuState& s) {
        s.gpr[1] = c.store_value;
        s.gpr[2] = store_va;
        s.gpr[3] = target_va;
    });
    const ShCpuState& s = rig.State();
    if (s.gpr[5] != 1u || s.gpr[0] != c.want_r0) {
        TestFail("%s %s: R5 = %08X R0 = %08X, want 1 and %u", mode, c.name, s.gpr[5], s.gpr[0], c.want_r0);
    }
    return rig.BlocksCompiled() - before;
}

void RunOwnBlockStore(ShIrRig& rig, std::mt19937& rng, const char* mode) {
    const std::string halt = rig.RunToHalt({kMovWR1R2, kShRigNop, kShRigNop}, ShRigSeededState(rng),
                                           "own-block store", [](ShCpuState& s) {
                                               s.gpr[1] = kShRigNop;
                                               s.gpr[2] = s.pc + 4u;
                                           });
    if (halt.find("rewrites the translated block that holds it") == std::string::npos) {
        TestFail("%s own-block store: halt \"%s\"", mode, halt.c_str());
    }
}

void RunAll(bool verify, const char* mode) {
    std::mt19937 rng(0x5C0DEu);
    ShIrRig      rig(false, verify, true);
    const uint32_t rewrite =
        RunCase(rig, rng, {"rewrite through P2", 0x8000u, kP2Base, 0u, kP2Base, kMovTwoR0, 2u}, mode);
    RunCase(rig, rng, {"rewrite through P1", 0x8400u, kP2Base, 0u, kP1Base, kMovTwoR0, 2u}, mode);
    const uint32_t beside =
        RunCase(rig, rng, {"store beside code", 0x8800u, kP2Base, kBesideGap, kP2Base, kMovTwoR0, 1u}, mode);
    if (rewrite != beside + 1u) {
        TestFail("%s: %u compiles after a rewrite, %u after a store beside the code", mode, rewrite, beside);
    }
    RunCase(rig, rng, {"rewrite of a delay slot on the next page", 0x8BFCu, kP2Base, 4u, kP2Base, kMovTwoR0, 2u},
            mode);
    rig.Host().MirrorRamAt(kShRigRamSize);
    RunCase(rig, rng, {"store through the repeated RAM decode", 0x8C40u, kP2Base, 0u, kMirror, kMovTwoR0, 2u}, mode);
    RunCase(rig, rng, {"fetch through the repeated RAM decode", 0x8C80u, kMirror, 0u, kP2Base, kMovTwoR0, 2u}, mode);
    RunOwnBlockStore(rig, rng, mode);
    std::printf("%s: SH self-modifying code passed\n", mode);
}

}

int main() {
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    return 0;
}
