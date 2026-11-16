#pragma once

#include "mips32_program_generator.h"

#include <cerfengine/cycle_deadline.h>
#include <cerfengine/mips/mips_jit.h>

constexpr uint32_t kHandler    = 0xBFC0F100u;
constexpr uint32_t kUserPage   = 0x00400000u;
constexpr uint32_t kUserPa     = 0x1FC04000u;
constexpr uint32_t kSsegPage   = 0xC0001000u;
constexpr uint32_t kSsegPa     = 0x1FC05000u;
constexpr uint32_t kKseg0Data  = 0x9FC08000u;
constexpr uint32_t kHighVa     = 0xE0000000u;
constexpr uint32_t kDataValue  = 0x12345678u;
constexpr uint32_t kSsegValue  = 0x0BADF00Du;
constexpr uint32_t kLoop       = kUserPage + 8u;
constexpr uint32_t kUser       = 0x10u;
constexpr uint32_t kSupervisor = 0x08u;
constexpr uint32_t kKuc        = 0x02u;
constexpr uint32_t kUx         = 1u << 5;
constexpr uint32_t kSx         = 1u << 6;
constexpr uint32_t kCu0        = 1u << 28;
constexpr uint32_t kCeMask     = 3u << 28;
constexpr uint32_t kBd         = 1u << 31;
constexpr uint32_t kBranchSelf = 0x1000FFFFu;
constexpr uint32_t kBudget     = 1u << 20;

constexpr uint32_t kAdEL = 4u;
constexpr uint32_t kAdES = 5u;
constexpr uint32_t kSys  = 8u;
constexpr uint32_t kRi   = 10u;
constexpr uint32_t kCpU  = 11u;

inline uint64_t Sext(uint32_t v) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)));
}

inline uint32_t Cop0(uint32_t rs, uint32_t rt, uint32_t rd) {
    return (cerfengine::MipsOp::kCOP0 << 26) | (rs << 21) | (rt << 16) | (rd << 11);
}

inline std::vector<uint32_t> OneInsn(uint32_t insn) { return {insn, 0u, kBranchSelf, 0u}; }

class PrivilegeSystem : public cerfengine::MipsSystemHost {
public:
    explicit PrivilegeSystem(bool r3000) : r3000_(r3000) {}

    void Attach(cerfengine::Setup& setup) {
        setup.mips_tlb.context                  = this;
        setup.mips_tlb.map_address              = &MapAddress;
        setup.mips_tlb.page_global              = &PageGlobal;
        setup.mips_tlb.write_indexed            = &TlbOp;
        setup.mips_tlb.write_random             = &TlbOp;
        setup.mips_tlb.probe                    = &TlbOp;
        setup.mips_tlb.read                     = &TlbOp;
        setup.mips_exception.context            = this;
        setup.mips_exception.enter              = &EnterException;
        setup.mips_exception.set_mmu_fault_regs = &SetMmuFaultRegs;
        setup.mips_exception.interrupts_enabled = &InterruptsEnabled;
        setup.mips_exception.eret               = &StateHook;
        setup.mips_exception.rfe                = &StateHook;
        setup.mips_system                       = this;
    }

    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd) override {
        cerfengine::MipsCp0Decision d;
        d.present  = true;
        d.writable = true;
        switch (rd) {
        case cerfengine::MipsCp0::kStatus:
            d.offset = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_status));
            break;
        case cerfengine::MipsCp0::kCause:
            d.offset = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_cause));
            break;
        default:
            TestFail("unexpected CP0 register %u", rd);
        }
        return d;
    }

    void MipsReset(cerfengine::MipsCpuState* st) override {
        st->min_page_shift = 12;
        st->pc             = kHandler;
    }

    uint32_t exceptions = 0;
    uint32_t exc_code   = 0;

private:
    static PrivilegeSystem* Self(void* context) { return static_cast<PrivilegeSystem*>(context); }

    static cerfengine::MipsTlbResult CERFENGINE_FASTCALL MapAddress(
        void*, cerfengine::MipsCpuState*, uint32_t va, cerfengine::MipsAccess, uint32_t* pa) {
        if ((va & ~0xFFFu) == kUserPage) {
            *pa = kUserPa | (va & 0xFFFu);
            return cerfengine::MipsTlbResult::kMatch;
        }
        if ((va & ~0xFFFu) == kSsegPage) {
            *pa = kSsegPa | (va & 0xFFFu);
            return cerfengine::MipsTlbResult::kMatch;
        }
        TestFail("unexpected mapped access at 0x%08X", va);
    }
    static bool CERFENGINE_FASTCALL PageGlobal(void*, cerfengine::MipsCpuState*, uint32_t) {
        return true;
    }
    static void CERFENGINE_FASTCALL TlbOp(void*, cerfengine::MipsTlbContext&) {
        TestFail("a TLB instruction executed");
    }
    static void CERFENGINE_FASTCALL SetMmuFaultRegs(void*, cerfengine::MipsCpuState*,
                                                    uint32_t va) {
        TestFail("unexpected TLB fault at 0x%08X", va);
    }
    static bool CERFENGINE_FASTCALL InterruptsEnabled(void*, const cerfengine::MipsCpuState&) {
        return false;
    }
    static void CERFENGINE_FASTCALL StateHook(void*, cerfengine::MipsCpuState*) {
        TestFail("an exception return executed");
    }
    static void CERFENGINE_FASTCALL EnterException(void* context, cerfengine::MipsCpuState* st,
                                                   uint32_t cause, bool) {
        PrivilegeSystem* self = Self(context);
        ++self->exceptions;
        self->exc_code    = cause;
        const bool in_bds = st->branch_state != cerfengine::MipsBranch::kNone;
        st->cp0_epc       = in_bds ? st->pc - st->branch_len : st->pc;
        st->cp0_cause     = (st->cp0_cause & ~(kBd | 0x7Cu)) | (in_bds ? kBd : 0u) | (cause << 2);
        if (self->r3000_) {
            st->cp0_status = (st->cp0_status & ~0x3Fu) | ((st->cp0_status << 2) & 0x3Cu);
        } else {
            st->cp0_status |= 0x2u;
        }
        st->branch_state = cerfengine::MipsBranch::kNone;
        st->isa_mode     = 0u;
        st->pc           = kHandler;
    }

    bool r3000_;
};

inline cerfengine::MipsFeatures BaseFeatures(bool r3000, bool cop1_cop2_unusable) {
    cerfengine::MipsFeatures f;
    f.isa_level = r3000 ? cerfengine::MipsIsaLevel::kMips1 : cerfengine::MipsIsaLevel::kMips3;
    f.cop1_cop2_unusable = cop1_cop2_unusable;
    return f;
}

class PrivilegeRig {
public:
    using RunStatus = cerfengine::GuestEngine::RunStatus;

    explicit PrivilegeRig(bool r3000, bool cop1_cop2_unusable = false)
        : PrivilegeRig(BaseFeatures(r3000, cop1_cop2_unusable), r3000) {}

    PrivilegeRig(const cerfengine::MipsFeatures& features, bool r3000) : system_(r3000) {
        setup_.isa  = cerfengine::Isa::kMips;
        setup_.mips = features;
        host_.Attach(setup_);
        system_.Attach(setup_);
        engine_ = std::make_unique<cerfengine::Engine>(setup_);
        std::string error;
        if (!engine_->Start(error)) TestFail("Start: %s", error.c_str());
        engine_->BindClock(host_);
        cpu_ = &engine_->Get<cerfengine::MipsCpu>();
        host_.ObserveCycles(&cpu_->State()->guest_cycle_counter);
        jit_ = &engine_->Get<cerfengine::MipsJit>();
        cpu_->ResetState();
        const std::vector<uint32_t> loop = {kBranchSelf, 0u};
        LoadPa(kHandler & 0x1FFFFFFFu, loop);
        LoadPa(kKseg0Data & 0x1FFFFFFFu, {kDataValue, kDataValue + 1u});
        LoadPa(kSsegPa, {kSsegValue});
    }

    void LoadPa(uint32_t pa, const std::vector<uint32_t>& words) {
        host_.LoadWords(pa, words.data(), words.size());
    }

    void LoadUser(const std::vector<uint32_t>& words) { LoadPa(kUserPa, words); }

    void Start(uint32_t status, uint32_t pc) {
        cerfengine::MipsCpuState& s = State();
        s.cp0_status                = status;
        s.cp0_cause                 = kCeMask;
        s.pc                        = pc;
        s.gpr[8]                    = Sext(kKseg0Data);
        s.gpr[9]                    = 0u;
        s.gpr[10]                   = Sext(kSsegPage);
        s.gpr[11]                   = Sext(kHighVa);
        system_.exceptions          = 0u;
    }

    void Start16(uint32_t status, uint32_t pc) {
        Start(status, pc);
        State().isa_mode = 1u;
    }

    RunStatus RunOnce() {
        cerfengine::MipsCpuState& s = State();
        s.guest_cycle_deadline      = cerfengine::CycleDeadline(s.guest_cycle_counter, kBudget);
        return jit_->Run();
    }

    void RunTo(uint32_t pc) {
        for (uint32_t steps = 0; State().pc != pc; ++steps) {
            if (steps > 8u) TestFail("no exit to 0x%08X (pc 0x%08X)", pc, State().pc);
            TEST_CHECK(RunOnce() == RunStatus::kRunning);
        }
    }

    void ExpectException(uint32_t code, uint32_t badvaddr, uint32_t epc, const char* name) {
        RunTo(kHandler);
        const cerfengine::MipsCpuState& s = State();
        std::printf("%s: exceptions %u code %u badvaddr 0x%08X epc 0x%08X cause 0x%08X\n", name,
                    system_.exceptions, system_.exc_code, s.cp0_badvaddr, s.cp0_epc,
                    s.cp0_cause);
        TEST_CHECK(system_.exceptions == 1u);
        TEST_CHECK(system_.exc_code == code);
        TEST_CHECK(s.cp0_epc == epc);
        if (code != kCpU && code != kRi && code != kSys) TEST_CHECK(s.cp0_badvaddr == badvaddr);
    }

    cerfengine::MipsCpuState& State() { return *cpu_->State(); }
    PrivilegeSystem&          System() { return system_; }

private:
    cerfengine::Setup                   setup_;
    MipsRigHost                         host_;
    PrivilegeSystem                     system_;
    std::unique_ptr<cerfengine::Engine> engine_;
    cerfengine::MipsCpu*                cpu_ = nullptr;
    cerfengine::MipsJit*                jit_ = nullptr;
};
