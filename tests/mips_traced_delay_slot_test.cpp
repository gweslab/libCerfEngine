#include "mips_ir_rig.h"

#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kOpBne   = 0x05u;
constexpr uint32_t kOpAddiu = 0x09u;
constexpr uint32_t kNop     = 0u;
constexpr uint32_t kJrRa    = (31u << 21) | 0x08u;

constexpr uint32_t IType(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFFu);
}

constexpr uint32_t JumpTo(uint32_t va) { return (0x02u << 26) | ((va >> 2) & 0x03FFFFFFu); }
constexpr uint32_t CallTo(uint32_t va) { return (0x03u << 26) | ((va >> 2) & 0x03FFFFFFu); }

struct Case {
    const char*           name;
    std::vector<uint32_t> program;
    uint32_t              traced_index;
    uint64_t              r1;
    uint64_t              r10;
    uint64_t              r11;
};

void RunCase(const cerfengine::MipsFeatures& features, uint32_t page_shift, bool verify,
             const Case& k) {
    const char* mode = verify ? "verified" : "unverified";
    MipsIrRig   rig(features, page_shift, verify);
    rig.Load(kMipsRigEndVa, {0x1000FFFFu, 0u, 0u, 0u, 0x650017FFu});
    rig.Load(kMipsRigVaBase, k.program);
    rig.Host().TracePc(kMipsRigVaBase + k.traced_index * 4u);
    cerfengine::MipsCpuState init{};
    init.pc     = kMipsRigVaBase;
    init.gpr[1] = k.r1;
    rig.Begin(init);
    for (uint32_t step = 0; step < 50u && !rig.Finished(); ++step) {
        if (rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted) {
            TestFail("%s %s: halted: %s", mode, k.name, rig.Engine().LastHalt().detail);
        }
    }
    const cerfengine::MipsCpuState& s = rig.State();
    if (!rig.Finished() || rig.Events().exceptions != 0u || rig.Host().TraceHits() != 1u ||
        s.gpr[2] != 0x14u || s.gpr[10] != k.r10 || s.gpr[11] != k.r11) {
        TestFail("%s %s: trace hits %u, r2 %llu r10 %llu r11 %llu pc %08X", mode, k.name,
                 rig.Host().TraceHits(), static_cast<unsigned long long>(s.gpr[2]),
                 static_cast<unsigned long long>(s.gpr[10]),
                 static_cast<unsigned long long>(s.gpr[11]), s.pc);
    }
}

}

int main() {
    const std::vector<Case> cases = {
        {"jr $ra with a traced delay slot",
         {CallTo(kMipsRigVaBase + 0x18u), kNop, IType(kOpAddiu, 0u, 10u, 1u),
          JumpTo(kMipsRigEndVa), kNop, kNop, kJrRa, IType(kOpAddiu, 0u, 2u, 0x14u),
          IType(kOpAddiu, 0u, 11u, 1u), JumpTo(kMipsRigEndVa), kNop},
         7u, 0u, 1u, 0u},
        {"taken bne with a traced delay slot",
         {IType(kOpBne, 1u, 0u, 3u), IType(kOpAddiu, 0u, 2u, 0x14u), IType(kOpAddiu, 0u, 10u, 1u),
          JumpTo(kMipsRigEndVa), kNop, IType(kOpAddiu, 0u, 11u, 1u), JumpTo(kMipsRigEndVa), kNop},
         1u, 5u, 0u, 1u},
        {"not-taken bne with a traced delay slot",
         {IType(kOpBne, 1u, 0u, 3u), IType(kOpAddiu, 0u, 2u, 0x14u), IType(kOpAddiu, 0u, 10u, 1u),
          JumpTo(kMipsRigEndVa), kNop, IType(kOpAddiu, 0u, 11u, 1u), JumpTo(kMipsRigEndVa), kNop},
         1u, 0u, 1u, 0u},
    };
    cerfengine::MipsFeatures wide;
    wide.isa_level          = cerfengine::MipsIsaLevel::kMips3;
    wide.vr41xx_power_modes = true;
    wide.phys_addr_mask     = 0x1FFFFFFFu;
    cerfengine::MipsFeatures narrow;
    narrow.tx39_multiply_add = true;
    for (bool verify : {true, false}) {
        for (const Case& k : cases) {
            RunCase(wide, 10u, verify, k);
            RunCase(narrow, 12u, verify, k);
        }
    }
    std::printf("mips_traced_delay_slot_test passed\n");
    return 0;
}
