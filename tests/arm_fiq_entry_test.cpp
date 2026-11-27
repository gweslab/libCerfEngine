#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_interrupt_channel.h>

#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kIrqVector = 0x00000018u;
constexpr uint32_t kFiqVector = 0x0000001Cu;

constexpr uint32_t kSvcUnmasked  = 0x00000013u;
constexpr uint32_t kSvcFiqMasked = 0x00000053u;

constexpr uint32_t kSvcR8 = 0x08080808u;

struct Expect {
    uint32_t pc;
    uint32_t mode;
    uint32_t lr;
    uint32_t irq_disable;
    uint32_t fiq_disable;
    uint32_t async_abort_disable;
};

void RunCase(bool verify, const char* name, bool fiq, bool irq, uint32_t cpsr,
             const Expect& want) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    auto& channel = rig.Engine().Get<cerfengine::ArmInterruptChannel>();
    const std::vector<uint32_t> loop = {kRigBranchSelf};
    rig.Host().LoadWords(kIrqVector, loop.data(), loop.size());
    rig.Host().LoadWords(kFiqVector, loop.data(), loop.size());
    if (fiq) channel.SetFiqPending();
    if (irq) channel.SetInterruptPending();

    cerfengine::ArmCpuState initial{};
    initial.gprs[8] = kSvcR8;
    rig.Run({kRigBranchSelf}, initial, 0u, cpsr);

    const auto& s        = rig.State();
    const bool  took_fiq = want.mode == cerfengine::ArmMode::kFiq;
    const bool  bad_bank = took_fiq && rig.State().r8_r12_fiq[0] != kSvcR8;
    const bool  bad_spsr =
        want.pc != 0u &&
        rig.Cpu().BankedSpsr(want.mode)->word != cpsr;
    if (s.gprs[15] != want.pc || s.cpsr.bits.mode != want.mode ||
        (want.pc != 0u && s.gprs[14] != want.lr) ||
        s.cpsr.bits.irq_disable != want.irq_disable ||
        s.cpsr.bits.fiq_disable != want.fiq_disable ||
        s.cpsr.bits.async_abort_disable != want.async_abort_disable || bad_bank || bad_spsr) {
        RigDump("state", s);
        TestFail("%s (%s): pc=0x%08X mode=0x%02X lr=0x%08X I=%u F=%u A=%u",
                 name, verify ? "verified" : "unverified", s.gprs[15],
                 s.cpsr.bits.mode, s.gprs[14], s.cpsr.bits.irq_disable,
                 s.cpsr.bits.fiq_disable, s.cpsr.bits.async_abort_disable);
    }
}

void RunKickCase(bool verify) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    auto& channel = rig.Engine().Get<cerfengine::ArmInterruptChannel>();
    channel.SetInterruptPending();
    channel.SetFiqPending();
    channel.ClearInterruptPending();
    const uint32_t exits = rig.State().chain_exit_request;
    if ((exits & cerfengine::kChainExitFiq) == 0u || (exits & cerfengine::kChainExitIrq) != 0u) {
        TestFail("irq clear (%s): chain exits 0x%08X", verify ? "verified" : "unverified", exits);
    }
    channel.ClearFiqPending();
    if ((rig.State().chain_exit_request & cerfengine::kChainExitFiq) != 0u) {
        TestFail("fiq clear (%s): chain exits 0x%08X", verify ? "verified" : "unverified",
                 rig.State().chain_exit_request);
    }
}

void RunAll(bool verify) {
    RunKickCase(verify);
    RunCase(verify, "fiq taken", true, false, kSvcUnmasked,
            {kFiqVector, cerfengine::ArmMode::kFiq, 4u, 1u, 1u, 1u});
    RunCase(verify, "fiq before irq", true, true, kSvcUnmasked,
            {kFiqVector, cerfengine::ArmMode::kFiq, 4u, 1u, 1u, 1u});
    RunCase(verify, "masked fiq leaves irq", true, true, kSvcFiqMasked,
            {kIrqVector, cerfengine::ArmMode::kIrq, 4u, 1u, 1u, 1u});
    RunCase(verify, "masked fiq", true, false, kSvcFiqMasked,
            {0u, cerfengine::ArmMode::kSupervisor, 0u, 0u, 1u, 0u});
}

}

int main() {
    RunAll(true);
    RunAll(false);
    std::printf("arm_fiq_entry_test passed\n");
    return 0;
}
