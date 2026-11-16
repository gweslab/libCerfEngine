#include "sh_fake_hosts.h"
#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <functional>

namespace {

using cerfengine::ShCpuState;

constexpr uint32_t kImask5    = 5u << cerfengine::kShSrImaskShift;
constexpr uint32_t kSrBlocked = cerfengine::kShSrMd | cerfengine::kShSrRb | cerfengine::kShSrBl | kImask5;
constexpr uint32_t kAboveMask = 6u;
constexpr uint32_t kAtMask    = 5u;

class BlockCases {
public:
    BlockCases(bool verify, const char* mode) : rig_(false, verify), mode_(mode) {
        rig_.LoadReturningHandler(kShRigInterruptVector);
        rig_.Interrupts().SetHost(&hosts_);
        rig_.Interrupts().SetSleepHost(&hosts_);
    }

    void Run() {
        BlockHoldsARequest();
        RequestThatIgnoresTheBlockIsTaken();
        RequestThatIgnoresTheBlockKeepsTheMask();
        std::printf("%s: SH interrupt block passed\n", mode_);
    }

private:
    ShCpuState Initial() const {
        ShCpuState s{};
        s.sr = kSrBlocked;
        return s;
    }

    std::function<void(ShCpuState&)> Prepare() {
        return [this](ShCpuState& s) {
            s.vbr     = kShRigVbr;
            s.gpr[13] = rig_.ReturnStubVa();
        };
    }

    void ExpectNoEntry(const char* what) {
        const ShCpuState& s = rig_.State();
        ShRigExpect(mode_, what, "interrupt handler entries", s.gpr[9], 0u);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 0u);
        ShRigExpect(mode_, what, "SR", s.sr, kSrBlocked);
    }

    void BlockHoldsARequest() {
        const char* what = "a request above the mask with SR.BL set";
        hosts_.accepted  = 0u;
        hosts_.Raise(rig_.Interrupts(), kAboveMask);
        rig_.Run({kShRigNop, kShRigNop}, Initial(), what, Prepare());
        ExpectNoEntry(what);
    }

    void RequestThatIgnoresTheBlockIsTaken() {
        const char* what = "a request above the mask that ignores SR.BL, with SR.BL set";
        hosts_.accepted  = 0u;
        hosts_.Raise(rig_.Interrupts(), kAboveMask, true);
        const uint32_t start = rig_.Run({kShRigNop, kShRigNop}, Initial(), what, Prepare());
        const ShCpuState& s  = rig_.State();
        ShRigExpect(mode_, what, "interrupt handler entries", s.gpr[9], 1u);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 1u);
        ShRigExpect(mode_, what, "INTEVT", s.intevt, kShRigFakeIntevt);
        ShRigExpect(mode_, what, "SPC", s.spc, start);
        ShRigExpect(mode_, what, "SSR", s.ssr, kSrBlocked);
        ShRigExpect(mode_, what, "SR", s.sr, kSrBlocked);
    }

    void RequestThatIgnoresTheBlockKeepsTheMask() {
        const char* what = "a request at the mask that ignores SR.BL, with SR.BL set";
        hosts_.accepted  = 0u;
        hosts_.Raise(rig_.Interrupts(), kAtMask, true);
        rig_.Run({kShRigNop, kShRigNop}, Initial(), what, Prepare());
        ExpectNoEntry(what);
    }

    ShIrRig        rig_;
    ShRigFakeHosts hosts_;
    const char*    mode_;
};

}

int main() {
    BlockCases(false, "plain").Run();
    BlockCases(true, "verified IR").Run();
    return 0;
}
