#include "arm_ir_rig.h"

namespace {

uint32_t RandomDataProcessing(std::mt19937& rng) {
    const uint32_t cond   = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t opcode = RigPick(rng, 16u);
    const bool     test   = opcode >= 8u && opcode <= 11u;
    const bool     move   = opcode == 13u || opcode == 15u;
    const uint32_t s      = test ? 1u : RigPick(rng, 2u);
    const uint32_t rd     = test ? 0u : RigPick(rng, 15u);
    uint32_t       rn     = move ? 0u : RigPick(rng, 16u);
    const uint32_t head   = (cond << 28) | (opcode << 21) | (s << 20) | (rd << 12);
    switch (RigPick(rng, 3u)) {
    case 0u: {
        const uint32_t rotate = RigPick(rng, 16u);
        const uint32_t imm8   = RigPick(rng, 256u);
        return head | (1u << 25) | (rn << 16) | (rotate << 8) | imm8;
    }
    case 1u: {
        const uint32_t imm5 = RigPick(rng, 32u);
        const uint32_t type = RigPick(rng, 4u);
        const uint32_t rm   = RigPick(rng, 16u);
        return head | (rn << 16) | (imm5 << 7) | (type << 5) | rm;
    }
    default: {
        if (rn == 15u) rn = RigPick(rng, 15u);
        const uint32_t rs   = RigPick(rng, 15u);
        const uint32_t type = RigPick(rng, 4u);
        const uint32_t rm   = RigPick(rng, 15u);
        return head | (rn << 16) | (rs << 8) | (type << 5) | (1u << 4) | rm;
    }
    }
}

uint32_t RandomRegister(std::mt19937& rng) {
    return RigPick(rng, 16u) == 0u ? 15u : RigPick(rng, 15u);
}

uint32_t RandomMultiply(std::mt19937& rng) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t row  = RigPick(rng, 8u);
    const uint32_t s    = RigPick(rng, 2u);
    const uint32_t rd   = RandomRegister(rng);
    uint32_t       ra   = RandomRegister(rng);
    const uint32_t rm   = RandomRegister(rng);
    const uint32_t rn   = RandomRegister(rng);
    if (row == 0u && RigPick(rng, 8u) != 0u) ra = 0u;
    return (cond << 28) | (row << 21) | (s << 20) | (rd << 16) | (ra << 12) | (rm << 8) | 0x90u |
           rn;
}

uint32_t RandomPcWrite(std::mt19937& rng) {
    static constexpr uint32_t kOpcodes[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 12u, 13u, 14u, 15u};
    const uint32_t cond   = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t opcode = kOpcodes[RigPick(rng, sizeof(kOpcodes) / sizeof(kOpcodes[0]))];
    const bool     move   = opcode == 13u || opcode == 15u;
    const uint32_t rn     = move ? 0u : RigPick(rng, 16u);
    const uint32_t head   = (cond << 28) | (opcode << 21) | (RigPick(rng, 2u) << 20) | (rn << 16) |
                          (15u << 12);
    if (RigPick(rng, 2u) == 0u) {
        const uint32_t rotate = RigPick(rng, 16u);
        const uint32_t imm8   = RigPick(rng, 256u);
        return head | (1u << 25) | (rotate << 8) | imm8;
    }
    const uint32_t imm5 = RigPick(rng, 32u);
    const uint32_t type = RigPick(rng, 4u);
    const uint32_t rm   = RigPick(rng, 16u);
    return head | (imm5 << 7) | (type << 5) | rm;
}

uint32_t RandomExit(std::mt19937& rng) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    switch (RigPick(rng, 6u)) {
    case 0u:
        return (cond << 28) | 0x012FFF10u | RigPick(rng, 16u);
    case 1u:
        return (cond << 28) | 0x012FFF30u | RigPick(rng, 16u);
    case 2u: {
        const uint32_t h      = RigPick(rng, 2u);
        const uint32_t offset = RigPick(rng, 0x1000000u);
        return 0xFA000000u | (h << 24) | offset;
    }
    case 3u:
        return (cond << 28) | 0x0F000000u | RigPick(rng, 0x1000000u);
    default:
        return RandomPcWrite(rng);
    }
}

uint32_t RandomFieldMask(std::mt19937& rng) {
    static constexpr uint32_t kMasks[] = {8u, 1u, 9u, 8u, 1u};
    return RigPick(rng, 2u) == 0u ? RigPick(rng, 16u) : kMasks[RigPick(rng, 5u)];
}

uint32_t RandomPsrImmediate(std::mt19937& rng) {
    static constexpr uint32_t kModes[] = {0x10u, 0x11u, 0x12u, 0x13u, 0x17u, 0x1Bu, 0x1Fu};
    switch (RigPick(rng, 3u)) {
    case 0u: {
        const uint32_t flags = RigPick(rng, 4u);
        return (flags << 6) | kModes[RigPick(rng, 7u)];
    }
    case 1u:  return (2u << 8) | RigPick(rng, 16u);
    default:  return RigPick(rng, 0x1000u);
    }
}

uint32_t RandomStatusRegister(std::mt19937& rng, bool v7) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t r    = RigPick(rng, 4u) == 0u ? 1u : 0u;
    switch (RigPick(rng, 3u)) {
    case 0u:
        return (cond << 28) | 0x010F0000u | (r << 22) | (RandomRegister(rng) << 12);
    case 1u: {
        const uint32_t mask = RandomFieldMask(rng);
        const uint32_t rn   = RandomRegister(rng);
        return (cond << 28) | 0x0120F000u | (r << 22) | (mask << 16) | rn;
    }
    default: {
        uint32_t mask = RandomFieldMask(rng);
        if (v7 && r == 0u && mask == 0u) mask = 8u;
        return (cond << 28) | 0x0320F000u | (r << 22) | (mask << 16) | RandomPsrImmediate(rng);
    }
    }
}

uint32_t RandomBitCount(std::mt19937& rng) {
    static constexpr uint32_t kOps[] = {0x016F0F10u, 0x06BF0F30u, 0x06BF0FB0u, 0x06FF0FB0u,
                                        0x06FF0F30u};
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t op   = kOps[RigPick(rng, 5u)];
    const uint32_t rd   = RandomRegister(rng);
    const uint32_t rm   = RandomRegister(rng);
    return (cond << 28) | op | (rd << 12) | rm;
}

uint32_t RandomExtend(std::mt19937& rng) {
    static constexpr uint32_t kOps[] = {0x06AF0070u, 0x06BF0070u, 0x06EF0070u, 0x06FF0070u};
    const uint32_t cond   = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t op     = kOps[RigPick(rng, 4u)];
    const uint32_t rd     = RandomRegister(rng);
    const uint32_t rotate = RigPick(rng, 4u);
    const uint32_t rm     = RandomRegister(rng);
    return (cond << 28) | op | (rd << 12) | (rotate << 10) | rm;
}

uint32_t RandomBitfield(std::mt19937& rng) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t lsb  = RigPick(rng, 4u) == 0u ? 0u : RigPick(rng, 32u);
    const uint32_t high = RigPick(rng, 4u) == 0u ? 31u : RigPick(rng, 32u);
    const uint32_t head = (cond << 28) | (RandomRegister(rng) << 12) | (lsb << 7) | (high << 16);
    if (RigPick(rng, 2u) == 0u) {
        const uint32_t rn = RigPick(rng, 4u) == 0u ? 15u : RandomRegister(rng);
        return head | (0x3Eu << 21) | (1u << 4) | rn;
    }
    const uint32_t op = RigPick(rng, 2u) == 0u ? 0x3Du : 0x3Fu;
    return head | (op << 21) | (5u << 4) | RandomRegister(rng);
}

uint32_t RandomSaturating(std::mt19937& rng) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t op   = RigPick(rng, 4u);
    const uint32_t rn   = RandomRegister(rng);
    const uint32_t rd   = RandomRegister(rng);
    const uint32_t rm   = RandomRegister(rng);
    return (cond << 28) | 0x01000050u | (op << 21) | (rn << 16) | (rd << 12) | rm;
}

uint32_t RandomDspMultiply(std::mt19937& rng) {
    const uint32_t cond = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    uint32_t       ra   = RandomRegister(rng);
    const uint32_t rd   = RandomRegister(rng);
    const uint32_t rm   = RandomRegister(rng);
    const uint32_t rn   = RandomRegister(rng);
    const uint32_t regs = (rd << 16) | (rm << 8) | rn;
    switch (RigPick(rng, 3u)) {
    case 0u: {
        const uint32_t row = RigPick(rng, 4u);
        const uint32_t n   = RigPick(rng, 2u);
        if ((row == 3u || (row == 1u && n == 1u)) && RigPick(rng, 8u) != 0u) ra = 0u;
        return (cond << 28) | 0x01000080u | (row << 21) | regs | (ra << 12) |
               (RigPick(rng, 2u) << 6) | (n << 5);
    }
    case 1u: {
        if (RigPick(rng, 4u) == 0u) ra = 15u;
        const uint32_t op = RigPick(rng, 2u);
        const uint32_t s  = RigPick(rng, 2u);
        const uint32_t m  = RigPick(rng, 2u);
        return (cond << 28) | 0x07000010u | (op << 22) | regs | (ra << 12) | (s << 6) | (m << 5);
    }
    default: {
        const uint32_t sub = RigPick(rng, 2u);
        if (sub == 0u && RigPick(rng, 4u) == 0u) ra = 15u;
        return (cond << 28) | 0x07500010u | regs | (ra << 12) | (sub * 0xC0u) |
               (RigPick(rng, 2u) << 5);
    }
    }
}

uint32_t RandomSignedSaturate(std::mt19937& rng) {
    const uint32_t cond    = RigPick(rng, 4u) == 0u ? RigPick(rng, 15u) : 14u;
    const uint32_t sat_imm = RigPick(rng, 4u) == 0u ? 31u : RigPick(rng, 32u);
    const uint32_t imm5    = RigPick(rng, 2u) == 0u ? 0u : RigPick(rng, 32u);
    const uint32_t rd      = RandomRegister(rng);
    const uint32_t sh      = RigPick(rng, 2u);
    const uint32_t rn      = RandomRegister(rng);
    return (cond << 28) | 0x06A00010u | (sat_imm << 16) | (rd << 12) | (imm5 << 7) | (sh << 6) |
           rn;
}

uint32_t RandomSpsr(std::mt19937& rng) {
    static constexpr uint32_t kModes[] = {0x10u, 0x1Fu, 0x13u, 0x12u};
    const uint32_t nzcv = RigPick(rng, 16u);
    const uint32_t af   = RigPick(rng, 4u);
    const uint32_t t    = RigPick(rng, 2u);
    return (nzcv << 28) | (af << 6) | (t << 5) | kModes[RigPick(rng, 4u)];
}

uint32_t RandomValue(std::mt19937& rng) {
    static constexpr uint32_t kSpecial[] = {0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u,
                                            0xFFFFFFFFu, 0x0000001Fu, 0x00000020u, 0x00000021u,
                                            0x000000FFu, 0x00000100u, 0x80000001u, 0xFFFFFF20u,
                                            0x80008000u, 0x7FFF7FFFu};
    switch (RigPick(rng, 3u)) {
    case 0u:  return kSpecial[RigPick(rng, sizeof(kSpecial) / sizeof(kSpecial[0]))];
    case 1u:  return RigPick(rng, 70u);
    default:  return static_cast<uint32_t>(rng());
    }
}

void TestDataProcessing(const char* name, const cerfengine::ArmFeatures& features, uint32_t seed) {
    ArmIrRig ir(true, features);
    ArmIrRig ir_unverified(false, features);
    std::mt19937 rng(seed);
    constexpr uint32_t kPrograms = 20000u;
    uint32_t           pc_exits   = 0;
    uint32_t           und_exits  = 0;
    uint32_t           mode_exits = 0;
    uint32_t           halts      = 0;
    for (uint32_t n = 0; n < kPrograms; ++n) {
        std::vector<uint32_t> program;
        const uint32_t        length = 1u + RigPick(rng, 6u);
        for (uint32_t i = 0; i < length; ++i) {
            const uint32_t pick = RigPick(rng, 9u);
            if (pick < 2u) {
                program.push_back(RandomMultiply(rng));
            } else if (pick == 8u) {
                program.push_back(RigPick(rng, 4u) == 0u ? RandomSignedSaturate(rng)
                                                         : RandomDspMultiply(rng));
            } else if (pick == 2u) {
                program.push_back(RandomStatusRegister(rng, features.arch_v7));
            } else if (pick == 3u) {
                const uint32_t misc = RigPick(rng, 4u);
                program.push_back(misc == 0u   ? RandomBitCount(rng)
                                  : misc == 1u ? RandomExtend(rng)
                                  : misc == 2u ? RandomBitfield(rng)
                                               : RandomSaturating(rng));
            } else {
                program.push_back(RandomDataProcessing(rng));
            }
        }
        if (RigPick(rng, 4u) == 0u) program.push_back(RandomExit(rng));
        program.push_back(kRigBranchSelf);

        static constexpr uint32_t kStartModes[] = {0u, 0x1D0u, 0x1DFu, 0x1D2u, 0x113u};
        const uint32_t mode_cpsr = kStartModes[RigPick(rng, 5u)];
        const bool     has_spsr  = mode_cpsr == 0u || mode_cpsr == 0x1D2u || mode_cpsr == 0x113u;
        const uint32_t spsr      = has_spsr ? RandomSpsr(rng) : 0u;
        const uint32_t sctlr     = features.arch_v7 && RigPick(rng, 4u) == 0u ? (1u << 27) : 0u;

        cerfengine::ArmCpuState initial{};
        for (uint32_t i = 0; i < 15u; ++i) initial.gprs[i] = RandomValue(rng);
        initial.gprs[15] = kRigRamBase;
        initial.nf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.zf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.cf       = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.vf       = static_cast<uint8_t>(RigPick(rng, 2u));

        const auto ir_status   = ir.Run(program, initial, sctlr, mode_cpsr, spsr);
        const auto fast_status = ir_unverified.Run(program, initial, sctlr, mode_cpsr, spsr);
        const bool halted      = ir_status == cerfengine::GuestEngine::RunStatus::kHalted;
        if (!RigSameRun(ir, ir_status, ir_unverified, fast_status)) {
            std::printf("program %u:", n);
            for (uint32_t word : program) std::printf(" %08X", word);
            std::printf("\nstatus verified=%d unverified=%d\n", static_cast<int>(ir_status),
                        static_cast<int>(fast_status));
            if (halted) std::printf("halt: %s\n", ir.Engine().LastHalt().detail);
            RigDump("initial   ", initial);
            RigDump("verified  ", ir.State());
            RigDump("unverified", ir_unverified.State());
            TestFail("CerfIR differs between verify modes");
        }
        if (halted) {
            ++halts;
            ir.Restart();
            ir_unverified.Restart();
            continue;
        }
        const cerfengine::ArmCpuState& s = ir.State();
        if (s.cpsr.bits.mode == cerfengine::ArmMode::kUndefined) {
            ++und_exits;
        } else if (mode_cpsr != 0u && s.cpsr.bits.mode != (mode_cpsr & 0x1Fu)) {
            ++mode_exits;
        } else if (s.gprs[15] != (program.size() - 1u) * 4u) {
            ++pc_exits;
        }
    }
    std::printf("data processing %s: %u programs agree in both verify modes (pc writes %u, "
                "undefined %u, mode changes %u, halts %u)\n",
                name, kPrograms, pc_exits, und_exits, mode_exits, halts);
    RigPrintCompileStats("cerfir verified  ", ir.Host());
    RigPrintCompileStats("cerfir unverified", ir_unverified.Host());
}

}

int main() {
    TestDataProcessing("v4", cerfengine::ArmFeatures{}, 0x43455246u);
    cerfengine::ArmFeatures v5{};
    v5.thumb                        = true;
    v5.dsp                          = true;
    v5.clz                          = true;
    v5.load_to_pc_interworking      = true;
    v5.blx_reg                      = true;
    v5.armv5_unconditional_space    = true;
    v5.accesses_spsr_in_system_mode = true;
    TestDataProcessing("v5 dsp", v5, 0x43455248u);
    cerfengine::ArmFeatures v7{};
    v7.arch_v6                      = true;
    v7.arch_v7                      = true;
    v7.thumb                        = true;
    v7.load_to_pc_interworking      = true;
    v7.data_proc_to_pc_interworking = true;
    v7.blx_reg                      = true;
    v7.armv5_unconditional_space    = true;
    v7.mls                          = true;
    v7.clz                          = true;
    v7.rev                          = true;
    v7.bit_field                    = true;
    v7.extend_rotate                = true;
    v7.dsp                          = true;
    TestDataProcessing("v7", v7, 0x43455247u);
    std::printf("arm_ir_diff_test passed\n");
    return 0;
}
