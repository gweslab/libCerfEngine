#include <cstdint>

#include "../../a64_emit.h"
#include "../../a64_emit_alu.h"
#include "../../a64_emit_memory.h"
#include "../../emit_fault.h"
#include "a64_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

const void* HostAddress(const Block& b, Ref r) {
    return reinterpret_cast<const void*>(static_cast<uintptr_t>(b.ConstValue(r)));
}

uint8_t ValueReg(A64RegAlloc& ra, uint8_t*& c, Ref value) {
    if (!IsConstRef(value)) return ra.Materialize(value);
    const uint32_t v = ra.ConstOf(value);
    if (v == 0u) return a64::kZr;
    a64::EmitMovImm32(c, kA64Tmp1, v);
    return kA64Tmp1;
}

}

void A64LowerHostLoad(A64Lowering& l, Ref ref) {
    const Block&   b     = l.GetBlock();
    const Inst&    inst  = b.At(ref);
    A64RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    const uint32_t width = inst.aux16 & kHostWidthMask;
    const bool     sign  = (inst.aux16 & kHostSigned) != 0u;
    if (!(width == 4u && !sign) && !(width == 1u && sign)) {
        throw EmitFault("CerfIR A64 lowering: host load width has no lowering");
    }
    const uint8_t reg = ra.AllocTemp();
    a64::EmitMovPtr(c, a64::kIp0, HostAddress(b, inst.args[0]));
    if (width == 4u) {
        a64::EmitLdrW(c, reg, a64::kIp0, 0);
    } else {
        a64::EmitLoadStore(c, a64::kByte, a64::kLoadSigned32, reg, a64::kIp0, 0);
    }
    ra.DefineInReg(ref, reg);
}

void A64LowerHostStore(A64Lowering& l, Ref ref) {
    const Block&   b     = l.GetBlock();
    const Inst&    inst  = b.At(ref);
    A64RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    const uint32_t width = inst.aux16 & kHostWidthMask;
    const Ref      value = inst.args[1];
    if (width != 1u && width != 4u) {
        throw EmitFault("CerfIR A64 lowering: host store width has no lowering");
    }
    const uint8_t reg = ValueReg(ra, c, value);
    a64::EmitMovPtr(c, a64::kIp0, HostAddress(b, inst.args[0]));
    if (width == 4u) {
        a64::EmitStrW(c, reg, a64::kIp0, 0);
    } else {
        a64::EmitStrb(c, reg, a64::kIp0, 0);
    }
    ra.Consume(value);
}

void A64LowerHostTest(A64Lowering& l, Ref ref) {
    const Block&   b    = l.GetBlock();
    const Inst&    inst = b.At(ref);
    A64RegAlloc&   ra   = l.Regs();
    uint8_t*&      c    = l.Cursor();
    const uint32_t mask = static_cast<uint32_t>(b.ConstValue(inst.args[1]));
    ra.BeforeFlagsClobber();
    a64::EmitMovPtr(c, a64::kIp0, HostAddress(b, inst.args[0]));
    if (mask <= 0xFFu) {
        a64::EmitLdrb(c, kA64Tmp0, a64::kIp0, 0);
    } else {
        a64::EmitLdrW(c, kA64Tmp0, a64::kIp0, 0);
    }
    if (a64::LogicImmFits(mask, false)) {
        a64::EmitLogicImm(c, a64::kAnds, a64::kZr, kA64Tmp0, mask);
    } else {
        a64::EmitMovImm32(c, kA64Tmp1, mask);
        a64::EmitTstReg(c, kA64Tmp0, kA64Tmp1);
    }
    l.ClearFlagSource();
    ra.DefineFlags(ref, a64::kNe);
}

void A64LowerHostReplace(A64Lowering& l, Ref ref) {
    const Block& b     = l.GetBlock();
    const Inst&  inst  = b.At(ref);
    A64RegAlloc& ra    = l.Regs();
    uint8_t*&    c     = l.Cursor();
    const Ref    value = inst.args[1];
    ra.BeforeFlagsClobber();
    const uint8_t reg = ValueReg(ra, c, value);
    a64::EmitMovPtr(c, a64::kIp0, HostAddress(b, inst.args[0]));
    a64::EmitLdrW(c, kA64Tmp0, a64::kIp0, 0);
    a64::EmitCmpReg(c, reg, kA64Tmp0);
    a64::EmitStrW(c, reg, a64::kIp0, 0);
    ra.Consume(value);
    l.ClearFlagSource();
    ra.DefineFlags(ref, a64::kNe);
}

}
