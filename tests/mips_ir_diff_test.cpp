#include "mips16_program_generator.h"
#include "mips32_program_generator.h"

#include <cinttypes>

namespace {

namespace MipsBranch = cerfengine::MipsBranch;

bool g_trace = false;

struct Config {
    const char*              name;
    cerfengine::MipsFeatures features;
    uint32_t                 page_shift;
    uint32_t                 programs;
    uint32_t                 seed;
};

struct Prepared {
    uint32_t                 load_va = 0;
    std::vector<uint32_t>    words;
    cerfengine::MipsCpuState init{};
    bool                     mips16 = false;
};

uint64_t Sext(uint32_t v) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)));
}

uint64_t RandomValue(std::mt19937& rng, bool wide) {
    static constexpr uint64_t kSpecial[] = {0u, 1u, 0x7FFFFFFFu, 0xFFFFFFFF80000000ull,
                                            0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull,
                                            0x7FFFFFFFFFFFFFFFull};
    const uint32_t pick = MipsRigPick(rng, 8u);
    if (pick == 0u) {
        const uint64_t special = kSpecial[MipsRigPick(rng, 7u)];
        return wide ? special : Sext(static_cast<uint32_t>(special));
    }
    if (!wide || pick < 5u) return Sext(rng());
    const uint64_t high = rng();
    const uint64_t low  = rng();
    return (high << 32) | low;
}

uint32_t StartVa(std::mt19937& rng, uint32_t page_shift, uint32_t length, bool mips16) {
    const uint32_t unit = mips16 ? 2u : 4u;
    if (MipsRigPick(rng, 3u) == 0u) {
        const uint32_t boundary = kMipsRigVaBase + (1u << page_shift) * (1u + MipsRigPick(rng, 4u));
        return boundary - unit * MipsRigPick(rng, mips16 ? 2u * length : length);
    }
    const uint32_t word = MipsRigPick(rng, 64u);
    const uint32_t half = mips16 ? MipsRigPick(rng, 2u) : 0u;
    return kMipsRigVaBase + 4u * word + 2u * half;
}

Prepared Prepare(std::mt19937& rng, const Config& cfg, Mips32ProgramGenerator& gen32,
                 Mips16ProgramGenerator& gen16) {
    const bool     wide   = cfg.features.isa_level != cerfengine::MipsIsaLevel::kMips1;
    const bool     mips16 = cfg.features.mips16 && MipsRigPick(rng, 2u) == 0u;
    const uint32_t length = 8u + MipsRigPick(rng, 40u);
    const uint32_t start  = StartVa(rng, cfg.page_shift, length, mips16);
    const bool     pend   = MipsRigPick(rng, 8u) == 0u;
    Prepared p;
    p.mips16 = mips16;
    cerfengine::MipsCpuState& init = p.init;
    for (uint32_t r = 1; r < 32u; ++r) init.gpr[r] = RandomValue(rng, wide);
    init.gpr[kMipsGenTargetReg] = Sext(kMipsRigEndVa);
    init.gpr[kMipsGenMmioReg]   = Sext(kMipsRigMmioVa);
    init.gpr[kMipsGenDataReg]   = Sext(kMipsRigDataVa);
    init.hi         = RandomValue(rng, wide);
    init.lo         = RandomValue(rng, wide);
    init.cp0_status = rng();
    init.pc         = start;
    if (mips16) {
        const Mips16Program prog = gen16.Program(start, length, pend);
        p.load_va         = prog.load_va;
        p.words           = prog.words;
        init.isa_mode     = 1u;
        init.gpr[29]      = Sext(kMipsRigDataVa);
        init.gpr[31]      = Sext(kMipsRigEnd16Va | 1u);
        if (pend) {
            init.branch_state = MipsRigPick(rng, 2u) == 0u ? MipsBranch::kUncond : MipsBranch::kRegister;
            init.btarget      = prog.start_vas[1u + MipsRigPick(rng, static_cast<uint32_t>(prog.start_vas.size()) - 1u)];
            init.btarget_isa  = 1u;
            init.branch_len   = MipsRigPick(rng, 2u) == 0u ? 2u : 4u;
        }
        return p;
    }
    p.load_va = start;
    p.words   = gen32.Program(start, length, pend);
    if (pend) {
        static constexpr uint32_t kKinds[] = {MipsBranch::kUncond, MipsBranch::kCond,
                                              MipsBranch::kCondLikely, MipsBranch::kRegister};
        init.branch_state = kKinds[MipsRigPick(rng, 4u)];
        init.btarget      = gen32.ForwardTarget(1u);
        init.bcond        = MipsRigPick(rng, 2u);
        init.branch_len   = 4u;
    }
    return p;
}

bool SamePendingBranch(const cerfengine::MipsCpuState& a, const cerfengine::MipsCpuState& b) {
    switch (a.branch_state) {
    case MipsBranch::kNone:
        return true;
    case MipsBranch::kCond:
    case MipsBranch::kCondLikely:
        return a.btarget == b.btarget && a.bcond == b.bcond && a.branch_len == b.branch_len;
    default:
        return a.btarget == b.btarget && a.btarget_isa == b.btarget_isa &&
               a.branch_len == b.branch_len;
    }
}

bool SameStep(const MipsIrRig& a, cerfengine::GuestEngine::RunStatus status_a, const MipsIrRig& b,
              cerfengine::GuestEngine::RunStatus status_b) {
    const cerfengine::MipsCpuState& x = a.State();
    const cerfengine::MipsCpuState& y = b.State();
    if (status_a != status_b || std::memcmp(x.gpr, y.gpr, sizeof(x.gpr)) != 0 || x.hi != y.hi ||
        x.lo != y.lo || x.pc != y.pc || x.isa_mode != y.isa_mode ||
        x.branch_state != y.branch_state || !SamePendingBranch(x, y) ||
        x.idle_wait != y.idle_wait || x.cp0_status != y.cp0_status ||
        x.cp0_cause != y.cp0_cause || x.cp0_epc != y.cp0_epc ||
        x.cp0_badvaddr != y.cp0_badvaddr || x.guest_cycle_counter != y.guest_cycle_counter) {
        return false;
    }
    return status_a != cerfengine::GuestEngine::RunStatus::kHalted ||
           std::strcmp(a.Engine().LastHalt().detail, b.Engine().LastHalt().detail) == 0;
}

bool SameEnd(const MipsIrRig& a, const MipsIrRig& b) {
    return a.Host().Ram() == b.Host().Ram() && a.Host().MmioLog() == b.Host().MmioLog() &&
           a.Events() == b.Events();
}

void PrintRig(const char* label, const MipsIrRig& c, const Prepared& prog) {
    const cerfengine::MipsCpuState& s = c.State();
    std::printf("  %s\n", label);
    for (uint32_t r = 0; r < 32u; ++r) {
        if (s.gpr[r] != prog.init.gpr[r]) {
            std::printf("  r%u %016" PRIX64 " (initial %016" PRIX64 ")\n", r, s.gpr[r],
                        prog.init.gpr[r]);
        }
    }
    std::printf("  pc %08X isa %u hi %016" PRIX64 " lo %016" PRIX64 " cycles %u bs %u btarget "
                "%08X bcond %u epc %08X cause %08X badv %08X exc %u mmio %zu\n",
                s.pc, s.isa_mode, s.hi, s.lo, s.guest_cycle_counter, s.branch_state, s.btarget,
                s.bcond, s.cp0_epc, s.cp0_cause, s.cp0_badvaddr, c.Events().exceptions,
                c.Host().MmioLog().size());
}

void Report(const MipsIrRig& c, const MipsIrRig& u, const Prepared& prog, const char* config,
            uint32_t index, uint32_t steps, bool halted) {
    std::printf("%s program %u (%s at %08X) after %u steps differs between verify modes\n",
                config, index, prog.mips16 ? "mips16" : "mips32", prog.init.pc, steps);
    for (uint32_t w : prog.words) std::printf(" %08X", w);
    std::printf("\n");
    if (halted) std::printf("  halt: %s\n", c.Engine().LastHalt().detail);
    PrintRig("verified", c, prog);
    PrintRig("unverified", u, prog);
    TestFail("MIPS CerfIR differs between verify modes");
}

void RunConfig(const Config& cfg) {
    MipsIrRig              c(cfg.features, cfg.page_shift, true);
    MipsIrRig              u(cfg.features, cfg.page_shift, false);
    std::mt19937           rng(cfg.seed);
    Mips32ProgramGenerator gen32(rng, cfg.features);
    Mips16ProgramGenerator gen16(rng, cfg.features.isa_level != cerfengine::MipsIsaLevel::kMips1);
    const std::vector<uint32_t> end_loops = {0x1000FFFFu, 0u, 0u, 0u, 0x650017FFu};
    c.Load(kMipsRigEndVa, end_loops);
    u.Load(kMipsRigEndVa, end_loops);
    uint32_t mips16 = 0, pending = 0, exceptions = 0, halts = 0, idles = 0, power = 0, tlb = 0;
    StateDigest digest;
    for (uint32_t p = 0; p < cfg.programs; ++p) {
        const Prepared prog = Prepare(rng, cfg, gen32, gen16);
        std::vector<uint32_t> data(0x200u);
        for (uint32_t& w : data) w = rng();
        for (MipsIrRig* rig : {&c, &u}) {
            rig->Load(prog.load_va, prog.words);
            rig->Load(kMipsRigDataVa, data);
            rig->Begin(prog.init);
        }
        if (g_trace) {
            std::printf("%s program %u %s at %08X:", cfg.name, p, prog.mips16 ? "mips16" : "mips32",
                        prog.init.pc);
            for (uint32_t w : prog.words) std::printf(" %08X", w);
            std::printf("\n");
        }
        mips16 += prog.mips16 ? 1u : 0u;
        pending += prog.init.branch_state != MipsBranch::kNone ? 1u : 0u;
        uint32_t step   = 0;
        bool     halted = false;
        auto     last   = cerfengine::GuestEngine::RunStatus::kRunning;
        for (; step < 400u && !c.Finished(); ++step) {
            const auto sc = c.Step();
            const auto su = u.Step();
            last          = sc;
            halted        = sc == cerfengine::GuestEngine::RunStatus::kHalted;
            if (!SameStep(c, sc, u, su)) Report(c, u, prog, cfg.name, p, step, halted);
            if (halted) {
                ++halts;
                if (g_trace) std::printf("  halt: %s\n", c.Engine().LastHalt().detail);
                break;
            }
        }
        if (!SameEnd(c, u)) Report(c, u, prog, cfg.name, p, step, halted);
        MipsRigDigestRun(digest, c, last);
        digest.Add32(step);
        exceptions += c.Events().exceptions;
        power += c.Events().power_downs;
        idles += c.State().idle_wait != 0u ? 1u : 0u;
        for (uint32_t k : c.Events().tlb_ops) tlb += k;
    }
    std::printf("%s: %u programs agree in both verify modes (%u mips16, %u pending entries, "
                "%u exceptions, %u halts, %u idle, %u power-down, %u tlb ops), digest %016llX\n",
                cfg.name, cfg.programs, mips16, pending, exceptions, halts, idles, power, tlb,
                digest.Value());
}

}

int main(int argc, char**) {
    g_trace = argc > 1;
    Config vr4121{"vr4121", {}, 10u, 3000u, 1u};
    vr4121.features.isa_level          = cerfengine::MipsIsaLevel::kMips3;
    vr4121.features.vr41xx_power_modes = true;
    vr4121.features.phys_addr_mask     = 0x1FFFFFFFu;
    Config vr4122                      = vr4121;
    vr4122.name                        = "vr4122";
    vr4122.features.mips16             = true;
    vr4122.programs                    = 5000u;
    vr4122.seed                        = 2u;
    Config tx39{"tx39", {}, 12u, 3000u, 3u};
    tx39.features.tx39_multiply_add = true;
    Config vr5500{"vr5500", {}, 12u, 3000u, 4u};
    vr5500.features.isa_level = cerfengine::MipsIsaLevel::kMips4;
    vr5500.features.fpu       = true;
    vr5500.features.llsc      = true;
    RunConfig(vr4121);
    RunConfig(vr4122);
    RunConfig(tx39);
    RunConfig(vr5500);
    std::printf("mips_ir_diff_test passed\n");
    return 0;
}
