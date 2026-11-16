#include "arm_ir_rig.h"

namespace {

constexpr uint32_t kSctlrMmuOn       = 0x00000001u;
constexpr uint32_t kSvcMode          = 0x000000D3u;
constexpr uint32_t kSpsrSvcBigEndian = 0x00000213u;
constexpr uint32_t kMovR0One         = 0xE3A00001u;
constexpr uint32_t kLdrR2AtR1        = 0xE5912000u;
constexpr uint32_t kLdmR0R0Pc        = 0xE8908001u;
constexpr uint32_t kLdmReturnR0R0Pc  = 0xE8D08001u;
constexpr uint32_t kDataPage         = 0x00008000u;
constexpr uint32_t kSplitWord        = 0x00008FFCu;
constexpr uint32_t kLoadedR0         = 0x12345678u;
constexpr uint32_t kLoadedPcTarget   = kRigRamBase + 8u;

void HostFailureInWalkLeavesAccessPc(bool verify, uint32_t address, const char* name) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    rig.System().ThrowOnWalkOf(kDataPage);
    const std::vector<uint32_t> program = {kMovR0One, kLdrR2AtR1, kRigBranchSelf};
    cerfengine::ArmCpuState initial{};
    initial.gprs[1] = address;
    bool thrown = false;
    try {
        rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    } catch (const HostThrow& failure) {
        thrown = (failure.value & ~0xFFFu) == kDataPage;
    }
    std::printf("%s (verify %d): thrown %d pc 0x%08X r0 0x%08X\n", name, verify ? 1 : 0, thrown ? 1 : 0,
                rig.State().gprs[15], rig.State().gprs[0]);
    TEST_CHECK(thrown);
    TEST_CHECK(rig.State().gprs[15] == kRigRamBase + 4u);
    TEST_CHECK(rig.State().gprs[0] == 1u);
}

void MissAfterLoadedPcKeepsIt(bool verify) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    rig.System().EnableIdentityWalk(0u, 0u);
    const uint32_t words[] = {kLoadedR0, kLoadedPcTarget};
    rig.Host().LoadWords(kSplitWord, words, 2u);
    const std::vector<uint32_t> program = {kLdmR0R0Pc, kRigBranchSelf, kRigBranchSelf};
    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = kSplitWord;
    rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    std::printf("split ldm (verify %d): pc 0x%08X r0 0x%08X\n", verify ? 1 : 0, rig.State().gprs[15],
                rig.State().gprs[0]);
    TEST_CHECK(rig.State().gprs[15] == kLoadedPcTarget);
    TEST_CHECK(rig.State().gprs[0] == kLoadedR0);
}

void HaltingReturnKeepsBase(bool verify, uint32_t base, const char* name) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    if (base == kRigMmioPa) {
        rig.Host().EnableMmioLog();
    } else {
        const uint32_t words[] = {kLoadedR0, kLoadedPcTarget};
        rig.Host().LoadWords(base, words, 2u);
    }
    const std::vector<uint32_t> program = {kLdmReturnR0R0Pc, kRigBranchSelf};
    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = base;
    const cerfengine::GuestEngine::RunStatus status = rig.Run(program, initial, 0u, kSvcMode, kSpsrSvcBigEndian);
    const bool halted = status == cerfengine::GuestEngine::RunStatus::kHalted;
    std::printf("%s (verify %d): halted %d r0 0x%08X\n", name, verify ? 1 : 0, halted ? 1 : 0,
                rig.State().gprs[0]);
    TEST_CHECK(halted);
    TEST_CHECK(rig.State().gprs[0] == base);
}

}

int main() {
    for (const bool verify : {false, true}) {
        HostFailureInWalkLeavesAccessPc(verify, kDataPage, "walk failure");
        HostFailureInWalkLeavesAccessPc(verify, kDataPage + 1u, "unaligned walk failure");
        MissAfterLoadedPcKeepsIt(verify);
        HaltingReturnKeepsBase(verify, kDataPage, "ldm return from ram");
        HaltingReturnKeepsBase(verify, kRigMmioPa, "ldm return from mmio");
    }
    std::printf("arm_restart_pc_test passed\n");
    return 0;
}
