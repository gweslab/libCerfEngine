#include <cerfengine/mips/mips_mmu.h>
#include <cerfengine/calling_convention.h>

#include <cerfengine/mips/mips_cpu.h>

namespace cerfengine {

CERFENGINE_REGISTER(MipsMmu);

void MipsMmu::OnReady() {
    tlb_   = engine_.GetSetup().mips_tlb;
    state_ = engine_.Get<MipsCpu>().State();
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
