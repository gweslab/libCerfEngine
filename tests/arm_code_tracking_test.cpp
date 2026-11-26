#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_mmu_state.h>

#include "arm/arm_block_compiler.h"

#include <vector>

namespace {

using cerfengine::ArmFeatures;

constexpr uint32_t kNoHole     = 0xFFFFF000u;
constexpr uint16_t kThumbAdds  = 0x1C49u;
constexpr uint16_t kThumbAddWh = 0xF101u;
constexpr uint16_t kThumbAddWl = 0x0101u;
constexpr uint16_t kThumbBSelf = 0xE7FEu;

constexpr uint32_t kSctlrMmuOn = 0x00000001u;
constexpr uint32_t kSvcMode    = 0x000000D3u;
constexpr uint32_t kBlSubAt0   = 0xEB0004BEu;
constexpr uint32_t kStrhR1AtR0 = 0xE1C010B0u;
constexpr uint32_t kMcrIciallu = 0xEE073F15u;
constexpr uint32_t kBlSubAtC   = 0xEB0004BBu;
constexpr uint32_t kMovR2One   = 0xE3A02001u;
constexpr uint32_t kMovPcLr    = 0xE1A0F00Eu;
constexpr uint32_t kSubPa      = 0x00001300u;
constexpr uint32_t kMovR2Two   = 0x00000200u;

class InvalidateCodeCoprocessor : public TestCoprocessor {
public:
    cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& a) override {
        cerfengine::ArmCoprocDecision d;
        if (a.cp_num == 15u && a.opc1 == 0u && a.crn == 7u && a.crm == 5u && a.opc2 == 0u &&
            !a.read) {
            d.action = cerfengine::ArmCoprocAction::kInvalidateCode;
        }
        return d;
    }
};

void Mark(std::vector<bool>& expected, uint32_t pa) {
    if (pa >= 4u) expected[(pa >> 2) - 1u] = true;
    expected[pa >> 2]         = true;
    expected[(pa + 3u) >> 2]  = true;
}

void EnableMmu(ArmIrRig& rig) {
    rig.System().EnableIdentityWalk(kNoHole, 0u);
    auto& mmu = rig.Engine().Get<cerfengine::ArmMmu>();
    mmu.SetControlRegister(1u);
    mmu.SetEffectiveControlRegister(1u);
}

void LoadHalfwords(ArmIrRig& rig, uint32_t pa, const std::vector<uint16_t>& halves) {
    const uint32_t base = pa & ~3u;
    std::vector<uint16_t> padded((pa - base) / 2u, 0u);
    padded.insert(padded.end(), halves.begin(), halves.end());
    if ((padded.size() & 1u) != 0u) padded.push_back(0u);
    std::vector<uint32_t> words(padded.size() / 2u);
    for (size_t i = 0; i < words.size(); ++i) {
        words[i] = static_cast<uint32_t>(padded[i * 2u]) |
                   (static_cast<uint32_t>(padded[i * 2u + 1u]) << 16);
    }
    rig.Host().LoadWords(base, words.data(), words.size());
}

void CheckBitmap(const char* label, ArmIrRig& rig, const std::vector<bool>& expected) {
    const cerfengine::ArmMmuState& st = *rig.Engine().Get<cerfengine::ArmMmu>().State();
    for (uint32_t w = 0; w < expected.size(); ++w) {
        const bool got = ((st.code_xlat_bitmap[w >> 3] >> (w & 7u)) & 1u) != 0u;
        if (got != expected[w]) {
            TestFail("%s: code word 0x%X marked %d, expected %d", label, w, got ? 1 : 0,
                     expected[w] ? 1 : 0);
        }
    }
}

void TestArm(bool mmu) {
    ArmIrRig rig(true, ArmFeatures{});
    std::vector<uint32_t> words(9u, 0xE2811001u);
    words[8] = kRigBranchSelf;
    rig.Host().LoadWords(0x2000u, words.data(), words.size());
    if (mmu) EnableMmu(rig);
    rig.Engine().Get<cerfengine::ArmBlockCompiler>().Compile(0x2000u);
    std::vector<bool> expected(kRigRamSize / 4u, false);
    if (mmu) {
        for (uint32_t i = 0; i < words.size(); ++i) Mark(expected, 0x2000u + i * 4u);
    }
    CheckBitmap(mmu ? "arm, mmu on" : "arm, mmu off", rig, expected);
}

void TestThumb(bool mmu) {
    ArmFeatures f{};
    f.thumb   = true;
    f.thumb2  = true;
    f.arch_v6 = true;
    f.arch_v7 = true;
    ArmIrRig rig(true, f);
    const std::vector<uint16_t> block = {
        kThumbAdds, kThumbAddWh, kThumbAddWl, kThumbAdds, kThumbAddWh, kThumbAddWl,
        kThumbAddWh, kThumbAddWl, kThumbAdds, kThumbBSelf,
    };
    LoadHalfwords(rig, 0x3002u, block);
    LoadHalfwords(rig, 0x33FEu, {kThumbAddWh, kThumbAddWl, kThumbBSelf});
    if (mmu) EnableMmu(rig);
    rig.Cpu().State()->cpsr.bits.thumb_mode = 1u;
    auto& compiler = rig.Engine().Get<cerfengine::ArmBlockCompiler>();
    compiler.Compile(0x3002u);
    compiler.Compile(0x33FEu);
    std::vector<bool> expected(kRigRamSize / 4u, false);
    if (mmu) {
        uint32_t pa = 0x3002u;
        size_t   i  = 0;
        while (i < block.size()) {
            Mark(expected, pa);
            if (block[i] == kThumbAddWh) {
                Mark(expected, pa + 2u);
                pa += 4u;
                i += 2u;
            } else {
                pa += 2u;
                i += 1u;
            }
        }
        Mark(expected, 0x33FEu);
        Mark(expected, 0x3400u);
    }
    CheckBitmap(mmu ? "thumb, mmu on" : "thumb, mmu off", rig, expected);
}

void TestPagedStoreIntoThirdWord() {
    ArmIrRig rig(true, ArmFeatures{});
    const uint32_t code[] = {0xE2811001u, kRigBranchSelf};
    rig.Host().LoadWords(0x3108u, code, 2u);
    EnableMmu(rig);
    rig.Engine().Get<cerfengine::ArmBlockCompiler>().Compile(0x3108u);
    auto& mmu = rig.Engine().Get<cerfengine::ArmMmu>();
    uint8_t bytes[8] = {};
    TEST_CHECK(mmu.AccessPaged(0x3101u, bytes, 8u, false, false));
    const cerfengine::ArmMmuState& st = *mmu.State();
    const bool dirty = (st.code_page_dirty[0] & (1u << 3)) != 0u;
    std::printf("paged 8-byte store from 0x3101 into code at 0x3108: page dirty %d\n", dirty ? 1 : 0);
    TEST_CHECK(dirty);
}

void TestStoreIntoCodeFromPreviousWord(bool verify) {
    ArmIrRig rig(verify, ArmFeatures{});
    InvalidateCodeCoprocessor coprocessor;
    rig.System().SetCoprocessor(&coprocessor);
    rig.System().EnableIdentityWalk(kNoHole, 0u);
    const uint32_t sub[] = {kMovR2One, kMovPcLr};
    rig.Host().LoadWords(kSubPa, sub, 2u);
    const std::vector<uint32_t> program = {kBlSubAt0, kStrhR1AtR0, kMcrIciallu, kBlSubAtC,
                                           kRigBranchSelf};
    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = kSubPa - 1u;
    initial.gprs[1] = kMovR2Two;
    rig.Run(program, initial, kSctlrMmuOn, kSvcMode);
    cerfengine::GuestEngine& guest = rig.Engine().Get<cerfengine::GuestEngine>();
    for (uint32_t i = 0; i < 16u && rig.State().gprs[15] != kRigRamBase + 16u; ++i) guest.Run();
    std::printf("store into code from the previous word (verify %d): pc 0x%08X r2 %u\n",
                verify ? 1 : 0, rig.State().gprs[15], rig.State().gprs[2]);
    TEST_CHECK(rig.State().gprs[15] == kRigRamBase + 16u);
    TEST_CHECK(rig.State().gprs[2] == 2u);
}

}

int main() {
    TestPagedStoreIntoThirdWord();
    TestStoreIntoCodeFromPreviousWord(false);
    TestStoreIntoCodeFromPreviousWord(true);
    TestArm(false);
    TestArm(true);
    TestThumb(false);
    TestThumb(true);
    std::printf("arm_code_tracking_test passed\n");
    return 0;
}
