#include "sh_ir_rig.h"

#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_on_chip_code_host.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace {

constexpr uint32_t kRoutine       = 0xE00001DEu;
constexpr uint32_t kOnChipCodeEnd = 0xF0000000u;
constexpr uint32_t kArgument      = 0x12345678u;
constexpr uint16_t kJsrR2         = 0x420Bu;
constexpr uint16_t kMovWAtR2ToR0  = 0x6021u;

class ReturningRoutine final : public cerfengine::ShOnChipCodeHost {
public:
    void Call(uint32_t va, cerfengine::ShCpuState& state) override {
        ++calls;
        called_va = va;
        r4        = state.gpr[4];
        state.pc  = state.pr;
    }

    uint32_t calls     = 0u;
    uint32_t called_va = 0u;
    uint32_t r4        = 0u;
};

class OnChipCodeCases {
public:
    OnChipCodeCases(bool verify, const char* mode)
        : bare_(false, verify), rig_(false, verify, false, &routine_), mode_(mode) {}

    void Run() {
        CallWithoutAHostHalts();
        CallReachesTheHost();
        DataLoadStillHalts();
        CallAboveTheCodeAreaHalts();
        std::printf("%s: SH on-chip code passed\n", mode_);
    }

private:
    std::function<void(cerfengine::ShCpuState&)> PointR2At(uint32_t va) {
        return [va](cerfengine::ShCpuState& s) {
            s.gpr[2] = va;
            s.gpr[4] = kArgument;
        };
    }

    cerfengine::ShCpuState Initial() const {
        cerfengine::ShCpuState s{};
        s.sr = kShRigSrBase;
        return s;
    }

    void CallWithoutAHostHalts() {
        const char*       what   = "JSR into the on-chip code area with no host";
        const std::string detail = bare_.RunToHalt({kJsrR2, kShRigNop}, Initial(), what, PointR2At(kRoutine));
        ShRigExpectHalt(mode_, what, detail, "instruction fetch at PC 0xE00001DE in P4 is not modeled");
    }

    void CallReachesTheHost() {
        const char*    what  = "JSR into the on-chip code area";
        const uint32_t start = rig_.Run({kJsrR2, kShRigNop, kShRigAddOneR9}, Initial(), what, PointR2At(kRoutine));
        const cerfengine::ShCpuState& s = rig_.State();
        ShRigExpect(mode_, what, "host calls", routine_.calls, 1u);
        ShRigExpect(mode_, what, "called address", routine_.called_va, kRoutine);
        ShRigExpect(mode_, what, "R4 at the call", routine_.r4, kArgument);
        ShRigExpect(mode_, what, "PR", s.pr, start + 4u);
        ShRigExpect(mode_, what, "R9", s.gpr[9], 1u);
    }

    void DataLoadStillHalts() {
        const char*       what   = "MOV.W from the on-chip code area";
        const std::string detail = rig_.RunToHalt({kMovWAtR2ToR0, kShRigNop}, Initial(), what, PointR2At(kRoutine));
        ShRigExpectHalt(mode_, what, detail, "outside P0-P3 and the P4 on-chip I/O space");
    }

    void CallAboveTheCodeAreaHalts() {
        const char*       what   = "JSR into the cache address array with a host";
        const uint32_t    before = routine_.calls;
        const std::string detail = rig_.RunToHalt({kJsrR2, kShRigNop}, Initial(), what, PointR2At(kOnChipCodeEnd));
        ShRigExpectHalt(mode_, what, detail, "instruction fetch at PC 0xF0000000 in P4 is not modeled");
        ShRigExpect(mode_, what, "host calls", routine_.calls, before);
    }

    ReturningRoutine routine_;
    ShIrRig          bare_;
    ShIrRig          rig_;
    const char*      mode_;
};

}

int main() {
    OnChipCodeCases(false, "plain").Run();
    OnChipCodeCases(true, "verified IR").Run();
    return 0;
}
