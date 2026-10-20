#include "mips32_program_generator.h"

#include <cerfengine/cycle_deadline.h>
#include <cerfengine/mips/mips_jit.h>
#include <cerfengine/mips/mips_mmu.h>

namespace {

namespace O = cerfengine::MipsOp;
namespace S = cerfengine::MipsSpecial;

using RunStatus = cerfengine::GuestEngine::RunStatus;

constexpr uint32_t kBase       = 0xBFC00000u;
constexpr uint32_t kEnd        = 0xBFC0F000u;
constexpr uint32_t kHandler    = 0xBFC0F100u;
constexpr uint32_t kMmioVa     = 0xB0000000u;
constexpr uint32_t kUserPage0  = 0x00400000u;
constexpr uint32_t kUserPage1  = 0x00401000u;
constexpr uint32_t kUserPa0    = 0x1FC04000u;
constexpr uint32_t kUserPa1    = 0x1FC05000u;
constexpr uint32_t kUserPa1Alt = 0x1FC06000u;
constexpr uint32_t kIp2        = 1u << 10;
constexpr uint32_t kBudget     = 1u << 20;

uint32_t Addiu(uint32_t rt, uint32_t rs, uint32_t imm) { return MipsGenI(O::kADDIU, rs, rt, imm); }

uint32_t Branch(uint32_t op, uint32_t rs, uint32_t rt, uint32_t pc, uint32_t target) {
    return MipsGenI(op, rs, rt, (target - (pc + 4u)) >> 2);
}

uint32_t Jr(uint32_t rs) { return MipsGenR(rs, 0u, 0u, 0u, S::kJR); }

uint32_t Mtc0(uint32_t rt, uint32_t rd) {
    return (O::kCOP0 << 26) | (cerfengine::MipsCop0Rs::kMTC0 << 21) | (rt << 16) | (rd << 11);
}

constexpr uint32_t kRfe = 0x42000010u;

class ChainSystem : public cerfengine::MipsSystemHost {
public:
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
        setup.mips_exception.eret               = &Eret;
        setup.mips_exception.rfe                = &Rfe;
        setup.mips_system                       = this;
    }

    cerfengine::MipsCp0Decision MipsCp0(uint32_t rd) override {
        cerfengine::MipsCp0Decision d;
        d.present  = true;
        d.writable = true;
        if (rd == cerfengine::MipsCp0::kStatus) {
            d.offset = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_status));
        } else if (rd == cerfengine::MipsCp0::kCause) {
            d.offset = static_cast<int32_t>(offsetof(cerfengine::MipsCpuState, cp0_cause));
        } else {
            TestFail("unexpected CP0 register %u", rd);
        }
        return d;
    }

    void MipsReset(cerfengine::MipsCpuState* st) override {
        st->min_page_shift = 12;
        st->pc             = kBase;
    }

    uint32_t page1_pa   = kUserPa1;
    uint32_t exceptions = 0;

private:
    static ChainSystem* Self(void* context) { return static_cast<ChainSystem*>(context); }

    static cerfengine::MipsTlbResult CERFENGINE_FASTCALL MapAddress(
        void* context, cerfengine::MipsCpuState*, uint32_t va, cerfengine::MipsAccess,
        uint32_t* pa) {
        if ((va & ~0xFFFu) == kUserPage0) {
            *pa = kUserPa0 | (va & 0xFFFu);
            return cerfengine::MipsTlbResult::kMatch;
        }
        if ((va & ~0xFFFu) == kUserPage1) {
            *pa = Self(context)->page1_pa | (va & 0xFFFu);
            return cerfengine::MipsTlbResult::kMatch;
        }
        return cerfengine::MipsTlbResult::kNoMatch;
    }
    static bool CERFENGINE_FASTCALL PageGlobal(void*, cerfengine::MipsCpuState*, uint32_t) {
        return true;
    }
    static void CERFENGINE_FASTCALL TlbOp(void*, cerfengine::MipsTlbContext&) {
        TestFail("unexpected TLB instruction");
    }
    static void CERFENGINE_FASTCALL SetMmuFaultRegs(void*, cerfengine::MipsCpuState*,
                                                    uint32_t va) {
        TestFail("unexpected MMU fault at 0x%08X", va);
    }
    static bool CERFENGINE_FASTCALL InterruptsEnabled(void*, const cerfengine::MipsCpuState& s) {
        return (s.cp0_status & 0x7u) == 0x1u;
    }
    static void CERFENGINE_FASTCALL EnterException(void* context, cerfengine::MipsCpuState* st,
                                                   uint32_t, bool) {
        ++Self(context)->exceptions;
        st->cp0_epc      = st->pc;
        st->cp0_status  |= 0x2u;
        st->branch_state = cerfengine::MipsBranch::kNone;
        st->isa_mode     = 0u;
        st->pc           = kHandler;
    }
    static void CERFENGINE_FASTCALL Eret(void*, cerfengine::MipsCpuState*) { TestFail("ERET"); }
    static void CERFENGINE_FASTCALL Rfe(void*, cerfengine::MipsCpuState* st) {
        st->cp0_status = (st->cp0_status & ~0xFu) | ((st->cp0_status >> 2) & 0xFu);
    }
};

class ChainHost : public TestHost {
public:
    ChainHost() : TestHost(kMipsRigRamPa, kMipsRigRamSize, 0x10000000u) {}

    void OnResetRequested(bool) override { ++reset_requests; }
    void OnRebootRequested() override {}
    void OnResetDelivered() override { ++resets_delivered; }

    uint32_t reset_requests   = 0;
    uint32_t resets_delivered = 0;
};

class ChainRig {
public:
    explicit ChainRig(const cerfengine::MipsFeatures& features) {
        setup_.isa                 = cerfengine::Isa::kMips;
        setup_.mips                = features;
        setup_.mips.device_ip_mask = kIp2;
        host_.Attach(setup_);
        system_.Attach(setup_);
        host_.EnableMmioLog();
        engine_ = std::make_unique<cerfengine::Engine>(setup_);
        std::string error;
        if (!engine_->Start(error)) TestFail("Start: %s", error.c_str());
        engine_->BindClock(host_);
        cpu_ = &engine_->Get<cerfengine::MipsCpu>();
        host_.ObserveCycles(&cpu_->State()->guest_cycle_counter);
        jit_ = &engine_->Get<cerfengine::MipsJit>();
        cpu_->ResetState();
        const std::vector<uint32_t> loops = {0x1000FFFFu, 0u};
        Load(kEnd, loops);
        Load(kHandler, loops);
    }

    void LoadPa(uint32_t pa, const std::vector<uint32_t>& words) {
        host_.LoadWords(pa, words.data(), words.size());
    }
    void Load(uint32_t kseg1_va, const std::vector<uint32_t>& words) {
        LoadPa(kseg1_va & 0x1FFFFFFFu, words);
    }

    RunStatus Step(uint32_t budget = kBudget) {
        cerfengine::MipsCpuState& s = State();
        s.guest_cycle_deadline      = cerfengine::CycleDeadline(s.guest_cycle_counter, budget);
        ++steps_;
        return jit_->Run();
    }

    void RunTo(uint32_t pc, uint32_t max_steps) {
        steps_ = 0u;
        while (State().pc != pc) {
            TEST_CHECK(Step() == RunStatus::kRunning);
            if (steps_ > max_steps) TestFail("no exit to 0x%08X after %u steps", pc, steps_);
        }
    }

    cerfengine::MipsCpuState& State() { return *cpu_->State(); }
    cerfengine::MipsJit&      Jit() { return *jit_; }
    cerfengine::Engine&       Engine() { return *engine_; }
    ChainHost&                Host() { return host_; }
    ChainSystem&              System() { return system_; }
    uint32_t                  Steps() const { return steps_; }

private:
    cerfengine::Setup                   setup_;
    ChainHost                           host_;
    ChainSystem                         system_;
    std::unique_ptr<cerfengine::Engine> engine_;
    cerfengine::MipsCpu*                cpu_   = nullptr;
    cerfengine::MipsJit*                jit_   = nullptr;
    uint32_t                            steps_ = 0;
};

cerfengine::MipsFeatures Mips3() {
    cerfengine::MipsFeatures f;
    f.isa_level = cerfengine::MipsIsaLevel::kMips3;
    return f;
}

std::vector<uint32_t> CountLoop(bool with_mmio) {
    std::vector<uint32_t> p = {Addiu(2u, 0u, 0u), Addiu(3u, 0u, 1000u), Addiu(2u, 2u, 1u)};
    if (with_mmio) p.push_back(MipsGenI(O::kLW, 27u, 9u, 0u));
    const uint32_t loop = kBase + 8u;
    const uint32_t bne  = kBase + static_cast<uint32_t>(p.size()) * 4u;
    p.push_back(Branch(O::kBNE, 2u, 3u, bne, loop));
    p.push_back(0u);
    p.push_back(MipsGenJ(O::kJ, kEnd));
    p.push_back(0u);
    return p;
}

void TestLoopChains() {
    ChainRig rig(Mips3());
    rig.Load(kBase, CountLoop(false));
    rig.RunTo(kEnd, 3u);
    TEST_CHECK(rig.State().gpr[2] == 1000u);
    rig.Engine().Get<cerfengine::MipsMmu>().TlbContext().InvalidateAddressSpace();
    rig.State().pc = kBase;
    rig.RunTo(kEnd, 1u);
    TEST_CHECK(rig.State().gpr[2] == 1000u);
}

void TestDeadlinePoll() {
    ChainRig rig(Mips3());
    rig.Load(kBase, CountLoop(false));
    rig.RunTo(kEnd, 3u);
    rig.State().pc = kBase;
    for (uint32_t n = 0; rig.State().pc != kEnd; ++n) {
        TEST_CHECK(n < 200u);
        TEST_CHECK(rig.Step(30u) == RunStatus::kRunning);
        const cerfengine::MipsCpuState& s = rig.State();
        if (s.pc == kEnd) break;
        TEST_CHECK(cerfengine::CycleDeadlineReached(s.guest_cycle_counter, s.guest_cycle_deadline));
        TEST_CHECK(s.guest_cycle_counter - s.guest_cycle_deadline < 3u);
    }
    TEST_CHECK(rig.State().gpr[2] == 1000u);
}

void TestHostExit() {
    ChainRig rig(Mips3());
    rig.Jit().SetHostChainExit(true);
    rig.Engine().Get<cerfengine::MipsCpu>().ResetState();
    rig.Load(kBase, CountLoop(false));
    rig.RunTo(kEnd, 2000u);
    TEST_CHECK(rig.Steps() > 900u);
    TEST_CHECK(rig.State().gpr[2] == 1000u);
}

void TestInterruptKick() {
    ChainRig rig(Mips3());
    uint32_t accesses = 0;
    rig.Host().RaiseIrqOnMmio([&rig, &accesses] {
        if (++accesses == 100u) rig.Jit().SetExternalInterruptLevel(kIp2);
    });
    rig.Load(kBase, CountLoop(true));
    rig.State().gpr[27] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(kMmioVa)));
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(rig.State().gpr[2] == 100u);
    TEST_CHECK(rig.State().pc == kBase + 8u);
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK((rig.State().cp0_cause & kIp2) != 0u);
    rig.RunTo(kEnd, 3u);
    TEST_CHECK(rig.State().gpr[2] == 1000u);
}

void TestPowerRequestKicks() {
    for (uint32_t reset = 0; reset < 2u; ++reset) {
        ChainRig rig(Mips3());
        uint32_t accesses = 0;
        rig.Host().RaiseIrqOnMmio([&rig, &accesses, reset] {
            if (++accesses != 100u) return;
            if (reset != 0u) rig.Jit().SetResetPending(false);
            else             rig.Jit().EnterDeepSleep();
        });
        rig.Load(kBase, CountLoop(true));
        rig.State().gpr[27] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(kMmioVa)));
        TEST_CHECK(rig.Step() == RunStatus::kRunning);
        TEST_CHECK(rig.Step() == RunStatus::kRunning);
        TEST_CHECK(rig.State().gpr[2] == 100u);
        TEST_CHECK(rig.State().pc == kBase + 8u);
        if (reset == 0u) {
            TEST_CHECK(rig.Jit().DeepSleep());
            continue;
        }
        TEST_CHECK(rig.Host().reset_requests == 1u && rig.Host().resets_delivered == 0u);
        TEST_CHECK(rig.Step() == RunStatus::kRunning);
        TEST_CHECK(rig.Host().resets_delivered == 1u);
        TEST_CHECK(rig.State().pc == kBase && rig.State().gpr[2] == 0u);
    }
}

void TestClockEventKick() {
    ChainRig rig(Mips3());
    rig.Load(kBase, CountLoop(true));
    cerfengine::MipsCpuState& s = rig.State();
    s.gpr[27] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(kMmioVa)));
    rig.Host().OnClockDispatch([&s] {
        s.cp0_cause |= cerfengine::kMipsCauseTimerIp;
        s.guest_cycle_deadline = cerfengine::CycleDeadline(s.guest_cycle_counter, kBudget);
    });
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(s.pc == kBase + 8u && s.gpr[2] == 1u);
    TEST_CHECK(rig.Step(2u + 4u * 50u) == RunStatus::kRunning);
    TEST_CHECK((s.cp0_cause & cerfengine::kMipsCauseTimerIp) != 0u);
    TEST_CHECK(s.gpr[2] == 52u);
    TEST_CHECK(s.pc == kBase + 8u);
}

void TestStatusWriteEndsBlock() {
    ChainRig rig(Mips3());
    rig.Load(kBase, {Addiu(4u, 0u, 0x401u), Mtc0(4u, cerfengine::MipsCp0::kStatus),
                     Addiu(5u, 0u, 7u), MipsGenJ(O::kJ, kEnd), 0u});
    rig.Jit().SetExternalInterruptLevel(kIp2);
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(rig.State().cp0_status == 0x401u);
    TEST_CHECK(rig.State().pc == kBase + 8u);
    TEST_CHECK(rig.State().gpr[5] == 0u);
    TEST_CHECK(rig.System().exceptions == 0u);
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(rig.System().exceptions == 1u);
    TEST_CHECK(rig.State().cp0_epc == kBase + 8u);
    TEST_CHECK(rig.State().gpr[5] == 0u);
}

void TestCauseWriteEndsBlock() {
    ChainRig rig(Mips3());
    rig.Load(kBase, {Addiu(4u, 0u, 0x100u), Mtc0(4u, cerfengine::MipsCp0::kCause),
                     Addiu(5u, 0u, 7u), MipsGenJ(O::kJ, kEnd), 0u});
    rig.State().cp0_status = 0x101u;
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK((rig.State().cp0_cause & 0x100u) != 0u);
    TEST_CHECK(rig.State().pc == kBase + 8u);
    TEST_CHECK(rig.State().gpr[5] == 0u);
    TEST_CHECK(rig.System().exceptions == 0u);
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(rig.System().exceptions == 1u);
    TEST_CHECK(rig.State().cp0_epc == kBase + 8u);
}

void TestStatusWriteInDelaySlot() {
    ChainRig rig(Mips3());
    const uint32_t target = kBase + 0x100u;
    rig.Load(kBase, {MipsGenJ(O::kJAL, target), Mtc0(0u, cerfengine::MipsCp0::kStatus)});
    rig.Load(target, {Addiu(6u, 0u, 9u), MipsGenJ(O::kJ, kEnd), 0u});
    rig.RunTo(kEnd, 4u);
    TEST_CHECK(rig.State().gpr[6] == 9u);
    rig.State().pc     = kBase;
    rig.State().gpr[6] = 0u;
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(rig.State().pc == target);
    TEST_CHECK(rig.State().gpr[6] == 0u);
    TEST_CHECK(static_cast<uint32_t>(rig.State().gpr[31]) == kBase + 8u);
}

void TestRfeEndsBlock() {
    cerfengine::MipsFeatures f;
    f.tx39_multiply_add = true;
    ChainRig       rig(f);
    const uint32_t target = kBase + 0x100u;
    rig.Load(kBase, {Jr(26u), kRfe});
    rig.Load(target, {Addiu(6u, 0u, 9u), MipsGenJ(O::kJ, kEnd), 0u});
    rig.State().gpr[26] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(target)));
    rig.RunTo(kEnd, 4u);
    TEST_CHECK(rig.State().gpr[6] == 9u);
    rig.State().pc         = kBase;
    rig.State().gpr[6]     = 0u;
    rig.State().cp0_status = 0x3Cu;
    TEST_CHECK(rig.Step() == RunStatus::kRunning);
    TEST_CHECK(rig.State().pc == target);
    TEST_CHECK(rig.State().cp0_status == 0x3Fu);
    TEST_CHECK(rig.State().gpr[6] == 0u);
}

void TestStoreUnlinksChain() {
    ChainRig       rig(Mips3());
    const uint32_t b    = kBase + 0x200u;
    const uint32_t loop = kBase + 8u;
    const uint32_t skip = kBase + 0x20u;
    rig.Load(kBase, {Addiu(5u, 0u, 0u), Addiu(6u, 0u, 0u), MipsGenJ(O::kJAL, b), 0u,
                     Addiu(6u, 6u, 1u), Branch(O::kBNE, 6u, 7u, kBase + 0x14u, skip), 0u,
                     MipsGenI(O::kSW, 8u, 10u, 0u), Branch(O::kBNE, 6u, 11u, skip, loop), 0u,
                     MipsGenJ(O::kJ, kEnd), 0u});
    rig.Load(b, {Addiu(5u, 5u, 1u), Jr(31u), 0u});
    cerfengine::MipsCpuState& s = rig.State();
    s.gpr[7]  = 10u;
    s.gpr[11] = 30u;
    s.gpr[8]  = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(b)));
    s.gpr[10] = Addiu(5u, 5u, 100u);
    rig.RunTo(kEnd, 200u);
    TEST_CHECK(s.gpr[5] == 10u + 20u * 100u);
}

void TestSplitBlockRevalidates() {
    ChainRig       rig(Mips3());
    const uint32_t s_va = kUserPage0 + 0xFF8u;
    const uint32_t x_va = kUserPage0 + 0xF00u;
    const uint32_t y_va = kUserPage0 + 0xE00u;
    rig.LoadPa(kUserPa0 + 0xE00u, {MipsGenJ(O::kJ, s_va), 0u});
    rig.LoadPa(kUserPa0 + 0xF00u, {MipsGenJ(O::kJ, s_va), 0u});
    rig.LoadPa(kUserPa0 + 0xFF8u, {Addiu(8u, 8u, 1u), Jr(26u)});
    rig.LoadPa(kUserPa1, {Addiu(10u, 0u, 1u)});
    rig.LoadPa(kUserPa1Alt, {Addiu(10u, 0u, 2u)});
    cerfengine::MipsCpuState& s = rig.State();
    s.gpr[26] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(kEnd)));
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        const uint32_t order[2] = {pass == 0u ? x_va : y_va, pass == 0u ? y_va : x_va};
        for (uint32_t start : order) {
            s.pc      = start;
            s.gpr[10] = 0u;
            rig.RunTo(kEnd, 10u);
            TEST_CHECK(s.gpr[10] == pass + 1u);
        }
        rig.System().page1_pa = kUserPa1Alt;
        rig.Engine().Get<cerfengine::MipsMmu>().TlbContext().InvalidatePage(kUserPage1);
    }
}

void TestSplitBlockNeverChainsToItself() {
    ChainRig       rig(Mips3());
    const uint32_t s_va = kUserPage0 + 0xFF8u;
    rig.LoadPa(kUserPa0 + 0xFF8u, {Addiu(8u, 8u, 1u), Branch(O::kBNE, 8u, 9u, s_va + 4u, s_va)});
    rig.LoadPa(kUserPa1, {Addiu(10u, 10u, 1u), Jr(26u), 0u});
    cerfengine::MipsCpuState& s = rig.State();
    s.gpr[9]  = 50u;
    s.gpr[26] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(kEnd)));
    s.pc     = s_va;
    for (uint32_t pass = 1; pass <= 3u; ++pass) {
        TEST_CHECK(rig.Step() == RunStatus::kRunning);
        TEST_CHECK(s.gpr[8] == pass);
        TEST_CHECK(s.gpr[10] == pass);
        TEST_CHECK(s.pc == s_va);
    }
    rig.RunTo(kEnd, 60u);
    TEST_CHECK(s.gpr[8] == 50u);
    TEST_CHECK(s.gpr[10] == 50u);
}

void TestIsaSwitchProbe() {
    cerfengine::MipsFeatures f = Mips3();
    f.mips16                   = true;
    ChainRig       rig(f);
    const uint32_t f16  = kBase + 0x300u;
    const uint32_t p2   = kBase + 0x100u;
    const uint32_t loop = p2 + 8u;
    rig.Load(kBase, {MipsGenJ(O::kJ, f16), 0u});
    rig.Load(f16, {0x6500E820u, Addiu(12u, 0u, 77u), MipsGenJ(O::kJ, p2), 0u});
    rig.Load(p2, {Addiu(12u, 0u, 0u), Addiu(13u, 0u, 0u), MipsGenJ(O::kJALX, f16), 0u,
                  Addiu(13u, 13u, 1u), Branch(O::kBNE, 13u, 14u, p2 + 0x14u, loop), 0u,
                  MipsGenJ(O::kJ, kEnd), 0u});
    rig.State().gpr[14] = 5u;
    rig.RunTo(kEnd, 60u);
    TEST_CHECK(rig.State().gpr[12] == 0u);
    TEST_CHECK(rig.State().gpr[13] == 5u);
}

}

int main() {
    TestLoopChains();
    TestDeadlinePoll();
    TestHostExit();
    TestInterruptKick();
    TestPowerRequestKicks();
    TestClockEventKick();
    TestStatusWriteEndsBlock();
    TestCauseWriteEndsBlock();
    TestStatusWriteInDelaySlot();
    TestRfeEndsBlock();
    TestStoreUnlinksChain();
    TestSplitBlockRevalidates();
    TestSplitBlockNeverChainsToItself();
    TestIsaSwitchProbe();
    std::printf("mips_chain_test passed\n");
    return 0;
}
