#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/arm/block_context.h>

#include "../arm_emit_services.h"
#include "arm_ir_fns.h"

namespace cerfengine {

namespace {

using cerfir::Op;
using cerfir::Ref;

/* ARM DDI 0406C.c A2.3.2 (p. A2-47) BranchWritePC: address<31:2>:'00' in
   ARM state, address<31:1>:'0' otherwise. */
void BranchWritePc(ArmIrContext& c, Ref address) {
    cerfir::Builder& b = *c.b;
    const uint32_t   mask = c.block->thumb ? 0xFFFFFFFEu : 0xFFFFFFFCu;
    ArmIrWriteGpr(c, ArmGpr::kR15, b.Op2(Op::kAnd, address, b.Const32(mask)));
}

}

/* BXWritePC (DDI 0406C A2.3.2, p. A2-47): bit0==1 -> Thumb at
   address<31:1>:'0'; else bit1==0 -> ARM; else UNPREDICTABLE. */
void ArmIrBxWritePc(ArmIrContext& c, Ref address) {
    cerfir::Builder& b         = *c.b;
    const uint32_t   cpsr_disp = static_cast<uint32_t>(ArmCpsrDisp());
    const uint32_t   pc_disp   = static_cast<uint32_t>(ArmGprDisp(ArmGpr::kR15));
    const Ref        cpsr      = b.GetSlot(cerfir::Type::kI32, cpsr_disp);
    if (cerfir::IsConstRef(address)) {
        const uint32_t target = static_cast<uint32_t>(b.GetBlock().ConstValue(address));
        const bool     thumb  = (target & 1u) != 0u;
        b.SetSlot(cpsr_disp, thumb ? b.Op2(Op::kOr, cpsr, b.Const32(kArmCpsrT))
                                   : b.Op2(Op::kAnd, cpsr, b.Const32(~kArmCpsrT)));
        b.SetSlot(pc_disp, b.Const32(target & (thumb ? 0xFFFFFFFEu : 0xFFFFFFFCu)));
        return;
    }
    b.Dup(address);
    b.Dup(cpsr);
    const Ref thumb = b.Compare(Op::kCmpNe, b.Op2(Op::kAnd, address, b.Const32(1u)), b.Const32(0u));
    b.Dup(thumb);
    b.SetSlot(cpsr_disp, b.Op3(Op::kSelect, thumb, b.Op2(Op::kOr, cpsr, b.Const32(kArmCpsrT)),
                               b.Op2(Op::kAnd, cpsr, b.Const32(~kArmCpsrT))));
    b.SetSlot(pc_disp, b.Op2(Op::kAnd, address,
                             b.Op3(Op::kSelect, thumb, b.Const32(0xFFFFFFFEu),
                                   b.Const32(0xFFFFFFFCu))));
}

void ArmIrFinishPcWrite(ArmIrContext& c) {
    const DecodedInsn& d = *c.d;
    if (d.itstate_after_valid != 0u && d.is_exception_return == 0u) {
        ArmIrStoreItState(c, d.itstate_after);
    }
    if (d.is_exception_return == 0u) {
        c.b->ExitIndirect();
    } else {
        c.b->ExitDispatcher();
    }
}

/* A2.3.2 (p. A2-48) LoadWritePC: BXWritePC from ArchVersion() >= 5, else
   BranchWritePC. */
void ArmIrLoadWritePc(ArmIrContext& c, Ref address) {
    if (c.block->emit->ProcessorConfig()->load_to_pc_interworking) {
        ArmIrBxWritePc(c, address);
    } else {
        BranchWritePc(c, address);
    }
    ArmIrFinishPcWrite(c);
}

/* A2.3.2 (p. A2-48) ALUWritePC: BXWritePC from ArchVersion() >= 7 in ARM
   state, else BranchWritePC. */
void ArmIrAluWritePc(ArmIrContext& c, Ref address) {
    if (!c.block->thumb && c.block->emit->ProcessorConfig()->data_proc_to_pc_interworking) {
        ArmIrBxWritePc(c, address);
    } else {
        BranchWritePc(c, address);
    }
    ArmIrFinishPcWrite(c);
}

/* B9.3.20 (p. B9-2013): CPSRWriteByInstr(SPSR[], '1111', TRUE), then
   BranchWritePC(result). */
void ArmIrExceptionReturn(ArmIrContext& c, Ref address) {
    cerfir::Builder&       b = *c.b;
    const cerfir::CallDesc ret{reinterpret_cast<const void*>(&ArmCpu::ExceptionReturnHelper),
                               cerfir::CallConv::kCdecl, 2u};
    const Ref new_pc = b.Call(ret, cerfir::Type::kI32, b.ConstPtr(c.block->emit->Cpu()), address,
                              cerfir::kNoRef);
    ArmIrWriteGpr(c, ArmGpr::kR15, new_pc);
    ArmIrFinishPcWrite(c);
}

/* B9.3.20 (p. B9-2013): UNPREDICTABLE in User and System mode. */
void ArmIrSpsrModeGuard(ArmIrContext& c, uint32_t system_label) {
    cerfir::Builder& b    = *c.b;
    const uint32_t   und  = b.NewLabel();
    const uint32_t   ok   = b.NewLabel();
    const Ref        mode = b.Op2(Op::kAnd,
                                  b.GetSlot(cerfir::Type::kI32, static_cast<uint32_t>(ArmCpsrDisp())),
                                  b.Const32(kArmCpsrModeMask));
    b.Dup(mode);
    b.Branch(b.Op2(Op::kCmpEq, mode, b.Const32(ArmMode::kUser)), und, true);
    if (system_label == kArmIrNoLabel) {
        b.Branch(b.Op2(Op::kCmpNe, mode, b.Const32(ArmMode::kSystem)), ok, true);
    } else {
        b.Branch(b.Op2(Op::kCmpEq, mode, b.Const32(ArmMode::kSystem)), system_label, true);
        b.Branch(b.Const1(true), ok, true);
    }
    b.Label(und);
    ArmIrRaiseUndefined(c);
    b.Label(ok);
}

}
