#pragma once

#include "test_host.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/cpu_state.h>
#include <cerfengine/guest_engine.h>

#include <cfenv>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

constexpr uint32_t kRigRamBase    = 0x00000000u;
constexpr uint32_t kRigRamSize    = 0x00010000u;
constexpr uint32_t kRigMmioPa     = 0x10000000u;
constexpr uint32_t kRigBranchSelf = 0xEAFFFFFEu;

class ArmIrRig {
public:
    ArmIrRig(bool verify, const cerfengine::ArmFeatures& features)
        : host_(kRigRamBase, kRigRamSize, kRigMmioPa) {
        setup_.isa       = cerfengine::Isa::kArm;
        setup_.arm       = features;
        setup_.verify_ir = verify;
        host_.Attach(setup_);
        system_.Attach(setup_);
        Start();
    }

    void Restart() {
        engine_.reset();
        Start();
    }

    cerfengine::GuestEngine::RunStatus Run(const std::vector<uint32_t>& program,
                                           const cerfengine::ArmCpuState& initial,
                                           uint32_t sctlr = 0u, uint32_t mode_cpsr = 0u,
                                           uint32_t spsr = 0u) {
        host_.LoadWords(kRigRamBase, program.data(), program.size());
        cpu_->RaiseResetException(kRigRamBase, false);
        engine_->Get<cerfengine::ArmMmu>().SetControlRegister(sctlr);
        if (mode_cpsr != 0u) cpu_->ReturnFromException(mode_cpsr, kRigRamBase);
        if (spsr != 0u) cpu_->BankedSpsr(cpu_->State()->cpsr.bits.mode)->word = spsr;
        guest_->FlushTranslationCache();
        cerfengine::ArmCpuState& s = *cpu_->State();
        std::memcpy(s.gprs, initial.gprs, sizeof(s.gprs));
        std::memcpy(s.vfp_d, initial.vfp_d, sizeof(s.vfp_d));
        s.fpscr               = initial.fpscr;
        s.fpexc               = initial.fpexc;
        s.nf                  = initial.nf;
        s.zf                  = initial.zf;
        s.cf                  = initial.cf;
        s.vf                  = initial.vf;
        s.guest_cycle_counter = 0u;
        s.chain_exit_request &= ~cerfengine::kChainExitClock;
        std::fesetround(FE_TONEAREST);
        return guest_->Run();
    }

    const cerfengine::ArmCpuState& State() const { return *cpu_->State(); }
    cerfengine::ArmCpu&            Cpu() { return *cpu_; }
    cerfengine::Engine&            Engine() { return *engine_; }
    TestHost&                      Host() { return host_; }
    const TestHost&                Host() const { return host_; }
    TestArmSystem&                 System() { return system_; }
    const TestArmSystem&           System() const { return system_; }

private:
    void Start() {
        engine_ = std::make_unique<cerfengine::Engine>(setup_);
        std::string error;
        if (!engine_->Start(error)) TestFail("Start: %s", error.c_str());
        engine_->BindClock(host_);
        cpu_ = &engine_->Get<cerfengine::ArmCpu>();
        host_.ObserveCycles(&cpu_->State()->guest_cycle_counter);
        cpu_->SetInitialStackPointer(kRigRamBase + kRigRamSize);
        cpu_->RaiseResetException(kRigRamBase, false);
        guest_ = &engine_->Get<cerfengine::GuestEngine>();
    }

    cerfengine::Setup                   setup_;
    TestHost                            host_;
    TestArmSystem                       system_;
    std::unique_ptr<cerfengine::Engine> engine_;
    cerfengine::ArmCpu*                 cpu_   = nullptr;
    cerfengine::GuestEngine*            guest_ = nullptr;
};

inline uint32_t RigPick(std::mt19937& rng, uint32_t n) {
    return static_cast<uint32_t>(rng() % n);
}

inline void RigDump(const char* label, const cerfengine::ArmCpuState& s) {
    std::printf("%s:", label);
    for (uint32_t i = 0; i < 16u; ++i) std::printf(" r%u=%08X", i, s.gprs[i]);
    std::printf(" nzcv=%u%u%u%u cpsr=%08X cycles=%u\n", s.nf, s.zf, s.cf, s.vf, s.cpsr.word,
                s.guest_cycle_counter);
}

inline bool RigSameState(const cerfengine::ArmCpuState& a, const cerfengine::ArmCpuState& b) {
    return std::memcmp(a.gprs, b.gprs, sizeof(a.gprs)) == 0 && a.nf == b.nf && a.zf == b.zf &&
           a.cf == b.cf && a.vf == b.vf && a.cpsr.word == b.cpsr.word &&
           a.guest_cycle_counter == b.guest_cycle_counter &&
           a.ldrex_monitor_addr == b.ldrex_monitor_addr &&
           a.ldrex_monitor_armed == b.ldrex_monitor_armed && a.idle_wait == b.idle_wait &&
           std::memcmp(a.sp_bank, b.sp_bank, sizeof(a.sp_bank)) == 0 &&
           std::memcmp(a.lr_bank, b.lr_bank, sizeof(a.lr_bank)) == 0 &&
           std::memcmp(a.spsr_bank, b.spsr_bank, sizeof(a.spsr_bank)) == 0 &&
           std::memcmp(a.r8_r12_fiq, b.r8_r12_fiq, sizeof(a.r8_r12_fiq)) == 0;
}

inline bool RigSameRun(ArmIrRig& a, cerfengine::GuestEngine::RunStatus status_a, ArmIrRig& b,
                       cerfengine::GuestEngine::RunStatus status_b) {
    if (status_a != status_b || !RigSameState(a.State(), b.State())) return false;
    return status_a != cerfengine::GuestEngine::RunStatus::kHalted ||
           std::strcmp(a.Engine().LastHalt().detail, b.Engine().LastHalt().detail) == 0;
}

inline void RigPrintCompileStats(const char* label, const TestHost& h) {
    const double insns = static_cast<double>(h.GuestInsnsCompiled());
    std::printf("%s: %llu guest insns, %.1f host bytes/insn, %.1f compile ns/insn\n", label,
                static_cast<unsigned long long>(h.GuestInsnsCompiled()),
                static_cast<double>(h.HostBytesEmitted()) / insns,
                static_cast<double>(h.CompileNs()) / insns);
}
