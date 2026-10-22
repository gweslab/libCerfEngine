#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_jit.h>

#include <array>

namespace {

using Rigs = std::array<std::unique_ptr<ArmIrRig>, 2>;

constexpr uint32_t kFrameBase  = 0x4000u;
constexpr uint32_t kFrameWords = 0x400u;
constexpr uint32_t kWfi        = 0xE320F003u;

cerfengine::ArmFeatures V7() {
    cerfengine::ArmFeatures f{};
    f.arch_v6                   = true;
    f.arch_v7                   = true;
    f.thumb                     = true;
    f.load_to_pc_interworking   = true;
    f.movw_movt                 = true;
    f.armv5_unconditional_space = true;
    return f;
}

Rigs MakeRigs() {
    Rigs rigs = {
        std::make_unique<ArmIrRig>(true, V7()),
        std::make_unique<ArmIrRig>(false, V7()),
    };
    for (auto& rig : rigs) rig->System().AllowAlignmentFaults();
    return rigs;
}

void ExpectSame(const char* what, uint32_t n, const std::vector<uint32_t>& program, Rigs& rigs,
                const cerfengine::GuestEngine::RunStatus* status) {
    const cerfengine::ArmCpuState& a = rigs[0]->State();
    const cerfengine::ArmCpuState& b = rigs[1]->State();
    if (RigSameRun(*rigs[0], status[0], *rigs[1], status[1]) &&
        rigs[0]->Host().Ram() == rigs[1]->Host().Ram() &&
        rigs[0]->System().AlignmentFaults() == rigs[1]->System().AlignmentFaults()) {
        return;
    }
    std::printf("%s program %u:", what, n);
    for (uint32_t word : program) std::printf(" %08X", word);
    std::printf("\nstatus verified=%d unverified=%d idle %u/%u\n", static_cast<int>(status[0]),
                static_cast<int>(status[1]), a.idle_wait, b.idle_wait);
    if (status[0] == cerfengine::GuestEngine::RunStatus::kHalted) {
        std::printf("halt: %s\n", rigs[0]->Engine().LastHalt().detail);
    }
    RigDump("verified  ", a);
    RigDump("unverified", b);
    TestFail("CerfIR differs between verify modes");
}

void RunAll(const char* what, uint32_t n, const std::vector<uint32_t>& program, Rigs& rigs,
            const cerfengine::ArmCpuState& initial, uint32_t mode_cpsr,
            cerfengine::GuestEngine::RunStatus* status) {
    for (uint32_t r = 0; r < 2u; ++r) status[r] = rigs[r]->Run(program, initial, 0u, mode_cpsr, 0u);
    ExpectSame(what, n, program, rigs, status);
}

void TestWfi() {
    const std::vector<uint32_t> program = {0xE3A00001u, kWfi, 0xE3A01002u, kRigBranchSelf};
    cerfengine::ArmCpuState     initial{};
    cerfengine::GuestEngine::RunStatus status[2];

    Rigs woke = MakeRigs();
    for (auto& rig : woke) rig->Engine().Get<cerfengine::ArmJit>().SetIdleWake(true);
    RunAll("wfi woke", 0u, program, woke, initial, 0u, status);
    TEST_CHECK(woke[0]->State().gprs[1] == 2u);

    Rigs idle = MakeRigs();
    for (auto& rig : idle) rig->Engine().Get<cerfengine::ArmJit>().SetHostChainExit(true);
    RunAll("wfi idle", 0u, program, idle, initial, 0u, status);
    TEST_CHECK(idle[0]->State().idle_wait == 1u);
    TEST_CHECK(idle[0]->State().gprs[15] == 8u);
    TEST_CHECK(idle[0]->State().gprs[1] == 0u);

    Rigs skipped = MakeRigs();
    const std::vector<uint32_t> conditional = {0x0320F003u, 0xE3A01002u, kRigBranchSelf};
    RunAll("wfi condition failed", 0u, conditional, skipped, initial, 0u, status);
    TEST_CHECK(skipped[0]->State().gprs[1] == 2u);
    std::printf("wfi: woke, idle and condition-failed paths match\n");
}

uint32_t FrameWord(std::mt19937& rng) {
    static constexpr uint32_t kModes[] = {0x10u, 0x11u, 0x12u, 0x13u, 0x17u, 0x1Bu, 0x1Fu};
    const uint32_t nzcv = RigPick(rng, 16u);
    const uint32_t af   = RigPick(rng, 4u);
    const uint32_t t    = RigPick(rng, 2u);
    return (nzcv << 28) | (af << 6) | (t << 5) | kModes[RigPick(rng, 7u)];
}

uint32_t FramePointer(std::mt19937& rng) {
    const uint32_t base = kFrameBase + 0x20u + RigPick(rng, kFrameWords * 4u - 0x40u);
    return RigPick(rng, 4u) == 0u ? base : (base & ~3u);
}

uint32_t RandomReturnState(std::mt19937& rng, uint32_t mode) {
    const uint32_t pu = RigPick(rng, 4u) << 23;
    const uint32_t w  = RigPick(rng, 2u) << 21;
    if (RigPick(rng, 2u) == 0u) {
        const uint32_t rn = RigPick(rng, 8u) == 0u ? 13u : RigPick(rng, 8u);
        return 0xF8100A00u | pu | w | (rn << 16);
    }
    return 0xF84D0500u | pu | w | mode;
}

void TestReturnState() {
    static constexpr uint32_t kStartModes[] = {0x1D3u, 0x1D2u, 0x1D7u, 0x1DBu, 0x1D1u, 0x1DFu,
                                               0x1D0u};
    Rigs         rigs = MakeRigs();
    std::mt19937 rng(0x52464531u);
    constexpr uint32_t kPrograms = 6000u;
    uint32_t     aborts  = 0;
    uint32_t     halts   = 0;
    uint32_t     returns = 0;
    StateDigest  digest;
    for (uint32_t n = 0; n < kPrograms; ++n) {
        const uint32_t mode_cpsr = kStartModes[RigPick(rng, 7u)];
        std::vector<uint32_t> program;
        const uint32_t length = 1u + RigPick(rng, 3u);
        for (uint32_t i = 0; i < length; ++i) {
            program.push_back(RigPick(rng, 4u) == 0u ? 0xE2800001u | (RigPick(rng, 8u) << 12)
                                                     : RandomReturnState(rng, mode_cpsr & 0x1Fu));
        }
        program.push_back(kRigBranchSelf);

        std::vector<uint32_t> frames(kFrameWords);
        for (uint32_t& word : frames) word = FrameWord(rng);
        cerfengine::ArmCpuState initial{};
        for (uint32_t i = 0; i < 8u; ++i) initial.gprs[i] = FramePointer(rng);
        for (uint32_t i = 8; i < 13u; ++i) initial.gprs[i] = static_cast<uint32_t>(rng());
        initial.gprs[13] = FramePointer(rng);
        initial.gprs[14] = static_cast<uint32_t>(rng());
        for (auto& rig : rigs) rig->Host().LoadWords(kFrameBase, frames.data(), frames.size());

        const uint32_t faults_before = rigs[0]->System().AlignmentFaults();
        cerfengine::GuestEngine::RunStatus status[2];
        RunAll("return state", n, program, rigs, initial, mode_cpsr, status);
        RigDigestRun(digest, *rigs[0], status[0]);
        if (status[0] == cerfengine::GuestEngine::RunStatus::kHalted) {
            ++halts;
            for (auto& rig : rigs) rig->Restart();
            continue;
        }
        if (rigs[0]->System().AlignmentFaults() != faults_before) ++aborts;
        if (rigs[0]->State().gprs[15] != (program.size() - 1u) * 4u) ++returns;
    }
    std::printf("return state: %u programs agree in both verify modes (pc changes %u, alignment "
                "aborts %u, halts %u), digest %016llX\n",
                kPrograms, returns, aborts, halts, digest.Value());
    RigPrintCompileStats("  cerfir verified  ", rigs[0]->Host());
    RigPrintCompileStats("  cerfir unverified", rigs[1]->Host());
}

void TestArmUnimplemented() {
    Rigs rigs = MakeRigs();
    const std::vector<uint32_t> program = {0xE6112F13u, kRigBranchSelf};
    cerfengine::ArmCpuState     initial{};
    cerfengine::GuestEngine::RunStatus status[2];
    RunAll("unimplemented", 0u, program, rigs, initial, 0u, status);
    TEST_CHECK(status[0] == cerfengine::GuestEngine::RunStatus::kHalted);
    std::printf("unimplemented: halt \"%s\" matches\n", rigs[0]->Engine().LastHalt().detail);
}

}

int main() {
    TestWfi();
    TestReturnState();
    TestArmUnimplemented();
    std::printf("arm_ir_system_diff_test passed\n");
    return 0;
}
