#include "arm_ir_rig.h"

namespace {

constexpr uint32_t kPage       = 0x00008000u;
constexpr uint32_t kSctlrMmuOn = 0x00000001u;
constexpr uint32_t kSvcMode    = 0x000000D3u;

constexpr uint8_t kStoreDeniedInSubpage1 = 0x57u;
constexpr uint8_t kStoreDeniedInSubpage3 = 0x7Fu;

void RunUntilAbort(ArmIrRig& rig) {
    cerfengine::GuestEngine& guest = rig.Engine().Get<cerfengine::GuestEngine>();
    for (uint32_t i = 0; i < 16u && rig.State().cpsr.bits.mode != cerfengine::ArmMode::kAbort;
         ++i) {
        guest.Run();
    }
}

void Run(const char* name, uint8_t access, const std::vector<uint32_t>& program, uint32_t walks,
         uint32_t fault_va) {
    ArmIrRig rig(true, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().SetSubpageAccess(kPage, access);
    cerfengine::ArmCpuState initial{};
    initial.gprs[1] = kPage;
    initial.gprs[2] = 0x11111111u;
    initial.gprs[3] = 0x22222222u;
    initial.gprs[7] = 0x33333333u;
    rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    RunUntilAbort(rig);
    std::printf("%s: mode 0x%02X walks %u faults %u fault va 0x%08X write %u r7 0x%08X\n", name,
                rig.State().cpsr.bits.mode, rig.System().SubpageWalks(), rig.System().WalkFaults(),
                rig.System().LastWalkFaultVa(), rig.System().LastWalkFaultWrite() ? 1u : 0u,
                rig.State().gprs[7]);
    TEST_CHECK(rig.State().cpsr.bits.mode == cerfengine::ArmMode::kAbort);
    TEST_CHECK(rig.System().SubpageWalks() == walks);
    TEST_CHECK(rig.System().WalkFaults() == 1u);
    TEST_CHECK(rig.System().LastWalkFaultVa() == fault_va);
    TEST_CHECK(rig.System().LastWalkFaultWrite());
    TEST_CHECK(rig.State().gprs[7] == 0x11111111u);
}

}

int main() {
    Run("sibling store after a load fill", kStoreDeniedInSubpage1,
        {0xE5916000u, 0xE5812804u, 0xE5917804u, 0xE5813400u, kRigBranchSelf}, 2u, kPage + 0x400u);
    Run("store after a whole-page read fill", kStoreDeniedInSubpage3,
        {0xE5916000u, 0xE5812000u, 0xE5812400u, 0xE5917400u, 0xE5813C00u, kRigBranchSelf}, 3u,
        kPage + 0xC00u);
    std::printf("arm_subpage_tlb_test passed\n");
    return 0;
}
