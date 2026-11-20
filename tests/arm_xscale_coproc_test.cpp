#include "arm_ir_rig.h"

#include <vector>

namespace {

using RunStatus = cerfengine::GuestEngine::RunStatus;

constexpr uint64_t kAcc40Mask = 0xFFFFFFFFFFull;
constexpr uint32_t kWcidValue = 0x69051000u;

uint32_t Mar(uint32_t lo, uint32_t hi) { return 0xEC400000u | (hi << 16) | (lo << 12); }
uint32_t Mra(uint32_t lo, uint32_t hi) { return 0xEC500000u | (hi << 16) | (lo << 12); }
uint32_t Tmcrr(uint32_t wr, uint32_t lo, uint32_t hi) { return Mar(lo, hi) | wr; }
uint32_t Tmrrc(uint32_t wr, uint32_t lo, uint32_t hi) { return Mra(lo, hi) | wr; }
uint32_t Mia(uint32_t op, uint32_t rm, uint32_t rs) { return 0xEE200010u | (op << 16) | (rs << 12) | rm; }
uint32_t Tmcr(uint32_t wc, uint32_t rd) { return 0xEE000110u | (wc << 16) | (rd << 12); }
uint32_t Tmrc(uint32_t rd, uint32_t wc) { return 0xEE100110u | (wc << 16) | (rd << 12); }

int64_t Half(uint32_t v, bool top) { return static_cast<int16_t>(top ? v >> 16 : v & 0xFFFFu); }

uint64_t MiaReference(uint64_t acc, uint32_t op, uint32_t rm, uint32_t rs) {
    int64_t product = 0;
    if (op == 0x0u) {
        product = static_cast<int64_t>(static_cast<int32_t>(rm)) * static_cast<int32_t>(rs);
    } else if (op == 0x8u) {
        product = Half(rm, true) * Half(rs, true) + Half(rm, false) * Half(rs, false);
    } else {
        product = Half(rm, (op & 2u) != 0u) * Half(rs, (op & 1u) != 0u);
    }
    return (acc + static_cast<uint64_t>(product)) & kAcc40Mask;
}

struct Lanes {
    ArmIrRig verified;
    ArmIrRig unverified;
};

const cerfengine::ArmCpuState& RunBoth(Lanes& lanes, const std::vector<uint32_t>& body,
                                       const cerfengine::ArmCpuState& initial, RunStatus want) {
    std::vector<uint32_t> program = body;
    program.push_back(kRigBranchSelf);
    RunStatus status[2];
    ArmIrRig* rigs[2] = {&lanes.verified, &lanes.unverified};
    for (uint32_t r = 0; r < 2u; ++r) {
        rigs[r]->Cpu().SetResetCoprocessorAccess(0x3u);
        status[r] = rigs[r]->Run(program, initial, 0u, 0u, 0u);
        if (status[r] == RunStatus::kHalted) rigs[r]->Restart();
    }
    if (status[0] != want || status[1] != want) {
        TestFail("status %d/%d, want %d", static_cast<int>(status[0]), static_cast<int>(status[1]),
                 static_cast<int>(want));
    }
    if (want != RunStatus::kHalted && !RigSameState(rigs[0]->State(), rigs[1]->State())) {
        TestFail("verify modes differ");
    }
    return rigs[0]->State();
}

void Expect(uint32_t got, uint32_t want, const char* what) {
    if (got != want) TestFail("%s: 0x%08X, want 0x%08X", what, got, want);
}

void TestAccumulator() {
    cerfengine::ArmFeatures f{};
    f.thumb = f.dsp = f.armv5_unconditional_space = true;
    f.xscale_accumulator = true;
    Lanes lanes{ArmIrRig(true, f), ArmIrRig(false, f)};

    cerfengine::ArmCpuState in{};
    in.gprs[0] = 0x89ABCDEFu;
    in.gprs[1] = 0xFFFFFF80u;
    const cerfengine::ArmCpuState& s = RunBoth(lanes, {Mar(0, 1), Mra(2, 3)}, in, RunStatus::kRunning);
    Expect(s.gprs[2], 0x89ABCDEFu, "MRA low");
    Expect(s.gprs[3], 0xFFFFFF80u, "MRA high sign extension");
    Expect(static_cast<uint32_t>(s.xscale_cp0[0] >> 32), 0x80u, "MAR high byte");

    static constexpr uint32_t kOps[] = {0x0u, 0x8u, 0xCu, 0xDu, 0xEu, 0xFu};
    static constexpr uint32_t kValues[][2] = {{0x7FFFFFFFu, 0x00000002u}, {0x80000001u, 0xFFFF8000u},
                                              {0x12345678u, 0x9ABCDEF0u}, {0xFFFFFFFFu, 0xFFFFFFFFu}};
    for (uint32_t op : kOps) {
        for (const auto& v : kValues) {
            cerfengine::ArmCpuState m{};
            m.gprs[0] = 0xFFFFFFFFu;
            m.gprs[1] = 0x7Fu;
            m.gprs[4] = v[0];
            m.gprs[5] = v[1];
            const cerfengine::ArmCpuState& r =
                RunBoth(lanes, {Mar(0, 1), Mia(op, 4, 5), Mra(6, 7)}, m, RunStatus::kRunning);
            const uint64_t want = MiaReference(0x7FFFFFFFFFull, op, v[0], v[1]);
            Expect(r.gprs[6], static_cast<uint32_t>(want), "MIA low");
            Expect(r.gprs[7], static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(want >> 32))),
                   "MIA high");
        }
    }

    RunBoth(lanes, {Mar(0, 1) | 1u}, in, RunStatus::kHalted);
    RunBoth(lanes, {Mia(0x1u, 4, 5)}, in, RunStatus::kHalted);
    RunBoth(lanes, {Mia(0x0u, 4, 5) | (1u << 5)}, in, RunStatus::kHalted);
    std::printf("xscale accumulator: MAR/MRA/MIA agree with the reference\n");
}

void TestIwmmxt() {
    cerfengine::ArmFeatures f{};
    f.thumb = f.dsp = f.armv5_unconditional_space = true;
    f.xscale_accumulator = true;
    f.iwmmxt             = true;
    f.wcid               = kWcidValue;
    Lanes lanes{ArmIrRig(true, f), ArmIrRig(false, f)};

    cerfengine::ArmCpuState in{};
    in.gprs[0] = 0x01234567u;
    in.gprs[1] = 0x89ABCDEFu;
    const cerfengine::ArmCpuState& a =
        RunBoth(lanes, {Tmcrr(5, 0, 1), Tmrrc(5, 2, 3), Tmrc(4, 1), Tmrc(6, 0)}, in, RunStatus::kRunning);
    Expect(a.gprs[2], 0x01234567u, "TMRRC low");
    Expect(a.gprs[3], 0x89ABCDEFu, "TMRRC high");
    Expect(a.gprs[4] & 2u, 2u, "TMCRR sets wCon.MUP");
    Expect(a.gprs[6], kWcidValue, "TMRC wCID");

    const cerfengine::ArmCpuState& b =
        RunBoth(lanes, {Tmcr(10, 0), Tmrc(2, 10), Tmrc(3, 1)}, in, RunStatus::kRunning);
    Expect(b.gprs[2], 0x01234567u, "TMCR wCGR2");
    Expect(b.gprs[3] & 1u, 1u, "TMCR wCGR sets wCon.CUP");

    cerfengine::ArmCpuState flags{};
    flags.xscale_cp1[1] = 0x3u;
    flags.xscale_cp1[2] = 0xFFu;
    flags.gprs[0]       = 0x1u;
    flags.gprs[1]       = 0x0Fu;
    const cerfengine::ArmCpuState& c =
        RunBoth(lanes, {Tmcr(1, 0), Tmcr(2, 1), Tmcr(0, 1), Tmcr(3, 1)}, flags, RunStatus::kRunning);
    Expect(c.xscale_cp1[1], 0x2u, "TMCR wCon clears the written bits after setting CUP");
    Expect(c.xscale_cp1[2], 0xF0u, "TMCR wCSSF clears the written bits");
    Expect(c.xscale_cp1[0], 0x0u, "TMCR wCID is ignored");

    const cerfengine::ArmCpuState& und = RunBoth(lanes, {Tmcr(5, 0)}, in, RunStatus::kRunning);
    Expect(und.cpsr.bits.mode, cerfengine::ArmMode::kUndefined, "TMCR to a reserved wCx");
    RunBoth(lanes, {Tmcrr(0, 0, 1) | (1u << 4)}, in, RunStatus::kHalted);
    std::printf("iwmmxt: TMCR/TMRC/TMCRR/TMRRC agree with the reference\n");
}

}

int main() {
    TestAccumulator();
    TestIwmmxt();
    std::printf("arm_xscale_coproc_test passed\n");
    return 0;
}
