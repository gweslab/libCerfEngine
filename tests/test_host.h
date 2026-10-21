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

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <utility>
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
        : ram_base_(ram_base), ram_(ram_size, 0), data_(ram_.data()), mmio_pa_(mmio_pa) {}

    void MoveRamTo(uint8_t* backing) {
        std::memcpy(backing, data_, ram_.size());
        data_ = backing;
    }

    void Attach(cerfengine::Setup& setup) {
        setup.host                  = this;
        setup.memory                = this;
        setup.mmio.context          = this;
        setup.mmio.generation       = &generation_;
        setup.report_block_compiles = true;
    }

    void LoadWords(uint32_t pa, const uint32_t* words, size_t count) {
        TEST_CHECK(pa >= ram_base_ && pa - ram_base_ + count * 4u <= ram_.size());
        uint8_t* out = data_ + (pa - ram_base_);
        for (size_t i = 0; i < count; ++i) cerfengine::le::Put32(out + i * 4u, words[i]);
    }

    struct MmioAccess {
        uint32_t pa;
        uint32_t value;
        uint32_t width;
        uint32_t cycles;
        bool     write;

        bool operator==(const MmioAccess&) const = default;
    };

    void ObserveCycles(const uint32_t* counter) { observed_cycles_ = counter; }
    void EnableMmioLog() { mmio_log_mode_ = true; }
    void RaiseExitOnMmio(uint32_t* word, uint32_t bit) {
        exit_word_ = word;
        exit_bit_  = bit;
    }
    void RaiseIrqOnMmio(std::function<void()> raise) { raise_irq_ = std::move(raise); }
    void OnClockDispatch(std::function<void()> dispatch) { clock_dispatch_ = std::move(dispatch); }
    void DispatchClock() {
        if (clock_dispatch_) clock_dispatch_();
    }
    void ResetMmioLog() {
        mmio_log_.clear();
        mmio_reads_ = 0u;
    }
    const std::vector<MmioAccess>& MmioLog() const { return mmio_log_; }
    const std::vector<uint8_t>& Ram() const {
        TEST_CHECK(data_ == ram_.data());
        return ram_;
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
    void OnBlockCompiled(const cerfengine::BlockCompileStats& stats) override {
        ++blocks_compiled_;
        guest_insns_ += stats.guest_insns;
        host_bytes_ += stats.host_bytes;
        compile_ns_ += stats.compile_ns;
    }

    uint64_t GuestInsnsCompiled() const { return guest_insns_; }
    uint64_t HostBytesEmitted() const { return host_bytes_; }
    uint64_t CompileNs() const { return compile_ns_; }

    void     TracePc(uint32_t pc) { traced_pcs_.push_back(pc); }
    uint32_t TraceHits() const { return trace_hits_; }
    void     ResetTraceHits() { trace_hits_ = 0u; }

    bool HasPcTrace(uint32_t pc) override {
        return std::find(traced_pcs_.begin(), traced_pcs_.end(), pc) != traced_pcs_.end();
    }
    bool HasPcTraceIn(uint32_t first, uint32_t last) override {
        return std::any_of(traced_pcs_.begin(), traced_pcs_.end(),
                           [first, last](uint32_t pc) { return pc >= first && pc <= last; });
    }
    void OnArmPcTrace(uint32_t pc, const uint32_t*, uint32_t) override { CountTrace(pc); }
    void OnMipsPcTrace(uint32_t pc, const cerfengine::MipsCpuState*) override { CountTrace(pc); }

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

    static uint64_t MmioReadDword(void* context, uint32_t pa) {
        auto* self = static_cast<TestHost*>(context);
        if (!self->mmio_log_mode_) TestFail("unexpected 64-bit MMIO read at PA 0x%08X", pa);
        const uint64_t lo = self->LogReadAt(pa, 8u);
        return lo | (static_cast<uint64_t>(self->LogReadAt(pa + 4u, 8u)) << 32);
    }

    static void MmioWriteDword(void* context, uint32_t pa, uint64_t value) {
        auto* self = static_cast<TestHost*>(context);
        if (!self->mmio_log_mode_) TestFail("unexpected 64-bit MMIO write at PA 0x%08X", pa);
        self->LogWriteAt(pa, static_cast<uint32_t>(value), 8u);
        self->LogWriteAt(pa + 4u, static_cast<uint32_t>(value >> 32), 8u);
    }

    static bool MmioFindRoute(void* context, uint32_t pa, cerfengine::MmioRoute& route) {
        TestHost* self = static_cast<TestHost*>(context);
        if (self->mmio_log_mode_) {
            const uint32_t ram_end = self->ram_base_ + static_cast<uint32_t>(self->ram_.size());
            if (pa >= self->ram_base_ && pa < ram_end) {
                TestFail("unexpected MMIO route request at RAM PA 0x%08X", pa);
            }
            if (pa < self->ram_base_) {
                route.base    = 0u;
                route.last    = self->ram_base_ - 1u;
                route.read    = &LogReadLow;
                route.write   = &LogWriteLow;
                route.context = self;
                return true;
            }
            route.base    = ram_end;
            route.last    = 0xFFFFFFFFu;
            route.read    = &LogRead;
            route.write   = &LogWrite;
            route.context = self;
            return true;
        }
        if (pa < self->mmio_pa_ || pa - self->mmio_pa_ > 3u) return false;
        route.base    = self->mmio_pa_;
        route.last    = self->mmio_pa_ + 3u;
        route.read    = &RouteRead;
        route.write   = &RouteWrite;
        route.context = self;
        return true;
    }

private:
    void CountTrace(uint32_t pc) {
        if (!HasPcTrace(pc)) TestFail("unexpected PC trace at 0x%08X", pc);
        ++trace_hits_;
    }

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

    static uint32_t CERFENGINE_CDECL LogRead(void* context, uint32_t offset, uint32_t width) {
        auto* self = static_cast<TestHost*>(context);
        return self->LogReadAt(self->ram_base_ + static_cast<uint32_t>(self->ram_.size()) + offset,
                               width);
    }

    static void CERFENGINE_CDECL LogWrite(void* context, uint32_t offset, uint32_t value,
                                          uint32_t width) {
        auto* self = static_cast<TestHost*>(context);
        self->LogWriteAt(self->ram_base_ + static_cast<uint32_t>(self->ram_.size()) + offset,
                         value, width);
    }

    static uint32_t CERFENGINE_CDECL LogReadLow(void* context, uint32_t offset, uint32_t width) {
        return static_cast<TestHost*>(context)->LogReadAt(offset, width);
    }

    static void CERFENGINE_CDECL LogWriteLow(void* context, uint32_t offset, uint32_t value,
                                             uint32_t width) {
        static_cast<TestHost*>(context)->LogWriteAt(offset, value, width);
    }

    uint32_t LogReadAt(uint32_t pa, uint32_t width) {
        const uint32_t value = 0x3000u + (pa * 0x9E3779B1u + mmio_reads_ * 0x85EBCA6Bu) % 0x6000u;
        ++mmio_reads_;
        mmio_log_.push_back(MmioAccess{pa, value, width, *observed_cycles_, false});
        RaiseExitAt(pa);
        return value;
    }

    void LogWriteAt(uint32_t pa, uint32_t value, uint32_t width) {
        mmio_log_.push_back(MmioAccess{pa, value, width, *observed_cycles_, true});
        RaiseExitAt(pa);
    }

    void RaiseExitAt(uint32_t pa) {
        if (exit_word_ != nullptr && (pa & 0x1Cu) == 0x08u) *exit_word_ |= exit_bit_;
        if (raise_irq_) raise_irq_();
    }

    uint8_t* Backing(uint32_t pa) {
        if (pa < ram_base_ || pa - ram_base_ >= ram_.size()) return nullptr;
        return data_ + (pa - ram_base_);
    }

    uint32_t              ram_base_;
    std::vector<uint8_t>  ram_;
    uint8_t*              data_;
    uint32_t              mmio_pa_;
    std::atomic<uint32_t> generation_{0};
    bool                  mmio_written_    = false;
    uint64_t              mmio_value_      = 0;
    bool                  mmio_log_mode_   = false;
    const uint32_t*       observed_cycles_ = nullptr;
    uint32_t*             exit_word_       = nullptr;
    uint32_t              exit_bit_        = 0;
    std::function<void()> raise_irq_;
    std::function<void()> clock_dispatch_;
    uint32_t              mmio_reads_      = 0;
    std::vector<MmioAccess> mmio_log_;
    uint32_t              blocks_compiled_ = 0;
    uint64_t              guest_insns_     = 0;
    uint64_t              host_bytes_      = 0;
    uint64_t              compile_ns_      = 0;
    std::vector<uint32_t> traced_pcs_;
    uint32_t              trace_hits_      = 0;
};

class TestCoprocessor {
public:
    virtual ~TestCoprocessor() = default;
    virtual cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& access) = 0;
};

class TestArmSystem : public cerfengine::ArmSystemHost {
public:
    void Attach(cerfengine::Setup& setup) {
        setup.arm_translation     = this;
        setup.arm_alignment_fault = this;
        setup.arm_system          = this;
    }

    void SetCoprocessor(TestCoprocessor* coprocessor) { coprocessor_ = coprocessor; }

    cerfengine::ArmCoprocDecision ArmCoprocessor(const cerfengine::ArmCoprocAccess& a) override {
        if (coprocessor_ == nullptr) TestFail("unexpected coprocessor access cp%u", a.cp_num);
        return coprocessor_->Decide(a);
    }
    cerfengine::ArmResetResult ArmReset() override { return {}; }

    void AllowAlignmentFaults() { allow_alignment_faults_ = true; }
    void RecordAlignmentFault(uint32_t va, bool write) {
        if (!allow_alignment_faults_) TestFail("unexpected alignment fault at 0x%08X", va);
        ++alignment_faults_;
        last_fault_va_    = va;
        last_fault_write_ = write;
    }
    uint32_t AlignmentFaults() const { return alignment_faults_; }
    uint32_t LastFaultVa() const { return last_fault_va_; }
    bool     LastFaultWrite() const { return last_fault_write_; }

    void EnableIdentityWalk(uint32_t hole_base, uint32_t hole_size) {
        walk_enabled_ = true;
        hole_base_    = hole_base;
        hole_size_    = hole_size;
    }

    void EnablePrivilegedRegion(uint32_t base, uint32_t size) {
        privileged_base_ = base;
        privileged_size_ = size;
    }

    void SetPagesGlobal(bool global) { pages_global_ = global; }

    void RemapPage(uint32_t va_page, uint32_t pa_page) {
        remap_va_ = va_page;
        remap_pa_ = pa_page;
    }

    cerfengine::ArmTranslation Walk(uint32_t mva, bool write, bool user) {
        if (!walk_enabled_) TestUnexpectedWalk(mva);
        cerfengine::ArmTranslation t;
        if (mva - hole_base_ < hole_size_ || (user && mva - privileged_base_ < privileged_size_)) {
            ++walk_faults_;
            last_walk_fault_va_    = mva;
            last_walk_fault_write_ = write;
            return t;
        }
        t.mapped = true;
        t.pa     = MappedPa(mva);
        t.global = pages_global_;
        return t;
    }

    bool Probe(uint32_t mva, uint32_t* pa) {
        if (!walk_enabled_) TestFail("unexpected ARM probe translation of 0x%08X", mva);
        if (mva - hole_base_ < hole_size_) return false;
        *pa = MappedPa(mva);
        return true;
    }

    uint32_t MappedPa(uint32_t mva) const {
        return (mva & ~0xFFFu) == remap_va_ ? remap_pa_ | (mva & 0xFFFu) : mva;
    }

    uint32_t WalkFaults() const { return walk_faults_; }
    uint32_t LastWalkFaultVa() const { return last_walk_fault_va_; }
    bool     LastWalkFaultWrite() const { return last_walk_fault_write_; }

private:
    TestCoprocessor* coprocessor_ = nullptr;

    bool     allow_alignment_faults_ = false;
    uint32_t alignment_faults_       = 0;
    uint32_t last_fault_va_          = 0;
    bool     last_fault_write_       = false;
    bool     walk_enabled_           = false;
    uint32_t hole_base_              = 0;
    uint32_t hole_size_              = 0;
    uint32_t privileged_base_        = 0;
    uint32_t privileged_size_        = 0;
    bool     pages_global_           = true;
    uint32_t remap_va_               = 0xFFFFFFFFu;
    uint32_t remap_pa_               = 0;
    uint32_t walk_faults_            = 0;
    uint32_t last_walk_fault_va_     = 0;
    bool     last_walk_fault_write_  = false;
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
