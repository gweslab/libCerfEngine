#pragma once

#include <cerfengine/byte_order.h>
#include <cerfengine/calling_convention.h>
#include <cerfengine/clock_host.h>
#include <cerfengine/engine.h>
#include <cerfengine/engine_host.h>
#include <cerfengine/engine_setup.h>
#include <cerfengine/host_exception.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mmio_bus_host.h>
#include <cerfengine/physical_memory_host.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
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

struct HostThrow {
    uint32_t value;
};

[[noreturn]] inline void ThrowFromHost(uint32_t value) {
    cerfengine::RaiseHostFailure(std::make_exception_ptr(HostThrow{value}));
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
    void OnMmioWrite(std::function<void(uint32_t)> on_write) { on_write_ = std::move(on_write); }
    void OnPanic(std::function<void(const char*)> on_panic) { on_panic_ = std::move(on_panic); }
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

    void     AcceptResets() { accept_resets_ = true; }
    uint32_t ResetRequests() const { return reset_requests_; }
    uint32_t RebootRequests() const { return reboot_requests_; }
    uint32_t ResetsDelivered() const { return resets_delivered_; }

    void OnResetRequested(bool) override {
        if (!accept_resets_) TestFail("the guest requested a reset");
        ++reset_requests_;
    }
    void OnRebootRequested() override {
        if (!accept_resets_) TestFail("the guest requested a reboot");
        ++reboot_requests_;
    }
    void OnResetDelivered() override { ++resets_delivered_; }
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
    void OnShPcTrace(uint32_t pc, const cerfengine::ShCpuState*) override { CountTrace(pc); }

    [[noreturn]] void Panic(const char* message) override {
        if (on_panic_) on_panic_(message);
        TestFail("engine panic: %s", message);
    }
    void Log(cerfengine::LogChannel, const char* text) override { std::fputs(text, stdout); }
    bool Symbolize(const void*, char*, size_t) override { return false; }

    void MakeRamReadOnly() { ram_read_only_ = true; }
    void MirrorRamAt(uint32_t pa) {
        mirror_base_ = pa;
        mirrored_    = true;
    }

    static uint8_t* PhysicalTranslate(void* context, uint32_t pa, bool write, cerfengine::PhysicalPage& page) {
        TestHost*      self    = static_cast<TestHost*>(context);
        const uint32_t page_pa = pa & ~0xFFFu;
        if (uint8_t* first = self->Backing(page_pa);
            first != nullptr && self->Backing(page_pa + 0xFFFu)) {
            page.read  = first;
            page.write = self->ram_read_only_ ? nullptr : first;
        }
        if (write && self->ram_read_only_) return nullptr;
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
        if (self->on_write_) self->on_write_(value);
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
        if (pa >= ram_base_ && pa - ram_base_ < ram_.size()) return data_ + (pa - ram_base_);
        if (mirrored_ && pa >= mirror_base_ && pa - mirror_base_ < ram_.size()) return data_ + (pa - mirror_base_);
        return nullptr;
    }

    uint32_t              ram_base_;
    std::vector<uint8_t>  ram_;
    uint8_t*              data_;
    uint32_t              mmio_pa_;
    uint32_t              mirror_base_   = 0;
    bool                  mirrored_      = false;
    bool                  ram_read_only_ = false;
    std::atomic<uint32_t> generation_{0};
    bool                  mmio_written_    = false;
    uint64_t              mmio_value_      = 0;
    bool                  mmio_log_mode_   = false;
    std::function<void(uint32_t)> on_write_;
    const uint32_t*       observed_cycles_ = nullptr;
    uint32_t*             exit_word_       = nullptr;
    uint32_t              exit_bit_        = 0;
    std::function<void()> raise_irq_;
    std::function<void()> clock_dispatch_;
    std::function<void(const char*)> on_panic_;
    uint32_t              mmio_reads_      = 0;
    std::vector<MmioAccess> mmio_log_;
    uint32_t              blocks_compiled_ = 0;
    uint64_t              guest_insns_     = 0;
    uint64_t              host_bytes_      = 0;
    uint64_t              compile_ns_      = 0;
    std::vector<uint32_t> traced_pcs_;
    uint32_t              trace_hits_      = 0;
    bool                  accept_resets_    = false;
    uint32_t              reset_requests_   = 0;
    uint32_t              reboot_requests_  = 0;
    uint32_t              resets_delivered_ = 0;
};
