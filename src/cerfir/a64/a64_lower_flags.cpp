#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "../../lowest_set_bit.h"
#include "../cerfir_build_class.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

CERFENGINE_FORCEINLINE bool FlagRange(const Block& b, uint32_t mask) {
    return b.FlagSlotsContiguous() && ((mask + (mask & (0u - mask))) & mask) == 0u;
}

}

void A64StoreProducerFlags(A64Lowering& l, Ref producer) {
    const A64FlagSource& source = l.FlagSource();
    if (source.producer != producer) {
        throw EmitFault("CerfIR A64 lowering: a flag store has no recorded source");
    }
    A64StoreFlagsFrom(l, l.GetBlock().At(producer).aux16 & 0x0Fu, source);
    l.Regs().Consume(producer);
}

void A64StoreFlagsFrom(A64Lowering& l, uint32_t mask, const A64FlagSource& source) {
    const Block&   b     = l.GetBlock();
    const int32_t* disp  = b.FlagSlotsOf().disp;
    A64RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    const bool     range = FlagRange(b, mask);
    if (range && mask != 0u) ra.BeforeSlotWrite(disp[LowestSetBit(mask)], kFlagCount[mask]);
    for (uint32_t i = 0; i < 4u; ++i) {
        if ((mask & (1u << i)) == 0u) continue;
        if (source.kind[i] == A64FlagKind::kNone) {
            throw EmitFault("CerfIR A64 lowering: a stored flag has no recorded source");
        }
        if (!range) ra.BeforeSlotWrite(disp[i], 1u);
        l.Pins().CheckUnpinned(disp[i], 1u);
        if (source.kind[i] == A64FlagKind::kNzcv) {
            a64::EmitCset(c, kA64Tmp0, source.where[i]);
            a64::EmitStrb(c, kA64Tmp0, a64::kStateReg, disp[i]);
        } else {
            a64::EmitStrb(c, source.where[i], a64::kStateReg, disp[i]);
            ra.ReleaseTemp(source.where[i]);
        }
    }
}

void A64StoreNzcvFlags(A64Lowering& l, uint32_t mask, const uint8_t (&where)[4]) {
    if (mask == 0u) return;
    const Block&   b     = l.GetBlock();
    const int32_t* disp  = b.FlagSlotsOf().disp;
    A64RegAlloc&   ra    = l.Regs();
    const bool     range = FlagRange(b, mask);
    if (range) ra.BeforeSlotWrite(disp[LowestSetBit(mask)], kFlagCount[mask]);
    for (uint32_t m = mask; m != 0u; m &= m - 1u) {
        const uint32_t i = LowestSetBit(m);
        if (!range) ra.BeforeSlotWrite(disp[i], 1u);
        l.Pins().CheckUnpinned(disp[i], 1u);
        uint8_t*& c = l.Cursor();
        a64::EmitCset(c, kA64Tmp0, where[i]);
        a64::EmitStrb(c, kA64Tmp0, a64::kStateReg, disp[i]);
    }
}

void A64LowerFlag(A64Lowering& l, Ref ref) {
    const Inst& inst     = l.GetBlock().At(ref);
    const Ref   producer = inst.args[0];
    uint32_t    index    = kA64FlagIndexV;
    switch (inst.op) {
    case Op::kNFlag: index = kA64FlagIndexN; break;
    case Op::kZFlag: index = kA64FlagIndexZ; break;
    case Op::kCFlag: index = kA64FlagIndexC; break;
    default:         break;
    }
    const A64FlagSource& source = l.FlagSource();
    if (source.producer != producer || source.kind[index] == A64FlagKind::kNone) {
        throw EmitFault("CerfIR A64 lowering: a flag has no recorded source");
    }
    if (source.kind[index] == A64FlagKind::kNzcv) {
        l.Regs().DefineFlags(ref, source.where[index]);
    } else {
        l.Regs().DefineInReg(ref, source.where[index]);
    }
    l.Regs().Consume(producer);
}

}
