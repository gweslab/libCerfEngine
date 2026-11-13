#pragma once

#include "test_host.h"

#include <cerfengine/calling_convention.h>
#include <cerfengine/engine_setup.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_system_host.h>

#include <cstdint>

class TestMipsSystem : public cerfengine::MipsSystemHost {
public:
    explicit TestMipsSystem(uint32_t entry) : entry_(entry) {}

    void Attach(cerfengine::Setup& setup) {
        setup.mips_tlb.context                  = this;
        setup.mips_tlb.map_address              = &MapAddress;
        setup.mips_tlb.page_global              = &PageGlobal;
        setup.mips_tlb.write_indexed            = &WriteIndexed;
        setup.mips_tlb.write_random             = &WriteRandom;
        setup.mips_tlb.probe                    = &Probe;
        setup.mips_tlb.read                     = &Read;
        setup.mips_exception.context            = this;
        setup.mips_exception.enter              = &EnterException;
        setup.mips_exception.set_mmu_fault_regs = &SetMmuFaultRegs;
        setup.mips_exception.interrupts_enabled = &InterruptsEnabled;
        setup.mips_exception.eret               = &Eret;
        setup.mips_exception.rfe                = &Rfe;
        setup.mips_system                       = this;
    }

    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd) override {
        TestFail("unexpected CP0 register %u", rd);
    }
    void MipsReset(cerfengine::MipsCpuState* st) override {
        st->min_page_shift = 12;
        st->pc             = entry_;
    }

private:
    static cerfengine::MipsTlbResult CERFENGINE_FASTCALL MapAddress(
        void*, cerfengine::MipsCpuState*, uint32_t va, cerfengine::MipsAccess, uint32_t*) {
        TestFail("unexpected mapped MIPS access at 0x%08X", va);
    }
    static bool CERFENGINE_FASTCALL PageGlobal(void*, cerfengine::MipsCpuState*, uint32_t va) {
        TestFail("unexpected mapped MIPS page query at 0x%08X", va);
    }
    static void CERFENGINE_FASTCALL WriteIndexed(void*, cerfengine::MipsTlbContext&) {
        TestFail("TLBWI");
    }
    static void CERFENGINE_FASTCALL WriteRandom(void*, cerfengine::MipsTlbContext&) {
        TestFail("TLBWR");
    }
    static void CERFENGINE_FASTCALL Probe(void*, cerfengine::MipsTlbContext&) {
        TestFail("TLBP");
    }
    static void CERFENGINE_FASTCALL Read(void*, cerfengine::MipsTlbContext&) {
        TestFail("TLBR");
    }

    static void CERFENGINE_FASTCALL EnterException(void*, cerfengine::MipsCpuState* st,
                                                   uint32_t cause, bool) {
        TestFail("unexpected MIPS exception %u at 0x%08X", cause, st->pc);
    }
    static void CERFENGINE_FASTCALL SetMmuFaultRegs(void*, cerfengine::MipsCpuState*,
                                                    uint32_t va) {
        TestFail("unexpected MIPS MMU fault at 0x%08X", va);
    }
    static bool CERFENGINE_FASTCALL InterruptsEnabled(void*, const cerfengine::MipsCpuState&) {
        return false;
    }

    static void CERFENGINE_FASTCALL Eret(void*, cerfengine::MipsCpuState*) { TestFail("ERET"); }
    static void CERFENGINE_FASTCALL Rfe(void*, cerfengine::MipsCpuState*) { TestFail("RFE"); }

    uint32_t entry_;
};
