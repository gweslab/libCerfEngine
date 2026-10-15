#pragma once

#include "../calling_convention.h"
#include <cstdint>

#include "../engine.h"
#include "mips_cpu_state.h"
#include "mips_system_host.h"
#include "mips_tlb_context.h"

namespace cerfengine {

namespace MipsSeg {
    constexpr uint32_t kKusegEnd     = 0x80000000;
    constexpr uint32_t kKseg1Base    = 0xA0000000;
    constexpr uint32_t kKseg2Base    = 0xC0000000;
    constexpr uint32_t kUnmappedMask = 0x1FFFFFFF;

    constexpr bool IsUnmapped(uint32_t va) {
        return va >= kKusegEnd && va < kKseg2Base;
    }
    constexpr uint32_t UnmappedPa(uint32_t va) { return va & kUnmappedMask; }
}

class MipsMmu : public cerfengine::EngineService {
public:
    using cerfengine::EngineService::EngineService;

    void OnReady() override;
    bool ShouldRegister() override {
        return engine_.GetSetup().isa == cerfengine::Isa::kMips;
    }

    void Bind(IsaBlockSpace* blocks32, IsaBlockSpace* blocks16) {
        context_.Bind(state_, blocks32, blocks16);
    }

    MipsTlbResult Translate(MipsCpuState* st, uint32_t va, MipsAccess acc,
                            uint32_t* pa) {
        if (fixed_window_size_ != 0u && va - fixed_window_va_ < fixed_window_size_) {
            *pa = fixed_window_pa_ + (va - fixed_window_va_);
            return MipsTlbResult::kMatch;
        }
        if (MipsSeg::IsUnmapped(va)) {
            *pa = MipsSeg::UnmappedPa(va);
            return MipsTlbResult::kMatch;
        }
        return tlb_.map_address(tlb_.context, st, va, acc, pa);
    }

    bool ExecPageGlobal(MipsCpuState* st, uint32_t va) {
        if (MipsSeg::IsUnmapped(va)) {
            return true;
        }
        return tlb_.page_global(tlb_.context, st, va);
    }

    void SetFixedWindow(uint32_t va, uint32_t pa, uint32_t size) {
        fixed_window_va_   = va;
        fixed_window_pa_   = pa;
        fixed_window_size_ = size;
    }

    cerfengine::MipsTlbContext& TlbContext() { return context_; }

    static void CERFENGINE_FASTCALL TlbWriteIndexedHelper(MipsMmu* mmu);
    static void CERFENGINE_FASTCALL TlbWriteRandomHelper(MipsMmu* mmu);
    static void CERFENGINE_FASTCALL TlbProbeHelper(MipsMmu* mmu);
    static void CERFENGINE_FASTCALL TlbReadHelper(MipsMmu* mmu);

private:
    cerfengine::MipsTlbHooks   tlb_;
    MipsCpuState*              state_ = nullptr;
    cerfengine::MipsTlbContext context_;
    uint32_t fixed_window_va_   = 0;
    uint32_t fixed_window_pa_   = 0;
    uint32_t fixed_window_size_ = 0;
};

}
