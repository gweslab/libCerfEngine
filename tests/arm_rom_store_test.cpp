#include "arm_ir_rig.h"

namespace {

constexpr uint32_t kRomPa      = 0x00020000u;
constexpr uint32_t kRomBytes   = 0x1000u;
constexpr uint32_t kSctlrMmuOn = 0x00000001u;
constexpr uint32_t kSvcMode    = 0x000000D3u;
constexpr uint32_t kStrR1AtR0  = 0xE5801000u;
constexpr uint32_t kSwpR3R1R0  = 0xE1003091u;
constexpr uint32_t kStored     = 0x11223344u;
constexpr uint32_t kEndPc      = kRigRamBase + 8u;

void TestStoresToRomReachTheBus(bool verify, uint32_t sctlr, const char* name) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    std::vector<uint8_t> rom(kRomBytes);
    for (uint32_t i = 0; i < kRomBytes; ++i) rom[i] = static_cast<uint8_t>(i * 7u + 1u);
    const std::vector<uint8_t> original = rom;
    rig.Host().AddRom(kRomPa, std::move(rom));
    rig.Host().EnableMmioLog();
    if (sctlr != 0u) rig.System().EnableIdentityWalk(0u, 0u);
    const std::vector<uint32_t> program = {kStrR1AtR0, kSwpR3R1R0, kRigBranchSelf};
    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = kRomPa;
    initial.gprs[1] = kStored;
    rig.Run(program, initial, sctlr, kSvcMode);
    cerfengine::GuestEngine& guest = rig.Engine().Get<cerfengine::GuestEngine>();
    for (uint32_t i = 0; i < 16u && rig.State().gprs[15] != kEndPc; ++i) guest.Run();
    uint32_t writes = 0;
    for (const TestHost::MmioAccess& a : rig.Host().MmioLog()) {
        if (a.pa != kRomPa || !a.write) TestFail("%s: bus access at PA 0x%08X", name, a.pa);
        TEST_CHECK(a.value == kStored);
        ++writes;
    }
    std::printf("%s: pc 0x%08X bus writes %u r3 0x%08X rom unchanged %d\n", name,
                rig.State().gprs[15], writes, rig.State().gprs[3],
                rig.Host().Rom() == original ? 1 : 0);
    TEST_CHECK(rig.State().gprs[15] == kEndPc);
    TEST_CHECK(writes == 2u);
    TEST_CHECK(rig.State().gprs[3] == cerfengine::le::U32(original.data()));
    TEST_CHECK(rig.Host().Rom() == original);
}

}

int main() {
    for (const bool verify : {false, true}) {
        TestStoresToRomReachTheBus(verify, 0u, verify ? "verified, mmu off" : "mmu off");
        TestStoresToRomReachTheBus(verify, kSctlrMmuOn, verify ? "verified, mmu on" : "mmu on");
    }
    std::printf("arm_rom_store_test passed\n");
    return 0;
}
