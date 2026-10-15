#pragma once

#include <cerfengine/arm/arm_system_host.h>
#include <cerfengine/byte_order.h>
#include <cerfengine/calling_convention.h>
#include <cerfengine/clock_host.h>
#include <cerfengine/engine.h>
#include <cerfengine/engine_host.h>
#include <cerfengine/engine_setup.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_system_host.h>
#include <cerfengine/mmio_bus_host.h>
#include <cerfengine/physical_memory_host.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

[[noreturn]] inline void TestFail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::printf("TEST FAILED: ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
    std::fflush(stdout);
    std::exit(1);
}

#define TEST_CHECK(condition)                                                   \
    do {                                                                        \
        if (!(condition)) TestFail("%s:%d: %s", __FILE__, __LINE__, #condition); \
    } while (0)

[[noreturn]] inline void TestUnexpectedWalk(uint32_t mva) {
    TestFail("unexpected ARM translation of 0x%08X with the MMU off", mva);
}

class TestHost : public cerfengine::EngineHost, public cerfengine::ClockHost {
public:
    TestHost(uint32_t ram_base, uint32_t ram_size, uint32_t mmio_pa)
        : ram_base_(ram_base), ram_(ram_size, 0), mmio_pa_(mmio_pa) {}

    void Attach(cerfengine::Setup& setup) {
        setup.host                  = this;
        setup.memory                = this;
        setup.mmio.context          = this;
        setup.mmio.generation       = &generation_;
        setup.report_block_compiles = true;
    }

    void LoadWords(uint32_t pa, const uint32_t* words, size_t count) {
        TEST_CHECK(pa >= ram_base_ && pa - ram_base_ + count * 4u <= ram_.size());
        uint8_t* out = ram_.data() + (pa - ram_base_);
        for (size_t i = 0; i < count; ++i) cerfengine::le::Put32(out + i * 4u, words[i]);
    }

    bool     MmioWritten() const { return mmio_written_; }
    uint64_t MmioValue() const { return mmio_value_; }
    uint32_t BlocksCompiled() const { return blocks_compiled_; }

    void     IdleStep(void*) override { TestFail("the guest entered an idle wait"); }
    void     WaitStep(void*) override { TestFail("the guest clock stopped"); }
    void     ExitIdle() override {}
    uint64_t Cycles() override { return 0; }

    std::vector<cerfengine::DramRegion> CachedDramRegions() override {
        return {cerfengine::DramRegion{ram_base_, static_cast<uint32_t>(ram_.size())}};
    }

    void OnResetRequested(bool) override { TestFail("the guest requested a reset"); }
    void OnRebootRequested() override { TestFail("the guest requested a reboot"); }
    void OnResetDelivered() override {}
    void OnPowerDownRequest() override { TestFail("the guest requested power down"); }
    void OnBlockCompiled() override { ++blocks_compiled_; }

    bool HasPcTrace(uint32_t) override { return false; }
    void OnArmPcTrace(uint32_t pc, const uint32_t*, uint32_t) override {
        TestFail("unexpected ARM PC trace at 0x%08X", pc);
    }
    void OnMipsPcTrace(uint32_t pc, const cerfengine::MipsCpuState*) override {
        TestFail("unexpected MIPS PC trace at 0x%08X", pc);
    }

    [[noreturn]] void Panic(const char* message) override {
        TestFail("engine panic: %s", message);
    }
    void Log(cerfengine::LogChannel, const char* text) override { std::fputs(text, stdout); }
    bool Symbolize(const void*, char*, size_t) override { return false; }

    static uint8_t* PhysicalTranslate(void* context, uint32_t pa, cerfengine::PhysicalPage& page) {
        TestHost*      self    = static_cast<TestHost*>(context);
        const uint32_t page_pa = pa & ~0xFFFu;
        if (uint8_t* first = self->Backing(page_pa);
            first != nullptr && self->Backing(page_pa + 0xFFFu)) {
            page.read  = first;
            page.write = first;
        }
        return self->Backing(pa);
    }

    static bool PhysicalSpanUniform(void* context, uint32_t pa, uint32_t span) {
        const TestHost* self = static_cast<TestHost*>(context);
        return pa >= self->ram_base_ && span <= self->ram_.size() &&
               pa - self->ram_base_ <= self->ram_.size() - span;
    }

    static bool MmioFindRoute(void* context, uint32_t pa, cerfengine::MmioRoute& route) {
        TestHost* self = static_cast<TestHost*>(context);
        if (pa < self->mmio_pa_ || pa - self->mmio_pa_ > 3u) return false;
        route.base    = self->mmio_pa_;
        route.last    = self->mmio_pa_ + 3u;
        route.read    = &RouteRead;
        route.write   = &RouteWrite;
        route.context = self;
        return true;
    }

private:
    static uint32_t CERFENGINE_CDECL RouteRead(void*, uint32_t offset, uint32_t) {
        TestFail("unexpected MMIO read at offset 0x%X", offset);
    }

    static void CERFENGINE_CDECL RouteWrite(void* context, uint32_t offset, uint32_t value,
                                            uint32_t width_bytes) {
        auto* self = static_cast<TestHost*>(context);
        if (offset != 0u || width_bytes != 4u) {
            TestFail("unexpected MMIO write at offset 0x%X width %u", offset, width_bytes);
        }
        self->mmio_written_ = true;
        self->mmio_value_   = value;
    }

    uint8_t* Backing(uint32_t pa) {
        if (pa < ram_base_ || pa - ram_base_ >= ram_.size()) return nullptr;
        return ram_.data() + (pa - ram_base_);
    }

    uint32_t              ram_base_;
    std::vector<uint8_t>  ram_;
    uint32_t              mmio_pa_;
    std::atomic<uint32_t> generation_{0};
    bool                  mmio_written_    = false;
    uint64_t              mmio_value_      = 0;
    uint32_t              blocks_compiled_ = 0;
};

class TestArmSystem : public cerfengine::ArmSystemHost {
public:
    void Attach(cerfengine::Setup& setup) {
        setup.arm_translation     = this;
        setup.arm_alignment_fault = this;
        setup.arm_system          = this;
    }

    cerfengine::ArmCoprocDecision ArmCoprocessor(const cerfengine::ArmCoprocAccess& a) override {
        TestFail("unexpected coprocessor access cp%u", a.cp_num);
    }
    cerfengine::ArmResetResult ArmReset() override { return {}; }
};

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
