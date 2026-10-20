#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_interrupt_channel.h>

#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kRamWordVa  = kRigRamBase + kRigRamSize - 4u;
constexpr uint32_t kMmioWordVa = kRigRamBase + kRigRamSize;
constexpr uint32_t kRamValue   = 0x5A5A1234u;
constexpr uint32_t kIrqVector  = 0x00000018u;

constexpr uint32_t kLdrR3FromR8   = 0xE5983000u;
constexpr uint32_t kLdmR0R1R2     = 0xE8900006u;
constexpr uint32_t kSvcIrqEnabled = 0x00000013u;

void RunCase(bool verify) {
    ArmIrRig rig(verify, cerfengine::ArmFeatures{});
    rig.Host().EnableMmioLog();
    cerfengine::ArmInterruptChannel* channel =
        &rig.Engine().Get<cerfengine::ArmInterruptChannel>();
    rig.Host().RaiseIrqOnMmio([channel] { channel->SetInterruptPending(); });
    const std::vector<uint32_t> ram_word = {kRamValue};
    rig.Host().LoadWords(kRamWordVa, ram_word.data(), ram_word.size());
    const std::vector<uint32_t> irq_loop = {kRigBranchSelf};
    rig.Host().LoadWords(kIrqVector, irq_loop.data(), irq_loop.size());
    channel->ClearInterruptPending();

    cerfengine::ArmCpuState initial{};
    initial.gprs[0] = kRamWordVa;
    initial.gprs[8] = kMmioWordVa;
    rig.Run({kLdrR3FromR8, kLdmR0R1R2, kRigBranchSelf}, initial, 0u, kSvcIrqEnabled);

    const auto& log = rig.Host().MmioLog();
    const auto& s   = rig.State();
    if (log.size() != 2u || log[1].write || log[1].pa != kMmioWordVa || log[1].width != 4u ||
        s.gprs[1] != kRamValue || s.gprs[2] != log[1].value || s.gprs[3] != log[0].value) {
        RigDump("state", s);
        TestFail("%s: an LDM whose second word reached MMIO with an IRQ pending did not complete "
                 "(mmio accesses %zu)",
                 verify ? "verified" : "unverified", log.size());
    }
}

}

int main() {
    RunCase(true);
    RunCase(false);
    std::printf("arm_block_transfer_irq_test passed\n");
    return 0;
}
