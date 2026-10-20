#pragma once

#include <array>
#include <cstddef>

#include <cerfengine/mips/mips_insn_kind.h>

#include "mips_ir_context.h"

namespace cerfengine {

using MipsIrTranslateFn = void (*)(MipsIrContext& c);

extern const std::array<MipsIrTranslateFn, kMipsInsnKindCount> kMipsIrTranslateTable;

inline MipsIrTranslateFn MipsIrTranslateFnFor(MipsInsnKind kind) {
    return kMipsIrTranslateTable[static_cast<size_t>(kind)];
}

void MipsIrUndefined(MipsIrContext& c);
void MipsIrNop(MipsIrContext& c);
void MipsIrCopUnusable(MipsIrContext& c);

void MipsIrAddu(MipsIrContext& c);
void MipsIrSubu(MipsIrContext& c);
void MipsIrAddiu(MipsIrContext& c);
void MipsIrLui(MipsIrContext& c);
void MipsIrOri(MipsIrContext& c);
void MipsIrXori(MipsIrContext& c);
void MipsIrAndi(MipsIrContext& c);
void MipsIrAnd(MipsIrContext& c);
void MipsIrOr(MipsIrContext& c);
void MipsIrXor(MipsIrContext& c);
void MipsIrNor(MipsIrContext& c);
void MipsIrSlt(MipsIrContext& c);
void MipsIrSltu(MipsIrContext& c);
void MipsIrSlti(MipsIrContext& c);
void MipsIrSltiu(MipsIrContext& c);
void MipsIrMovz(MipsIrContext& c);
void MipsIrMovn(MipsIrContext& c);

void MipsIrAdd(MipsIrContext& c);
void MipsIrSub(MipsIrContext& c);
void MipsIrAddi(MipsIrContext& c);
void MipsIrDadd(MipsIrContext& c);
void MipsIrDsub(MipsIrContext& c);
void MipsIrDaddi(MipsIrContext& c);
void MipsIrDaddu(MipsIrContext& c);
void MipsIrDsubu(MipsIrContext& c);
void MipsIrDaddiu(MipsIrContext& c);

void MipsIrSll(MipsIrContext& c);
void MipsIrSrl(MipsIrContext& c);
void MipsIrSra(MipsIrContext& c);
void MipsIrSllv(MipsIrContext& c);
void MipsIrSrlv(MipsIrContext& c);
void MipsIrSrav(MipsIrContext& c);
void MipsIrDsll(MipsIrContext& c);
void MipsIrDsrl(MipsIrContext& c);
void MipsIrDsra(MipsIrContext& c);
void MipsIrDsll32(MipsIrContext& c);
void MipsIrDsrl32(MipsIrContext& c);
void MipsIrDsra32(MipsIrContext& c);
void MipsIrDsllv(MipsIrContext& c);
void MipsIrDsrlv(MipsIrContext& c);
void MipsIrDsrav(MipsIrContext& c);

void MipsIrMult(MipsIrContext& c);
void MipsIrMultu(MipsIrContext& c);
void MipsIrDiv(MipsIrContext& c);
void MipsIrDivu(MipsIrContext& c);
void MipsIrDmult(MipsIrContext& c);
void MipsIrDmultu(MipsIrContext& c);
void MipsIrDdiv(MipsIrContext& c);
void MipsIrDdivu(MipsIrContext& c);
void MipsIrMadd(MipsIrContext& c);
void MipsIrMaddu(MipsIrContext& c);
void MipsIrMfhi(MipsIrContext& c);
void MipsIrMflo(MipsIrContext& c);
void MipsIrMthi(MipsIrContext& c);
void MipsIrMtlo(MipsIrContext& c);

void MipsIrLb(MipsIrContext& c);
void MipsIrLbu(MipsIrContext& c);
void MipsIrLh(MipsIrContext& c);
void MipsIrLhu(MipsIrContext& c);
void MipsIrLw(MipsIrContext& c);
void MipsIrLwu(MipsIrContext& c);
void MipsIrLd(MipsIrContext& c);
void MipsIrSb(MipsIrContext& c);
void MipsIrSh(MipsIrContext& c);
void MipsIrSw(MipsIrContext& c);
void MipsIrSd(MipsIrContext& c);
void MipsIrLwl(MipsIrContext& c);
void MipsIrLwr(MipsIrContext& c);
void MipsIrLdl(MipsIrContext& c);
void MipsIrLdr(MipsIrContext& c);
void MipsIrSwl(MipsIrContext& c);
void MipsIrSwr(MipsIrContext& c);
void MipsIrSdl(MipsIrContext& c);
void MipsIrSdr(MipsIrContext& c);

void MipsIrJ(MipsIrContext& c);
void MipsIrJal(MipsIrContext& c);
void MipsIrJalx(MipsIrContext& c);
void MipsIrJr(MipsIrContext& c);
void MipsIrJalr(MipsIrContext& c);
void MipsIrBeq(MipsIrContext& c);
void MipsIrBne(MipsIrContext& c);
void MipsIrBgtz(MipsIrContext& c);
void MipsIrBlez(MipsIrContext& c);
void MipsIrBltz(MipsIrContext& c);
void MipsIrBgez(MipsIrContext& c);
void MipsIrBeql(MipsIrContext& c);
void MipsIrBnel(MipsIrContext& c);
void MipsIrBgtzl(MipsIrContext& c);
void MipsIrBlezl(MipsIrContext& c);
void MipsIrBltzl(MipsIrContext& c);
void MipsIrBgezl(MipsIrContext& c);
void MipsIrBltzal(MipsIrContext& c);
void MipsIrBgezal(MipsIrContext& c);

void MipsIrMips16B(MipsIrContext& c);
void MipsIrMips16Beqz(MipsIrContext& c);
void MipsIrMips16Bnez(MipsIrContext& c);
void MipsIrMips16Jal(MipsIrContext& c);
void MipsIrMips16Jalx(MipsIrContext& c);
void MipsIrMips16Jalr(MipsIrContext& c);
void MipsIrMips16Addiupc(MipsIrContext& c);
void MipsIrMips16Lwpc(MipsIrContext& c);
void MipsIrMips16Ldpc(MipsIrContext& c);

void MipsIrMtc0(MipsIrContext& c);
void MipsIrMfc0(MipsIrContext& c);
void MipsIrDmtc0(MipsIrContext& c);
void MipsIrDmfc0(MipsIrContext& c);

void MipsIrTlbwi(MipsIrContext& c);
void MipsIrTlbwr(MipsIrContext& c);
void MipsIrTlbp(MipsIrContext& c);
void MipsIrTlbr(MipsIrContext& c);
void MipsIrRfe(MipsIrContext& c);
void MipsIrEret(MipsIrContext& c);
void MipsIrHibernate(MipsIrContext& c);
void MipsIrWait(MipsIrContext& c);
void MipsIrBreak(MipsIrContext& c);
void MipsIrSyscall(MipsIrContext& c);

}
