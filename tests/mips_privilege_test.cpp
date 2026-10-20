#include "mips32_program_generator.h"

#include <cerfengine/cycle_deadline.h>
#include <cerfengine/mips/mips_jit.h>

namespace {

namespace O = cerfengine::MipsOp;
namespace S = cerfengine::MipsSpecial;

using RunStatus = cerfengine::GuestEngine::RunStatus;

constexpr uint32_t kHandler    = 0xBFC0F100u;
constexpr uint32_t kUserPage   = 0x00400000u;
constexpr uint32_t kUserPa     = 0x1FC04000u;
constexpr uint32_t kSsegPage   = 0xC0001000u;
constexpr uint32_t kSsegPa     = 0x1FC05000u;
constexpr uint32_t kKseg0Data  = 0x9FC08000u;
constexpr uint32_t kKseg3Va    = 0xFFFF8000u;
constexpr uint32_t kHighVa     = 0xE0000000u;
constexpr uint32_t kDataValue  = 0x12345678u;
constexpr uint32_t kSsegValue  = 0x0BADF00Du;
constexpr uint32_t kLoop       = kUserPage + 8u;
constexpr uint32_t kUser       = 0x10u;
constexpr uint32_t kSupervisor = 0x08u;
constexpr uint32_t kKuc        = 0x02u;
constexpr uint32_t kCu0        = 1u << 28;
constexpr uint32_t kCeMask     = 3u << 28;
constexpr uint32_t kBd         = 1u << 31;
constexpr uint32_t kBranchSelf = 0x1000FFFFu;
constexpr uint32_t kBudget     = 1u << 20;

constexpr uint32_t kAdEL = 4u;
constexpr uint32_t kAdES = 5u;
constexpr uint32_t kCpU  = 11u;

uint64_t Sext(uint32_t v) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)));
}

uint32_t Cop0(uint32_t rs, uint32_t rt, uint32_t rd) {
    return (O::kCOP0 << 26) | (rs << 21) | (rt << 16) | (rd << 11);
}

constexpr uint32_t kTlbwi = 0x42000002u;

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

class PrivilegeRig {
public:
    explicit PrivilegeRig(bool r3000, bool cop1_cop2_unusable = false) : system_(r3000) {
        setup_.isa = cerfengine::Isa::kMips;
        setup_.mips.isa_level =
            r3000 ? cerfengine::MipsIsaLevel::kMips1 : cerfengine::MipsIsaLevel::kMips3;
        setup_.mips.cop1_cop2_unusable = cop1_cop2_unusable;
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

    void RunTo(uint32_t pc) {
        for (uint32_t steps = 0; State().pc != pc; ++steps) {
            if (steps > 8u) TestFail("no exit to 0x%08X (pc 0x%08X)", pc, State().pc);
            cerfengine::MipsCpuState& s = State();
            s.guest_cycle_deadline      = cerfengine::CycleDeadline(s.guest_cycle_counter, kBudget);
            TEST_CHECK(jit_->Run() == RunStatus::kRunning);
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
        if (code != kCpU) TEST_CHECK(s.cp0_badvaddr == badvaddr);
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

std::vector<uint32_t> OneInsn(uint32_t insn) { return {insn, 0u, kBranchSelf, 0u}; }

struct AccessCase {
    const char* name;
    uint32_t    insn;
    uint32_t    code;
    uint32_t    badvaddr;
    bool        kernel_reads;
};

void TestUserAccesses() {
    const AccessCase cases[] = {
        {"lw", MipsGenI(O::kLW, 8u, 9u, 0u), kAdEL, kKseg0Data, true},
        {"lw r0", MipsGenI(O::kLW, 8u, 0u, 0u), kAdEL, kKseg0Data, false},
        {"lw const", MipsGenI(O::kLW, 0u, 9u, kKseg3Va & 0xFFFFu), kAdEL, kKseg3Va, false},
        {"lh", MipsGenI(O::kLH, 8u, 9u, 2u), kAdEL, kKseg0Data + 2u, false},
        {"lbu", MipsGenI(O::kLBU, 8u, 9u, 1u), kAdEL, kKseg0Data + 1u, false},
        {"sw", MipsGenI(O::kSW, 8u, 9u, 0u), kAdES, kKseg0Data, false},
        {"sb", MipsGenI(O::kSB, 8u, 9u, 3u), kAdES, kKseg0Data + 3u, false},
        {"lwl", MipsGenI(O::kLWL, 8u, 9u, 0u), kAdEL, kKseg0Data, false},
        {"ld", MipsGenI(O::kLD, 8u, 9u, 0u), kAdEL, kKseg0Data, false},
        {"sd", MipsGenI(O::kSD, 8u, 9u, 0u), kAdES, kKseg0Data, false},
    };
    for (const AccessCase& c : cases) {
        PrivilegeRig rig(false);
        rig.LoadUser(OneInsn(c.insn));
        rig.Start(kUser, kUserPage);
        rig.ExpectException(c.code, c.badvaddr, kUserPage, c.name);
        rig.Start(kSupervisor, kUserPage);
        rig.ExpectException(c.code, c.badvaddr, kUserPage, c.name);
        if (!c.kernel_reads) continue;
        rig.Start(0u, kUserPage);
        rig.RunTo(kLoop);
        TEST_CHECK(rig.State().gpr[9] == kDataValue);
        TEST_CHECK(rig.System().exceptions == 0u);
    }
}

void TestSupervisorSegments() {
    PrivilegeRig rig(false);
    rig.LoadUser(OneInsn(MipsGenI(O::kLW, 10u, 9u, 0u)));
    rig.Start(kSupervisor, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == kSsegValue);
    TEST_CHECK(rig.System().exceptions == 0u);
    rig.Start(kUser, kUserPage);
    rig.ExpectException(kAdEL, kSsegPage, kUserPage, "user sseg");

    PrivilegeRig high(false);
    high.LoadUser(OneInsn(MipsGenI(O::kLW, 11u, 9u, 0u)));
    high.Start(kSupervisor, kUserPage);
    high.ExpectException(kAdEL, kHighVa, kUserPage, "supervisor 0xE0000000");
}

void TestUserFetch() {
    PrivilegeRig rig(false);
    rig.Start(kUser, kKseg0Data);
    rig.ExpectException(kAdEL, kKseg0Data, kKseg0Data, "user fetch");

    PrivilegeRig jump(false);
    jump.LoadUser({MipsGenR(8u, 0u, 0u, 0u, S::kJR), 0u});
    jump.Start(kUser, kUserPage);
    jump.ExpectException(kAdEL, kKseg0Data, kKseg0Data, "user jr");
}

void TestBlocksKeyedByPrivilege() {
    PrivilegeRig rig(false);
    rig.LoadUser(OneInsn(MipsGenI(O::kLW, 8u, 9u, 0u)));
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        rig.Start(0u, kUserPage);
        rig.RunTo(kLoop);
        TEST_CHECK(rig.State().gpr[9] == kDataValue);
        rig.Start(kUser, kUserPage);
        rig.ExpectException(kAdEL, kKseg0Data, kUserPage, "keyed user");
    }
}

void TestCop0Usable() {
    PrivilegeRig rig(false);
    rig.LoadUser(OneInsn(Cop0(cerfengine::MipsCop0Rs::kMFC0, 9u, cerfengine::MipsCp0::kStatus)));
    rig.Start(kUser, kUserPage);
    rig.ExpectException(kCpU, 0u, kUserPage, "user mfc0");
    TEST_CHECK((rig.State().cp0_cause & kCeMask) == 0u);
    rig.Start(kUser | kCu0, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == Sext(kUser | kCu0));
    rig.Start(0u, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == 0u);

    PrivilegeRig tlb(false);
    tlb.LoadUser(OneInsn(kTlbwi));
    tlb.Start(kSupervisor, kUserPage);
    tlb.ExpectException(kCpU, 0u, kUserPage, "supervisor tlbwi");

    PrivilegeRig cache(false);
    cache.LoadUser(OneInsn(MipsGenI(O::kCACHE, 8u, 0u, 0u)));
    cache.Start(kUser, kUserPage);
    cache.ExpectException(kCpU, 0u, kUserPage, "user cache");
    cache.Start(0u, kUserPage);
    cache.RunTo(kLoop);
    TEST_CHECK(cache.System().exceptions == 0u);
}

void TestCop0InDelaySlot() {
    PrivilegeRig rig(false);
    rig.LoadUser({MipsGenI(O::kBEQ, 0u, 0u, 1u), MipsGenI(O::kCACHE, 8u, 0u, 0u), 0u,
                  kBranchSelf, 0u});
    const uint32_t target = kUserPage + 12u;
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        rig.Start(0u, kUserPage);
        rig.RunTo(target);
        TEST_CHECK(rig.System().exceptions == 0u);
        rig.Start(kUser, kUserPage);
        rig.ExpectException(kCpU, 0u, kUserPage, "user cache in delay slot");
        TEST_CHECK((rig.State().cp0_cause & kBd) != 0u);
    }
}

void TestCopUnusableOpcodes() {
    struct CopCase {
        const char* name;
        uint32_t    insn;
        uint32_t    unit;
    };
    const CopCase cases[] = {
        {"cop1", (O::kCOP1 << 26) | (9u << 16), 1u},
        {"cop2", (O::kCOP2 << 26) | (9u << 16), 2u},
        {"lwc1", MipsGenI(O::kLWC1, 8u, 0u, 0u), 1u},
        {"lwc2", MipsGenI(O::kLWC2, 8u, 0u, 0u), 2u},
        {"ldc1", MipsGenI(O::kLDC1, 8u, 0u, 0u), 1u},
        {"ldc2", MipsGenI(O::kLDC2, 8u, 0u, 0u), 2u},
        {"swc1", MipsGenI(O::kSWC1, 8u, 0u, 0u), 1u},
        {"swc2", MipsGenI(O::kSWC2, 8u, 0u, 0u), 2u},
        {"sdc1", MipsGenI(O::kSDC1, 8u, 0u, 0u), 1u},
        {"sdc2", MipsGenI(O::kSDC2, 8u, 0u, 0u), 2u},
    };
    constexpr uint32_t kCu1Cu2 = 3u << 29;
    for (const CopCase& c : cases) {
        PrivilegeRig rig(false, true);
        rig.LoadUser(OneInsn(c.insn));
        for (uint32_t status : {0u, kCu1Cu2, kUser | kCu1Cu2}) {
            rig.Start(status, kUserPage);
            rig.ExpectException(kCpU, 0u, kUserPage, c.name);
            TEST_CHECK((rig.State().cp0_cause & kCeMask) == (c.unit << 28));
        }
    }
}

void TestR3000User() {
    PrivilegeRig rig(true);
    rig.LoadUser(OneInsn(MipsGenI(O::kLW, 8u, 9u, 0u)));
    rig.Start(kKuc, kUserPage);
    rig.ExpectException(kAdEL, kKseg0Data, kUserPage, "r3000 user lw");
    TEST_CHECK((rig.State().cp0_status & 0x3Fu) == (kKuc << 2));
    rig.Start(0u, kUserPage);
    rig.RunTo(kLoop);
    TEST_CHECK(rig.State().gpr[9] == kDataValue);

    PrivilegeRig cop0(true);
    cop0.LoadUser(OneInsn(Cop0(cerfengine::MipsCop0Rs::kMFC0, 9u, cerfengine::MipsCp0::kCause)));
    cop0.Start(kKuc, kUserPage);
    cop0.ExpectException(kCpU, 0u, kUserPage, "r3000 user mfc0");
}

}

int main() {
    TestUserAccesses();
    TestSupervisorSegments();
    TestUserFetch();
    TestBlocksKeyedByPrivilege();
    TestCop0Usable();
    TestCop0InDelaySlot();
    TestCopUnusableOpcodes();
    TestR3000User();
    std::printf("mips_privilege_test passed\n");
    return 0;
}
