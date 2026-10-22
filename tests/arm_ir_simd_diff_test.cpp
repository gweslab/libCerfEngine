#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_jit.h>

#include <array>

namespace {

constexpr uint32_t kCpacrFullCp10Cp11 = 0x00F00000u;
constexpr uint16_t kThumbBranchSelf   = 0xE7FEu;

bool     g_trace        = false;
uint32_t g_dump_program = 0xFFFFFFFFu;

struct Config {
    const char*             name;
    cerfengine::ArmFeatures features;
    bool                    thumb;
    uint32_t                seed;
};

uint32_t PointerValue(std::mt19937& rng) {
    const uint32_t base = 0x3000u + RigPick(rng, 0x6000u);
    return RigPick(rng, 4u) == 0u ? base : (base & ~7u);
}

uint32_t BaseRegister(std::mt19937& rng) {
    return RigPick(rng, 10u);
}

uint32_t NeonData(std::mt19937& rng) {
    return 0xF2000000u | (static_cast<uint32_t>(rng()) & 0x01FFFFFFu);
}

uint32_t NeonLoadStore(std::mt19937& rng) {
    static constexpr uint32_t kRm[] = {13u, 15u, 0u, 1u, 2u, 3u};
    const uint32_t rm   = kRm[RigPick(rng, 6u)];
    const uint32_t bits = static_cast<uint32_t>(rng()) & 0x00F0FFF0u;
    const uint32_t rn   = BaseRegister(rng);
    return 0xF4000000u | bits | (rn << 16) | rm;
}

uint32_t VfpDataOperation(std::mt19937& rng) {
    return 0xEE000A00u | (static_cast<uint32_t>(rng()) & 0x00FFF1EFu);
}

uint32_t VfpLoadStore(std::mt19937& rng) {
    const uint32_t bits = static_cast<uint32_t>(rng()) & 0x01F0F1FFu;
    const uint32_t rn   = BaseRegister(rng);
    return 0xEC000A00u | bits | (rn << 16);
}

uint32_t VfpRegisterTransfer(std::mt19937& rng) {
    return 0xEE000A10u | (static_cast<uint32_t>(rng()) & 0x00FFF1EFu);
}

uint32_t VfpTwoRegisterTransfer(std::mt19937& rng) {
    const uint32_t op   = RigPick(rng, 2u);
    const uint32_t rt2  = RigPick(rng, 15u);
    const uint32_t rt   = RigPick(rng, 15u);
    const uint32_t c    = RigPick(rng, 2u);
    const uint32_t bits = static_cast<uint32_t>(rng()) & 0x2Fu;
    return 0xEC400A10u | (op << 20) | (rt2 << 16) | (rt << 12) | (c << 8) | bits;
}

uint32_t ArmWord(std::mt19937& rng) {
    switch (RigPick(rng, 7u)) {
    case 0u:  return NeonData(rng);
    case 1u:  return NeonLoadStore(rng);
    case 2u:  return VfpDataOperation(rng);
    case 3u:  return VfpLoadStore(rng);
    case 4u:  return VfpRegisterTransfer(rng);
    case 5u:  return VfpTwoRegisterTransfer(rng);
    default:  return 0xE2800000u | (static_cast<uint32_t>(rng()) & 0x000FFFFFu);
    }
}

/* ARM DDI 0406C.c A7.4 (p. A7-261): the U bit is bit[24] of the ARM encoding and
   bit[12] of the first Thumb halfword, "1 1 1 U 1 1 1 1". A7.7 (p. A7-275): the
   ARM element and structure load/store prefix 1111 0100 is 1111 1001 in Thumb. */
uint32_t ThumbWord(std::mt19937& rng) {
    uint32_t word = ArmWord(rng);
    while ((word & 0xFE000000u) == 0xE2000000u) word = ArmWord(rng);
    if ((word & 0xFE000000u) == 0xF2000000u) {
        return 0xEF000000u | ((word >> 24) & 1u) << 28 | (word & 0x00FFFFFFu);
    }
    if ((word & 0xFF000000u) == 0xF4000000u) return 0xF9000000u | (word & 0x00FFFFFFu);
    return word;
}

std::vector<uint32_t> Program(std::mt19937& rng, bool thumb) {
    const uint32_t length = 1u + RigPick(rng, 6u);
    std::vector<uint32_t> words;
    if (!thumb) {
        for (uint32_t i = 0; i < length; ++i) words.push_back(ArmWord(rng));
        words.push_back(kRigBranchSelf);
        return words;
    }
    std::vector<uint16_t> halfwords;
    for (uint32_t i = 0; i < length; ++i) {
        const uint32_t word = ThumbWord(rng);
        halfwords.push_back(static_cast<uint16_t>(word >> 16));
        halfwords.push_back(static_cast<uint16_t>(word));
    }
    halfwords.push_back(kThumbBranchSelf);
    words.assign((halfwords.size() + 1u) / 2u, 0u);
    for (size_t i = 0; i < halfwords.size(); ++i) {
        words[i / 2u] |= static_cast<uint32_t>(halfwords[i]) << (16u * (i % 2u));
    }
    return words;
}

void Prepare(ArmIrRig& rig) {
    rig.Host().EnableMmioLog();
    rig.System().AllowAlignmentFaults();
    rig.Cpu().SetResetCoprocessorAccess(kCpacrFullCp10Cp11);
    rig.Engine().Get<cerfengine::ArmJit>().SetIdleWake(true);
}

bool SameVfp(const cerfengine::ArmCpuState& a, const cerfengine::ArmCpuState& b) {
    return std::memcmp(a.vfp_d, b.vfp_d, sizeof(a.vfp_d)) == 0 && a.fpscr == b.fpscr &&
           a.fpexc == b.fpexc;
}

void TestSimd(const Config& config) {
    std::array<std::unique_ptr<ArmIrRig>, 2> rigs = {
        std::make_unique<ArmIrRig>(true, config.features),
        std::make_unique<ArmIrRig>(false, config.features),
    };
    for (auto& rig : rigs) Prepare(*rig);
    std::mt19937       rng(config.seed);
    constexpr uint32_t kPrograms = 20000u;
    uint32_t           und_exits = 0;
    uint32_t           halts     = 0;
    StateDigest        digest;
    for (uint32_t n = 0; n < kPrograms; ++n) {
        const std::vector<uint32_t> program = Program(rng, config.thumb);
        static constexpr uint32_t kModes[] = {0x10u, 0x13u, 0x1Fu, 0xD3u};
        const uint32_t mode_cpsr = kModes[RigPick(rng, 4u)] | (config.thumb ? 0x20u : 0u);

        cerfengine::ArmCpuState initial{};
        for (uint32_t i = 0; i < 8u; ++i) initial.gprs[i] = PointerValue(rng);
        initial.gprs[8]  = 0x10000000u + RigPick(rng, 0x40u) * 4u;
        initial.gprs[9]  = PointerValue(rng);
        for (uint32_t i = 10; i < 15u; ++i) initial.gprs[i] = static_cast<uint32_t>(rng());
        initial.gprs[15] = kRigRamBase;
        for (uint64_t& d : initial.vfp_d) {
            const uint64_t high = static_cast<uint32_t>(rng());
            const uint64_t low  = static_cast<uint32_t>(rng());
            d = (high << 32) | low;
        }
        initial.fpscr = static_cast<uint32_t>(rng()) & 0x03C0009Fu;
        initial.fpexc = 0x40000000u;
        initial.nf    = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.zf    = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.cf    = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.vf    = static_cast<uint8_t>(RigPick(rng, 2u));

        cerfengine::GuestEngine::RunStatus status[2];
        for (uint32_t r = 0; r < 2u; ++r) {
            rigs[r]->Host().ResetMmioLog();
            status[r] = rigs[r]->Run(program, initial, 0u, mode_cpsr);
        }
        ArmIrRig&  a      = *rigs[0];
        ArmIrRig&  b      = *rigs[1];
        const bool halted = status[0] == cerfengine::GuestEngine::RunStatus::kHalted;
        if (!RigSameRun(a, status[0], b, status[1]) || !SameVfp(a.State(), b.State()) ||
            a.Host().MmioLog() != b.Host().MmioLog() || a.Host().Ram() != b.Host().Ram()) {
            const cerfengine::ArmCpuState& c = a.State();
            const cerfengine::ArmCpuState& u = b.State();
            std::printf("program %u:", n);
            for (uint32_t w : program) std::printf(" %08X", w);
            std::printf("\nstatus verified=%d unverified=%d\n", static_cast<int>(status[0]),
                        static_cast<int>(status[1]));
            if (halted) std::printf("halt: %s\n", a.Engine().LastHalt().detail);
            RigDump("initial   ", initial);
            RigDump("verified  ", c);
            RigDump("unverified", u);
            std::printf("fpscr %08X/%08X fpexc %08X/%08X mmio accesses %zu/%zu\n", c.fpscr,
                        u.fpscr, c.fpexc, u.fpexc, a.Host().MmioLog().size(),
                        b.Host().MmioLog().size());
            for (uint32_t i = 0; i < 32u; ++i) {
                if (c.vfp_d[i] == u.vfp_d[i]) continue;
                std::printf("d%u %016llX unverified %016llX\n", i,
                            static_cast<unsigned long long>(c.vfp_d[i]),
                            static_cast<unsigned long long>(u.vfp_d[i]));
            }
            TestFail("CerfIR differs between verify modes on VFP/NEON");
        }
        RigDigestRun(digest, a, status[0]);
        if (g_trace) {
            StateDigest one;
            RigDigestRun(one, a, status[0]);
            std::printf("%s %u %016llX:", config.name, n, one.Value());
            for (uint32_t w : program) std::printf(" %08X", w);
            std::printf("\n");
        }
        if (n == g_dump_program) {
            RigDump("initial", initial);
            RigDump("final  ", a.State());
            std::printf("fpscr %08X -> %08X\n", initial.fpscr, a.State().fpscr);
            for (uint32_t i = 0; i < 32u; ++i) {
                std::printf("d%u %016llX -> %016llX\n", i,
                            static_cast<unsigned long long>(initial.vfp_d[i]),
                            static_cast<unsigned long long>(a.State().vfp_d[i]));
            }
        }
        if (halted) {
            ++halts;
            for (auto& rig : rigs) {
                rig->Restart();
                Prepare(*rig);
            }
            continue;
        }
        if (a.State().cpsr.bits.mode == cerfengine::ArmMode::kUndefined) ++und_exits;
    }
    std::printf("simd %s: %u programs agree in both verify modes (undefined %u, halts %u), "
                "digest %016llX\n",
                config.name, kPrograms, und_exits, halts, digest.Value());
    RigPrintCompileStats("cerfir verified  ", rigs[0]->Host());
    RigPrintCompileStats("cerfir unverified", rigs[1]->Host());
}

cerfengine::ArmFeatures V7Simd(uint32_t mvfr0, uint32_t mvfr1) {
    cerfengine::ArmFeatures f;
    f.arch_v6                      = true;
    f.arch_v7                      = true;
    f.thumb                        = true;
    f.thumb2                       = true;
    f.blx_reg                      = true;
    f.dsp                          = true;
    f.clz                          = true;
    f.rev                          = true;
    f.bit_field                    = true;
    f.extend_rotate                = true;
    f.movw_movt                    = true;
    f.mls                          = true;
    f.ldrex_strex                  = true;
    f.ldrex_strex_v6k              = true;
    f.load_store_double            = true;
    f.barrier_insn                 = true;
    f.preload                      = true;
    f.load_to_pc_interworking      = true;
    f.data_proc_to_pc_interworking = true;
    f.armv5_unconditional_space    = true;
    f.vfp                          = true;
    f.neon                         = true;
    f.fpsid                        = 0x410330C3u;
    f.mvfr0                        = mvfr0;
    f.mvfr1                        = mvfr1;
    return f;
}

}

int main(int argc, char** argv) {
    g_trace = argc == 2;
    if (argc == 3) g_dump_program = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    TestSimd({"arm vfpv3", V7Simd(0x11110222u, 0x00011111u), false, 0x53494D44u});
    TestSimd({"arm vfpv4", V7Simd(0x10110222u, 0x11111111u), false, 0x53494D45u});
    TestSimd({"thumb vfpv3", V7Simd(0x11110222u, 0x00011111u), true, 0x53494D46u});
    std::printf("arm_ir_simd_diff_test passed\n");
    return 0;
}
