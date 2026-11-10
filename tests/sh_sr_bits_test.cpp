#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <random>

namespace {

using cerfengine::kShSrM;
using cerfengine::kShSrQ;
using cerfengine::kShSrS;
using cerfengine::kShSrT;
using cerfengine::ShCpuState;

constexpr uint16_t kMovtR2 = 0x0229u;

struct Form {
    const char* what;
    uint16_t    op;
    uint32_t    bit;
    bool        set;
};

constexpr Form kForms[] = {
    {"CLRT", 0x0008u, kShSrT, false},
    {"SETT", 0x0018u, kShSrT, true},
    {"CLRS", 0x0048u, kShSrS, false},
    {"SETS", 0x0058u, kShSrS, true},
};

uint32_t FlagCombination(uint32_t bits) {
    return ((bits & 1u) != 0u ? kShSrT : 0u) | ((bits & 2u) != 0u ? kShSrS : 0u) | ((bits & 4u) != 0u ? kShSrQ : 0u) |
           ((bits & 8u) != 0u ? kShSrM : 0u);
}

void RunAll(bool dsp, bool verify, const char* mode) {
    ShIrRig      rig(dsp, verify);
    std::mt19937 rng(0x5E7B175u);
    uint32_t     runs = 0;
    for (const Form& f : kForms) {
        for (uint32_t bits = 0; bits < 16u; ++bits) {
            ShCpuState init = ShRigSeededState(rng);
            init.sr         = kShRigSrBase | FlagCombination(bits);
            ShCpuState want = init;
            want.sr         = f.set ? init.sr | f.bit : init.sr & ~f.bit;
            want.gpr[2]     = want.sr & kShSrT;
            rig.Run({f.op, kMovtR2}, init, f.what);
            ShRigCheckState(rig.State(), want, mode, f.what, f.op);
            ++runs;
        }
    }
    std::printf("%s: %u SH SR bit runs passed\n", mode, runs);
}

}

int main() {
    RunAll(false, false, "plain");
    RunAll(false, true, "verified IR");
    RunAll(true, false, "SH3-DSP");
    return 0;
}
