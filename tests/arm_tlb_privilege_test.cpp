#include "arm_ir_rig.h"

namespace {

constexpr uint32_t kPrivilegedPage = 0x00008000u;
constexpr uint32_t kSctlrMmuOn     = 0x00000001u;
constexpr uint32_t kSvcMode        = 0x000000D3u;
constexpr uint32_t kMsrUserMode    = 0xE321F010u;
constexpr uint32_t kMovPcR4        = 0xE1A0F004u;

void RunUntilAbort(ArmIrRig& rig) {
    cerfengine::GuestEngine& guest = rig.Engine().Get<cerfengine::GuestEngine>();
    for (uint32_t i = 0; i < 16u && rig.State().cpsr.bits.mode != cerfengine::ArmMode::kAbort;
         ++i) {
        guest.Run();
    }
}

void CheckAbort(ArmIrRig& rig, const char* name) {
    std::printf("%s: mode 0x%02X pc 0x%08X r3 0x%08X walk faults %u\n", name,
                rig.State().cpsr.bits.mode, rig.State().gprs[15], rig.State().gprs[3],
                rig.System().WalkFaults());
    TEST_CHECK(rig.State().cpsr.bits.mode == cerfengine::ArmMode::kAbort);
    TEST_CHECK(rig.System().WalkFaults() == 1u);
}

void TestUserAccessAfterPrivilegedFill(bool store, const char* name) {
    ArmIrRig rig(true, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().EnablePrivilegedRegion(kPrivilegedPage, 0x1000u);
    const std::vector<uint32_t> program = {
        store ? 0xE5812000u : 0xE5912000u,
        kMsrUserMode,
        store ? 0xE5813000u : 0xE5913000u,
        kRigBranchSelf,
    };
    cerfengine::ArmCpuState initial{};
    initial.gprs[1] = kPrivilegedPage;
    initial.gprs[2] = 0x11111111u;
    initial.gprs[3] = 0x22222222u;
    rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    RunUntilAbort(rig);
    CheckAbort(rig, name);
    if (!store) TEST_CHECK(rig.State().gprs[3] == 0x22222222u);
}

void TestUserProbeAfterPrivilegedBlock(const char* name) {
    ArmIrRig rig(true, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().EnablePrivilegedRegion(kPrivilegedPage, 0x1000u);
    const std::vector<uint32_t> program = {
        0xE5912000u, kMovPcR4,     kRigBranchSelf, kRigBranchSelf,
        0xE5913000u, kMsrUserMode, kMovPcR4,       kRigBranchSelf,
    };
    cerfengine::ArmCpuState initial{};
    initial.gprs[1] = kPrivilegedPage;
    initial.gprs[4] = 0x10u;
    rig.Cpu().State()->guest_cycle_deadline =
        cerfengine::CycleDeadline(0u, cerfengine::kMaxCycleDeadlineAhead);
    rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    RunUntilAbort(rig);
    CheckAbort(rig, name);
}

void TestResetDropsFilledEntry(const char* name) {
    ArmIrRig rig(true, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    const std::vector<uint32_t> program = {0xE5912000u, kRigBranchSelf};
    cerfengine::ArmCpuState initial{};
    initial.gprs[1] = kPrivilegedPage;
    rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    TEST_CHECK(rig.State().cpsr.bits.mode == 0x13u);
    rig.System().EnableIdentityWalk(kPrivilegedPage, 0x1000u);
    rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    CheckAbort(rig, name);
}

}

int main() {
    for (const bool store : {false, true}) {
        TestUserAccessAfterPrivilegedFill(store, store ? "store" : "load");
    }
    TestUserProbeAfterPrivilegedBlock("probe");
    TestResetDropsFilledEntry("reset");
    std::printf("arm_tlb_privilege_test passed\n");
    return 0;
}
