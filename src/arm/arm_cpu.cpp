#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/calling_convention.h>

#include <atomic>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_system_host.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmCpu);

namespace {

constexpr uint32_t kBankIdx(ArmBank b)     { return static_cast<uint32_t>(b); }
constexpr uint32_t kSpsrIdx(ArmSpsrBank b) { return static_cast<uint32_t>(b); }

}

/* ARM ARM DDI 0406C.c B1.3.2, p. B1-1145: RBankSelect() '11111' System selects
   the usr bank. Monitor and Hyp are extension-only (Table B1-1, p. B1-1139) and
   every other encoding is reserved. */
ArmBank ArmCpu::SelectBank(uint32_t mode) {
    switch (mode) {
    case ArmMode::kUser:
    case ArmMode::kSystem:     return ArmBank::kUsr;
    case ArmMode::kFiq:        return ArmBank::kFiq;
    case ArmMode::kIrq:        return ArmBank::kIrq;
    case ArmMode::kSupervisor: return ArmBank::kSvc;
    case ArmMode::kAbort:      return ArmBank::kAbt;
    case ArmMode::kUndefined:  return ArmBank::kUnd;
    default:
        engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                     "ArmCpu: bank for CPSR.M=0x%02X unmodelled", mode);
    }
}

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1152: SPSR[] is "otherwise UNPREDICTABLE"
   outside the seven modes it enumerates, so User and System have none. */
ArmSpsrBank ArmCpu::SelectSpsrBank(uint32_t mode) {
    switch (mode) {
    case ArmMode::kFiq:        return ArmSpsrBank::kFiq;
    case ArmMode::kIrq:        return ArmSpsrBank::kIrq;
    case ArmMode::kSupervisor: return ArmSpsrBank::kSvc;
    case ArmMode::kAbort:      return ArmSpsrBank::kAbt;
    case ArmMode::kUndefined:  return ArmSpsrBank::kUnd;
    default:
        engine_.Halt(cerfengine::HaltReason::kUnpredictable,
                     "ArmCpu: SPSR access in CPSR.M=0x%02X is UNPREDICTABLE", mode);
    }
}

bool ArmCpu::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

/* ARM ARM DDI 0406C.c B1.3.2, p. B1-1143: the application-level registers are
   "selected from a larger set of registers, that includes Banked copies of some
   registers, with the current register selected by the execution mode". */
void ArmCpu::SwitchModeBanks(uint32_t old_mode, uint32_t new_mode) {
    if (old_mode == new_mode) {
        return;
    }

    const ArmBank old_bank = SelectBank(old_mode);
    const ArmBank new_bank = SelectBank(new_mode);
    if (old_bank == new_bank) {
        return;
    }

    state_.sp_bank[kBankIdx(old_bank)] = state_.gprs[ArmGpr::kR13];
    state_.gprs[ArmGpr::kR13]          = state_.sp_bank[kBankIdx(new_bank)];

    state_.lr_bank[kBankIdx(old_bank)] = state_.gprs[ArmGpr::kR14];
    state_.gprs[ArmGpr::kR14]          = state_.lr_bank[kBankIdx(new_bank)];

    /* ARM ARM DDI 0406C.c Figure B1-2, p. B1-1144: only FIQ banks R8-R12. */
    const bool old_fiq = (old_mode == ArmMode::kFiq);
    const bool new_fiq = (new_mode == ArmMode::kFiq);
    if (old_fiq != new_fiq) {
        for (uint32_t i = 0; i < 5u; ++i) {
            const uint32_t live = state_.gprs[8u + i];
            state_.gprs[8u + i]   = state_.r8_r12_fiq[i];
            state_.r8_r12_fiq[i]  = live;
        }
    }
}

uint32_t* ArmCpu::BankedSp(uint32_t mode) {
    const ArmBank bank = SelectBank(mode);
    if (bank == SelectBank(state_.cpsr.bits.mode)) {
        return &state_.gprs[ArmGpr::kR13];
    }
    return &state_.sp_bank[kBankIdx(bank)];
}

ArmPsrFull* ArmCpu::BankedSpsr(uint32_t mode) {
    return &state_.spsr_bank[kSpsrIdx(SelectSpsrBank(mode))];
}

void ArmCpu::UpdateCpsrWithFlags(ArmPsrFull psr) {
    if (psr.bits.thumb_mode && !engine_.GetSetup().arm.thumb) {
        engine_.Halt(cerfengine::HaltReason::kUnpredictable,
                     "ArmCpu: CPSR write sets T on a core that reports no Thumb state");
    }

    const uint32_t old_mode = state_.cpsr.bits.mode;

    ArmApplyCpsr(state_, psr.word);
    SwitchModeBanks(old_mode, state_.cpsr.bits.mode);
}

/* ARM ARM DDI 0406C.c B1.9, pp. B1-1206 (Undefined Instruction), B1-1210 (SVC),
   B1-1213 (Prefetch Abort), B1-1215 (Data Abort), B1-1219 (IRQ): SPSR[] = old
   CPSR, R[14] = the exception's return value, CPSR.M = its mode, CPSR.I = '1'. */
void ArmCpu::EnterException(uint32_t target_mode,
                            uint32_t new_lr_value,
                            uint32_t vect_offset,
                            bool     set_async_abort_mask) {
    /* ARM DDI 0406C.c B3.15.5 (p. B3-1461): "the explicit synchronization
       occurs as the first step of any Context synchronization operation". */
    engine_.Get<ArmMmu>().SynchronizeSctlr();

    ArmPsrFull old_cpsr;
    old_cpsr.word = ArmPackCpsr(state_);
    const uint32_t old_mode = old_cpsr.bits.mode;

    state_.cpsr.bits.mode = target_mode;
    SwitchModeBanks(old_mode, target_mode);

    state_.spsr_bank[kSpsrIdx(SelectSpsrBank(target_mode))] = old_cpsr;
    state_.gprs[ArmGpr::kR14] = new_lr_value;

    state_.cpsr.bits.irq_disable = 1u;
    state_.cpsr.bits.it_low      = 0u;
    state_.cpsr.bits.it_high     = 0u;
    state_.cpsr.bits.jazelle     = 0u;
    if (set_async_abort_mask) {
        state_.cpsr.bits.async_abort_disable = 1u;
    }
    ApplySctlrExecutionState();
    state_.gprs[ArmGpr::kR15] = ExcVectorBase() + vect_offset;
}

void ArmCpu::RaiseUndefinedException(uint32_t guest_pc) {
    EnterException(ArmMode::kUndefined, ReturnAddress(guest_pc, 2u, 4u), 4u, false);
}

/* ARM ARM DDI 0406C.c TakeSVCException() (p. B1-1211): "SPSR is to be the
   current CPSR, after changing the IT[] bits to give them the correct values
   for the following instruction". */
void ArmCpu::RaiseSwiException(uint32_t guest_pc) {
    ArmItStoreToCpsr(state_, ArmItAdvance(ArmItFromCpsr(state_)));
    EnterException(ArmMode::kSupervisor, ReturnAddress(guest_pc, 2u, 4u), 8u, false);
}

void ArmCpu::RaiseAbortPrefetchException(uint32_t guest_pc) {
    EnterException(ArmMode::kAbort, ReturnAddress(guest_pc, 0u, 4u), 12u, true);
}

void ArmCpu::RaiseIrqException(uint32_t guest_pc) {
    EnterException(ArmMode::kIrq, ReturnAddress(guest_pc, 0u, 4u), 24u, true);
}

/* ARM ARM DDI 0406C.c A2.3, p. A2-45: "When executing an ARM instruction, PC
   reads as the address of the current instruction plus 8"; Thumb reads plus 4. */
uint32_t ArmCpu::ReturnAddress(uint32_t guest_pc, int32_t thumb_sub, int32_t arm_sub) {
    return state_.cpsr.bits.thumb_mode
               ? static_cast<uint32_t>(static_cast<int32_t>(guest_pc + 4u) - thumb_sub)
               : static_cast<uint32_t>(static_cast<int32_t>(guest_pc + 8u) - arm_sub);
}

/* ARM ARM DDI 0406C.c B1.8.1 ExcVectorBase(): SCTLR.V=1 -> 0xFFFF0000
   (Hivecs); else HaveSecurityExt() -> VBAR, else 0. */
uint32_t ArmCpu::ExcVectorBase() {
    return engine_.Get<ArmMmu>().State()->effective_control_register.bits.v ? 0xFFFF0000u
                                                                             : 0u;
}

/* ARM ARM DDI 0406C.c B1.9.1 TakeReset(): "CPSR.J = '0'; CPSR.T = SCTLR.TE"
   and "CPSR.E = SCTLR.EE". TE exists on ARMv6T2/ARMv7 only, EE from ARMv6
   (D12.7.4); ARMv4/v5 SCTLR bits[31:16] are Reserved UNK/SBZP (D15.7). */
void ArmCpu::ApplySctlrExecutionState() {
    const ArmSctlr sctlr = engine_.Get<ArmMmu>().State()->effective_control_register;
    const cerfengine::ArmFeatures& config = engine_.GetSetup().arm;
    state_.cpsr.bits.thumb_mode = config.arch_v7 ? sctlr.bits.te : 0u;
    state_.cpsr.bits.endian     = config.arch_v6 ? sctlr.bits.ee : 0u;
}

void ArmCpu::RaiseAbortDataException(uint32_t guest_pc) {
    EnterException(ArmMode::kAbort, ReturnAddress(guest_pc, -4, 0), 16u, true);
}

/* ARM ARM DDI 0406C.c B1.9.1 TakeReset(): CPSR.M = '10011' Supervisor, then
   CPSR.I = '1'; CPSR.F = '1'; CPSR.A = '1'. */
void ArmCpu::RaiseResetException(uint32_t initial_pc, bool initial_thumb) {
    initial_pc_    = initial_pc;
    initial_thumb_ = initial_thumb;

    const uint32_t old_mode = state_.cpsr.bits.mode;
    state_.cpsr.bits.mode   = ArmMode::kSupervisor;
    SwitchModeBanks(old_mode, ArmMode::kSupervisor);

    /* ARM ARM DDI 0406C.c B1.9.1 TakeReset(): "if HaveAdvSIMDorVFP() then
       FPEXC.EN = '0'". */
    state_.fpexc = 0u;

    state_.cpsr.bits.irq_disable         = 1u;
    state_.cpsr.bits.fiq_disable         = 1u;
    state_.cpsr.bits.async_abort_disable = 1u;
    state_.cpsr.bits.it_low              = 0u;
    state_.cpsr.bits.it_high             = 0u;
    state_.cpsr.bits.jazelle             = 0u;

    ArmMmu& mmu = engine_.Get<ArmMmu>();
    mmu.ResetControlRegisters();
    mmu.State()->coprocessor_access = reset_coprocessor_access_;
    const cerfengine::ArmResetResult reset = engine_.GetSetup().arm_system->ArmReset();
    if (reset.resume_mmu) {
        mmu.SetControlRegister(reset.control_register);
        mmu.SetEffectiveControlRegister(reset.control_register);
    }

    state_.deep_sleep    = 0u;
    state_.reset_pending = 0u;
    state_.idle_wait     = 0u;
    std::atomic_ref<uint32_t>(state_.chain_exit_request)
        .fetch_and(~kChainExitIdle, std::memory_order_acq_rel);

    ApplySctlrExecutionState();

    if (pending_resume_pc_set_) {
        state_.gprs[ArmGpr::kR15] = pending_resume_pc_;
        pending_resume_pc_set_    = false;
    } else {
        state_.cpsr.bits.thumb_mode = initial_thumb_ ? 1u : 0u;
        state_.gprs[ArmGpr::kR13]   = initial_sp_;
        state_.gprs[ArmGpr::kR15]   = initial_pc;
    }
}

void ArmCpu::RaiseResetException() {
    RaiseResetException(initial_pc_, initial_thumb_);
}

void ArmCpu::SetInitialStackPointer(uint32_t sp) {
    initial_sp_ = sp;
}

void ArmCpu::SetResetCoprocessorAccess(uint32_t value) {
    reset_coprocessor_access_ = value;
    engine_.Get<ArmMmu>().State()->coprocessor_access = value;
}

void ArmCpu::SetPendingResumeVector(uint32_t pc) {
    pending_resume_pc_     = pc;
    pending_resume_pc_set_ = true;
}

void CERFENGINE_CDECL ArmCpu::RaiseUndefinedExceptionHelper(ArmCpu* cpu, uint32_t guest_pc) {
    cpu->RaiseUndefinedException(guest_pc);
}

void CERFENGINE_CDECL ArmCpu::RaiseSwiExceptionHelper(ArmCpu* cpu, uint32_t guest_pc) {
    cpu->RaiseSwiException(guest_pc);
}

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1148: condition flags N[31] Z[30] C[29]
   V[28]. */
void CERFENGINE_CDECL ArmCpu::UpdateNzcvOnlyHelper(ArmCpu* cpu, uint32_t nzcv_source) {
    cpu->state_.nf = static_cast<uint8_t>((nzcv_source >> 31) & 1u);
    cpu->state_.zf = static_cast<uint8_t>((nzcv_source >> 30) & 1u);
    cpu->state_.cf = static_cast<uint8_t>((nzcv_source >> 29) & 1u);
    cpu->state_.vf = static_cast<uint8_t>((nzcv_source >> 28) & 1u);
}

/* ARM DDI 0100I A4.1.39 (p. A4-77): mask = byte_mask AND UserMask in User
   mode, AND (UserMask OR PrivMask) privileged; ARM DDI 0406C.c
   CPSRWriteByInstr (p. B1-1153): SCTLR.NMFI gates setting F. */
void CERFENGINE_CDECL ArmCpu::WriteCpsrByInstrHelper(ArmCpu* cpu, uint32_t value,
                                                     uint32_t mask_user,
                                                     uint32_t mask_priv,
                                                     uint32_t state_trip_mask,
                                                     uint32_t sctlr) {
    const bool privileged = cpu->state_.cpsr.bits.mode != ArmMode::kUser;
    if (privileged && (value & state_trip_mask) != 0u) {
        cpu->engine_.Halt(cerfengine::HaltReason::kUnpredictable,
                          "ArmCpu: privileged MSR CPSR with execution-state bits "
                          "set (value=0x%08X StateMask=0x%08X) is UNPREDICTABLE",
                          value, state_trip_mask);
    }
    uint32_t mask = privileged ? mask_priv : mask_user;
    if ((sctlr & (1u << 27)) != 0u && (value & (1u << 6)) != 0u) {
        mask &= ~(1u << 6);
    }
    ArmPsrFull merged;
    merged.word = (ArmPackCpsr(cpu->state_) & ~mask) | (value & mask);
    if (merged.bits.endian != 0u) {
        cpu->engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                          "ArmCpu: MSR sets CPSR.E; big-endian data accesses are "
                          "not modelled");
    }
    cpu->UpdateCpsrWithFlags(merged);
}

/* ARM DDI 0100I A4.1.39 (p. A4-77); ARM DDI 0406C.c SPSRWriteByInstr
   (p. B1-1154). */
void CERFENGINE_CDECL ArmCpu::WriteSpsrByInstrHelper(ArmCpu* cpu, uint32_t value,
                                                     uint32_t mask) {
    ArmPsrFull* spsr = cpu->BankedSpsr(cpu->state_.cpsr.bits.mode);
    spsr->word = (spsr->word & ~mask) | (value & mask);
}

uint32_t CERFENGINE_CDECL ArmCpu::ReadSpsrHelper(ArmCpu* cpu) {
    return cpu->BankedSpsr(cpu->state_.cpsr.bits.mode)->word;
}

/* ARM DDI 0406C.c B9.3.6/B9.3.17 (pp. B9-1989/B9-2008): Rmode[i, '10000']
   selects the User-mode instance; Figure B1-2 (p. B1-1144): FIQ banks
   R8-R12, and SP/LR bank per mode with System sharing the usr bank. */
uint32_t CERFENGINE_CDECL ArmCpu::ReadUserRegHelper(ArmCpu* cpu, uint32_t reg) {
    ArmCpuState* st = &cpu->state_;
    if (reg >= 8u && reg <= 12u && st->cpsr.bits.mode == ArmMode::kFiq) {
        return st->r8_r12_fiq[reg - 8u];
    }
    if (reg == ArmGpr::kR13) {
        return *cpu->BankedSp(ArmMode::kUser);
    }
    if (reg == ArmGpr::kR14) {
        if (cpu->SelectBank(st->cpsr.bits.mode) == ArmBank::kUsr) {
            return st->gprs[ArmGpr::kR14];
        }
        return st->lr_bank[kBankIdx(ArmBank::kUsr)];
    }
    return st->gprs[reg];
}

void CERFENGINE_CDECL ArmCpu::WriteUserRegHelper(ArmCpu* cpu, uint32_t reg,
                                                 uint32_t value) {
    ArmCpuState* st = &cpu->state_;
    if (reg >= 8u && reg <= 12u && st->cpsr.bits.mode == ArmMode::kFiq) {
        st->r8_r12_fiq[reg - 8u] = value;
        return;
    }
    if (reg == ArmGpr::kR13) {
        *cpu->BankedSp(ArmMode::kUser) = value;
        return;
    }
    if (reg == ArmGpr::kR14) {
        if (cpu->SelectBank(st->cpsr.bits.mode) == ArmBank::kUsr) {
            st->gprs[ArmGpr::kR14] = value;
        } else {
            st->lr_bank[kBankIdx(ArmBank::kUsr)] = value;
        }
        return;
    }
    st->gprs[reg] = value;
}

uint32_t ArmCpu::ReturnFromException(uint32_t new_cpsr_word, uint32_t new_pc) {
    /* ARM DDI 0406C.c B3.15.5 (p. B3-1461) + Glossary "Context
       synchronization operation": returning from an exception synchronizes
       as its first step. */
    engine_.Get<ArmMmu>().SynchronizeSctlr();

    ArmPsrFull source;
    source.word = new_cpsr_word;
    ArmPsrFull merged = source;
    /* CPSRWriteByInstr bytemask '1111', is_excpt_return TRUE (p. B1-1153):
       23:20 SBZP, SCTLR.NMFI gates F. Pre-v7 whole copy - ARM DDI 0100I
       A4.1.22 (p. A4-41). */
    if (engine_.GetSetup().arm.arch_v7) {
        uint32_t mask = 0xFF0FFFFFu;
        if (engine_.Get<ArmMmu>().State()->effective_control_register.bits.nmfi &&
            source.bits.fiq_disable) {
            mask &= ~(1u << 6);
        }
        merged.word = (ArmPackCpsr(state_) & ~mask) | (source.word & mask);
    }
    if (merged.bits.endian != 0u) {
        engine_.Halt(cerfengine::HaltReason::kUnimplemented,
                     "ArmCpu: exception return restores CPSR.E; big-endian "
                     "data accesses are not modelled");
    }
    UpdateCpsrWithFlags(merged);
    /* BranchWritePC (ARM DDI 0406C.c A2.3.2, p. A2-47). */
    return merged.bits.thumb_mode ? (new_pc & ~1u) : (new_pc & ~3u);
}

/* ARM DDI 0406C.c B9.3.5 (p. B9-1987): CPSRWriteByInstr(SPSR[], '1111',
   TRUE) then BranchWritePC(new_pc_value). */
uint32_t CERFENGINE_CDECL ArmCpu::ExceptionReturnHelper(ArmCpu* cpu, uint32_t new_pc) {
    const uint32_t spsr = cpu->BankedSpsr(cpu->state_.cpsr.bits.mode)->word;
    return cpu->ReturnFromException(spsr, new_pc);
}

void ArmCpu::OnStateRestored() {
    std::atomic_ref<uint32_t>(state_.chain_exit_request)
        .store((state_.reset_pending != 0u ? kChainExitReset : 0u) |
                   (state_.idle_wait != 0u ? kChainExitIdle : 0u),
               std::memory_order_release);
}

}
