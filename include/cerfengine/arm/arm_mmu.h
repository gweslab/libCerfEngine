#pragma once

#include "../calling_convention.h"
#include <cstdint>
#include <functional>
#include <vector>

#include "../engine.h"
#include "arm_mmu_state.h"
#include "arm_system_host.h"
#include "cpu_state.h"

namespace cerfengine {

class ArmTranslator;
class ArmTranslationCache;

class ArmMmu : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;
    ~ArmMmu() override;

    void OnReady() override;
    bool ShouldRegister() override;

    ArmMmuState* State() { return &state_; }

    void BindTranslator(ArmTranslator* translator) { translator_ = translator; }
    void BindTranslationCache(ArmTranslationCache* cache) { cache_ = cache; }

    void RegisterControlRegisterListener(std::function<void()> fn);
    void SetControlRegister(uint32_t value);
    void SetEffectiveControlRegister(uint32_t value);

    /* Table A3-1 (p. A3-108) and Table D12-1 (D12.3.1, p. D12-2506) give the
       alignment model by architecture version and SCTLR.U; D15.3.1
       (p. D15-2592) gives the v4/v5 legacy behaviour. */
    bool UnalignedAccessesFault() const {
        return processor_config_->arch_v7 ||
               (processor_config_->arch_v6 && state_.effective_control_register.bits.u);
    }
    uint32_t DoublewordAlignMask()    const;

    /* DDI 0211I Table 4-2 (p. 4-17) puts LDC/LDM/RFE/SRS/STC/STM in the
       Multi-word access type; Table 4-3 (pp. 4-19/4-20) gives A=0 U=0
       Word[Align32(Addr)] "unaffected by Addr[1:0]". */
    bool     AlignMultiWordOrFault(uint32_t& address, bool is_write);

    /* ARM DDI 0406C.c B1.9.1 TakeReset(); B3.15.2 (p. B3-1450) requires a reset value
       for "The SCTLR, CPACR, and TTBCR" and for "The FCSEIDR, if the implementation
       includes the Fast Context Switch Extension". */
    void ResetControlRegisters();

    void InvalidateAllTlbs();
    void InvalidateInstructionTlbs();
    void InvalidateDataTlbs();
    void SynchronizeSctlr();

    static void CERFENGINE_FASTCALL InvalidateAllTlbsHelper(ArmMmu* mmu);

    void RefreshFcseFold();

    static uint8_t* CERFENGINE_FASTCALL TranslateReadHelper(uint32_t va, ArmMmu* mmu);
    static uint8_t* CERFENGINE_FASTCALL TranslateWriteHelper(uint32_t va, ArmMmu* mmu);
    static uint8_t* CERFENGINE_FASTCALL TranslateReadWriteHelper(uint32_t va, ArmMmu* mmu);

    static uint8_t* CERFENGINE_FASTCALL TranslateUserReadHelper(uint32_t va, ArmMmu* mmu);
    static uint8_t* CERFENGINE_FASTCALL TranslateUserWriteHelper(uint32_t va, ArmMmu* mmu);

    /* Word-aligned VFP/NEON multi-byte loads may page-cross (ARM ARM DDI0406C A3.2 Table A3-1). */
    bool AccessPaged(uint32_t va, uint8_t* host_buf, uint32_t n, bool is_load,
                     bool force_user = false, uint32_t* completed = nullptr);

    uint32_t io_pending_address() const { return io_pending_address_; }

    bool io_pending() const { return io_pending_valid_ != 0u; }

    void ClearIoPending() {
        io_pending_valid_   = 0u;
        io_pending_address_ = 0u;
    }

    uint32_t* IoPendingAddressPtr() { return &io_pending_address_; }
    uint32_t* IoPendingValidPtr()   { return &io_pending_valid_; }

    void RaiseAlignmentFault(uint32_t va, bool is_write);

    static void CERFENGINE_FASTCALL AlignmentFaultReadHelper(uint32_t va, ArmMmu* mmu);
    static void CERFENGINE_FASTCALL AlignmentFaultWriteHelper(uint32_t va, ArmMmu* mmu);

    static uint32_t CERFENGINE_CDECL UnalignedHalfwordLoadHelper(ArmMmu* mmu,
                                                                 uint32_t va,
                                                                 uint32_t force_user);
    static uint32_t CERFENGINE_CDECL UnalignedHalfwordStoreHelper(ArmMmu* mmu,
                                                                  uint32_t va,
                                                                  uint32_t value,
                                                                  uint32_t force_user);

    static uint64_t CERFENGINE_CDECL UnalignedWordLoadHelper(ArmMmu* mmu, uint32_t va,
                                                             uint32_t force_user);
    static uint32_t CERFENGINE_CDECL UnalignedWordStoreHelper(ArmMmu* mmu, uint32_t va,
                                                              uint32_t value,
                                                              uint32_t force_user);

    void SetIoPending(uint32_t pa);

private:
    ArmMmuState                        state_{};
    std::vector<std::function<void()>> control_register_listeners_;
    ArmTranslator*                     translator_       = nullptr;
    ArmTranslationCache*               cache_            = nullptr;
    const cerfengine::ArmFeatures*     processor_config_ = nullptr;
    void*                              alignment_fault_  = nullptr;

    uint32_t io_pending_address_ = 0;
    uint32_t io_pending_valid_   = 0;

    std::vector<uint8_t> code_xlat_bitmap_storage_;
    std::vector<uint8_t> code_page_dirty_storage_;
    ArmTlbSpanTracker data_tlb_span_tracker_[kArmTlbPrivilegeCount];
    ArmTlbSpanTracker instruction_tlb_span_tracker_[kArmTlbPrivilegeCount];
};

}
