#include <array>

#include "mips_ir_fns.h"

namespace cerfengine {

namespace {

constexpr MipsUnprivilegedCheck kOpen   = MipsUnprivilegedCheck::kNone;
constexpr MipsUnprivilegedCheck kCop0   = MipsUnprivilegedCheck::kCop0Usable;
constexpr MipsUnprivilegedCheck kWide   = MipsUnprivilegedCheck::kWide64Enabled;
constexpr MipsUnprivilegedCheck kCop0W  = MipsUnprivilegedCheck::kCop0UsableWide64Enabled;

constexpr std::array<MipsIrTranslation, kMipsInsnKindCount> BuildTranslateTable() {
    std::array<MipsIrTranslation, kMipsInsnKindCount> t{};
    auto set = [&t](MipsInsnKind kind, MipsIrTranslateFn fn, MipsUnprivilegedCheck check) {
        t[static_cast<size_t>(kind)] = {fn, check};
    };
    set(MipsInsnKind::kUndefined,      &MipsIrUndefined,      kOpen);
    set(MipsInsnKind::kNop,            &MipsIrNop,            kOpen);
    set(MipsInsnKind::kCache,          &MipsIrNop,            kCop0);
    set(MipsInsnKind::kCopUnusable,    &MipsIrCopUnusable,    kOpen);
    set(MipsInsnKind::kAddu,           &MipsIrAddu,           kOpen);
    set(MipsInsnKind::kLui,            &MipsIrLui,            kOpen);
    set(MipsInsnKind::kOri,            &MipsIrOri,            kOpen);
    set(MipsInsnKind::kXori,           &MipsIrXori,           kOpen);
    set(MipsInsnKind::kOr,             &MipsIrOr,             kOpen);
    set(MipsInsnKind::kAddiu,          &MipsIrAddiu,          kOpen);
    set(MipsInsnKind::kMtc0,           &MipsIrMtc0,           kCop0);
    set(MipsInsnKind::kMfc0,           &MipsIrMfc0,           kCop0);
    set(MipsInsnKind::kDmtc0,          &MipsIrDmtc0,          kCop0W);
    set(MipsInsnKind::kDmfc0,          &MipsIrDmfc0,          kCop0W);
    set(MipsInsnKind::kTlbwi,          &MipsIrTlbwi,          kCop0);
    set(MipsInsnKind::kTlbwr,          &MipsIrTlbwr,          kCop0);
    set(MipsInsnKind::kTlbp,           &MipsIrTlbp,           kCop0);
    set(MipsInsnKind::kRfe,            &MipsIrRfe,            kCop0);
    set(MipsInsnKind::kEret,           &MipsIrEret,           kCop0);
    set(MipsInsnKind::kHibernate,      &MipsIrHibernate,      kCop0);
    set(MipsInsnKind::kWait,           &MipsIrWait,           kCop0);
    set(MipsInsnKind::kJ,              &MipsIrJ,              kOpen);
    set(MipsInsnKind::kJal,            &MipsIrJal,            kOpen);
    set(MipsInsnKind::kJalx,           &MipsIrJalx,           kOpen);
    set(MipsInsnKind::kJr,             &MipsIrJr,             kOpen);
    set(MipsInsnKind::kJalr,           &MipsIrJalr,           kOpen);
    set(MipsInsnKind::kBeq,            &MipsIrBeq,            kOpen);
    set(MipsInsnKind::kBne,            &MipsIrBne,            kOpen);
    set(MipsInsnKind::kBgtz,           &MipsIrBgtz,           kOpen);
    set(MipsInsnKind::kBlez,           &MipsIrBlez,           kOpen);
    set(MipsInsnKind::kBltz,           &MipsIrBltz,           kOpen);
    set(MipsInsnKind::kBgez,           &MipsIrBgez,           kOpen);
    set(MipsInsnKind::kBeql,           &MipsIrBeql,           kOpen);
    set(MipsInsnKind::kBnel,           &MipsIrBnel,           kOpen);
    set(MipsInsnKind::kBgtzl,          &MipsIrBgtzl,          kOpen);
    set(MipsInsnKind::kBlezl,          &MipsIrBlezl,          kOpen);
    set(MipsInsnKind::kBltzl,          &MipsIrBltzl,          kOpen);
    set(MipsInsnKind::kBgezl,          &MipsIrBgezl,          kOpen);
    set(MipsInsnKind::kBltzal,         &MipsIrBltzal,         kOpen);
    set(MipsInsnKind::kBgezal,         &MipsIrBgezal,         kOpen);
    set(MipsInsnKind::kSw,             &MipsIrSw,             kOpen);
    set(MipsInsnKind::kAddi,           &MipsIrAddi,           kOpen);
    set(MipsInsnKind::kLw,             &MipsIrLw,             kOpen);
    set(MipsInsnKind::kDaddiu,         &MipsIrDaddiu,         kWide);
    set(MipsInsnKind::kDaddi,          &MipsIrDaddi,          kWide);
    set(MipsInsnKind::kDadd,           &MipsIrDadd,           kWide);
    set(MipsInsnKind::kDsub,           &MipsIrDsub,           kWide);
    set(MipsInsnKind::kTlbr,           &MipsIrTlbr,           kCop0);
    set(MipsInsnKind::kSd,             &MipsIrSd,             kWide);
    set(MipsInsnKind::kDsubu,          &MipsIrDsubu,          kWide);
    set(MipsInsnKind::kSll,            &MipsIrSll,            kOpen);
    set(MipsInsnKind::kLd,             &MipsIrLd,             kWide);
    set(MipsInsnKind::kAnd,            &MipsIrAnd,            kOpen);
    set(MipsInsnKind::kSltu,           &MipsIrSltu,           kOpen);
    set(MipsInsnKind::kSlt,            &MipsIrSlt,            kOpen);
    set(MipsInsnKind::kSltiu,          &MipsIrSltiu,          kOpen);
    set(MipsInsnKind::kXor,            &MipsIrXor,            kOpen);
    set(MipsInsnKind::kAndi,           &MipsIrAndi,           kOpen);
    set(MipsInsnKind::kSubu,           &MipsIrSubu,           kOpen);
    set(MipsInsnKind::kSdr,            &MipsIrSdr,            kWide);
    set(MipsInsnKind::kSdl,            &MipsIrSdl,            kWide);
    set(MipsInsnKind::kMovz,           &MipsIrMovz,           kOpen);
    set(MipsInsnKind::kSwr,            &MipsIrSwr,            kOpen);
    set(MipsInsnKind::kDsll32,         &MipsIrDsll32,         kWide);
    set(MipsInsnKind::kDsrl32,         &MipsIrDsrl32,         kWide);
    set(MipsInsnKind::kDsrl,           &MipsIrDsrl,           kWide);
    set(MipsInsnKind::kDaddu,          &MipsIrDaddu,          kWide);
    set(MipsInsnKind::kLwl,            &MipsIrLwl,            kOpen);
    set(MipsInsnKind::kLwr,            &MipsIrLwr,            kOpen);
    set(MipsInsnKind::kLdl,            &MipsIrLdl,            kWide);
    set(MipsInsnKind::kLdr,            &MipsIrLdr,            kWide);
    set(MipsInsnKind::kMovn,           &MipsIrMovn,           kOpen);
    set(MipsInsnKind::kDsll,           &MipsIrDsll,           kWide);
    set(MipsInsnKind::kDsllv,          &MipsIrDsllv,          kWide);
    set(MipsInsnKind::kDsrlv,          &MipsIrDsrlv,          kWide);
    set(MipsInsnKind::kDsrav,          &MipsIrDsrav,          kWide);
    set(MipsInsnKind::kDsra32,         &MipsIrDsra32,         kWide);
    set(MipsInsnKind::kSwl,            &MipsIrSwl,            kOpen);
    set(MipsInsnKind::kLb,             &MipsIrLb,             kOpen);
    set(MipsInsnKind::kLh,             &MipsIrLh,             kOpen);
    set(MipsInsnKind::kLhu,            &MipsIrLhu,            kOpen);
    set(MipsInsnKind::kLbu,            &MipsIrLbu,            kOpen);
    set(MipsInsnKind::kLwu,            &MipsIrLwu,            kWide);
    set(MipsInsnKind::kNor,            &MipsIrNor,            kOpen);
    set(MipsInsnKind::kDivu,           &MipsIrDivu,           kOpen);
    set(MipsInsnKind::kDiv,            &MipsIrDiv,            kOpen);
    set(MipsInsnKind::kMultu,          &MipsIrMultu,          kOpen);
    set(MipsInsnKind::kDmultu,         &MipsIrDmultu,         kWide);
    set(MipsInsnKind::kDmult,          &MipsIrDmult,          kWide);
    set(MipsInsnKind::kDdiv,           &MipsIrDdiv,           kWide);
    set(MipsInsnKind::kDdivu,          &MipsIrDdivu,          kWide);
    set(MipsInsnKind::kDsra,           &MipsIrDsra,           kWide);
    set(MipsInsnKind::kMult,           &MipsIrMult,           kOpen);
    set(MipsInsnKind::kMadd,           &MipsIrMadd,           kOpen);
    set(MipsInsnKind::kMaddu,          &MipsIrMaddu,          kOpen);
    set(MipsInsnKind::kMacc,           &MipsIrMacc,           kOpen);
    set(MipsInsnKind::kDmacc,          &MipsIrDmacc,          kWide);
    set(MipsInsnKind::kMadd16,         &MipsIrMadd16,         kOpen);
    set(MipsInsnKind::kDmadd16,        &MipsIrDmadd16,        kWide);
    set(MipsInsnKind::kMfhi,           &MipsIrMfhi,           kOpen);
    set(MipsInsnKind::kMflo,           &MipsIrMflo,           kOpen);
    set(MipsInsnKind::kMthi,           &MipsIrMthi,           kOpen);
    set(MipsInsnKind::kMtlo,           &MipsIrMtlo,           kOpen);
    set(MipsInsnKind::kSlti,           &MipsIrSlti,           kOpen);
    set(MipsInsnKind::kAdd,            &MipsIrAdd,            kOpen);
    set(MipsInsnKind::kSh,             &MipsIrSh,             kOpen);
    set(MipsInsnKind::kSb,             &MipsIrSb,             kOpen);
    set(MipsInsnKind::kSrlv,           &MipsIrSrlv,           kOpen);
    set(MipsInsnKind::kSllv,           &MipsIrSllv,           kOpen);
    set(MipsInsnKind::kSrl,            &MipsIrSrl,            kOpen);
    set(MipsInsnKind::kSra,            &MipsIrSra,            kOpen);
    set(MipsInsnKind::kSrav,           &MipsIrSrav,           kOpen);
    set(MipsInsnKind::kSub,            &MipsIrSub,            kOpen);
    set(MipsInsnKind::kBreak,          &MipsIrBreak,          kOpen);
    set(MipsInsnKind::kSyscall,        &MipsIrSyscall,        kOpen);
    set(MipsInsnKind::kMips16B,        &MipsIrMips16B,        kOpen);
    set(MipsInsnKind::kMips16Beqz,     &MipsIrMips16Beqz,     kOpen);
    set(MipsInsnKind::kMips16Bnez,     &MipsIrMips16Bnez,     kOpen);
    set(MipsInsnKind::kMips16Jal,      &MipsIrMips16Jal,      kOpen);
    set(MipsInsnKind::kMips16Jalx,     &MipsIrMips16Jalx,     kOpen);
    set(MipsInsnKind::kMips16Jalr,     &MipsIrMips16Jalr,     kOpen);
    set(MipsInsnKind::kMips16Addiupc,  &MipsIrMips16Addiupc,  kOpen);
    set(MipsInsnKind::kMips16Daddiupc, &MipsIrMips16Addiupc,  kWide);
    set(MipsInsnKind::kMips16Lwpc,     &MipsIrMips16Lwpc,     kOpen);
    set(MipsInsnKind::kMips16Ldpc,     &MipsIrMips16Ldpc,     kWide);
    return t;
}

constexpr std::array<MipsIrTranslation, kMipsInsnKindCount> kTranslateTable = BuildTranslateTable();

constexpr bool EveryKindHasTranslator() {
    for (const MipsIrTranslation& entry : kTranslateTable) {
        if (entry.fn == nullptr) return false;
    }
    return true;
}

static_assert(EveryKindHasTranslator(), "a MipsInsnKind has no CerfIR translator");

}

extern const std::array<MipsIrTranslation, kMipsInsnKindCount> kMipsIrTranslateTable = kTranslateTable;

}
