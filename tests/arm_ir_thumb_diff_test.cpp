#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_jit.h>

#include <array>

namespace {

constexpr uint16_t kThumbBranchSelf = 0xE7FEu;

class UndefinedCoprocessor : public TestCoprocessor {
public:
    cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess&) override {
        return cerfengine::ArmCoprocDecision{};
    }
};

struct Config {
    const char*             name;
    cerfengine::ArmFeatures features;
    uint32_t                seed;
};

uint32_t RandomRegisterValue(std::mt19937& rng) {
    static constexpr uint32_t kSpecial[] = {0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u,
                                            0xFFFFFFFFu, 0x0000001Fu, 0x00000020u, 0x80000001u,
                                            0xFFFFFF20u, 0x80008000u, 0x7FFF7FFFu};
    switch (RigPick(rng, 4u)) {
    case 0u:  return kSpecial[RigPick(rng, sizeof(kSpecial) / sizeof(kSpecial[0]))];
    case 1u:  return RigPick(rng, 70u);
    case 2u:  return static_cast<uint32_t>(rng());
    default:  return 0x1000u + RigPick(rng, kRigRamSize - 0x2000u);
    }
}

void AppendWide(std::vector<uint16_t>& p, uint32_t hw1, uint32_t hw2) {
    p.push_back(static_cast<uint16_t>(hw1));
    p.push_back(static_cast<uint16_t>(hw2));
}

void AppendInstruction(std::mt19937& rng, const cerfengine::ArmFeatures& f,
                       std::vector<uint16_t>& p) {
    const uint32_t pick = RigPick(rng, 20u);
    if (f.thumb2 && pick < 6u) {
        const uint32_t hw1 = 0xE800u + RigPick(rng, 0x1800u);
        const uint32_t hw2 = RigPick(rng, 0x10000u);
        AppendWide(p, hw1, hw2);
        return;
    }
    if (pick == 6u) {
        const uint32_t offset = RigPick(rng, 0x800u);
        p.push_back(static_cast<uint16_t>(0xF000u | offset));
        p.push_back(static_cast<uint16_t>(0xF800u | RigPick(rng, 0x800u)));
        return;
    }
    if (pick == 7u) {
        const uint32_t high   = RigPick(rng, 2u) != 0u ? 0xF000u : 0xF800u;
        const uint32_t offset = RigPick(rng, 0x800u);
        p.push_back(static_cast<uint16_t>(high | offset));
        return;
    }
    if (f.thumb2 && pick == 8u) {
        const uint32_t op  = RigPick(rng, 2u);
        const uint32_t i   = RigPick(rng, 2u);
        const uint32_t imm = RigPick(rng, 32u);
        const uint32_t rn  = RigPick(rng, 8u);
        p.push_back(static_cast<uint16_t>(0xB100u | (op << 11) | (i << 9) | (imm << 3) | rn));
        return;
    }
    if (f.thumb2 && pick == 9u) {
        uint32_t rn = RigPick(rng, 16u);
        if (rn == 13u) rn = 14u;
        const uint32_t h  = RigPick(rng, 2u);
        const uint32_t rm = RigPick(rng, 13u);
        AppendWide(p, 0xE8D0u | rn, 0xF000u | (h << 4) | rm);
        return;
    }
    if (pick == 10u) {
        const uint32_t e   = RigPick(rng, 2u);
        const uint32_t aif = 1u + RigPick(rng, 7u);
        p.push_back(static_cast<uint16_t>(0xB660u | (e << 4) | aif));
        return;
    }
    if (f.thumb2 && pick == 11u) {
        AppendWide(p, 0xF3BFu, 0x8F2Fu);
        return;
    }
    if (pick == 12u) {
        const uint32_t firstcond = RigPick(rng, 15u);
        const uint32_t mask      = 1u + RigPick(rng, 15u);
        p.push_back(static_cast<uint16_t>(0xBF00u | (firstcond << 4) | mask));
        return;
    }
    p.push_back(static_cast<uint16_t>(RigPick(rng, 0xE800u)));
}

std::vector<uint32_t> Pack(const std::vector<uint16_t>& halfwords) {
    std::vector<uint32_t> words((halfwords.size() + 1u) / 2u, 0u);
    for (size_t i = 0; i < halfwords.size(); ++i) {
        words[i / 2u] |= static_cast<uint32_t>(halfwords[i]) << (16u * (i % 2u));
    }
    return words;
}

void Prepare(ArmIrRig& rig, UndefinedCoprocessor& coprocessor) {
    rig.Host().EnableMmioLog();
    rig.System().AllowAlignmentFaults();
    rig.System().SetCoprocessor(&coprocessor);
    rig.Engine().Get<cerfengine::ArmJit>().SetIdleWake(true);
}

void TestThumb(const Config& config) {
    UndefinedCoprocessor                     coprocessor;
    std::array<std::unique_ptr<ArmIrRig>, 2> rigs = {
        std::make_unique<ArmIrRig>(true, config.features),
        std::make_unique<ArmIrRig>(false, config.features),
    };
    for (auto& rig : rigs) Prepare(*rig, coprocessor);
    std::mt19937       rng(config.seed);
    constexpr uint32_t kPrograms = 30000u;
    uint32_t           und_exits = 0;
    uint32_t           halts     = 0;
    for (uint32_t n = 0; n < kPrograms; ++n) {
        std::vector<uint16_t> halfwords;
        const uint32_t        length = 1u + RigPick(rng, 8u);
        for (uint32_t i = 0; i < length; ++i) AppendInstruction(rng, config.features, halfwords);
        halfwords.push_back(kThumbBranchSelf);
        const std::vector<uint32_t> program = Pack(halfwords);

        static constexpr uint32_t kStartModes[] = {0x30u, 0x1F0u, 0x1FFu, 0x1F2u, 0x133u};
        const uint32_t mode_cpsr = kStartModes[RigPick(rng, 5u)];
        const bool     has_spsr  = (mode_cpsr & 0x1Fu) == 0x12u || (mode_cpsr & 0x1Fu) == 0x13u;
        const uint32_t spsr      = has_spsr ? (RigPick(rng, 16u) << 28) | 0x1F0u : 0u;

        cerfengine::ArmCpuState initial{};
        for (uint32_t i = 0; i < 15u; ++i) initial.gprs[i] = RandomRegisterValue(rng);
        initial.gprs[15] = kRigRamBase;
        initial.nf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.zf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.cf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.vf       = static_cast<uint8_t>(RigPick(rng, 2u));

        cerfengine::GuestEngine::RunStatus status[2];
        for (uint32_t r = 0; r < 2u; ++r) {
            rigs[r]->Host().ResetMmioLog();
            status[r] = rigs[r]->Run(program, initial, 0u, mode_cpsr, spsr);
        }
        ArmIrRig&  a      = *rigs[0];
        ArmIrRig&  b      = *rigs[1];
        const bool halted = status[0] == cerfengine::GuestEngine::RunStatus::kHalted;
        if (!RigSameRun(a, status[0], b, status[1]) || a.Host().MmioLog() != b.Host().MmioLog() ||
            a.Host().Ram() != b.Host().Ram()) {
            std::printf("program %u:", n);
            for (uint16_t h : halfwords) std::printf(" %04X", h);
            std::printf("\nstatus verified=%d unverified=%d\n", static_cast<int>(status[0]),
                        static_cast<int>(status[1]));
            if (halted) std::printf("halt: %s\n", a.Engine().LastHalt().detail);
            RigDump("initial   ", initial);
            RigDump("verified  ", a.State());
            RigDump("unverified", b.State());
            TestFail("CerfIR differs between verify modes in Thumb state");
        }
        if (halted) {
            ++halts;
            for (auto& rig : rigs) {
                rig->Restart();
                rig->Engine().Get<cerfengine::ArmJit>().SetIdleWake(true);
            }
            continue;
        }
        if (a.State().cpsr.bits.mode == cerfengine::ArmMode::kUndefined) ++und_exits;
    }
    std::printf("thumb %s: %u programs agree in both verify modes (undefined %u, halts %u)\n",
                config.name, kPrograms, und_exits, halts);
    RigPrintCompileStats("cerfir verified  ", rigs[0]->Host());
    RigPrintCompileStats("cerfir unverified", rigs[1]->Host());
}

}

int main() {
    Config v5{"v5t", {}, 0x54484D35u};
    v5.features.thumb                        = true;
    v5.features.dsp                          = true;
    v5.features.clz                          = true;
    v5.features.blx_reg                      = true;
    v5.features.load_to_pc_interworking      = true;
    v5.features.armv5_unconditional_space    = true;
    v5.features.accesses_spsr_in_system_mode = true;
    TestThumb(v5);

    Config v7{"v7 thumb2", {}, 0x54484D37u};
    v7.features.arch_v6                      = true;
    v7.features.arch_v7                      = true;
    v7.features.thumb                        = true;
    v7.features.thumb2                       = true;
    v7.features.blx_reg                      = true;
    v7.features.dsp                          = true;
    v7.features.clz                          = true;
    v7.features.rev                          = true;
    v7.features.bit_field                    = true;
    v7.features.extend_rotate                = true;
    v7.features.movw_movt                    = true;
    v7.features.mls                          = true;
    v7.features.ldrex_strex                  = true;
    v7.features.ldrex_strex_v6k              = true;
    v7.features.load_store_double            = true;
    v7.features.barrier_insn                 = true;
    v7.features.preload                      = true;
    v7.features.load_to_pc_interworking      = true;
    v7.features.data_proc_to_pc_interworking = true;
    v7.features.armv5_unconditional_space    = true;
    TestThumb(v7);
    std::printf("arm_ir_thumb_diff_test passed\n");
    return 0;
}
