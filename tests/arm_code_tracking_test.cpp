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

void Mark(std::vector<bool>& expected, uint32_t pa) {
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

}

int main() {
    TestArm(false);
    TestArm(true);
    TestThumb(false);
    TestThumb(true);
    std::printf("arm_code_tracking_test passed\n");
    return 0;
}
