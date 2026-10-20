#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/engine.h>

#include "../arm_emit_services.h"
#include "../arm_expand_imm.h"
#include "../arm_psr_write_masks.h"
#include "../arm_status_register_rules.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;
using cerfir::Type;

struct PsrSource {
    bool     is_register;
    uint32_t value;
};

uint32_t SystemModeLabel(ArmIrContext& c) {
    return c.block->emit->ProcessorConfig()->accesses_spsr_in_system_mode ? c.b->NewLabel()
                                                                         : kArmIrNoLabel;
}

void BindLabel(ArmIrContext& c, uint32_t label) {
    c.b->Label(label);
}

Ref ReadSource(ArmIrContext& c, PsrSource source) {
    return source.is_register ? ArmIrReadGpr(c, source.value) : c.b->Const32(source.value);
}

void ReadPsr(ArmIrContext& c) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    ArmEmitServices*   emit = c.block->emit;
    if (d.n != 0u) {
        const uint32_t system_mode = SystemModeLabel(c);
        ArmIrSpsrModeGuard(c, system_mode);
        const cerfir::CallDesc read{reinterpret_cast<const void*>(&ArmCpu::ReadSpsrHelper),
                                    cerfir::CallConv::kCdecl, 1u};
        ArmIrWriteGpr(c, d.rd,
                      b.Call(read, Type::kI32, b.ConstPtr(emit->Cpu()), cerfir::kNoRef,
                             cerfir::kNoRef));
        if (system_mode == kArmIrNoLabel) return;
        const uint32_t done = b.NewLabel();
        b.Branch(b.Const1(true), done, true);
        BindLabel(c, system_mode);
        ArmIrWriteGpr(c, d.rd, b.Const32(0u));
        BindLabel(c, done);
        return;
    }
    const Ref flags = b.GetSlot(Type::kI32, static_cast<uint32_t>(ArmNfDisp()));
    const Ref nzcv  = b.Op2(Op::kAnd, b.Op2(Op::kMul, flags, b.Const32(kNzcvGatherMultiplier)),
                            b.Const32(kCpsrNzcvMask));
    const Ref rest  = b.Op2(Op::kAnd, b.GetSlot(Type::kI32, static_cast<uint32_t>(ArmCpsrDisp())),
                            b.Const32(~kCpsrNzcvMask));
    Ref cpsr = b.Op2(Op::kOr, rest, nzcv);
    if (emit->ProcessorConfig()->arch_v7) {
        cpsr = b.Op2(Op::kAnd, cpsr, b.Const32(kArmV7MrsCpsrReadMask));
    }
    ArmIrWriteGpr(c, d.rd, cpsr);
}

void WritePsr(ArmIrContext& c, PsrSource source) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    ArmEmitServices*   emit = c.block->emit;
    const ArmFeatures& f    = *emit->ProcessorConfig();
    if (d.n != 0u) {
        const uint32_t system_mode = SystemModeLabel(c);
        ArmIrSpsrModeGuard(c, system_mode);
        const cerfir::CallDesc write{
            reinterpret_cast<const void*>(&ArmCpu::WriteSpsrByInstrHelper),
            cerfir::CallConv::kCdecl, 3u};
        b.Call(write, Type::kVoid, b.ConstPtr(emit->Cpu()), ReadSource(c, source),
               b.Const32(ArmSpsrWriteMask(f, d.crn)));
        if (system_mode != kArmIrNoLabel) BindLabel(c, system_mode);
        return;
    }
    const ArmCpsrWriteMasks masks = ArmCpsrWriteMasksFor(f, d.crn);
    cerfir::CallDesc write{reinterpret_cast<const void*>(&ArmCpu::WriteCpsrByInstrHelper),
                           cerfir::CallConv::kCdecl, 3u};
    write.num_extra = 3u;
    write.extra[0]  = masks.priv;
    write.extra[1]  = masks.state_trip;
    if (masks.nmfi_gate) {
        write.extra[2] = cerfir::CallHostWord(&emit->Mmu()->State()->effective_control_register);
    }
    b.Call(write, Type::kVoid, b.ConstPtr(emit->Cpu()), ReadSource(c, source),
           b.Const32(masks.user));
}

}

void ArmIrMrsOrMsr(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.s == 0u) {
        if (ArmMrsUnpredictable(d)) {
            ArmIrUndefined(c);
            return;
        }
        ReadPsr(c);
        return;
    }
    if (ArmMsrRegisterUnpredictable(d, *c.block->emit->ProcessorConfig())) {
        ArmIrUndefined(c);
        return;
    }
    WritePsr(c, PsrSource{true, d.rm});
}

/* ARM DDI 0406C.c B9.3.1 CPS (Thumb) Operation (p. B9-1979):
   "CPSRWriteByInstr(cpsr_val, '1111', FALSE)" under CurrentModeIsNotUser(). */
void ArmIrCps(ArmIrContext& c) {
    const DecodedInsn& d    = *c.d;
    cerfir::Builder&   b    = *c.b;
    ArmEmitServices*   emit = c.block->emit;
    cerfir::CallDesc   write{reinterpret_cast<const void*>(&ArmCpu::WriteCpsrByInstrHelper),
                             cerfir::CallConv::kCdecl, 3u};
    write.num_extra = 3u;
    write.extra[0]  = d.op1;
    write.extra[1]  = 0u;
    if (emit->ProcessorConfig()->arch_v7) {
        write.extra[2] = cerfir::CallHostWord(&emit->Mmu()->State()->effective_control_register);
    }
    b.Call(write, Type::kVoid, b.ConstPtr(emit->Cpu()), b.Const32(d.immediate), b.Const32(0u));
}

void ArmIrMsrImmediate(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (ArmMsrImmediateUnpredictable(d, c.block)) {
        ArmIrUndefined(c);
        return;
    }
    WritePsr(c, PsrSource{false, ArmExpandImm(d.immediate)});
}

}
