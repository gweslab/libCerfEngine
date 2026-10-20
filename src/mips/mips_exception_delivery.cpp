#include <cerfengine/mips/mips_exception_delivery.h>
#include <cerfengine/calling_convention.h>

#include "../host_platform.h"
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>

namespace cerfengine {

CERFENGINE_REGISTER(MipsExceptionDelivery);

void MipsExceptionDelivery::OnReady() {
    cpu_state_ = engine_.Get<MipsCpu>().State();
    hooks_     = engine_.GetSetup().mips_exception;
}

void MipsExceptionDelivery::Unwind() {
    cerfengine::platform::RaiseUnwind(cerfengine::platform::kGuestUnwindCode);
}

void MipsExceptionDelivery::EnterException(uint32_t cause, bool refill_eligible) {
    hooks_.enter(hooks_.context, cpu_state_, cause, refill_eligible);
}

void MipsExceptionDelivery::SetMmuFaultRegs(uint32_t va) {
    hooks_.set_mmu_fault_regs(hooks_.context, cpu_state_, va);
}

void MipsExceptionDelivery::RaiseTlbException(uint32_t va, MipsAccess acc,
                                              MipsTlbResult res) {
    if (res == MipsTlbResult::kAddressError) {
        RaiseAddressError(va, acc);
        return;
    }
    SetMmuFaultRegs(va);

    uint32_t cause = 0;
    bool nomatch = false;
    switch (res) {
        case MipsTlbResult::kNoMatch:
            cause = (acc == MipsAccess::kWrite) ? MipsExcCode::kTLBS : MipsExcCode::kTLBL;
            nomatch = true;
            break;
        case MipsTlbResult::kInvalid:
            cause = (acc == MipsAccess::kWrite) ? MipsExcCode::kTLBS : MipsExcCode::kTLBL;
            break;
        case MipsTlbResult::kModified:
            cause = MipsExcCode::kMod;
            break;
        case MipsTlbResult::kMatch:
        case MipsTlbResult::kAddressError:
            engine_.Panic("MipsExceptionDelivery::RaiseTlbException: result %u is not a "
                          "TLB fault (va=0x%08X)", static_cast<unsigned>(res), va);
    }

    EnterException(cause, nomatch);
    Unwind();
}

void MipsExceptionDelivery::DeliverFetchTlbException(uint32_t va,
                                                     MipsTlbResult res) {
    if (res == MipsTlbResult::kAddressError) {
        DeliverFetchAddressError(va);
        return;
    }
    SetMmuFaultRegs(va);
    EnterException(MipsExcCode::kTLBL, res == MipsTlbResult::kNoMatch);
}

void MipsExceptionDelivery::DeliverFetchAddressError(uint32_t va) {
    /* QEMU MIPS mips_cpu_do_unaligned_access MMU_INST_FETCH -> EXCP_AdEL
       (op_helper.c); cause 4, general vector. */
    cpu_state_->cp0_badvaddr = va;
    EnterException(MipsExcCode::kAdEL, false);
}

void MipsExceptionDelivery::RaiseAddressError(uint32_t va, MipsAccess acc) {
    /* QEMU MIPS mips_cpu_do_unaligned_access (op_helper.c): BadVAddr
       only (no Context / EntryHi), AdES for a store else AdEL. General vector. */
    cpu_state_->cp0_badvaddr = va;
    const uint32_t cause = (acc == MipsAccess::kWrite) ? MipsExcCode::kAdES
                                                       : MipsExcCode::kAdEL;
    EnterException(cause, false);
    Unwind();
}

void MipsExceptionDelivery::RaiseOverflowException() {
    EnterException(MipsExcCode::kOv, false);
    Unwind();
}

/* VR4121 UM Figure 7-7 (p.203) and TMPR39xx-um Figure 6-2 (p.51): Cause CE<29:28> holds
   the coprocessor number of a Coprocessor Unusable exception. */
void MipsExceptionDelivery::RaiseCoprocessorUnusable(uint32_t unit) {
    cpu_state_->cp0_cause = (cpu_state_->cp0_cause & ~(3u << MipsCauseBit::kCE)) |
                            (unit << MipsCauseBit::kCE);
    EnterException(MipsExcCode::kCpU, false);
    Unwind();
}

void MipsExceptionDelivery::LeaveBlock() {
    Unwind();
}

bool MipsExceptionDelivery::InterruptReady() const {
    const MipsCpuState& s = *cpu_state_;
    if (!hooks_.interrupts_enabled(hooks_.context, s)) return false;
    /* pending = (Cause.IP & Status.IM) over bits 8..15 (QEMU MIPS internal.h
       cpu_mips_hw_interrupts_pending). Both cores place IntMask and the Int/Sw
       pending bits at 15:8 (TMPR39xx-um §6.2.3). */
    return (s.cp0_cause & s.cp0_status & 0x0000FF00u) != 0u;
}

void MipsExceptionDelivery::DeliverInterrupt() {
    EnterException(MipsExcCode::kInt, false);
}

void CERFENGINE_FASTCALL MipsExceptionDelivery::SyscallHelper(MipsExceptionDelivery* ex) {
    /* QEMU MIPS SYSCALL -> Sys, cause 8, general vector
       (tlb_helper.c mips_cpu_do_interrupt EXCP_SYSCALL). */
    ex->EnterException(MipsExcCode::kSys, false);
    ex->Unwind();
}

void CERFENGINE_FASTCALL MipsExceptionDelivery::BreakHelper(MipsExceptionDelivery* ex) {
    /* QEMU MIPS BREAK -> Bp, cause 9, general vector
       (tlb_helper.c mips_cpu_do_interrupt EXCP_BREAK). */
    ex->EnterException(MipsExcCode::kBp, false);
    ex->Unwind();
}

/* VR4121 UM §7.4.12 (p.224) and TMPR39xx-um Figure 6-4 CU (p.55): CP0 is usable in Kernel mode
   regardless of CU0; elsewhere a CP0 instruction with CU0 = 0 raises Coprocessor Unusable. */
void CERFENGINE_FASTCALL MipsExceptionDelivery::Cop0UsableHelper(MipsExceptionDelivery* ex) {
    if ((ex->cpu_state_->cp0_status & (1u << MipsStatusBit::kCU0)) != 0u) return;
    ex->RaiseCoprocessorUnusable(0u);
}

void CERFENGINE_FASTCALL MipsExceptionDelivery::CopUnusableHelper(MipsExceptionDelivery* ex,
                                                                  uint32_t unit) {
    ex->RaiseCoprocessorUnusable(unit);
}

void CERFENGINE_CDECL MipsExceptionDelivery::ArithOverflowHelper(MipsExceptionDelivery* ex,
                                                                 uint32_t ) {
    ex->RaiseOverflowException();
}

}
