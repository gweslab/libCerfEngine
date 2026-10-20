#include <cstdint>

#include "../../emit_fault.h"
#include "../../x86_emit.h"
#include "../../x86_emit_compare.h"
#include "x86_lower_fns.h"

namespace cerfengine::cerfir {

namespace {

const void* HostAddress(const Block& b, Ref r) {
    return reinterpret_cast<const void*>(static_cast<uintptr_t>(b.ConstValue(r)));
}

}

void X86LowerHostLoad(X86Lowering& l, Ref ref) {
    const Block&   b     = l.GetBlock();
    const Inst&    inst  = b.At(ref);
    X86RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    const uint32_t width = inst.aux16 & kHostWidthMask;
    const bool     sign  = (inst.aux16 & kHostSigned) != 0u;
    const void*    addr  = HostAddress(b, inst.args[0]);
    const uint8_t  reg   = ra.AllocTemp(kX86AllocRegs);
    if (width == 4u && !sign) {
        x86::EmitMovRegDwordPtr(c, reg, addr);
    } else if (width == 1u && sign) {
        x86::EmitMovsxRegBytePtr(c, reg, addr);
    } else {
        throw EmitFault("CerfIR x86 lowering: host load width has no lowering");
    }
    ra.DefineInReg(ref, reg, true);
}

void X86LowerHostStore(X86Lowering& l, Ref ref) {
    const Block&   b     = l.GetBlock();
    const Inst&    inst  = b.At(ref);
    X86RegAlloc&   ra    = l.Regs();
    uint8_t*&      c     = l.Cursor();
    const uint32_t width = inst.aux16 & kHostWidthMask;
    const Ref      value = inst.args[1];
    const void*    addr  = HostAddress(b, inst.args[0]);
    if (width != 1u && width != 4u) {
        throw EmitFault("CerfIR x86 lowering: host store width has no lowering");
    }
    if (IsConstRef(value)) {
        const uint32_t v = static_cast<uint32_t>(b.ConstValue(value));
        if (width == 4u) {
            x86::EmitMovDwordPtrImm32(c, addr, v);
        } else {
            x86::EmitMovBytePtrImm8(c, addr, static_cast<uint8_t>(v));
        }
        return;
    }
    if (width == 4u) {
        x86::EmitMovDwordPtrReg(c, addr, ra.MaterializeWide(value, kX86AllocRegs));
    } else {
        x86::EmitMovBytePtrReg8(c, addr, ra.MaterializeWide(value, kX86ByteRegs));
    }
    ra.Consume(value);
}

void X86LowerHostTest(X86Lowering& l, Ref ref) {
    const Block&   b    = l.GetBlock();
    const Inst&    inst = b.At(ref);
    X86RegAlloc&   ra   = l.Regs();
    uint8_t*&      c    = l.Cursor();
    const void*    addr = HostAddress(b, inst.args[0]);
    const uint32_t mask = static_cast<uint32_t>(b.ConstValue(inst.args[1]));
    ra.BeforeFlagsClobber();
    if (mask <= 0xFFu) {
        x86::EmitTestBytePtrImm8(c, addr, static_cast<uint8_t>(mask));
    } else {
        x86::EmitTestDwordPtrImm32(c, addr, mask);
    }
    l.ClearFlagSource();
    ra.DefineFlags(ref, kX86CondNz);
}

void X86LowerHostReplace(X86Lowering& l, Ref ref) {
    const Block& b     = l.GetBlock();
    const Inst&  inst  = b.At(ref);
    X86RegAlloc& ra    = l.Regs();
    uint8_t*&    c     = l.Cursor();
    const Ref    value = inst.args[1];
    const void*  addr  = HostAddress(b, inst.args[0]);
    ra.BeforeFlagsClobber();
    uint8_t reg;
    if (IsConstRef(value)) {
        reg = ra.AllocTemp(kX86AllocRegs);
        x86::EmitMovRegImm32(c, reg, static_cast<uint32_t>(b.ConstValue(value)));
    } else {
        reg = ra.MaterializeWide(value, kX86AllocRegs);
    }
    x86::EmitCmpRegDwordPtr(c, reg, addr);
    x86::EmitMovDwordPtrReg(c, addr, reg);
    if (IsConstRef(value)) {
        ra.ReleaseTemp(reg);
    } else {
        ra.Consume(value);
    }
    l.ClearFlagSource();
    ra.DefineFlags(ref, kX86CondNz);
}

}
