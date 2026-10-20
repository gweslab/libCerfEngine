#include "mips_ir_fns.h"

#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_exception_delivery.h>
#include <cerfengine/mips/mips_interrupt_channel.h>
#include <cerfengine/mips/mips_mmu.h>

#include "../mips_block_compiler.h"
#include "../mips_emit_services.h"
#include "../mips_opcode.h"

namespace cerfengine {

namespace {

using cerfir::kNoRef;
using cerfir::Type;

void MmuOp(MipsIrContext& c, void (CERFENGINE_FASTCALL* helper)(MipsMmu*)) {
    MipsIrFastcallConst(c, reinterpret_cast<const void*>(helper), 1u,
                        cerfir::CallHostAddress(c.block->emit->Mmu()), 0u);
}

void StateHook(MipsIrContext& c, MipsStateHook hook) {
    const MipsExceptionHooks& hooks = c.block->emit->Owner().GetSetup().mips_exception;
    MipsIrFastcallConst(c, reinterpret_cast<const void*>(hook), 2u,
                        cerfir::CallHostAddress(hooks.context),
                        cerfir::CallHostAddress(c.block->emit->CpuState()));
}

void Exception(MipsIrContext& c, void (CERFENGINE_FASTCALL* helper)(MipsExceptionDelivery*)) {
    MipsIrFastcallConst(c, reinterpret_cast<const void*>(helper), 1u,
                        cerfir::CallHostAddress(c.block->emit->Exceptions()), 0u);
}

}

void MipsIrUndefined(MipsIrContext& c) {
    MipsIrCdeclConst(c, reinterpret_cast<const void*>(&MipsBlockCompiler::UnimplementedHelper), 3u,
                     cerfir::CallHostAddress(c.block->compiler), c.d->guest_address, c.d->raw);
    if (c.d->ends_block != 0u) c.b->ExitDispatcher();
}

void MipsIrTlbwi(MipsIrContext& c) { MmuOp(c, &MipsMmu::TlbWriteIndexedHelper); }
void MipsIrTlbwr(MipsIrContext& c) { MmuOp(c, &MipsMmu::TlbWriteRandomHelper); }
void MipsIrTlbp(MipsIrContext& c) { MmuOp(c, &MipsMmu::TlbProbeHelper); }
void MipsIrTlbr(MipsIrContext& c) { MmuOp(c, &MipsMmu::TlbReadHelper); }

void MipsIrRfe(MipsIrContext& c) {
    StateHook(c, c.block->emit->Owner().GetSetup().mips_exception.rfe);
}

void MipsIrEret(MipsIrContext& c) {
    StateHook(c, c.block->emit->Owner().GetSetup().mips_exception.eret);
    c.b->ExitDispatcher();
}

void MipsIrHibernate(MipsIrContext& c) {
    MipsIrFastcallConst(c, reinterpret_cast<const void*>(&MipsCpu::HibernateHelper), 2u,
                        c.d->guest_address + 4u, cerfir::CallHostAddress(c.block->emit->Cpu()));
    c.b->ExitDispatcher();
}

void MipsIrWait(MipsIrContext& c) {
    const MipsDecodedInsn& d = *c.d;
    const void* helper = d.funct == MipsCop0Funct::kSUSPEND
                             ? reinterpret_cast<const void*>(&MipsInterruptChannel::SuspendInsnHelper)
                             : reinterpret_cast<const void*>(&MipsInterruptChannel::StandbyInsnHelper);
    MipsIrFastcallConst(c, helper, 2u, cerfir::CallHostAddress(c.block->emit->InterruptChannel()),
                        d.guest_address + d.length);
    c.b->ExitDispatcher();
}

void MipsIrCopUnusable(MipsIrContext& c) {
    MipsIrFastcallConst(c, reinterpret_cast<const void*>(&MipsExceptionDelivery::CopUnusableHelper),
                        2u, cerfir::CallHostAddress(c.block->emit->Exceptions()), c.d->op & 3u);
}

void MipsIrBreak(MipsIrContext& c) { Exception(c, &MipsExceptionDelivery::BreakHelper); }
void MipsIrSyscall(MipsIrContext& c) { Exception(c, &MipsExceptionDelivery::SyscallHelper); }

}
