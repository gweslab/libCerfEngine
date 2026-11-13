#pragma once

#include "test_host.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/host_request_channel.h>
#include <cerfengine/sh/sh_bus_master_coherence.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_interrupt_channel.h>
#include <cerfengine/sh/sh_mmu.h>

#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

constexpr uint32_t kShRigRamPa   = 0x00000000u;
constexpr uint32_t kShRigRamSize = 0x00020000u;
constexpr uint32_t kShRigMmioPa  = 0x10000000u;
constexpr uint32_t kShRigVaBase  = 0xA0000000u;
constexpr uint32_t kShRigPageSize = 0x400u;
constexpr uint32_t kShRigPageMask = kShRigPageSize - 1u;

constexpr uint16_t kShRigNop        = 0x0009u;
constexpr uint16_t kShRigBraNextNop = 0xA000u;
constexpr uint16_t kShRigAddOneR9   = 0x7901u;
constexpr uint16_t kShRigJmpR13     = 0x4D2Bu;
constexpr uint16_t kShRigRte        = 0x002Bu;
constexpr uint16_t kShRigRts        = 0x000Bu;
constexpr uint16_t kShRigJsrR1      = 0x410Bu;
constexpr uint16_t kShRigJsrR3      = 0x430Bu;
constexpr uint16_t kShRigLoadLR1R2  = 0x6212u;
constexpr uint16_t kShRigSleep      = 0x001Bu;

constexpr uint32_t kShRigReturnStubBytes = 4u;
constexpr uint32_t kShRigVbr             = 0xA0010000u;
constexpr uint32_t kShRigGeneralVector   = 0x100u;
constexpr uint32_t kShRigInterruptVector = 0x600u;
constexpr uint32_t kShRigDataPa          = 0x00018000u;
constexpr uint32_t kShRigDataVa          = kShRigVaBase + kShRigDataPa;
constexpr uint32_t kShRigVpnMask         = 0xFFFFFC00u;
constexpr uint8_t  kShRigAsid            = 0x2Au;

constexpr uint32_t kMmucrAt      = 0x001u;
constexpr uint32_t kMmucrIx      = 0x002u;
constexpr uint32_t kMmucrTf      = 0x004u;
constexpr uint32_t kMmucrSv      = 0x100u;
constexpr uint32_t kMmucrRcShift = 4u;
constexpr uint32_t kMmucrRc      = 3u << kMmucrRcShift;
constexpr uint32_t kMmucr620lx   = 0x0Du;

constexpr uint32_t kPtelV    = 0x100u;
constexpr uint32_t kPtelPrRw = 0x020u;
constexpr uint32_t kPtelSz   = 0x010u;
constexpr uint32_t kPtelD    = 0x004u;
constexpr uint32_t kPtelSh   = 0x002u;
constexpr uint32_t kPage4kRw = kPtelV | kPtelPrRw | kPtelSz | kPtelD;
constexpr uint32_t kPage1kRw = kPtelV | kPtelPrRw | kPtelD;

constexpr uint32_t kShRigExpevtAddressRead         = 0x0E0u;
constexpr uint32_t kShRigExpevtAddressWrite        = 0x100u;
constexpr uint32_t kShRigExpevtReservedInstruction = 0x180u;
constexpr uint32_t kShRigExpevtIllegalSlot         = 0x1A0u;

constexpr uint32_t kShRigSrBase =
    cerfengine::kShSrMd | cerfengine::kShSrRb | cerfengine::kShSrBl | cerfengine::kShSrImask;
constexpr uint32_t kShRigSrPrivileged = cerfengine::kShSrMd | cerfengine::kShSrRb | cerfengine::kShSrImask;

constexpr uint32_t ShRigPa(uint32_t va) { return va - kShRigVaBase + kShRigRamPa; }

constexpr uint32_t ShRigSignExtend(uint32_t value, uint32_t width) {
    if (width == 1u) return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(value)));
    if (width == 2u) return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
    return value;
}

inline void ShRigExpect(const char* mode, const char* what, const char* field, uint32_t got, uint32_t want) {
    if (got != want) TestFail("%s %s: %s = 0x%08X, want 0x%08X", mode, what, field, got, want);
}

inline void ShRigExpectHalt(const char* mode, const char* what, const std::string& detail, const std::string& text) {
    if (detail.find(text) == std::string::npos) {
        TestFail("%s %s: halt \"%s\", want \"%s\"", mode, what, detail.c_str(), text.c_str());
    }
}

inline cerfengine::ShCpuState ShRigSeededState(std::mt19937& rng) {
    cerfengine::ShCpuState s{};
    for (uint32_t r = 0; r < cerfengine::kShNumGpr; ++r) s.gpr[r] = rng();
    s.sr   = kShRigSrBase | ((rng() & 1u) != 0u ? cerfengine::kShSrT : 0u);
    s.gbr  = rng();
    s.mach = rng();
    s.macl = rng();
    return s;
}

inline void ShRigCheckState(const cerfengine::ShCpuState& got, const cerfengine::ShCpuState& want, const char* mode,
                            const char* what, uint16_t op) {
    for (uint32_t r = 0; r < cerfengine::kShNumGpr; ++r) {
        if (got.gpr[r] != want.gpr[r]) {
            TestFail("%s %s %04X: R%u = %08X, want %08X", mode, what, op, r, got.gpr[r], want.gpr[r]);
        }
    }
    if (got.sr != want.sr || got.gbr != want.gbr || got.mach != want.mach || got.macl != want.macl) {
        TestFail("%s %s %04X: SR %08X GBR %08X MACH %08X MACL %08X, want SR %08X GBR %08X MACH %08X MACL %08X", mode,
                 what, op, got.sr, got.gbr, got.mach, got.macl, want.sr, want.gbr, want.mach, want.macl);
    }
}

class ShIrRig {
public:
    ShIrRig(bool dsp, bool verify, bool writable_ram = false) : host_(kShRigRamPa, kShRigRamSize, kShRigMmioPa) {
        setup_.isa       = cerfengine::Isa::kSh;
        setup_.sh.dsp    = dsp;
        setup_.verify_ir = verify;
        if (!writable_ram) host_.MakeRamReadOnly();
        host_.Attach(setup_);
        engine_ = std::make_unique<cerfengine::Engine>(setup_);
        std::string error;
        if (!engine_->Start(error)) TestFail("Start: %s", error.c_str());
        engine_->BindClock(host_);
        cpu_ = &engine_->Get<cerfengine::ShCpu>();
        host_.ObserveCycles(&cpu_->State()->guest_cycle_counter);
        guest_ = &engine_->Get<cerfengine::GuestEngine>();
        guest_->SetHostChainExit(true);
    }

    void LoadCode(uint32_t pa, std::vector<uint16_t> code) {
        if ((code.size() & 1u) != 0u) code.push_back(kShRigNop);
        std::vector<uint32_t> words(code.size() / 2u, 0u);
        for (size_t i = 0; i < code.size(); ++i) {
            words[i / 2u] |= static_cast<uint32_t>(code[i]) << ((i & 1u) * 16u);
        }
        host_.LoadWords(pa, words.data(), words.size());
    }

    void ResetMmu(uint32_t mode, uint8_t asid) {
        cerfengine::ShMmu& mmu = Mmu();
        mmu.WriteMmucr(kMmucrAt | kMmucrTf | mode);
        mmu.WritePteh(asid);
    }

    void Preload(uint32_t way, uint32_t vpn, uint8_t asid, uint32_t ptel) {
        cerfengine::ShMmu& mmu = Mmu();
        mmu.WriteMmucr((mmu.ReadMmucr() & ~kMmucrRc) | (way << kMmucrRcShift));
        mmu.WritePteh(vpn | asid);
        mmu.WritePtel(ptel);
        mmu.LoadTlb();
    }

    void LoadReturningHandler(uint32_t vector) {
        LoadCode(ShRigPa(kShRigVbr + vector), {kShRigAddOneR9, kShRigJmpR13, kShRigNop});
    }

    uint32_t ReturnStubVa() const { return end_va_ - kShRigReturnStubBytes; }

    uint32_t Run(std::vector<uint16_t> body, const cerfengine::ShCpuState& initial, const char* what,
                 const std::function<void(cerfengine::ShCpuState&)>& prepare = {}) {
        const uint32_t start = Start(std::move(body), initial, prepare);
        Finish(what);
        return start;
    }

    std::string RunToHalt(std::vector<uint16_t> body, const cerfengine::ShCpuState& initial, const char* what,
                          const std::function<void(cerfengine::ShCpuState&)>& prepare = {}) {
        Start(std::move(body), initial, prepare);
        if (!Advance()) TestFail("%s: reached PC 0x%08X with no halt", what, cpu_->State()->pc);
        return engine_->LastHalt().detail;
    }

    uint32_t RunSteps(std::vector<uint16_t> body, const cerfengine::ShCpuState& initial, uint32_t steps,
                      const char* what, const std::function<void(cerfengine::ShCpuState&)>& prepare = {}) {
        const uint32_t start = Start(std::move(body), initial, prepare);
        Step(steps, what);
        return start;
    }

    void Step(uint32_t steps, const char* what) {
        for (uint32_t step = 0; step < steps; ++step) {
            if (guest_->Run() == cerfengine::GuestEngine::RunStatus::kHalted) {
                TestFail("%s: halted: %s", what, engine_->LastHalt().detail);
            }
        }
    }

    void StepWithoutHostExit(const char* what) {
        guest_->SetHostChainExit(false);
        Step(1u, what);
        guest_->SetHostChainExit(true);
    }

    void Finish(const char* what) {
        if (Advance()) TestFail("%s: halted: %s", what, engine_->LastHalt().detail);
        if (cpu_->State()->pc != end_va_) TestFail("%s: did not reach 0x%08X, PC 0x%08X", what, end_va_, cpu_->State()->pc);
    }

    TestHost&                     Host() { return host_; }
    cerfengine::ShCpu&            Cpu() { return *cpu_; }
    cerfengine::GuestEngine&      Guest() { return *guest_; }
    cerfengine::ShMmu&            Mmu() { return engine_->Get<cerfengine::ShMmu>(); }
    cerfengine::ShBusMasterCoherence& BusMasterCoherence() { return engine_->Get<cerfengine::ShBusMasterCoherence>(); }
    cerfengine::ShInterruptChannel& Interrupts() { return engine_->Get<cerfengine::ShInterruptChannel>(); }
    cerfengine::HostRequestChannel& HostRequests() { return engine_->Get<cerfengine::HostRequestChannel>(); }
    const cerfengine::ShCpuState& State() const { return *cpu_->State(); }
    uint32_t                      BlocksCompiled() const { return host_.BlocksCompiled(); }

private:
    uint32_t Start(std::vector<uint16_t> body, const cerfengine::ShCpuState& initial,
                   const std::function<void(cerfengine::ShCpuState&)>& prepare) {
        if ((body.size() & 1u) != 0u) body.push_back(kShRigNop);
        body.push_back(kShRigBraNextNop);
        body.push_back(kShRigNop);
        const uint32_t size  = static_cast<uint32_t>(body.size()) * 2u;
        const uint32_t start = (next_va_ & kShRigPageMask) + size > kShRigPageSize
                                   ? (next_va_ + kShRigPageMask) & ~kShRigPageMask
                                   : next_va_;
        end_va_              = start + size;
        next_va_             = end_va_;
        LoadCode(ShRigPa(start), std::move(body));

        cpu_->ResetState(cerfengine::ShResetKind::kPowerOn);
        cerfengine::ShCpuState& s = *cpu_->State();
        std::memcpy(s.gpr, initial.gpr, sizeof(s.gpr));
        s.sr                  = initial.sr;
        s.gbr                 = initial.gbr;
        s.mach                = initial.mach;
        s.macl                = initial.macl;
        s.pc                  = start;
        s.guest_cycle_counter = 0u;
        if (prepare) prepare(s);
        return start;
    }

    bool Advance() {
        for (uint32_t step = 0; step < 64u && cpu_->State()->pc != end_va_; ++step) {
            if (guest_->Run() == cerfengine::GuestEngine::RunStatus::kHalted) return true;
        }
        return false;
    }

    cerfengine::Setup                   setup_;
    TestHost                            host_;
    std::unique_ptr<cerfengine::Engine> engine_;
    cerfengine::ShCpu*                  cpu_     = nullptr;
    cerfengine::GuestEngine*            guest_   = nullptr;
    uint32_t                            next_va_ = kShRigVaBase;
    uint32_t                            end_va_  = 0u;
};
