#include "mips_place_fn_selector.h"

#include "mips_opcode.h"
#include "mips_place_fns.h"

namespace cerfengine {

CERFENGINE_REGISTER(MipsPlaceFnSelector);

MipsPlaceFn MipsPlaceFnSelector::Select(const MipsDecodedInsn* d) {
    switch (d->op) {
        case MipsOp::kLUI:   return &PlaceMipsLui;
        case MipsOp::kADDIU: return &PlaceMipsAddiu;
        case MipsOp::kADDI:  return &PlaceMipsAddi;
        case MipsOp::kSLTIU: return &PlaceMipsSltiu;
        case MipsOp::kSLTI:  return &PlaceMipsSlti;
        case MipsOp::kDADDIU: return &PlaceMipsDaddiu;
        case MipsOp::kDADDI:  return &PlaceMipsDaddi;
        case MipsOp::kORI:   return &PlaceMipsOri;
        case MipsOp::kXORI:  return &PlaceMipsXori;
        case MipsOp::kANDI:  return &PlaceMipsAndi;
        case MipsOp::kJ:     return &PlaceMipsJ;
        case MipsOp::kJAL:   return &PlaceMipsJal;
        case MipsOp::kJALX:  return &PlaceMipsJalx;
        case MipsOp::kLW:    return &PlaceMipsLw;
        case MipsOp::kLD:    return &PlaceMipsLd;
        case MipsOp::kSW:    return &PlaceMipsSw;
        case MipsOp::kSH:    return &PlaceMipsSh;
        case MipsOp::kSB:    return &PlaceMipsSb;
        case MipsOp::kSD:    return &PlaceMipsSd;
        case MipsOp::kSDR:   return &PlaceMipsSdr;
        case MipsOp::kSDL:   return &PlaceMipsSdl;
        case MipsOp::kSWR:   return &PlaceMipsSwr;
        case MipsOp::kLWL:   return &PlaceMipsLwl;
        case MipsOp::kLWR:   return &PlaceMipsLwr;
        case MipsOp::kLDL:   return &PlaceMipsLdl;
        case MipsOp::kLDR:   return &PlaceMipsLdr;
        case MipsOp::kSWL:   return &PlaceMipsSwl;
        case MipsOp::kLB:    return &PlaceMipsLb;
        case MipsOp::kLH:    return &PlaceMipsLh;
        case MipsOp::kLHU:   return &PlaceMipsLhu;
        case MipsOp::kLBU:   return &PlaceMipsLbu;
        case MipsOp::kLWU:   return &PlaceMipsLwu;
        case MipsOp::kBGTZ:  return &PlaceMipsBgtz;
        case MipsOp::kBLEZ:  return &PlaceMipsBlez;
        case MipsOp::kBEQL:  return &PlaceMipsBeql;
        case MipsOp::kBNEL:  return &PlaceMipsBnel;
        case MipsOp::kBLEZL: return &PlaceMipsBlezl;
        case MipsOp::kBGTZL: return &PlaceMipsBgtzl;
        /* QEMU MIPS translate.c OPC_PREF and OPC_CACHE: "Treat as NOP."; helper_cache
           (special_helper.c): Index/Hit Invalidate "no-op". */
        case MipsOp::kPREF:  return &PlaceMipsNop;
        case MipsOp::kCACHE: return &PlaceMipsNop;
        case MipsOp::kREGIMM:
            if (d->rt == MipsRegimm::kBLTZ)        return &PlaceMipsBltz;
            if (d->rt == MipsRegimm::kBGEZ)        return &PlaceMipsBgez;
            if (d->rt == MipsRegimm::kBLTZL)       return &PlaceMipsBltzl;
            if (d->rt == MipsRegimm::kBGEZL)       return &PlaceMipsBgezl;
            if (d->rt == MipsRegimm::kBLTZAL)      return &PlaceMipsBltzal;
            if (d->rt == MipsRegimm::kBGEZAL)      return &PlaceMipsBgezal;
            return &PlaceMipsUndefined;
        case MipsOp::kSPECIAL2:
            if (d->funct == MipsSpecial2::kMADD)   return &PlaceMipsMadd;
            if (d->funct == MipsSpecial2::kMADDU)  return &PlaceMipsMaddu;
            return &PlaceMipsUndefined;
        case MipsOp::kBEQ:   return &PlaceMipsBeq;
        case MipsOp::kBNE:   return &PlaceMipsBne;
        case MipsOp::kSPECIAL:
            if (d->raw == 0u)                      return &PlaceMipsNop;
            if (d->funct == MipsSpecial::kSLL)     return &PlaceMipsSll;
            if (d->funct == MipsSpecial::kSRL)     return &PlaceMipsSrl;
            if (d->funct == MipsSpecial::kSRA)     return &PlaceMipsSra;
            if (d->funct == MipsSpecial::kSRLV)    return &PlaceMipsSrlv;
            if (d->funct == MipsSpecial::kSRAV)    return &PlaceMipsSrav;
            if (d->funct == MipsSpecial::kSLLV)    return &PlaceMipsSllv;
            if (d->funct == MipsSpecial::kADD)     return &PlaceMipsAdd;
            if (d->funct == MipsSpecial::kADDU)    return &PlaceMipsAddu;
            if (d->funct == MipsSpecial::kSUB)     return &PlaceMipsSub;
            if (d->funct == MipsSpecial::kSUBU)    return &PlaceMipsSubu;
            if (d->funct == MipsSpecial::kSYSCALL) return &PlaceMipsSyscall;
            if (d->funct == MipsSpecial::kBREAK)   return &PlaceMipsBreak;
            if (d->funct == MipsSpecial::kOR)      return &PlaceMipsOr;
            if (d->funct == MipsSpecial::kAND)     return &PlaceMipsAnd;
            if (d->funct == MipsSpecial::kXOR)     return &PlaceMipsXor;
            if (d->funct == MipsSpecial::kNOR)     return &PlaceMipsNor;
            if (d->funct == MipsSpecial::kDIVU)    return &PlaceMipsDivu;
            if (d->funct == MipsSpecial::kDIV)     return &PlaceMipsDiv;
            if (d->funct == MipsSpecial::kMULTU)   return &PlaceMipsMultu;
            if (d->funct == MipsSpecial::kMULT)    return &PlaceMipsMult;
            if (d->funct == MipsSpecial::kDMULT)   return &PlaceMipsDmult;
            if (d->funct == MipsSpecial::kDMULTU)  return &PlaceMipsDmultu;
            if (d->funct == MipsSpecial::kDDIV)    return &PlaceMipsDdiv;
            if (d->funct == MipsSpecial::kDDIVU)   return &PlaceMipsDdivu;
            if (d->funct == MipsSpecial::kMFHI)    return &PlaceMipsMfhi;
            if (d->funct == MipsSpecial::kMFLO)    return &PlaceMipsMflo;
            if (d->funct == MipsSpecial::kMTHI)    return &PlaceMipsMthi;
            if (d->funct == MipsSpecial::kMTLO)    return &PlaceMipsMtlo;
            if (d->funct == MipsSpecial::kSLTU)    return &PlaceMipsSltu;
            if (d->funct == MipsSpecial::kSLT)     return &PlaceMipsSlt;
            if (d->funct == MipsSpecial::kJR)      return &PlaceMipsJr;
            if (d->funct == MipsSpecial::kJALR)    return &PlaceMipsJalr;
            if (d->funct == MipsSpecial::kDADD)    return &PlaceMipsDadd;
            if (d->funct == MipsSpecial::kDADDU)   return &PlaceMipsDaddu;
            if (d->funct == MipsSpecial::kDSUB)    return &PlaceMipsDsub;
            if (d->funct == MipsSpecial::kDSUBU)   return &PlaceMipsDsubu;
            if (d->funct == MipsSpecial::kMOVZ)    return &PlaceMipsMovz;
            if (d->funct == MipsSpecial::kMOVN)    return &PlaceMipsMovn;
            if (d->funct == MipsSpecial::kSYNC)    return &PlaceMipsNop;
            if (d->funct == MipsSpecial::kDSLLV)   return &PlaceMipsDsllv;
            if (d->funct == MipsSpecial::kDSRLV)   return &PlaceMipsDsrlv;
            if (d->funct == MipsSpecial::kDSRAV)   return &PlaceMipsDsrav;
            if (d->funct == MipsSpecial::kDSLL)    return &PlaceMipsDsll;
            if (d->funct == MipsSpecial::kDSRL)    return &PlaceMipsDsrl;
            if (d->funct == MipsSpecial::kDSRA)    return &PlaceMipsDsra;
            if (d->funct == MipsSpecial::kDSLL32)  return &PlaceMipsDsll32;
            if (d->funct == MipsSpecial::kDSRL32)  return &PlaceMipsDsrl32;
            if (d->funct == MipsSpecial::kDSRA32)  return &PlaceMipsDsra32;
            return &PlaceMipsUndefined;
        case MipsOp::kCOP0:
            if (d->rs == MipsCop0Rs::kMTC0)        return &PlaceMipsMtc0;
            if (d->rs == MipsCop0Rs::kMFC0)        return &PlaceMipsMfc0;
            if (d->rs == MipsCop0Rs::kDMTC0)       return &PlaceMipsDmtc0;
            if (d->rs == MipsCop0Rs::kDMFC0)       return &PlaceMipsDmfc0;
            if (d->rs >= MipsCop0Rs::kCO) {
                if (d->funct == MipsCop0Funct::kTLBR)  return &PlaceMipsTlbr;
                if (d->funct == MipsCop0Funct::kTLBWI) return &PlaceMipsTlbwi;
                if (d->funct == MipsCop0Funct::kTLBWR) return &PlaceMipsTlbwr;
                if (d->funct == MipsCop0Funct::kTLBP)  return &PlaceMipsTlbp;
                if (d->funct == MipsCop0Funct::kRFE)   return &PlaceMipsRfe;
                if (d->funct == MipsCop0Funct::kERET)  return &PlaceMipsEret;
                /* STANDBY (0x21) / SUSPEND (0x22): halt the CPU core until any
                   interrupt (VR4102 UM ch.27 p643/p646). */
                if (d->funct == MipsCop0Funct::kSTANDBY ||
                    d->funct == MipsCop0Funct::kSUSPEND) return &PlaceMipsWait;
                /* HIBERNATE freezes the pipeline until a Cold Reset (UM ch.27 p587). */
                if (d->funct == MipsCop0Funct::kHIBERNATE) return &PlaceMipsHibernate;
            }
            return &PlaceMipsUndefined;
        default:
            return &PlaceMipsUndefined;
    }
}

}
