#pragma once

#include "test_host.h"

#include <cerfengine/guest_engine.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_tlb_context.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

constexpr uint32_t kMipsRigRamPa   = 0x1FC00000u;
constexpr uint32_t kMipsRigRamSize = 0x00010000u;
constexpr uint32_t kMipsRigVaBase  = 0xBFC00000u;
constexpr uint32_t kMipsRigEndVa   = 0xBFC07000u;
constexpr uint32_t kMipsRigEnd16Va = 0xBFC07010u;
constexpr uint32_t kMipsRigDataVa  = 0xBFC08000u;
constexpr uint32_t kMipsRigMmioVa  = kMipsRigVaBase + kMipsRigRamSize;

struct MipsRigEvents {
    uint32_t exceptions  = 0;
    uint32_t last_cause  = 0;
    uint32_t mmu_faults  = 0;
    uint32_t count       = 0;
    uint32_t tlb_ops[4]  = {};
    uint32_t erets       = 0;
    uint32_t rfes        = 0;
    uint32_t power_downs = 0;

    bool operator==(const MipsRigEvents&) const = default;
};

class MipsRigHost : public TestHost {
public:
    MipsRigHost() : TestHost(kMipsRigRamPa, kMipsRigRamSize, 0x10000000u) {}

    void OnPowerDownRequest() override { ++events->power_downs; }

    MipsRigEvents* events = nullptr;
};

class MipsIrTestSystem : public cerfengine::MipsSystemHost {
public:
    void Attach(cerfengine::Setup& setup) {
        setup.mips_tlb.context                  = this;
        setup.mips_tlb.map_address              = &MapAddress;
        setup.mips_tlb.page_global              = &PageGlobal;
        setup.mips_tlb.write_indexed            = &TlbOp<0>;
        setup.mips_tlb.write_random             = &TlbOp<1>;
        setup.mips_tlb.probe                    = &TlbOp<2>;
        setup.mips_tlb.read                     = &TlbOp<3>;
        setup.mips_exception.context            = this;
        setup.mips_exception.enter              = &EnterException;
        setup.mips_exception.set_mmu_fault_regs = &SetMmuFaultRegs;
        setup.mips_exception.interrupts_enabled = &InterruptsEnabled;
        setup.mips_exception.eret               = &Eret;
        setup.mips_exception.rfe                = &Rfe;
        setup.mips_system                       = this;
    }

    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd) override {
        cerfengine::MipsCp0Decision d;
        d.present = true;
        switch (rd) {
        case 8u:
            d.offset = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_badvaddr));
            return d;
        case 9u:
            d.writable      = true;
            d.read          = &ReadCount;
            d.read_context  = this;
            d.write         = &WriteCount;
            d.write_context = this;
            return d;
        case 12u:
            d.writable = true;
            d.offset   = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_status));
            return d;
        case 13u:
            d.writable = true;
            d.offset   = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_cause));
            return d;
        case 14u:
            d.writable = true;
            d.offset   = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_epc));
            return d;
        default:
            return cerfengine::MipsCp0Decision{};
        }
    }

    void MipsReset(cerfengine::MipsCpuState* st) override {
        st->min_page_shift = page_shift;
        st->pc             = kMipsRigVaBase;
    }

    MipsRigEvents events;
    uint32_t      page_shift = 12;

private:
    static MipsIrTestSystem* Self(void* context) { return static_cast<MipsIrTestSystem*>(context); }

    static cerfengine::MipsTlbResult CERFENGINE_FASTCALL MapAddress(
        void*, cerfengine::MipsCpuState*, uint32_t, cerfengine::MipsAccess, uint32_t*) {
        return cerfengine::MipsTlbResult::kNoMatch;
    }
    static bool CERFENGINE_FASTCALL PageGlobal(void*, cerfengine::MipsCpuState*, uint32_t va) {
        TestFail("unexpected mapped MIPS page query at 0x%08X", va);
    }
    template <uint32_t kOp>
    static void CERFENGINE_FASTCALL TlbOp(void* context, cerfengine::MipsTlbContext& tlb) {
        ++Self(context)->events.tlb_ops[kOp];
        tlb.State()->cp0_badvaddr ^= 0x1000u << kOp;
    }
    static void CERFENGINE_FASTCALL SetMmuFaultRegs(void* context, cerfengine::MipsCpuState* st,
                                                    uint32_t va) {
        ++Self(context)->events.mmu_faults;
        st->cp0_badvaddr = va;
    }
    static bool CERFENGINE_FASTCALL InterruptsEnabled(void*, const cerfengine::MipsCpuState&) {
        return false;
    }
    static void CERFENGINE_FASTCALL Eret(void* context, cerfengine::MipsCpuState* st) {
        ++Self(context)->events.erets;
        st->pc       = kMipsRigEndVa;
        st->isa_mode = 0u;
    }
    static void CERFENGINE_FASTCALL Rfe(void* context, cerfengine::MipsCpuState* st) {
        ++Self(context)->events.rfes;
        st->cp0_status = (st->cp0_status & ~0xFu) | ((st->cp0_status >> 2) & 0xFu);
    }

    static void CERFENGINE_FASTCALL EnterException(void* context, cerfengine::MipsCpuState* st,
                                                   uint32_t cause, bool) {
        MipsRigEvents& e      = Self(context)->events;
        const bool     in_bds = st->branch_state != cerfengine::MipsBranch::kNone;
        ++e.exceptions;
        e.last_cause     = cause;
        st->cp0_epc      = (in_bds ? st->pc - st->branch_len : st->pc) | st->isa_mode;
        st->cp0_cause    = (in_bds ? 0x80000000u : 0u) | (cause << 2);
        st->branch_state = cerfengine::MipsBranch::kNone;
        st->isa_mode     = 0u;
        st->pc           = kMipsRigEndVa;
    }

    static uint32_t CERFENGINE_FASTCALL ReadCount(void* context) {
        return Self(context)->events.count += 3u;
    }
    static void CERFENGINE_FASTCALL WriteCount(void* context, uint32_t value) {
        Self(context)->events.count = value;
    }
};

class MipsIrRig {
public:
    MipsIrRig(const cerfengine::MipsFeatures& features, uint32_t page_shift, bool verify) {
        system_.page_shift  = page_shift;
        setup_.isa          = cerfengine::Isa::kMips;
        setup_.mips         = features;
        setup_.verify_ir    = verify;
        host_.events        = &system_.events;
        host_.Attach(setup_);
        system_.Attach(setup_);
        host_.EnableMmioLog();
        engine_ = std::make_unique<cerfengine::Engine>(setup_);
        std::string error;
        if (!engine_->Start(error)) TestFail("Start: %s", error.c_str());
        engine_->BindClock(host_);
        cpu_ = &engine_->Get<cerfengine::MipsCpu>();
        host_.ObserveCycles(&cpu_->State()->guest_cycle_counter);
        guest_ = &engine_->Get<cerfengine::GuestEngine>();
        guest_->SetHostChainExit(true);
    }

    void Load(uint32_t va, const std::vector<uint32_t>& words) {
        host_.LoadWords(va - kMipsRigVaBase + kMipsRigRamPa, words.data(), words.size());
    }

    void Begin(const cerfengine::MipsCpuState& initial) {
        cpu_->ResetState();
        guest_->FlushTranslationCache();
        host_.ResetMmioLog();
        system_.events = MipsRigEvents{};
        cerfengine::MipsCpuState& s = *cpu_->State();
        std::memcpy(s.gpr, initial.gpr, sizeof(s.gpr));
        s.hi                  = initial.hi;
        s.lo                  = initial.lo;
        s.pc                  = initial.pc;
        s.isa_mode            = initial.isa_mode;
        s.branch_state        = initial.branch_state;
        s.btarget             = initial.btarget;
        s.bcond               = initial.bcond;
        s.btarget_isa         = initial.btarget_isa;
        s.branch_len          = initial.branch_len;
        s.cp0_status          = initial.cp0_status;
        s.guest_cycle_counter = 0u;
    }

    cerfengine::GuestEngine::RunStatus Step() { return guest_->Run(); }

    bool Finished() const {
        const cerfengine::MipsCpuState& s = *cpu_->State();
        return s.idle_wait != 0u || ((s.pc == kMipsRigEndVa || s.pc == kMipsRigEnd16Va) &&
                                     s.branch_state == cerfengine::MipsBranch::kNone);
    }

    const cerfengine::MipsCpuState& State() const { return *cpu_->State(); }
    const TestHost&                 Host() const { return host_; }
    TestHost&                       Host() { return host_; }
    const MipsRigEvents&            Events() const { return system_.events; }
    const cerfengine::Engine&       Engine() const { return *engine_; }

private:
    cerfengine::Setup                   setup_;
    MipsRigHost                         host_;
    MipsIrTestSystem                    system_;
    std::unique_ptr<cerfengine::Engine> engine_;
    cerfengine::MipsCpu*                cpu_   = nullptr;
    cerfengine::GuestEngine*            guest_ = nullptr;
};

inline uint32_t MipsRigPick(std::mt19937& rng, uint32_t n) {
    return static_cast<uint32_t>(rng() % n);
}
