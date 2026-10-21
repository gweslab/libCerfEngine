#include "arm_ir_rig.h"

#include <sys/mman.h>

namespace {

constexpr uint32_t kDataPa     = 0x00008000u;
constexpr uint32_t kEndPc      = 0x00000024u;
constexpr uint32_t kSctlrMmuOn = 0x00000001u;
constexpr uint32_t kSvcMode    = 0x000000D3u;
constexpr uint32_t kFirst      = 0x12345678u;
constexpr uint32_t kSecond     = 0x9ABCDEF0u;
constexpr uint64_t kFourGb     = 0x100000000ull;

uint8_t* MapRamWithZeroLowWordAtData() {
    const size_t reserve_bytes = static_cast<size_t>(2u * kFourGb);
    void* reserve = mmap(nullptr, reserve_bytes, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (reserve == MAP_FAILED) TestFail("reserving %zu bytes of address space failed", reserve_bytes);
    const uintptr_t data_host =
        (reinterpret_cast<uintptr_t>(reserve) + kDataPa + kFourGb - 1u) & ~(kFourGb - 1u);
    void* ram = mmap(reinterpret_cast<void*>(data_host - kDataPa), kRigRamSize,
                     PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (ram == MAP_FAILED) TestFail("mapping the rig RAM inside the reservation failed");
    return static_cast<uint8_t*>(ram);
}

void TestDataAtZeroLowWord(bool verify, uint32_t sctlr, const char* name) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    uint8_t* ram = MapRamWithZeroLowWordAtData();
    TEST_CHECK((reinterpret_cast<uintptr_t>(ram + kDataPa) & 0xFFFFFFFFu) == 0u);
    rig.Host().MoveRamTo(ram);
    rig.Restart();
    rig.System().EnableIdentityWalk(0u, 0u);
    const std::vector<uint32_t> program = {
        0xE5810000u, 0xE5912000u, 0xE4810004u, 0xE2411004u, 0xE4913004u,
        0xE2411004u, 0xE1014095u, 0xE8810021u, 0xE89100C0u, kRigBranchSelf,
    };
    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = kFirst;
    initial.gprs[1] = kDataPa;
    initial.gprs[5] = kSecond;
    rig.Run(program, initial, sctlr, kSvcMode);
    const cerfengine::ArmCpuState& s = rig.State();
    std::printf("%s: mode 0x%02X pc 0x%08X r2 0x%08X r3 0x%08X r4 0x%08X r6 0x%08X r7 0x%08X\n",
                name, s.cpsr.bits.mode, s.gprs[15], s.gprs[2], s.gprs[3], s.gprs[4], s.gprs[6],
                s.gprs[7]);
    TEST_CHECK(s.cpsr.bits.mode == 0x13u);
    TEST_CHECK(s.gprs[15] == kEndPc);
    TEST_CHECK(s.gprs[2] == kFirst);
    TEST_CHECK(s.gprs[3] == kFirst);
    TEST_CHECK(s.gprs[4] == kFirst);
    TEST_CHECK(s.gprs[6] == kFirst);
    TEST_CHECK(s.gprs[7] == kSecond);
    TEST_CHECK(cerfengine::le::U32(ram + kDataPa) == kFirst);
    TEST_CHECK(cerfengine::le::U32(ram + kDataPa + 4u) == kSecond);
}

}

int main() {
    for (const bool verify : {true, false}) {
        TestDataAtZeroLowWord(verify, 0u, verify ? "verified, mmu off" : "mmu off");
        TestDataAtZeroLowWord(verify, kSctlrMmuOn, verify ? "verified, mmu on" : "mmu on");
    }
    std::printf("arm_host_address_test passed\n");
    return 0;
}
