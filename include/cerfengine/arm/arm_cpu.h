#pragma once

#include "../calling_convention.h"
#include <cstdint>

#include "../engine.h"
#include "cpu_state.h"

namespace cerfengine {

class ArmCpu : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    bool ShouldRegister() override;

    ArmCpuState* State() { return &state_; }

    void OnStateRestored();

    void UpdateCpsrWithFlags(ArmPsrFull psr);

    uint32_t ReturnFromException(uint32_t new_cpsr_word, uint32_t new_pc);

    uint32_t*   BankedSp(uint32_t mode);
    ArmPsrFull* BankedSpsr(uint32_t mode);

    void SetInitialStackPointer(uint32_t sp);
    void SetResetCoprocessorAccess(uint32_t value);

    void SetPendingResumeVector(uint32_t pc);

    void RaiseUndefinedException(uint32_t guest_pc);
    void RaiseAbortDataException(uint32_t guest_pc);
    void RaiseAbortPrefetchException(uint32_t guest_pc);
    void RaiseIrqException(uint32_t guest_pc);
    void RaiseSwiException(uint32_t guest_pc);
    void RaiseResetException(uint32_t initial_pc, bool initial_thumb);
    void RaiseResetException();

    static void CERFENGINE_CDECL RaiseUndefinedExceptionHelper(ArmCpu* cpu, uint32_t guest_pc);
    static void CERFENGINE_CDECL RaiseSwiExceptionHelper(ArmCpu* cpu, uint32_t guest_pc);
    static void CERFENGINE_CDECL UpdateNzcvOnlyHelper(ArmCpu* cpu, uint32_t nzcv_source);

    static void CERFENGINE_CDECL WriteCpsrByInstrHelper(ArmCpu* cpu, uint32_t value,
                                                        uint32_t mask_user,
                                                        uint32_t mask_priv,
                                                        uint32_t state_trip_mask,
                                                        uint32_t sctlr);
    static void CERFENGINE_CDECL WriteSpsrByInstrHelper(ArmCpu* cpu, uint32_t value,
                                                        uint32_t mask);
    static uint32_t CERFENGINE_CDECL ReadSpsrHelper(ArmCpu* cpu);

    static uint32_t CERFENGINE_CDECL ReadUserRegHelper(ArmCpu* cpu, uint32_t reg);
    static void     CERFENGINE_CDECL WriteUserRegHelper(ArmCpu* cpu, uint32_t reg,
                                                        uint32_t value);
    static uint32_t CERFENGINE_CDECL ExceptionReturnHelper(ArmCpu* cpu, uint32_t new_pc);

private:
    ArmBank     SelectBank(uint32_t mode);
    ArmSpsrBank SelectSpsrBank(uint32_t mode);
    void     SwitchModeBanks(uint32_t old_mode, uint32_t new_mode);
    void     EnterException(uint32_t target_mode, uint32_t new_lr_value,
                            uint32_t vect_offset, bool set_async_abort_mask);
    uint32_t ReturnAddress(uint32_t guest_pc, int32_t thumb_sub, int32_t arm_sub);
    uint32_t ExcVectorBase();
    void     ApplySctlrExecutionState();

    ArmCpuState state_{};

    uint32_t pending_resume_pc_        = 0;
    bool     pending_resume_pc_set_    = false;
    uint32_t initial_pc_               = 0;
    bool     initial_thumb_            = false;
    uint32_t initial_sp_               = 0;
    uint32_t reset_coprocessor_access_ = 0;
};

}
