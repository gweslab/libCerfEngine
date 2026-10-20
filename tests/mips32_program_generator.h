#pragma once

#include "mips_ir_rig.h"

#include "mips/mips_opcode.h"

constexpr uint32_t kMipsGenTargetReg = 26u;
constexpr uint32_t kMipsGenMmioReg   = 27u;
constexpr uint32_t kMipsGenDataReg   = 28u;

inline uint32_t MipsGenR(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa, uint32_t funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct;
}

inline uint32_t MipsGenI(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFFu);
}

inline uint32_t MipsGenJ(uint32_t op, uint32_t target) {
    return (op << 26) | ((target >> 2) & 0x03FFFFFFu);
}

class Mips32ProgramGenerator {
public:
    Mips32ProgramGenerator(std::mt19937& rng, const cerfengine::MipsFeatures& f)
        : rng_(rng),
          wide_(f.isa_level != cerfengine::MipsIsaLevel::kMips1),
          movz_(f.isa_level == cerfengine::MipsIsaLevel::kMips4),
          madd_(f.tx39_multiply_add),
          power_(f.vr41xx_power_modes),
          mips16_(f.mips16),
          llsc_fpu_(f.llsc || f.fpu) {}

    std::vector<uint32_t> Program(uint32_t start_va, uint32_t length, bool first_simple) {
        words_.clear();
        start_va_ = start_va;
        length_   = length;
        if (first_simple) words_.push_back(Simple());
        while (words_.size() + 2u < length_) {
            const uint32_t pick = Pick(100u);
            if (pick < 14u) {
                Branch();
            } else if (pick < 17u && words_.size() + 3u < length_) {
                RegisterJump();
            } else if (pick < 18u && mips16_) {
                words_.push_back(MipsGenJ(cerfengine::MipsOp::kJALX, kMipsRigEnd16Va));
                words_.push_back(Simple());
            } else {
                words_.push_back(Simple());
            }
        }
        while (words_.size() < length_) words_.push_back(Simple());
        words_.push_back(MipsGenJ(cerfengine::MipsOp::kJ, kMipsRigEndVa));
        words_.push_back(0u);
        return words_;
    }

    uint32_t ForwardTarget(uint32_t from) {
        return start_va_ + 4u * (from + Pick(length_ + 1u - from));
    }

private:
    uint32_t Pick(uint32_t n) { return MipsRigPick(rng_, n); }

    uint32_t Dest() {
        static constexpr uint32_t kPool[] = {1u,  2u,  3u,  4u,  5u,  6u,  7u,  8u,  9u,  10u, 11u,
                                             12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u,
                                             23u, 24u, 25u, 29u, 30u, 31u};
        return Pick(16u) == 0u ? 0u : kPool[Pick(sizeof(kPool) / sizeof(kPool[0]))];
    }
    uint32_t Src() { return Pick(32u); }

    uint32_t Alu() {
        namespace S = cerfengine::MipsSpecial;
        static constexpr uint32_t kNarrow[] = {S::kADDU, S::kSUBU, S::kAND, S::kOR,
                                               S::kXOR,  S::kNOR,  S::kSLT, S::kSLTU};
        static constexpr uint32_t kWide[]   = {S::kDADDU, S::kDSUBU};
        const uint32_t funct = wide_ && Pick(4u) == 0u ? kWide[Pick(2u)] : kNarrow[Pick(8u)];
        return MipsGenR(Src(), Src(), Dest(), 0u, funct);
    }

    uint32_t Shift() {
        namespace S = cerfengine::MipsSpecial;
        static constexpr uint32_t kImm[]  = {S::kSLL, S::kSRL, S::kSRA};
        static constexpr uint32_t kVar[]  = {S::kSLLV, S::kSRLV, S::kSRAV};
        static constexpr uint32_t kWImm[] = {S::kDSLL,   S::kDSRL,   S::kDSRA,
                                             S::kDSLL32, S::kDSRL32, S::kDSRA32};
        static constexpr uint32_t kWVar[] = {S::kDSLLV, S::kDSRLV, S::kDSRAV};
        const uint32_t rs_field = Pick(64u) == 0u ? 1u : 0u;
        switch (Pick(wide_ ? 4u : 2u)) {
        case 0u: return MipsGenR(rs_field, Src(), Dest(), Pick(32u), kImm[Pick(3u)]);
        case 1u: return MipsGenR(Src(), Src(), Dest(), 0u, kVar[Pick(3u)]);
        case 2u: return MipsGenR(rs_field, Src(), Dest(), Pick(32u), kWImm[Pick(6u)]);
        default: return MipsGenR(Src(), Src(), Dest(), 0u, kWVar[Pick(3u)]);
        }
    }

    uint32_t Immediate() {
        namespace O = cerfengine::MipsOp;
        static constexpr uint32_t kOps[] = {O::kADDIU, O::kSLTI, O::kSLTIU, O::kANDI,
                                            O::kORI,   O::kXORI, O::kLUI};
        const uint32_t op  = wide_ && Pick(6u) == 0u ? O::kDADDIU : kOps[Pick(7u)];
        const uint32_t imm = Pick(4u) == 0u ? Pick(8u) - 4u : static_cast<uint32_t>(rng_());
        return MipsGenI(op, op == O::kLUI ? 0u : Src(), Dest(), imm);
    }

    uint32_t Trapping() {
        namespace O = cerfengine::MipsOp;
        namespace S = cerfengine::MipsSpecial;
        if (Pick(3u) == 0u) {
            const uint32_t op = wide_ && Pick(2u) == 0u ? O::kDADDI : O::kADDI;
            return MipsGenI(op, Src(), Dest(), static_cast<uint32_t>(rng_()));
        }
        static constexpr uint32_t kNarrow[] = {S::kADD, S::kSUB};
        static constexpr uint32_t kWide[]   = {S::kDADD, S::kDSUB};
        const uint32_t funct = wide_ && Pick(2u) == 0u ? kWide[Pick(2u)] : kNarrow[Pick(2u)];
        return MipsGenR(Src(), Src(), Dest(), 0u, funct);
    }

    uint32_t MulDiv() {
        namespace S = cerfengine::MipsSpecial;
        static constexpr uint32_t kNarrow[] = {S::kMULT, S::kMULTU, S::kDIV, S::kDIVU};
        static constexpr uint32_t kWide[]   = {S::kDMULT, S::kDMULTU, S::kDDIV, S::kDDIVU};
        static constexpr uint32_t kMove[]   = {S::kMFHI, S::kMFLO, S::kMTHI, S::kMTLO};
        switch (Pick(madd_ ? 4u : 3u)) {
        case 0u:
            if (wide_ && Pick(3u) == 0u) return MipsGenR(Src(), Src(), 0u, 0u, kWide[Pick(4u)]);
            return MipsGenR(Src(), Src(), 0u, 0u, kNarrow[Pick(4u)]);
        case 1u:
        case 2u: {
            const uint32_t funct = kMove[Pick(4u)];
            const bool     to    = funct == S::kMTHI || funct == S::kMTLO;
            return to ? MipsGenR(Src(), 0u, 0u, 0u, funct) : MipsGenR(0u, 0u, Dest(), 0u, funct);
        }
        default:
            return (cerfengine::MipsOp::kSPECIAL2 << 26) | MipsGenR(Src(), Src(), Dest(), 0u, Pick(2u));
        }
    }

    static bool IsStore(uint32_t op) {
        namespace O = cerfengine::MipsOp;
        return op == O::kSB || op == O::kSH || op == O::kSW || op == O::kSWL || op == O::kSWR ||
               op == O::kSD || op == O::kSDL || op == O::kSDR;
    }

    uint32_t Memory() {
        namespace O = cerfengine::MipsOp;
        static constexpr uint32_t kNarrow[] = {O::kLB,  O::kLBU, O::kLH,  O::kLHU,
                                               O::kLW,  O::kSB,  O::kSH,  O::kSW,
                                               O::kLWL, O::kLWR, O::kSWL, O::kSWR};
        static constexpr uint32_t kWide[]   = {O::kLWU, O::kLD,  O::kSD, O::kLDL,
                                               O::kLDR, O::kSDL, O::kSDR};
        const bool     wide = wide_ && Pick(4u) == 0u;
        const uint32_t op   = wide ? kWide[Pick(7u)] : kNarrow[Pick(12u)];
        const uint32_t rt   = IsStore(op) ? Src() : Dest();
        switch (Pick(24u)) {
        case 0u:
        case 1u:
        case 4u:
        case 5u: {
            static constexpr uint32_t kMmio[] = {O::kLB, O::kLBU, O::kLH, O::kLHU,
                                                 O::kLW, O::kSB,  O::kSH, O::kSW};
            const uint32_t m     = kMmio[Pick(8u)];
            const uint32_t width = m == O::kLW || m == O::kSW                   ? 4u
                                   : m == O::kLB || m == O::kLBU || m == O::kSB ? 1u
                                                                                : 2u;
            return MipsGenI(m, kMipsGenMmioReg, IsStore(m) ? Src() : Dest(), Pick(16u) * width);
        }
        case 2u: return MipsGenI(op, 0u, rt, static_cast<uint32_t>(rng_()));
        case 3u: return MipsGenI(op, Src(), rt, static_cast<uint32_t>(rng_()));
        default: {
            const uint32_t offset = Pick(8u) == 0u ? Pick(0x800u) : Pick(0x200u) & ~3u;
            return MipsGenI(op, kMipsGenDataReg, rt, offset);
        }
        }
    }

    uint32_t Cop0() {
        namespace O  = cerfengine::MipsOp;
        namespace R0 = cerfengine::MipsCop0Rs;
        static constexpr uint32_t kRegs[] = {8u, 9u, 12u, 13u, 14u};
        const uint32_t rd = Pick(16u) == 0u ? Pick(32u) : kRegs[Pick(5u)];
        uint32_t       rs = 0u;
        uint32_t       rt = 0u;
        if (Pick(2u) == 0u) {
            rs = wide_ && Pick(3u) == 0u ? R0::kDMFC0 : R0::kMFC0;
            rt = Dest();
        } else {
            rs = wide_ && Pick(3u) == 0u ? R0::kDMTC0 : R0::kMTC0;
            rt = Src();
        }
        const uint32_t low = Pick(32u) == 0u ? 1u + Pick(7u) : 0u;
        return (O::kCOP0 << 26) | (rs << 21) | (rt << 16) | (rd << 11) | low;
    }

    uint32_t System() {
        namespace O = cerfengine::MipsOp;
        namespace F = cerfengine::MipsCop0Funct;
        static constexpr uint32_t kTlb[]   = {F::kTLBR, F::kTLBWI, F::kTLBWR, F::kTLBP};
        static constexpr uint32_t kPower[] = {F::kSTANDBY, F::kSUSPEND, F::kHIBERNATE};
        const uint32_t co = (O::kCOP0 << 26) | (cerfengine::MipsCop0Rs::kCO << 21);
        switch (Pick(power_ ? 4u : 3u)) {
        case 0u:
        case 1u: return co | kTlb[Pick(4u)];
        case 2u: return co | (wide_ ? F::kERET : F::kRFE);
        default: return co | kPower[Pick(3u)];
        }
    }

    uint32_t BreakOrSyscall() {
        namespace S = cerfengine::MipsSpecial;
        return MipsGenR(0u, 0u, 0u, Pick(1024u) >> 4, Pick(2u) == 0u ? S::kBREAK : S::kSYSCALL);
    }

    uint32_t Unusual() {
        namespace O = cerfengine::MipsOp;
        switch (Pick(llsc_fpu_ ? 3u : 1u)) {
        case 0u:
            return (O::kCOP0 << 26) | (cerfengine::MipsCop0Rs::kCO << 21) |
                   (Pick(2u) == 0u ? cerfengine::MipsCop0Funct::kWAIT
                                   : (wide_ ? cerfengine::MipsCop0Funct::kRFE
                                            : cerfengine::MipsCop0Funct::kERET));
        case 1u: return MipsGenI(Pick(2u) == 0u ? O::kLL : O::kSC, kMipsGenDataReg, Dest(), Pick(0x80u) * 4u);
        default: return (O::kCOP1 << 26) | (static_cast<uint32_t>(rng_()) & 0x03FFFFFFu);
        }
    }

    uint32_t Simple() {
        const uint32_t pick = Pick(100u);
        if (pick < 20u) return Alu();
        if (pick < 31u) return Shift();
        if (pick < 45u) return Immediate();
        if (pick < 50u) return Trapping();
        if (pick < 59u) return MulDiv();
        if (pick < 81u) return Memory();
        if (pick < 86u) return Cop0();
        if (pick < 89u) return System();
        if (pick < 92u && movz_) {
            namespace S = cerfengine::MipsSpecial;
            return MipsGenR(Src(), Src(), Dest(), 0u, Pick(2u) == 0u ? S::kMOVZ : S::kMOVN);
        }
        if (pick == 95u) return BreakOrSyscall();
        if (pick == 96u && Pick(4u) == 0u) return Unusual();
        return 0u;
    }

    void Branch() {
        namespace O = cerfengine::MipsOp;
        namespace G = cerfengine::MipsRegimm;
        const uint32_t at     = static_cast<uint32_t>(words_.size());
        const uint32_t target = ForwardTarget(at + 2u);
        const uint32_t offset = ((target - start_va_) >> 2) - (at + 1u);
        switch (Pick(5u)) {
        case 0u: {
            static constexpr uint32_t kOps[] = {O::kBEQ, O::kBNE, O::kBEQL, O::kBNEL};
            const uint32_t src = Pick(4u) == 0u ? 0u : Src();
            words_.push_back(MipsGenI(kOps[Pick(4u)], Src(), src, offset));
            break;
        }
        case 1u: {
            static constexpr uint32_t kOps[] = {O::kBLEZ, O::kBGTZ, O::kBLEZL, O::kBGTZL};
            words_.push_back(MipsGenI(kOps[Pick(4u)], Src(), 0u, offset));
            break;
        }
        case 2u: {
            static constexpr uint32_t kRt[] = {G::kBLTZ,  G::kBGEZ,   G::kBLTZL,
                                               G::kBGEZL, G::kBLTZAL, G::kBGEZAL};
            words_.push_back(MipsGenI(O::kREGIMM, Src(), kRt[Pick(6u)], offset));
            break;
        }
        default:
            words_.push_back(MipsGenJ(Pick(2u) == 0u ? O::kJ : O::kJAL, target));
            break;
        }
        words_.push_back(Simple());
    }

    void RegisterJump() {
        namespace O = cerfengine::MipsOp;
        namespace S = cerfengine::MipsSpecial;
        const uint32_t at     = static_cast<uint32_t>(words_.size());
        const uint32_t target = ForwardTarget(at + 3u);
        words_.push_back(MipsGenI(O::kADDIU, kMipsGenDataReg, kMipsGenTargetReg, target - kMipsRigDataVa));
        words_.push_back(Pick(2u) == 0u ? MipsGenR(kMipsGenTargetReg, 0u, 0u, 0u, S::kJR)
                                        : MipsGenR(kMipsGenTargetReg, 0u, Dest(), 0u, S::kJALR));
        words_.push_back(Simple());
    }

    std::mt19937&         rng_;
    const bool            wide_;
    const bool            movz_;
    const bool            madd_;
    const bool            power_;
    const bool            mips16_;
    const bool            llsc_fpu_;
    std::vector<uint32_t> words_;
    uint32_t              start_va_ = 0;
    uint32_t              length_   = 0;
};
