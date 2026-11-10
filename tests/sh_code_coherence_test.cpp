#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {

using cerfengine::ShCpuState;

constexpr uint16_t kMovOneR0       = 0xE001u;
constexpr uint16_t kMovLR1AtR2     = 0x2212u;
constexpr uint16_t kMovThreeR0     = 0xE003u;
constexpr uint32_t kCodePa         = 0x8000u;
constexpr uint32_t kCodeBytes      = 6u;
constexpr uint32_t kAddressArrayVa = 0xF0000000u;

const std::vector<uint16_t> kCall                      = {kShRigJsrR3, kShRigNop};
const std::vector<uint16_t> kAddressArrayWriteThenCall = {kMovLR1AtR2, kShRigJsrR3, kShRigNop};

uint32_t CallTarget(ShIrRig& rig, std::mt19937& rng, const std::vector<uint16_t>& program, const char* what) {
    rig.Run(program, ShRigSeededState(rng), what, [](ShCpuState& s) {
        s.gpr[1] = 0u;
        s.gpr[2] = kAddressArrayVa;
        s.gpr[3] = kShRigVaBase | kCodePa;
    });
    return rig.State().gpr[0];
}

void RunAll(bool verify, const char* mode) {
    std::mt19937 rng(0xCAC4Eu);
    ShIrRig      rig(false, verify, true);
    rig.LoadCode(kCodePa, {kMovOneR0, kShRigRts, kShRigNop});
    const uint32_t first = CallTarget(rig, rng, kCall, "first call");
    if (first != 1u) TestFail("%s first call: R0 = %08X, want 1", mode, first);
    const uint32_t before_call = rig.BlocksCompiled();
    const uint32_t again       = CallTarget(rig, rng, kCall, "second call");
    if (again != 1u) TestFail("%s second call: R0 = %08X, want 1", mode, again);
    const uint32_t call_blocks  = rig.BlocksCompiled() - before_call;
    const uint32_t before_write = rig.BlocksCompiled();
    const uint32_t after_write =
        CallTarget(rig, rng, kAddressArrayWriteThenCall, "call after a cache address array write");
    if (after_write != 1u) TestFail("%s call after a cache address array write: R0 = %08X, want 1", mode, after_write);
    if (rig.BlocksCompiled() - before_write != call_blocks) {
        TestFail("%s call after a cache address array write: %u blocks compiled, want %u", mode,
                 rig.BlocksCompiled() - before_write, call_blocks);
    }
    rig.LoadCode(kCodePa, {kMovThreeR0, kShRigRts, kShRigNop});
    rig.BusMasterCoherence().AfterWrite(kCodePa, kCodeBytes);
    const uint32_t bus_master = CallTarget(rig, rng, kCall, "call after a bus master write");
    if (bus_master != 3u) TestFail("%s call after a bus master write: R0 = %08X, want 3", mode, bus_master);
    std::printf("%s: SH code coherence passed\n", mode);
}

}

int main() {
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    return 0;
}
