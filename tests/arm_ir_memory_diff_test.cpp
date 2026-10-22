#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_interrupt_channel.h>

#include <array>

namespace {

constexpr uint32_t kDataBase  = 0x1000u;
constexpr uint32_t kDataBytes = kRigRamSize - kDataBase;
constexpr uint32_t kMmuBit    = 1u << 0;
constexpr uint32_t kAlignBit  = 1u << 1;
constexpr uint32_t kHoleBase  = 0x8000u;
constexpr uint32_t kHoleSize  = 0x1000u;

struct Config {
    const char*             name;
    cerfengine::ArmFeatures features;
    uint32_t                sctlr;
};

uint32_t RandomTransfer(std::mt19937& rng) {
    const uint32_t cond     = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t reg_form = RigPick(rng, 3u) == 0u ? 1u : 0u;
    const uint32_t p        = RigPick(rng, 2u);
    const uint32_t u        = RigPick(rng, 2u);
    const uint32_t b        = RigPick(rng, 2u);
    const uint32_t w        = RigPick(rng, 4u) == 0u ? 1u : 0u;
    const uint32_t l        = RigPick(rng, 2u);
    uint32_t       rn       = RigPick(rng, 16u) == 0u ? 15u : RigPick(rng, 10u);
    if (l == 0u && rn == 15u) rn = RigPick(rng, 10u);
    const uint32_t rd       = RigPick(rng, 12u) == 0u ? 15u : RigPick(rng, 13u);
    uint32_t word = (cond << 28) | (1u << 26) | (reg_form << 25) | (p << 24) | (u << 23) |
                    (b << 22) | (w << 21) | (l << 20) | (rn << 16) | (rd << 12);
    if (reg_form == 0u) {
        return word | (RigPick(rng, 8u) == 0u ? RigPick(rng, 0x1000u) : RigPick(rng, 0x40u));
    }
    const uint32_t type = RigPick(rng, 3u);
    const uint32_t imm5 = type == 0u ? RigPick(rng, 4u) : RigPick(rng, 32u);
    return word | (imm5 << 7) | (type << 5) | (10u + RigPick(rng, 3u));
}

uint32_t RandomExtraTransfer(std::mt19937& rng) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    uint32_t       rn   = RigPick(rng, 16u) == 0u ? 15u : RigPick(rng, 10u);
    if (RigPick(rng, 6u) == 0u) {
        const uint32_t rt  = RigPick(rng, 12u) == 0u ? 15u : RigPick(rng, 13u);
        const uint32_t rt2 = RigPick(rng, 13u);
        return (cond << 28) | (1u << 24) | (RigPick(rng, 2u) << 22) | (rn << 16) | (rt << 12) |
               0x90u | rt2;
    }
    const uint32_t imm_form = RigPick(rng, 3u) != 0u ? 1u : 0u;
    const uint32_t p        = RigPick(rng, 2u);
    const uint32_t u        = RigPick(rng, 2u);
    const uint32_t w        = RigPick(rng, 4u) == 0u ? 1u : 0u;
    const uint32_t l        = RigPick(rng, 2u);
    const uint32_t op2      = 1u + RigPick(rng, 3u);
    const bool     dual     = l == 0u && op2 >= 2u;
    if (l == 0u && op2 != 2u && rn == 15u) rn = RigPick(rng, 10u);
    uint32_t       rd       = RigPick(rng, 12u) == 0u ? 15u : RigPick(rng, 13u);
    if (dual && RigPick(rng, 4u) != 0u) rd = 2u * RigPick(rng, 6u);
    uint32_t word = (cond << 28) | (p << 24) | (u << 23) | (imm_form << 22) | (w << 21) |
                    (l << 20) | (rn << 16) | (rd << 12) | (1u << 7) | (op2 << 5) | (1u << 4);
    if (imm_form != 0u) {
        const uint32_t imm8 = RigPick(rng, 8u) == 0u ? RigPick(rng, 0x100u) : RigPick(rng, 0x20u);
        return word | ((imm8 >> 4) << 8) | (imm8 & 0xFu);
    }
    const uint32_t rm = RigPick(rng, 16u) == 0u ? 15u : 10u + RigPick(rng, 3u);
    return word | rm;
}

uint32_t RandomBlockTransfer(std::mt19937& rng) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t l    = RigPick(rng, 2u);
    const uint32_t s    = RigPick(rng, 8u) == 0u ? 1u : 0u;
    const uint32_t w    = RigPick(rng, 2u);
    const uint32_t rn   = RigPick(rng, 16u) == 0u ? 15u : RigPick(rng, 10u);
    uint32_t       list = 0u;
    if (RigPick(rng, 16u) != 0u) {
        for (uint32_t i = 0; i < 15u; ++i) {
            if (RigPick(rng, 4u) == 0u) list |= 1u << i;
        }
        if (RigPick(rng, 6u) == 0u) list |= 0x8000u;
    }
    const uint32_t p = RigPick(rng, 2u);
    const uint32_t u = RigPick(rng, 2u);
    return (cond << 28) | (4u << 25) | (p << 24) | (u << 23) | (s << 22) | (w << 21) | (l << 20) |
           (rn << 16) | list;
}

void PushExclusive(std::mt19937& rng, std::vector<uint32_t>& program) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t size = RigPick(rng, 4u);
    const uint32_t rn   = RigPick(rng, 10u);
    const uint32_t rt   = size == 1u ? 2u * RigPick(rng, 6u) : RigPick(rng, 13u);
    if (RigPick(rng, 3u) != 0u) {
        program.push_back((cond << 28) | 0x01900F9Fu | (size << 21) | (rn << 16) | (rt << 12));
    }
    if (RigPick(rng, 3u) != 0u) {
        const uint32_t source = size == 1u ? 2u * RigPick(rng, 5u) : RigPick(rng, 10u);
        const uint32_t status = 10u + RigPick(rng, 3u);
        program.push_back((cond << 28) | 0x01800F90u | (size << 21) | (rn << 16) | (status << 12) |
                          source);
    }
}

uint32_t PointerValue(std::mt19937& rng) {
    const uint32_t base = 0x3000u + RigPick(rng, 0x6000u);
    switch (RigPick(rng, 8u)) {
    case 0u:  return (base | 0x3FFu) - RigPick(rng, 4u);
    case 1u:
    case 2u:  return base;
    default:  return base & ~3u;
    }
}

std::vector<uint32_t> DataPattern(uint32_t seed) {
    std::vector<uint32_t> words(kDataBytes / 4u);
    for (uint32_t i = 0; i < words.size(); ++i) {
        words[i] = 0x3000u + ((i + seed) * 0x9E3779B1u) % 0x6000u;
    }
    return words;
}

bool SameSystem(const ArmIrRig& a, const ArmIrRig& b) {
    return a.Host().Ram() == b.Host().Ram() && a.Host().MmioLog() == b.Host().MmioLog() &&
           a.System().AlignmentFaults() == b.System().AlignmentFaults() &&
           a.System().LastFaultVa() == b.System().LastFaultVa() &&
           a.System().LastFaultWrite() == b.System().LastFaultWrite() &&
           a.System().WalkFaults() == b.System().WalkFaults() &&
           a.System().LastWalkFaultVa() == b.System().LastWalkFaultVa() &&
           a.System().LastWalkFaultWrite() == b.System().LastWalkFaultWrite();
}

void RunConfig(const Config& config, uint32_t seed, uint32_t programs) {
    std::array<std::unique_ptr<ArmIrRig>, 2> rigs = {
        std::make_unique<ArmIrRig>(true, config.features),
        std::make_unique<ArmIrRig>(false, config.features),
    };
    for (auto& rig : rigs) {
        rig->Host().EnableMmioLog();
        rig->Host().RaiseExitOnMmio(&rig->Cpu().State()->chain_exit_request,
                                    cerfengine::kChainExitClock);
        cerfengine::ArmInterruptChannel* channel =
            &rig->Engine().Get<cerfengine::ArmInterruptChannel>();
        rig->Host().RaiseIrqOnMmio([channel] { channel->SetInterruptPending(); });
        rig->System().AllowAlignmentFaults();
        if ((config.sctlr & kMmuBit) != 0u) rig->System().EnableIdentityWalk(kHoleBase, kHoleSize);
    }
    std::mt19937 rng(seed);
    uint32_t     mmio_accesses = 0;
    uint32_t     und_exits     = 0;
    uint32_t     abort_exits   = 0;
    uint32_t     pc_exits      = 0;
    StateDigest  digest;
    for (uint32_t n = 0; n < programs; ++n) {
        std::vector<uint32_t> program;
        const uint32_t        length = 1u + RigPick(rng, 6u);
        for (uint32_t i = 0; i < length; ++i) {
            switch (RigPick(rng, 7u)) {
            case 0u:
            case 1u:  program.push_back(RandomExtraTransfer(rng)); break;
            case 2u:  program.push_back(RandomBlockTransfer(rng)); break;
            case 3u:  PushExclusive(rng, program); break;
            default:  program.push_back(RandomTransfer(rng)); break;
            }
        }
        program.push_back(kRigBranchSelf);

        cerfengine::ArmCpuState initial{};
        for (uint32_t i = 0; i < 8u; ++i) initial.gprs[i] = PointerValue(rng);
        initial.gprs[8]  = 0x00010000u + RigPick(rng, 0x100u);
        initial.gprs[9]  = 0x10000000u + RigPick(rng, 0x100u) * 4u;
        for (uint32_t i = 10; i < 13u; ++i) initial.gprs[i] = RigPick(rng, 0x40u);
        initial.gprs[13] = static_cast<uint32_t>(rng());
        initial.gprs[14] = static_cast<uint32_t>(rng());
        initial.gprs[15] = kRigRamBase;
        initial.nf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.zf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.cf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.vf       = static_cast<uint8_t>(RigPick(rng, 2u));
        const uint32_t nzcv = RigPick(rng, 16u);
        const uint32_t spsr = (nzcv << 28) | (RigPick(rng, 2u) == 0u ? 0x10u : 0x13u);
        const uint32_t mode = RigPick(rng, 4u) == 0u ? 0x13u : 0u;

        const std::vector<uint32_t> data = DataPattern(n);
        cerfengine::GuestEngine::RunStatus status[2];
        for (uint32_t r = 0; r < 2u; ++r) {
            rigs[r]->Host().LoadWords(kDataBase, data.data(), data.size());
            rigs[r]->Host().ResetMmioLog();
            rigs[r]->Engine().Get<cerfengine::ArmInterruptChannel>().ClearInterruptPending();
            status[r] = rigs[r]->Run(program, initial, config.sctlr, mode, spsr);
        }
        if (!RigSameRun(*rigs[0], status[0], *rigs[1], status[1]) ||
            !SameSystem(*rigs[0], *rigs[1])) {
            std::printf("%s program %u:", config.name, n);
            for (uint32_t word : program) std::printf(" %08X", word);
            std::printf("\nstatus verified=%d unverified=%d mmio %zu/%zu faults %u/%u\n",
                        static_cast<int>(status[0]), static_cast<int>(status[1]),
                        rigs[0]->Host().MmioLog().size(), rigs[1]->Host().MmioLog().size(),
                        rigs[0]->System().AlignmentFaults(), rigs[1]->System().AlignmentFaults());
            if (status[0] == cerfengine::GuestEngine::RunStatus::kHalted) {
                std::printf("halt: %s\n", rigs[0]->Engine().LastHalt().detail);
            }
            RigDump("initial   ", initial);
            RigDump("verified  ", rigs[0]->State());
            RigDump("unverified", rigs[1]->State());
            TestFail("CerfIR differs between verify modes");
        }
        RigDigestRun(digest, *rigs[0], status[0]);
        const cerfengine::ArmCpuState& s = rigs[0]->State();
        mmio_accesses += static_cast<uint32_t>(rigs[0]->Host().MmioLog().size());
        if (s.cpsr.bits.mode == cerfengine::ArmMode::kUndefined) {
            ++und_exits;
        } else if (s.cpsr.bits.mode == cerfengine::ArmMode::kAbort) {
            ++abort_exits;
        } else if (s.gprs[15] != (program.size() - 1u) * 4u) {
            ++pc_exits;
        }
    }
    std::printf("%s: %u programs agree in both verify modes (mmio %u, alignment faults %u, "
                "translation faults %u, undefined %u, abort %u, other exits %u), digest %016llX\n",
                config.name, programs, mmio_accesses, rigs[0]->System().AlignmentFaults(),
                rigs[0]->System().WalkFaults(), und_exits, abort_exits, pc_exits, digest.Value());
    RigPrintCompileStats("  cerfir verified  ", rigs[0]->Host());
    RigPrintCompileStats("  cerfir unverified", rigs[1]->Host());
}

}

int main() {
    cerfengine::ArmFeatures v4{};

    cerfengine::ArmFeatures v5{};
    v5.load_to_pc_interworking = true;
    v5.load_store_double       = true;

    cerfengine::ArmFeatures v7{};
    v7.arch_v6                   = true;
    v7.arch_v7                   = true;
    v7.thumb                     = true;
    v7.movw_movt                 = true;
    v7.load_store_double         = true;
    v7.load_to_pc_interworking   = true;
    v7.base_restored_abort_model = true;
    v7.ldrex_strex               = true;
    v7.ldrex_strex_v6k           = true;

    RunConfig(Config{"v4 legacy", v4, 0u}, 0x4C445231u, 6000u);
    RunConfig(Config{"v5 legacy interworking", v5, 0u}, 0x4C445232u, 6000u);
    RunConfig(Config{"v7 unaligned", v7, 0u}, 0x4C445233u, 6000u);
    RunConfig(Config{"v4 alignment faults", v4, kAlignBit}, 0x4C445234u, 6000u);
    RunConfig(Config{"v7 alignment faults", v7, kAlignBit}, 0x4C445235u, 6000u);
    RunConfig(Config{"v4 translation faults", v4, kMmuBit}, 0x4C445236u, 6000u);
    RunConfig(Config{"v7 translation faults", v7, kMmuBit}, 0x4C445237u, 6000u);
    RunConfig(Config{"v4 translation and alignment faults", v4, kMmuBit | kAlignBit}, 0x4C445238u,
              6000u);
    std::printf("arm_ir_memory_diff_test passed\n");
    return 0;
}
