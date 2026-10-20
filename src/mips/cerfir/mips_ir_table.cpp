#include <array>

#include "mips_ir_fns.h"

namespace cerfengine {

namespace {

constexpr std::array<MipsIrTranslateFn, kMipsInsnKindCount> BuildTranslateTable() {
    std::array<MipsIrTranslateFn, kMipsInsnKindCount> t{};
    auto set = [&t](MipsInsnKind kind, MipsIrTranslateFn fn) { t[static_cast<size_t>(kind)] = fn; };
    set(MipsInsnKind::kUndefined,     &MipsIrUndefined);
    set(MipsInsnKind::kNop,           &MipsIrNop);
    set(MipsInsnKind::kCache,         &MipsIrNop);
    set(MipsInsnKind::kCopUnusable,   &MipsIrCopUnusable);
    set(MipsInsnKind::kAddu,          &MipsIrAddu);
    set(MipsInsnKind::kLui,           &MipsIrLui);
    set(MipsInsnKind::kOri,           &MipsIrOri);
    set(MipsInsnKind::kXori,          &MipsIrXori);
    set(MipsInsnKind::kOr,            &MipsIrOr);
    set(MipsInsnKind::kAddiu,         &MipsIrAddiu);
    set(MipsInsnKind::kMtc0,          &MipsIrMtc0);
    set(MipsInsnKind::kMfc0,          &MipsIrMfc0);
    set(MipsInsnKind::kDmtc0,         &MipsIrDmtc0);
    set(MipsInsnKind::kDmfc0,         &MipsIrDmfc0);
    set(MipsInsnKind::kTlbwi,         &MipsIrTlbwi);
    set(MipsInsnKind::kTlbwr,         &MipsIrTlbwr);
    set(MipsInsnKind::kTlbp,          &MipsIrTlbp);
    set(MipsInsnKind::kRfe,           &MipsIrRfe);
    set(MipsInsnKind::kEret,          &MipsIrEret);
    set(MipsInsnKind::kHibernate,     &MipsIrHibernate);
    set(MipsInsnKind::kWait,          &MipsIrWait);
    set(MipsInsnKind::kJ,             &MipsIrJ);
    set(MipsInsnKind::kJal,           &MipsIrJal);
    set(MipsInsnKind::kJalx,          &MipsIrJalx);
    set(MipsInsnKind::kJr,            &MipsIrJr);
    set(MipsInsnKind::kJalr,          &MipsIrJalr);
    set(MipsInsnKind::kBeq,           &MipsIrBeq);
    set(MipsInsnKind::kBne,           &MipsIrBne);
    set(MipsInsnKind::kBgtz,          &MipsIrBgtz);
    set(MipsInsnKind::kBlez,          &MipsIrBlez);
    set(MipsInsnKind::kBltz,          &MipsIrBltz);
    set(MipsInsnKind::kBgez,          &MipsIrBgez);
    set(MipsInsnKind::kBeql,          &MipsIrBeql);
    set(MipsInsnKind::kBnel,          &MipsIrBnel);
    set(MipsInsnKind::kBgtzl,         &MipsIrBgtzl);
    set(MipsInsnKind::kBlezl,         &MipsIrBlezl);
    set(MipsInsnKind::kBltzl,         &MipsIrBltzl);
    set(MipsInsnKind::kBgezl,         &MipsIrBgezl);
    set(MipsInsnKind::kBltzal,        &MipsIrBltzal);
    set(MipsInsnKind::kBgezal,        &MipsIrBgezal);
    set(MipsInsnKind::kSw,            &MipsIrSw);
    set(MipsInsnKind::kAddi,          &MipsIrAddi);
    set(MipsInsnKind::kLw,            &MipsIrLw);
    set(MipsInsnKind::kDaddiu,        &MipsIrDaddiu);
    set(MipsInsnKind::kDaddi,         &MipsIrDaddi);
    set(MipsInsnKind::kDadd,          &MipsIrDadd);
    set(MipsInsnKind::kDsub,          &MipsIrDsub);
    set(MipsInsnKind::kTlbr,          &MipsIrTlbr);
    set(MipsInsnKind::kSd,            &MipsIrSd);
    set(MipsInsnKind::kDsubu,         &MipsIrDsubu);
    set(MipsInsnKind::kSll,           &MipsIrSll);
    set(MipsInsnKind::kLd,            &MipsIrLd);
    set(MipsInsnKind::kAnd,           &MipsIrAnd);
    set(MipsInsnKind::kSltu,          &MipsIrSltu);
    set(MipsInsnKind::kSlt,           &MipsIrSlt);
    set(MipsInsnKind::kSltiu,         &MipsIrSltiu);
    set(MipsInsnKind::kXor,           &MipsIrXor);
    set(MipsInsnKind::kAndi,          &MipsIrAndi);
    set(MipsInsnKind::kSubu,          &MipsIrSubu);
    set(MipsInsnKind::kSdr,           &MipsIrSdr);
    set(MipsInsnKind::kSdl,           &MipsIrSdl);
    set(MipsInsnKind::kMovz,          &MipsIrMovz);
    set(MipsInsnKind::kSwr,           &MipsIrSwr);
    set(MipsInsnKind::kDsll32,        &MipsIrDsll32);
    set(MipsInsnKind::kDsrl32,        &MipsIrDsrl32);
    set(MipsInsnKind::kDsrl,          &MipsIrDsrl);
    set(MipsInsnKind::kDaddu,         &MipsIrDaddu);
    set(MipsInsnKind::kLwl,           &MipsIrLwl);
    set(MipsInsnKind::kLwr,           &MipsIrLwr);
    set(MipsInsnKind::kLdl,           &MipsIrLdl);
    set(MipsInsnKind::kLdr,           &MipsIrLdr);
    set(MipsInsnKind::kMovn,          &MipsIrMovn);
    set(MipsInsnKind::kDsll,          &MipsIrDsll);
    set(MipsInsnKind::kDsllv,         &MipsIrDsllv);
    set(MipsInsnKind::kDsrlv,         &MipsIrDsrlv);
    set(MipsInsnKind::kDsrav,         &MipsIrDsrav);
    set(MipsInsnKind::kDsra32,        &MipsIrDsra32);
    set(MipsInsnKind::kSwl,           &MipsIrSwl);
    set(MipsInsnKind::kLb,            &MipsIrLb);
    set(MipsInsnKind::kLh,            &MipsIrLh);
    set(MipsInsnKind::kLhu,           &MipsIrLhu);
    set(MipsInsnKind::kLbu,           &MipsIrLbu);
    set(MipsInsnKind::kLwu,           &MipsIrLwu);
    set(MipsInsnKind::kNor,           &MipsIrNor);
    set(MipsInsnKind::kDivu,          &MipsIrDivu);
    set(MipsInsnKind::kDiv,           &MipsIrDiv);
    set(MipsInsnKind::kMultu,         &MipsIrMultu);
    set(MipsInsnKind::kDmultu,        &MipsIrDmultu);
    set(MipsInsnKind::kDmult,         &MipsIrDmult);
    set(MipsInsnKind::kDdiv,          &MipsIrDdiv);
    set(MipsInsnKind::kDdivu,         &MipsIrDdivu);
    set(MipsInsnKind::kDsra,          &MipsIrDsra);
    set(MipsInsnKind::kMult,          &MipsIrMult);
    set(MipsInsnKind::kMadd,          &MipsIrMadd);
    set(MipsInsnKind::kMaddu,         &MipsIrMaddu);
    set(MipsInsnKind::kMfhi,          &MipsIrMfhi);
    set(MipsInsnKind::kMflo,          &MipsIrMflo);
    set(MipsInsnKind::kMthi,          &MipsIrMthi);
    set(MipsInsnKind::kMtlo,          &MipsIrMtlo);
    set(MipsInsnKind::kSlti,          &MipsIrSlti);
    set(MipsInsnKind::kAdd,           &MipsIrAdd);
    set(MipsInsnKind::kSh,            &MipsIrSh);
    set(MipsInsnKind::kSb,            &MipsIrSb);
    set(MipsInsnKind::kSrlv,          &MipsIrSrlv);
    set(MipsInsnKind::kSllv,          &MipsIrSllv);
    set(MipsInsnKind::kSrl,           &MipsIrSrl);
    set(MipsInsnKind::kSra,           &MipsIrSra);
    set(MipsInsnKind::kSrav,          &MipsIrSrav);
    set(MipsInsnKind::kSub,           &MipsIrSub);
    set(MipsInsnKind::kBreak,         &MipsIrBreak);
    set(MipsInsnKind::kSyscall,       &MipsIrSyscall);
    set(MipsInsnKind::kMips16B,       &MipsIrMips16B);
    set(MipsInsnKind::kMips16Beqz,    &MipsIrMips16Beqz);
    set(MipsInsnKind::kMips16Bnez,    &MipsIrMips16Bnez);
    set(MipsInsnKind::kMips16Jal,     &MipsIrMips16Jal);
    set(MipsInsnKind::kMips16Jalx,    &MipsIrMips16Jalx);
    set(MipsInsnKind::kMips16Jalr,    &MipsIrMips16Jalr);
    set(MipsInsnKind::kMips16Addiupc, &MipsIrMips16Addiupc);
    set(MipsInsnKind::kMips16Lwpc,    &MipsIrMips16Lwpc);
    set(MipsInsnKind::kMips16Ldpc,    &MipsIrMips16Ldpc);
    return t;
}

constexpr std::array<MipsIrTranslateFn, kMipsInsnKindCount> kTranslateTable = BuildTranslateTable();

constexpr bool EveryKindHasTranslator() {
    for (MipsIrTranslateFn fn : kTranslateTable) {
        if (fn == nullptr) return false;
    }
    return true;
}

static_assert(EveryKindHasTranslator(), "a MipsInsnKind has no CerfIR translator");

}

extern const std::array<MipsIrTranslateFn, kMipsInsnKindCount> kMipsIrTranslateTable = kTranslateTable;

}
