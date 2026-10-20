#include "mips_ir_rig.h"

#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kOpBeq   = 0x04u;
constexpr uint32_t kOpBne   = 0x05u;
constexpr uint32_t kOpBeql  = 0x14u;
constexpr uint32_t kOpBnel  = 0x15u;
constexpr uint32_t kOpAddiu = 0x09u;
constexpr uint32_t kNop     = 0u;

constexpr uint32_t IType(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFFu);
}

constexpr uint32_t JumpTo(uint32_t va) { return (0x02u << 26) | ((va >> 2) & 0x03FFFFFFu); }

struct Case {
    const char* name;
    uint32_t    op;
    uint32_t    rt;
    uint64_t    r1;
    uint64_t    r2;
    bool        slot_writes_rs;
    bool        taken;
};

void RunCase(MipsIrRig& rig, const Case& k, const char* mode) {
    const std::vector<uint32_t> program = {
        IType(k.op, 1u, k.rt, 3u),
        k.slot_writes_rs ? IType(kOpAddiu, 1u, 1u, 0x10u) : kNop,
        IType(kOpAddiu, 0u, 10u, 1u),
        JumpTo(kMipsRigEndVa),
        kNop,
        IType(kOpAddiu, 0u, 11u, 1u),
        JumpTo(kMipsRigEndVa),
        kNop,
    };
    rig.Load(kMipsRigVaBase, program);
    cerfengine::MipsCpuState init{};
    init.pc     = kMipsRigVaBase;
    init.gpr[1] = k.r1;
    init.gpr[2] = k.r2;
    rig.Begin(init);
    for (uint32_t step = 0; step < 50u && !rig.Finished(); ++step) {
        if (rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted) {
            TestFail("%s %s: halted: %s", mode, k.name, rig.Engine().LastHalt().detail);
        }
    }
    const cerfengine::MipsCpuState& s = rig.State();
    if (!rig.Finished() || rig.Events().exceptions != 0u || s.gpr[10] != (k.taken ? 0u : 1u) ||
        s.gpr[11] != (k.taken ? 1u : 0u)) {
        TestFail("%s %s: expected %s, r10 %llu r11 %llu pc %08X exceptions %u", mode, k.name,
                 k.taken ? "taken" : "not taken", static_cast<unsigned long long>(s.gpr[10]),
                 static_cast<unsigned long long>(s.gpr[11]), s.pc, rig.Events().exceptions);
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
    cerfengine::MipsFeatures wide;
    wide.isa_level          = cerfengine::MipsIsaLevel::kMips3;
    wide.vr41xx_power_modes = true;
    wide.phys_addr_mask     = 0x1FFFFFFFu;
    const std::vector<Case> wide_cases = {
        {"beq low equal high differ", kOpBeq, 2u, 0x0000000500000007ull, 0x0000000600000007ull, false, false},
        {"bne low equal high differ", kOpBne, 2u, 0x0000000500000007ull, 0x0000000600000007ull, false, true},
        {"beql low equal high differ", kOpBeql, 2u, 0x0000000500000007ull, 0x0000000600000007ull, false, false},
        {"bnel low equal high differ", kOpBnel, 2u, 0x0000000500000007ull, 0x0000000600000007ull, false, true},
        {"beq equal negative", kOpBeq, 2u, 0xFFFFFFFF80000000ull, 0xFFFFFFFF80000000ull, false, true},
        {"bne equal negative", kOpBne, 2u, 0xFFFFFFFF80000000ull, 0xFFFFFFFF80000000ull, false, false},
        {"beq high zero against one", kOpBeq, 2u, 0x0000000000000007ull, 0x0000000100000007ull, false, false},
        {"beq zero test high set", kOpBeq, 0u, 0x0000000100000000ull, 0u, false, false},
        {"bne zero test high set", kOpBne, 0u, 0x0000000100000000ull, 0u, false, true},
        {"beq zero test zero", kOpBeq, 0u, 0u, 0u, false, true},
        {"beq delay slot writes rs", kOpBeq, 2u, 5u, 5u, true, true},
        {"bne delay slot writes rs", kOpBne, 2u, 5u, 5u, true, false},
    };
    cerfengine::MipsFeatures narrow;
    narrow.tx39_multiply_add = true;
    const std::vector<Case> narrow_cases = {
        {"beq equal", kOpBeq, 2u, 5u, 5u, false, true},
        {"beq differ", kOpBeq, 2u, 5u, 6u, false, false},
        {"bne zero test negative", kOpBne, 0u, 0xFFFFFFFF80000000ull, 0u, false, true},
        {"beql zero test zero", kOpBeql, 0u, 0u, 0u, false, true},
        {"beq delay slot writes rs", kOpBeq, 2u, 5u, 5u, true, true},
    };
    for (bool verify : {true, false}) {
        const char* mode = verify ? "verified" : "unverified";
        RunCore(wide, 10u, verify, wide_cases, mode);
        RunCore(narrow, 12u, verify, narrow_cases, mode);
    }
    std::printf("mips_equality_branch_test passed\n");
    return 0;
}
