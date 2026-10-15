#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/calling_convention.h>

#include <cstring>

#include <cerfengine/byte_order.h>
#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_system_host.h>
#include "arm_tlb_ops.h"
#include "arm_translation_cache.h"
#include <cerfengine/arm/arm_translator.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmMmu);

bool ArmMmu::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

ArmMmu::~ArmMmu() = default;

bool ArmMmu::UnalignedAccessesFault() const {
    return processor_config_->arch_v7 ||
           (processor_config_->arch_v6 &&
            state_.effective_control_register.bits.u);
}

bool ArmMmu::AlignMultiWordOrFault(uint32_t& address, bool is_write) {
    if (!UnalignedAccessesFault() &&
        !state_.effective_control_register.bits.a) {
        address &= 0xFFFFFFFCu;
        return false;
    }
    if ((address & 3u) != 0u) {
        RaiseAlignmentFault(address, is_write);
        return true;
    }
    return false;
}

uint32_t ArmMmu::DoublewordAlignMask() const {
    return (processor_config_->arch_v6 && !processor_config_->arch_v7 &&
            !state_.effective_control_register.bits.u)
               ? 7u : 3u;
}

void ArmMmu::ResetControlRegisters() {
    /* ARM DDI 0100I B3.4.1 (p. B3-12): "Apart from bits that read as 1
       according to this rule, all bits in CP15 register 1 are set to 0 on
       reset." */
    SetControlRegister(0);

    /* ARM DDI 0406C.c B4.1.40 (p. B4-1553): "When implemented as an RW
       field, cpn resets to zero." */
    state_.coprocessor_access = 0;

    /* ARM DDI 0100I B8.3 (p. B8-5): "the PID is initialized to 0b0000000 on
       reset, resulting in the FCSE being effectively disabled." */
    state_.process_id = 0;

    state_.effective_control_register = state_.control_register;
    RefreshFcseFold();

    ArmTlbFlushAll(&state_.data_tlb);
    ArmTlbFlushAll(&state_.instruction_tlb);
}

void ArmMmu::InvalidateAllTlbs() {
    ArmTlbFlushAll(&state_.instruction_tlb);
    ArmTlbFlushAll(&state_.data_tlb);
}

/* ARM DDI 0406C.c D10.1.1 / DDI 0100I B4.2.3 (p. B4-6): the FCSE PID "is SBZ
   when the MMU is disabled", and "Behavior is UNPREDICTABLE if the FCSE PID is
   not zero when the MMU is disabled". */
void ArmMmu::RefreshFcseFold() {
    state_.fcse_fold_id =
        state_.effective_control_register.bits.m ? state_.process_id : 0u;
}

void ArmMmu::SynchronizeSctlr() {
    if (state_.effective_control_register.word == state_.control_register.word) {
        return;
    }
    state_.effective_control_register = state_.control_register;
    RefreshFcseFold();
    InvalidateAllTlbs();
    cache_->InvalidateVaCachesAll();
}

uint8_t* CERFENGINE_FASTCALL ArmMmu::TranslateReadHelper(uint32_t va, ArmMmu* mmu) {
    return mmu->translator_->TranslateRead(va);
}

uint8_t* CERFENGINE_FASTCALL ArmMmu::TranslateWriteHelper(uint32_t va, ArmMmu* mmu) {
    return mmu->translator_->TranslateWrite(va);
}

uint8_t* CERFENGINE_FASTCALL ArmMmu::TranslateReadWriteHelper(uint32_t va, ArmMmu* mmu) {
    return mmu->translator_->TranslateReadWrite(va);
}

uint8_t* CERFENGINE_FASTCALL ArmMmu::TranslateUserReadHelper(uint32_t va, ArmMmu* mmu) {
    return mmu->translator_->TranslateUserRead(va);
}

uint8_t* CERFENGINE_FASTCALL ArmMmu::TranslateUserWriteHelper(uint32_t va, ArmMmu* mmu) {
    return mmu->translator_->TranslateUserWrite(va);
}

void ArmMmu::RegisterControlRegisterListener(std::function<void()> fn) {
    control_register_listeners_.push_back(std::move(fn));
}

void ArmMmu::SetControlRegister(uint32_t value) {
    if (state_.control_register.word == value) return;
    state_.control_register.word = value;
    for (auto& fn : control_register_listeners_) fn();
}

void ArmMmu::SetEffectiveControlRegister(uint32_t value) {
    state_.effective_control_register.word = value;
    RefreshFcseFold();
}

void ArmMmu::OnReady() {
    processor_config_ = &engine_.GetSetup().arm;
    alignment_fault_  = engine_.GetSetup().arm_alignment_fault;
    state_.data_tlb.span_tracker = &data_tlb_span_tracker_;
    state_.instruction_tlb.span_tracker = &instruction_tlb_span_tracker_;

    uint32_t dram_min = 0xFFFFFFFFu;
    uint32_t dram_max = 0u;
    for (const auto& r : engine_.Host().CachedDramRegions()) {
        if (r.pa_base < dram_min)          dram_min = r.pa_base;
        if (r.pa_base + r.size > dram_max) dram_max = r.pa_base + r.size;
    }
    state_.code_word_base         = dram_min;
    state_.code_word_top          = dram_max;
    state_.code_word_bitmap_bytes =
        (dram_max > dram_min) ? (((dram_max - dram_min) >> 2) + 7u) / 8u : 0u;

    code_xlat_bitmap_storage_.assign(state_.code_word_bitmap_bytes, 0u);
    state_.code_xlat_bitmap = code_xlat_bitmap_storage_.data();

    state_.code_page_dirty_bytes =
        (dram_max > dram_min) ? (((dram_max - dram_min) >> 12) + 7u) / 8u : 0u;
    code_page_dirty_storage_.assign(state_.code_page_dirty_bytes, 0u);
    state_.code_page_dirty = code_page_dirty_storage_.data();

    ArmTlbFlushAll(&state_.data_tlb);
    ArmTlbFlushAll(&state_.instruction_tlb);
}

void ArmMmu::RaiseAlignmentFault(uint32_t va, bool is_write) {
    host::ArmAlignmentFault(alignment_fault_, va, is_write);
    ClearIoPending();
}

void CERFENGINE_FASTCALL ArmMmu::AlignmentFaultReadHelper(uint32_t va, ArmMmu* mmu) {
    mmu->RaiseAlignmentFault(va, false);
}

void CERFENGINE_FASTCALL ArmMmu::AlignmentFaultWriteHelper(uint32_t va, ArmMmu* mmu) {
    mmu->RaiseAlignmentFault(va, true);
}

uint32_t CERFENGINE_CDECL ArmMmu::UnalignedHalfwordLoadHelper(ArmMmu* mmu, uint32_t va,
                                                              uint32_t force_user) {
    const bool fu = force_user != 0u;
    uint8_t b[2];
    if (!mmu->AccessPaged(va,      &b[0], 1, true, fu) ||
        !mmu->AccessPaged(va + 1u, &b[1], 1, true, fu)) {
        return 0xFFFFFFFFu;
    }
    return le::U16(b);
}

uint32_t CERFENGINE_CDECL ArmMmu::UnalignedHalfwordStoreHelper(ArmMmu* mmu, uint32_t va,
                                                               uint32_t value,
                                                               uint32_t force_user) {
    const bool fu = force_user != 0u;
    uint8_t b[2];
    le::Put16(b, static_cast<uint16_t>(value));
    if (!mmu->AccessPaged(va,      &b[0], 1, false, fu) ||
        !mmu->AccessPaged(va + 1u, &b[1], 1, false, fu)) {
        return 0xFFFFFFFFu;
    }
    return 0u;
}

uint64_t CERFENGINE_CDECL ArmMmu::UnalignedWordLoadHelper(ArmMmu* mmu, uint32_t va,
                                                          uint32_t force_user) {
    const bool fu = force_user != 0u;
    uint8_t b[4];
    for (uint32_t i = 0; i < 4u; ++i) {
        if (!mmu->AccessPaged(va + i, &b[i], 1, true, fu)) {
            return 0u;
        }
    }
    return (1ull << 32) | le::U32(b);
}

uint32_t CERFENGINE_CDECL ArmMmu::UnalignedWordStoreHelper(ArmMmu* mmu, uint32_t va,
                                                           uint32_t value,
                                                           uint32_t force_user) {
    const bool fu = force_user != 0u;
    uint8_t b[4];
    le::Put32(b, value);
    for (uint32_t i = 0; i < 4u; ++i) {
        if (!mmu->AccessPaged(va + i, &b[i], 1, false, fu)) {
            return 0xFFFFFFFFu;
        }
    }
    return 0u;
}

void ArmMmu::SetIoPending(uint32_t pa) {
    io_pending_address_ = pa;
    io_pending_valid_   = 1u;
}

bool ArmMmu::AccessPaged(uint32_t va, uint8_t* host_buf, uint32_t n, bool is_load,
                         bool force_user, uint32_t* completed) {
    for (uint32_t done = 0; done < n; ) {
        const uint32_t va_cur = va + done;
        uint8_t* host;
        if (force_user) {
            host = is_load ? translator_->TranslateUserRead (va_cur)
                           : translator_->TranslateUserWrite(va_cur);
        } else {
            host = is_load ? translator_->TranslateRead (va_cur)
                           : translator_->TranslateWrite(va_cur);
        }
        if (host == nullptr) {
            if (completed != nullptr) *completed = done;
            return false;
        }
        /* ARM DDI 0406C.c Table D15-10 (p. D15-2609): a Tiny page maps 1 KB. */
        const uint32_t page_left = 0x400u - (va_cur & 0x3FFu);
        const uint32_t chunk = (n - done < page_left) ? (n - done) : page_left;
        if (is_load) std::memcpy(host_buf + done, host, chunk);
        else         std::memcpy(host, host_buf + done, chunk);
        done += chunk;
    }
    return true;
}

}
