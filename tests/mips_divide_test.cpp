#include "mips_ir_rig.h"

#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kFunctDiv  = 0x1Au;
constexpr uint32_t kFunctDivu = 0x1Bu;
constexpr uint32_t kNop       = 0u;

constexpr uint32_t RType(uint32_t rs, uint32_t rt, uint32_t funct) {
    return (rs << 21) | (rt << 16) | funct;
}

constexpr uint32_t JumpTo(uint32_t va) { return (0x02u << 26) | ((va >> 2) & 0x03FFFFFFu); }

struct Case {
    const char* name;
    uint32_t    funct;
    uint32_t    rs;
    uint32_t    rt;
    uint64_t    r1;
    uint64_t    r2;
    uint64_t    lo;
    uint64_t    hi;
};

void RunCase(MipsIrRig& rig, const Case& k, const char* mode) {
    rig.Load(kMipsRigVaBase, {RType(k.rs, k.rt, k.funct), JumpTo(kMipsRigEndVa), kNop});
    cerfengine::MipsCpuState init{};
    init.pc     = kMipsRigVaBase;
    init.gpr[1] = k.r1;
    init.gpr[2] = k.r2;
    init.lo     = 0x5A5A5A5A5A5A5A5Aull;
    init.hi     = 0xA5A5A5A5A5A5A5A5ull;
    rig.Begin(init);
    for (uint32_t step = 0; step < 50u && !rig.Finished(); ++step) {
        if (rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted) {
            TestFail("%s %s: halted: %s", mode, k.name, rig.Engine().LastHalt().detail);
        }
    }
    const cerfengine::MipsCpuState& s = rig.State();
    if (!rig.Finished() || rig.Events().exceptions != 0u || s.lo != k.lo || s.hi != k.hi) {
        TestFail("%s %s: lo %016llX hi %016llX, expected lo %016llX hi %016llX", mode, k.name,
                 static_cast<unsigned long long>(s.lo), static_cast<unsigned long long>(s.hi),
                 static_cast<unsigned long long>(k.lo), static_cast<unsigned long long>(k.hi));
    }
}

void RunCore(const cerfengine::MipsFeatures& features, uint32_t page_shift, bool verify,
             const std::vector<Case>& cases, const char* mode) {
    MipsIrRig rig(features, page_shift, verify);
    rig.Load(kMipsRigEndVa, {0x1000FFFFu, 0u, 0u, 0u, 0x650017FFu});
    for (const Case& k : cases) RunCase(rig, k, mode);
}

}

int main() {
    const std::vector<Case> cases = {
        {"div 7 by 2", kFunctDiv, 1u, 2u, 7u, 2u, 0x3u, 0x1u},
        {"div -7 by 2", kFunctDiv, 1u, 2u, 0xFFFFFFFFFFFFFFF9ull, 2u, 0xFFFFFFFFFFFFFFFDull,
         0xFFFFFFFFFFFFFFFFull},
        {"div 7 by -2", kFunctDiv, 1u, 2u, 7u, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFDull, 0x1u},
        {"div min by -1", kFunctDiv, 1u, 2u, 0xFFFFFFFF80000000ull, 0xFFFFFFFFFFFFFFFFull,
         0xFFFFFFFF80000000ull, 0u},
        {"div 9 by 0", kFunctDiv, 1u, 2u, 9u, 0u, 0x9u, 0u},
        {"div by itself", kFunctDiv, 1u, 1u, 5u, 0u, 0x1u, 0u},
        {"div by r0", kFunctDiv, 1u, 0u, 0xFFFFFFFFFFFFFFF9ull, 0u, 0xFFFFFFFFFFFFFFF9ull, 0u},
        {"div r0 by r0", kFunctDiv, 0u, 0u, 0u, 0u, 0u, 0u},
        {"divu max by 2", kFunctDivu, 1u, 2u, 0xFFFFFFFFFFFFFFFFull, 2u, 0x7FFFFFFFu, 0x1u},
        {"divu fffffff9 by 16", kFunctDivu, 1u, 2u, 0xFFFFFFFFFFFFFFF9ull, 16u, 0x0FFFFFFFu, 0x9u},
        {"divu 5 by 0", kFunctDivu, 1u, 2u, 5u, 0u, 0x5u, 0u},
        {"divu min by 1", kFunctDivu, 1u, 2u, 0xFFFFFFFF80000000ull, 1u, 0xFFFFFFFF80000000ull, 0u},
    };
    std::vector<Case> wide_cases = cases;
    wide_cases.push_back({"div low words of 64-bit registers", kFunctDiv, 1u, 2u,
                          0x0000000500000007ull, 0x0000000600000002ull, 0x3u, 0x1u});
    cerfengine::MipsFeatures wide;
    wide.isa_level          = cerfengine::MipsIsaLevel::kMips3;
    wide.vr41xx_power_modes = true;
    wide.phys_addr_mask     = 0x1FFFFFFFu;
    cerfengine::MipsFeatures narrow;
    narrow.tx39_multiply_add = true;
    for (bool verify : {true, false}) {
        const char* mode = verify ? "verified" : "unverified";
        RunCore(wide, 10u, verify, wide_cases, mode);
        RunCore(narrow, 12u, verify, cases, mode);
    }
    std::printf("mips_divide_test passed\n");
    return 0;
}
