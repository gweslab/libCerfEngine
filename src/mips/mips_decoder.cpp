#include "mips_decoder.h"

#include <cerfengine/mips/mips_cpu_state.h>

namespace cerfengine {

namespace {

using Kind = MipsInsnKind;

Kind Cop0Kind(const MipsDecodedInsn* d) {
    if (d->rs == MipsCop0Rs::kMTC0)  return Kind::kMtc0;
    if (d->rs == MipsCop0Rs::kMFC0)  return Kind::kMfc0;
    if (d->rs == MipsCop0Rs::kDMTC0) return Kind::kDmtc0;
    if (d->rs == MipsCop0Rs::kDMFC0) return Kind::kDmfc0;
    if (d->rs >= MipsCop0Rs::kCO) {
        if (d->funct == MipsCop0Funct::kTLBR)  return Kind::kTlbr;
        if (d->funct == MipsCop0Funct::kTLBWI) return Kind::kTlbwi;
        if (d->funct == MipsCop0Funct::kTLBWR) return Kind::kTlbwr;
        if (d->funct == MipsCop0Funct::kTLBP)  return Kind::kTlbp;
        if (d->funct == MipsCop0Funct::kRFE)   return Kind::kRfe;
        if (d->funct == MipsCop0Funct::kERET)  return Kind::kEret;
        /* STANDBY (0x21) / SUSPEND (0x22): halt the CPU core until any
           interrupt (VR4102 UM ch.27 p643/p646). */
        if (d->funct == MipsCop0Funct::kSTANDBY ||
            d->funct == MipsCop0Funct::kSUSPEND) return Kind::kWait;
        /* HIBERNATE freezes the pipeline until a Cold Reset (UM ch.27 p587). */
        if (d->funct == MipsCop0Funct::kHIBERNATE) return Kind::kHibernate;
    }
    return Kind::kUndefined;
}

}

void MipsDecoder::Configure(bool has_fpu, bool has_llsc, bool has_mips4,
                            bool has_vr41xx_power_modes, bool has_64bit, bool has_eret,
                            bool has_rfe, bool has_mips16, bool has_tx39_multiply_add,
                            bool has_cop1_cop2_unusable) {
    has_cop1_cop2_unusable_ = has_cop1_cop2_unusable;
    has_fpu_                = has_fpu;
    has_llsc_               = has_llsc;
    has_mips4_              = has_mips4;
    has_vr41xx_power_modes_ = has_vr41xx_power_modes;
    has_64bit_              = has_64bit;
    has_eret_               = has_eret;
    has_rfe_                = has_rfe;
    has_mips16_             = has_mips16;
    has_tx39_multiply_add_  = has_tx39_multiply_add;
    for (uint32_t i = 0; i < 64u; ++i) {
        primary_[i] = ClassifyPrimary(i);
        special_[i] = ClassifySpecial(i);
    }
    for (uint32_t i = 0; i < 32u; ++i) regimm_[i] = ClassifyRegimm(i);
    primary_[MipsOp::kCOP0]     = Entry{MipsInsnKind::kUndefined, kRare};
    primary_[MipsOp::kSPECIAL2] = Entry{MipsInsnKind::kUndefined, kRare};
}

MipsDecoder::Entry MipsDecoder::ClassifyPrimary(uint32_t op) const {
    const auto entry = [](Kind kind, bool recognized, uint8_t flags = 0u) {
        return Entry{recognized ? kind : Kind::kUndefined,
                     static_cast<uint8_t>(flags | (recognized ? kRecognized : 0u))};
    };
    /* VR4121 UM Figure 28-1 (p.770-771) and VR4102 UM Figure 27-1 (p.674): COP1, COP2,
       LWC1/2, LDC1/2, SWC1/2 and SDC1/2 are marked pi, "invalid and cause coprocessor
       unusable exception". */
    if (has_cop1_cop2_unusable_) {
        switch (op) {
        case MipsOp::kCOP1: case MipsOp::kCOP2: case MipsOp::kLWC1: case MipsOp::kLWC2:
        case MipsOp::kLDC1: case MipsOp::kLDC2: case MipsOp::kSWC1: case MipsOp::kSWC2:
        case MipsOp::kSDC1: case MipsOp::kSDC2:
            return entry(Kind::kCopUnusable, true);
        default:
            break;
        }
    }
    switch (op) {
    case MipsOp::kBEQ:   return entry(Kind::kBeq, true, kBranch);
    case MipsOp::kBNE:   return entry(Kind::kBne, true, kBranch);
    case MipsOp::kBLEZ:  return entry(Kind::kBlez, true, kBranch);
    case MipsOp::kBGTZ:  return entry(Kind::kBgtz, true, kBranch);
    case MipsOp::kJ:     return entry(Kind::kJ, true, kBranch);
    case MipsOp::kJAL:   return entry(Kind::kJal, true, kBranch);
    case MipsOp::kJALX:  return entry(Kind::kJalx, has_mips16_, kBranch);
    case MipsOp::kBEQL:  return entry(Kind::kBeql, true, kBranch | kLikely);
    case MipsOp::kBNEL:  return entry(Kind::kBnel, true, kBranch | kLikely);
    case MipsOp::kBLEZL: return entry(Kind::kBlezl, true, kBranch | kLikely);
    case MipsOp::kBGTZL: return entry(Kind::kBgtzl, true, kBranch | kLikely);
    case MipsOp::kCOP1:  return entry(Kind::kUndefined, has_fpu_);
    case MipsOp::kLL:
    case MipsOp::kSC:    return entry(Kind::kUndefined, has_llsc_);
    case MipsOp::kADDI:  return entry(Kind::kAddi, true);
    case MipsOp::kADDIU: return entry(Kind::kAddiu, true);
    case MipsOp::kSLTI:  return entry(Kind::kSlti, true);
    case MipsOp::kSLTIU: return entry(Kind::kSltiu, true);
    case MipsOp::kANDI:  return entry(Kind::kAndi, true);
    case MipsOp::kORI:   return entry(Kind::kOri, true);
    case MipsOp::kXORI:  return entry(Kind::kXori, true);
    case MipsOp::kLUI:   return entry(Kind::kLui, true);
    case MipsOp::kLB:    return entry(Kind::kLb, true);
    case MipsOp::kLH:    return entry(Kind::kLh, true);
    case MipsOp::kLWL:   return entry(Kind::kLwl, true);
    case MipsOp::kLW:    return entry(Kind::kLw, true);
    case MipsOp::kLBU:   return entry(Kind::kLbu, true);
    case MipsOp::kLHU:   return entry(Kind::kLhu, true);
    case MipsOp::kLWR:   return entry(Kind::kLwr, true);
    case MipsOp::kSB:    return entry(Kind::kSb, true);
    case MipsOp::kSH:    return entry(Kind::kSh, true);
    case MipsOp::kSWL:   return entry(Kind::kSwl, true);
    case MipsOp::kSW:    return entry(Kind::kSw, true);
    case MipsOp::kSWR:   return entry(Kind::kSwr, true);
    /* QEMU MIPS translate.c OPC_PREF and OPC_CACHE: "Treat as NOP."; helper_cache
       (special_helper.c): Index/Hit Invalidate "no-op". */
    case MipsOp::kCACHE: return entry(Kind::kCache, true);
    case MipsOp::kPREF:  return entry(Kind::kNop, has_mips4_);
    case MipsOp::kDADDI:  return entry(Kind::kDaddi, has_64bit_);
    case MipsOp::kDADDIU: return entry(Kind::kDaddiu, has_64bit_);
    case MipsOp::kLWU:    return entry(Kind::kLwu, has_64bit_);
    case MipsOp::kSDL:    return entry(Kind::kSdl, has_64bit_);
    case MipsOp::kSDR:    return entry(Kind::kSdr, has_64bit_);
    case MipsOp::kLDL:    return entry(Kind::kLdl, has_64bit_);
    case MipsOp::kLDR:    return entry(Kind::kLdr, has_64bit_);
    case MipsOp::kLD:     return entry(Kind::kLd, has_64bit_);
    case MipsOp::kSD:     return entry(Kind::kSd, has_64bit_);
    default:              return Entry{};
    }
}

MipsDecoder::Entry MipsDecoder::ClassifySpecial(uint32_t funct) const {
    const auto entry = [](Kind kind, bool recognized, uint8_t flags = 0u) {
        return Entry{recognized ? kind : Kind::kUndefined,
                     static_cast<uint8_t>(flags | (recognized ? kRecognized : 0u))};
    };
    switch (funct) {
    case MipsSpecial::kSLL:     return entry(Kind::kSll, true);
    case MipsSpecial::kSRL:     return entry(Kind::kSrl, true);
    case MipsSpecial::kSRA:     return entry(Kind::kSra, true);
    case MipsSpecial::kSLLV:    return entry(Kind::kSllv, true);
    case MipsSpecial::kSRLV:    return entry(Kind::kSrlv, true);
    case MipsSpecial::kSRAV:    return entry(Kind::kSrav, true);
    case MipsSpecial::kJR:      return entry(Kind::kJr, true, kBranch);
    case MipsSpecial::kJALR:    return entry(Kind::kJalr, true, kBranch);
    case MipsSpecial::kSYSCALL: return entry(Kind::kSyscall, true);
    case MipsSpecial::kBREAK:   return entry(Kind::kBreak, true);
    case MipsSpecial::kSYNC:    return entry(Kind::kNop, true);
    case MipsSpecial::kMFHI:    return entry(Kind::kMfhi, true);
    case MipsSpecial::kMTHI:    return entry(Kind::kMthi, true);
    case MipsSpecial::kMFLO:    return entry(Kind::kMflo, true);
    case MipsSpecial::kMTLO:    return entry(Kind::kMtlo, true);
    case MipsSpecial::kMULT:    return entry(Kind::kMult, true);
    case MipsSpecial::kMULTU:   return entry(Kind::kMultu, true);
    case MipsSpecial::kDIV:     return entry(Kind::kDiv, true);
    case MipsSpecial::kDIVU:    return entry(Kind::kDivu, true);
    case MipsSpecial::kADD:     return entry(Kind::kAdd, true);
    case MipsSpecial::kADDU:    return entry(Kind::kAddu, true);
    case MipsSpecial::kSUB:     return entry(Kind::kSub, true);
    case MipsSpecial::kSUBU:    return entry(Kind::kSubu, true);
    case MipsSpecial::kAND:     return entry(Kind::kAnd, true);
    case MipsSpecial::kOR:      return entry(Kind::kOr, true);
    case MipsSpecial::kXOR:     return entry(Kind::kXor, true);
    case MipsSpecial::kNOR:     return entry(Kind::kNor, true);
    case MipsSpecial::kSLT:     return entry(Kind::kSlt, true);
    case MipsSpecial::kSLTU:    return entry(Kind::kSltu, true);
    case MipsSpecial::kDADD:    return entry(Kind::kDadd, has_64bit_);
    case MipsSpecial::kDADDU:   return entry(Kind::kDaddu, has_64bit_);
    case MipsSpecial::kDSUB:    return entry(Kind::kDsub, has_64bit_);
    case MipsSpecial::kDSUBU:   return entry(Kind::kDsubu, has_64bit_);
    case MipsSpecial::kDMULT:   return entry(Kind::kDmult, has_64bit_);
    case MipsSpecial::kDMULTU:  return entry(Kind::kDmultu, has_64bit_);
    case MipsSpecial::kDDIV:    return entry(Kind::kDdiv, has_64bit_);
    case MipsSpecial::kDDIVU:   return entry(Kind::kDdivu, has_64bit_);
    case MipsSpecial::kDSLLV:   return entry(Kind::kDsllv, has_64bit_);
    case MipsSpecial::kDSRLV:   return entry(Kind::kDsrlv, has_64bit_);
    case MipsSpecial::kDSRAV:   return entry(Kind::kDsrav, has_64bit_);
    case MipsSpecial::kDSLL:    return entry(Kind::kDsll, has_64bit_);
    case MipsSpecial::kDSRL:    return entry(Kind::kDsrl, has_64bit_);
    case MipsSpecial::kDSRA:    return entry(Kind::kDsra, has_64bit_);
    case MipsSpecial::kDSLL32:  return entry(Kind::kDsll32, has_64bit_);
    case MipsSpecial::kDSRL32:  return entry(Kind::kDsrl32, has_64bit_);
    case MipsSpecial::kDSRA32:  return entry(Kind::kDsra32, has_64bit_);
    case MipsSpecial::kMOVZ:    return entry(Kind::kMovz, has_mips4_);
    case MipsSpecial::kMOVN:    return entry(Kind::kMovn, has_mips4_);
    default:                    return Entry{};
    }
}

MipsDecoder::Entry MipsDecoder::ClassifyRegimm(uint32_t rt) const {
    switch (rt) {
    case MipsRegimm::kBLTZ:   return Entry{Kind::kBltz, static_cast<uint8_t>(kBranch | kRecognized)};
    case MipsRegimm::kBGEZ:   return Entry{Kind::kBgez, static_cast<uint8_t>(kBranch | kRecognized)};
    case MipsRegimm::kBLTZAL: return Entry{Kind::kBltzal, static_cast<uint8_t>(kBranch | kRecognized)};
    case MipsRegimm::kBGEZAL: return Entry{Kind::kBgezal, static_cast<uint8_t>(kBranch | kRecognized)};
    case MipsRegimm::kBLTZL:
        return Entry{Kind::kBltzl, static_cast<uint8_t>(kBranch | kLikely | kRecognized)};
    case MipsRegimm::kBGEZL:
        return Entry{Kind::kBgezl, static_cast<uint8_t>(kBranch | kLikely | kRecognized)};
    default:
        return Entry{};
    }
}

bool MipsDecoder::DecodeRare(MipsDecodedInsn* d) const {
    d->is_branch = 0;
    d->is_likely = 0;
    bool recognized = false;
    if (d->op == MipsOp::kSPECIAL2) {
        /* MADD / MADDU: rs, rt, rd, bits 10..6 zero, funct 000000 / 000001 (TMPR39xx-um
           p.155-156). */
        recognized = has_tx39_multiply_add_ && d->sa == 0 &&
                     (d->funct == MipsSpecial2::kMADD || d->funct == MipsSpecial2::kMADDU);
        d->kind = !recognized                         ? Kind::kUndefined
                  : d->funct == MipsSpecial2::kMADD   ? Kind::kMadd
                                                      : Kind::kMaddu;
        return recognized;
    }
    if (d->rs >= MipsCop0Rs::kCO &&
        ((d->funct == MipsCop0Funct::kERET && has_eret_) ||
         ((d->funct == MipsCop0Funct::kHIBERNATE || d->funct == MipsCop0Funct::kSTANDBY ||
           d->funct == MipsCop0Funct::kSUSPEND) &&
          has_vr41xx_power_modes_) ||
         (d->funct == MipsCop0Funct::kRFE && has_rfe_))) {
        d->ends_block = 1;
    }
    if ((d->rs == MipsCop0Rs::kMTC0 || (d->rs == MipsCop0Rs::kDMTC0 && has_64bit_)) &&
        (d->rd == MipsCp0::kStatus || d->rd == MipsCp0::kCause)) {
        d->ends_block = 1;
    }
    if (d->rs == MipsCop0Rs::kMFC0 || d->rs == MipsCop0Rs::kMTC0) {
        recognized = true;
    } else if (d->rs == MipsCop0Rs::kDMFC0 || d->rs == MipsCop0Rs::kDMTC0) {
        recognized = has_64bit_;
    } else if (d->rs >= MipsCop0Rs::kCO) {
        switch (d->funct) {
        case MipsCop0Funct::kTLBR:  case MipsCop0Funct::kTLBWI:
        case MipsCop0Funct::kTLBWR: case MipsCop0Funct::kTLBP:
        case MipsCop0Funct::kDERET:
            recognized = true;
            break;
        case MipsCop0Funct::kRFE:
            recognized = has_rfe_;
            break;
        case MipsCop0Funct::kERET:  case MipsCop0Funct::kWAIT:
            recognized = has_eret_;
            break;
        case MipsCop0Funct::kSTANDBY: case MipsCop0Funct::kSUSPEND:
        case MipsCop0Funct::kHIBERNATE:
            recognized = has_vr41xx_power_modes_;
            break;
        default:
            break;
        }
    }
    d->kind = recognized ? Cop0Kind(d) : Kind::kUndefined;
    return recognized;
}

}
