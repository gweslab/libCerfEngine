#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/calling_convention.h>

#include <cerfengine/mips/mips_cpu.h>

namespace cerfengine {

CERFENGINE_REGISTER(MipsMmu);

void MipsMmu::OnReady() {
    tlb_   = engine_.GetSetup().mips_tlb;
    state_ = engine_.Get<MipsCpu>().State();
    LeaveErlMode();
    if (engine_.GetSetup().mips.isa_level != MipsIsaLevel::kMips1) erl_mask_ = 1u << MipsStatusBit::kERL;
    ArmErlMap();
}

void MipsMmu::ArmErlMap() {
    if (erl_mask_ == 0u) return;
    map_         = &ErlMapAddress;
    map_context_ = this;
}

void MipsMmu::LeaveErlMode() {
    map_         = tlb_.map_address;
    map_context_ = tlb_.context;
}

MipsTlbResult CERFENGINE_FASTCALL MipsMmu::ErlMapAddress(void* context, MipsCpuState* st,
                                                         uint32_t va, MipsAccess acc,
                                                         uint32_t* pa) {
    MipsMmu* const mmu = static_cast<MipsMmu*>(context);
    if ((st->cp0_status & mmu->erl_mask_) == 0u) {
        mmu->LeaveErlMode();
    } else if (va < MipsSeg::kKusegEnd) {
        if (acc == MipsAccess::kFetch) mmu->ErlKusegFetch(va);
        *pa = va & st->phys_addr_mask;
        return MipsTlbResult::kMatch;
    }
    return mmu->tlb_.map_address(mmu->tlb_.context, st, va, acc, pa);
}

void MipsMmu::ErlKusegFetch(uint32_t va) {
    engine_.Halt(HaltReason::kUnimplemented,
                 "MipsMmu: instruction fetch from kuseg VA 0x%08X with Status.ERL set is not modeled", va);
}

MipsTlbResult MipsMmu::TranslateUnprivileged(uint32_t privilege, MipsCpuState* st, uint32_t va,
                                             MipsAccess acc, uint32_t* pa) {
    if (privilege == kBlockUser) return Translate<kBlockUser>(st, va, acc, pa);
    return Translate<kBlockSupervisor>(st, va, acc, pa);
}

void CERFENGINE_FASTCALL MipsMmu::ErlSetHelper(MipsMmu* mmu) {
    mmu->ArmErlMap();
    mmu->context_.InvalidateAddressSpace();
}

void CERFENGINE_FASTCALL MipsMmu::TlbWriteIndexedHelper(MipsMmu* mmu) {
    mmu->tlb_.write_indexed(mmu->tlb_.context, mmu->context_);
}

void CERFENGINE_FASTCALL MipsMmu::TlbWriteRandomHelper(MipsMmu* mmu) {
    mmu->tlb_.write_random(mmu->tlb_.context, mmu->context_);
}

void CERFENGINE_FASTCALL MipsMmu::TlbProbeHelper(MipsMmu* mmu) {
    mmu->tlb_.probe(mmu->tlb_.context, mmu->context_);
}

void CERFENGINE_FASTCALL MipsMmu::TlbReadHelper(MipsMmu* mmu) {
    mmu->tlb_.read(mmu->tlb_.context, mmu->context_);
}

}
