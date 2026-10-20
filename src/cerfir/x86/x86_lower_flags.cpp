#include "../../emit_fault.h"
#include "../../lowest_set_bit.h"
#include "../../x86_emit.h"
#include "../../x86_emit_compare.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

constexpr uint8_t kFlagCount[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

}

void X86StoreProducerFlags(X86Lowering& l, Ref producer) {
    const X86FlagSource& source = l.FlagSource();
    if (source.producer != producer) {
        throw EmitFault("CerfIR x86 lowering: a flag store has no recorded source");
    }
    X86StoreFlagsFrom(l, l.GetBlock().At(producer).aux16 & 0x0Fu, source);
    l.Regs().Consume(producer);
}

void X86StoreFlagsFrom(X86Lowering& l, uint32_t mask, const X86FlagSource& source) {
    const Block&   b     = l.GetBlock();
    const int32_t* disp  = b.FlagSlotsOf().disp;
    X86RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    const uint32_t first = LowestSetBit(mask | 0x10u);
    const bool     range = b.FlagSlotsContiguous() && ((mask + (mask & (0u - mask))) & mask) == 0u;
    if (range && mask != 0u) ra.BeforeSlotWrite(disp[first], kFlagCount[mask]);
    for (uint32_t i = 0; i < 4u; ++i) {
        if ((mask & (1u << i)) == 0u) continue;
        if (source.kind[i] == X86FlagKind::kNone) {
            throw EmitFault("CerfIR x86 lowering: a stored flag has no recorded source");
        }
        if (!range) ra.BeforeSlotWrite(disp[i], 1u);
        if (source.kind[i] == X86FlagKind::kEflags) {
            x86::EmitSetccBaseDisp32(c, static_cast<uint8_t>(x86::kSetO + source.where[i]),
                                     x86::kStateReg, disp[i]);
        } else {
            x86::EmitMovBaseDisp32Byte(c, x86::kStateReg, disp[i], source.where[i]);
            ra.ReleaseTemp(source.where[i]);
        }
    }
}

void X86StoreEflagsFlags(X86Lowering& l, uint32_t mask, const uint8_t (&where)[4]) {
    if (mask == 0u) return;
    const Block&   b     = l.GetBlock();
    const int32_t* disp  = b.FlagSlotsOf().disp;
    X86RegAlloc&   ra    = l.Regs();
    const uint32_t first = LowestSetBit(mask);
    const bool     range = b.FlagSlotsContiguous() && ((mask + (mask & (0u - mask))) & mask) == 0u;
    if (range) ra.BeforeSlotWrite(disp[first], kFlagCount[mask]);
    uint8_t* c = l.Cursor();
    for (uint32_t m = mask; m != 0u; m &= m - 1u) {
        const uint32_t i = LowestSetBit(m);
        if (!range) {
            l.Cursor() = c;
            ra.BeforeSlotWrite(disp[i], 1u);
            c = l.Cursor();
        }
        x86::EmitSetccBaseDisp32(c, static_cast<uint8_t>(x86::kSetO + where[i]), x86::kStateReg,
                                 disp[i]);
    }
    l.Cursor() = c;
}

void X86LowerFlag(X86Lowering& l, Ref ref) {
    const Inst& inst     = l.GetBlock().At(ref);
    const Ref   producer = inst.args[0];
    uint32_t    index    = kFlagIndexV;
    switch (inst.op) {
    case Op::kNFlag: index = kFlagIndexN; break;
    case Op::kZFlag: index = kFlagIndexZ; break;
    case Op::kCFlag: index = kFlagIndexC; break;
    default:         break;
    }
    const X86FlagSource& source = l.FlagSource();
    if (source.producer != producer || source.kind[index] == X86FlagKind::kNone) {
        throw EmitFault("CerfIR x86 lowering: a flag has no recorded source");
    }
    if (source.kind[index] == X86FlagKind::kEflags) {
        l.Regs().DefineFlags(ref, source.where[index]);
    } else {
        l.Regs().DefineInReg(ref, source.where[index], false);
    }
    l.Regs().Consume(producer);
}

}
