#include "arm_ir_rig.h"

#include <cerfengine/cycle_deadline.h>

#include "arm/arm_translation_cache.h"

namespace {

constexpr uint32_t kSctlrMmuOn   = 0x00000001u;
constexpr uint32_t kSvcThumbMode = 0x000000F3u;
constexpr uint32_t kX            = 0x0F00u;
constexpr uint32_t kS            = 0x0FFEu;
constexpr uint32_t kSecondPage   = 0x1000u;
constexpr uint32_t kAltPage      = 0x2000u;
constexpr uint32_t kEnd          = 0x1002u;
constexpr uint16_t kBranchSelf   = 0xE7FEu;

void PutBranchW(std::vector<uint16_t>& half, uint32_t pc, uint32_t target) {
    const uint32_t off = target - (pc + 4u);
    const uint32_t s   = (off >> 24) & 1u;
    const uint32_t i1  = (off >> 23) & 1u;
    const uint32_t i2  = (off >> 22) & 1u;
    const uint32_t j1  = (i1 ^ 1u) ^ s;
    const uint32_t j2  = (i2 ^ 1u) ^ s;
    half[pc / 2u]      = static_cast<uint16_t>(0xF000u | (s << 10) | ((off >> 12) & 0x3FFu));
    half[pc / 2u + 1u] = static_cast<uint16_t>(0x9000u | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7FFu));
}

cerfengine::ArmFeatures Thumb2() {
    cerfengine::ArmFeatures f;
    f.arch_v6   = true;
    f.arch_v7   = true;
    f.thumb     = true;
    f.thumb2    = true;
    f.movw_movt = true;
    return f;
}

void RunTo(ArmIrRig& rig, uint32_t pc) {
    cerfengine::GuestEngine& guest = rig.Engine().Get<cerfengine::GuestEngine>();
    for (uint32_t i = 0; rig.State().gprs[15] != pc; ++i) {
        if (i == 16u) TestFail("no exit to 0x%04X (pc 0x%08X)", pc, rig.State().gprs[15]);
        cerfengine::ArmCpuState& s = *rig.Cpu().State();
        s.guest_cycle_deadline     = cerfengine::CycleDeadline(s.guest_cycle_counter, 10000u);
        TEST_CHECK(guest.Run() == cerfengine::GuestEngine::RunStatus::kRunning);
    }
}

void TestRemappedSecondPage(bool verify) {
    ArmIrRig rig(verify, Thumb2());
    rig.System().EnableIdentityWalk(0u, 0u);
    std::vector<uint16_t> half((kAltPage + 4u) / 2u, kBranchSelf);
    PutBranchW(half, 0u, kX);
    PutBranchW(half, kX, kS);
    half[kS / 2u]            = 0xF240u;
    half[kSecondPage / 2u]   = 0x0111u;
    half[kAltPage / 2u]      = 0x0122u;
    std::vector<uint32_t> words(half.size() / 2u);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = half[2u * i] | (static_cast<uint32_t>(half[2u * i + 1u]) << 16);
    }
    rig.Cpu().State()->guest_cycle_deadline = cerfengine::CycleDeadline(0u, 10000u);
    TEST_CHECK(rig.Run(words, cerfengine::ArmCpuState{}, kSctlrMmuOn, kSvcThumbMode) ==
               cerfengine::GuestEngine::RunStatus::kRunning);
    RunTo(rig, kEnd);
    TEST_CHECK(rig.State().gprs[1] == 0x11u);

    rig.System().RemapPage(kSecondPage, kAltPage);
    cerfengine::ArmTranslationCache::ItlbInvalidateMvaHelper(
        kSecondPage, &rig.Engine().Get<cerfengine::ArmTranslationCache>());
    rig.Cpu().State()->gprs[1]  = 0u;
    rig.Cpu().State()->gprs[15] = kX;
    RunTo(rig, kEnd);
    std::printf("%s: r1 0x%02X after the remap\n", verify ? "verified" : "unverified",
                rig.State().gprs[1]);
    TEST_CHECK(rig.State().gprs[1] == 0x22u);
}

}

int main() {
    TestRemappedSecondPage(true);
    TestRemappedSecondPage(false);
    std::printf("arm_split_block_test passed\n");
    return 0;
}
