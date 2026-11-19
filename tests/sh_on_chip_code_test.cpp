#include "sh_ir_rig.h"

#include <cerfengine/byte_order.h>
#include <cerfengine/sh/sh_cpu_state.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kRoutine         = kShRigOnChipCodePa;
constexpr uint32_t kUnbackedCode    = 0xE0000000u;
constexpr uint32_t kOnChipCodeEnd   = 0xF0000000u;
constexpr uint32_t kArgument        = 0x12345678u;
constexpr uint16_t kJsrR2           = 0x420Bu;
constexpr uint16_t kMovWAtR2ToR0    = 0x6021u;
constexpr uint16_t kMovWR4ToAtR2    = 0x2241u;
constexpr uint16_t kRoutineFirstOp  = 0x000Bu;
constexpr uint16_t kRoutineSlotOp   = 0x2542u;

const std::vector<uint8_t> kStoreRoutine = {0x0Bu, 0x00u, 0x42u, 0x25u};

class OnChipCodeCases {
public:
    OnChipCodeCases(bool verify, const char* mode)
        : bare_(false, verify), rig_(false, verify, true, kStoreRoutine), mode_(mode) {}

    void Run() {
        CallWithoutCodeHalts();
        CallRunsTheRoutine();
        DataLoadReadsTheCode();
        StoreIntoTheCodeHalts();
        CallOutsideTheCodeHalts();
        CallAboveTheCodeAreaHalts();
        std::printf("%s: SH on-chip code passed\n", mode_);
    }

private:
    std::function<void(cerfengine::ShCpuState&)> PointR2At(uint32_t va) {
        return [va](cerfengine::ShCpuState& s) {
            s.gpr[2] = va;
            s.gpr[4] = kArgument;
            s.gpr[5] = kShRigDataVa;
        };
    }

    cerfengine::ShCpuState Initial() const {
        cerfengine::ShCpuState s{};
        s.sr = kShRigSrBase;
        return s;
    }

    void CallWithoutCodeHalts() {
        const char*       what   = "JSR into the on-chip code area with no code there";
        const std::string detail = bare_.RunToHalt({kJsrR2, kShRigNop}, Initial(), what, PointR2At(kRoutine));
        ShRigExpectHalt(mode_, what, detail, "reads PA 0xE00001DE, which no memory backs");
    }

    void CallRunsTheRoutine() {
        const char*    what  = "JSR into the on-chip routine";
        const uint32_t start = rig_.Run({kJsrR2, kShRigNop, kShRigAddOneR9}, Initial(), what, PointR2At(kRoutine));
        const cerfengine::ShCpuState& s    = rig_.State();
        const uint8_t*                data = rig_.Host().Ram().data() + (kShRigDataPa - kShRigRamPa);
        ShRigExpect(mode_, what, "stored word", cerfengine::le::U32(data), kArgument);
        ShRigExpect(mode_, what, "PR", s.pr, start + 4u);
        ShRigExpect(mode_, what, "R9", s.gpr[9], 1u);
    }

    void DataLoadReadsTheCode() {
        const char* what = "MOV.W from the on-chip routine";
        rig_.Run({kMovWAtR2ToR0, kShRigNop}, Initial(), what, PointR2At(kRoutine));
        ShRigExpect(mode_, what, "R0 at the first word", rig_.State().gpr[0], kRoutineFirstOp);
        rig_.Run({kMovWAtR2ToR0, kShRigNop}, Initial(), what, PointR2At(kRoutine + 2u));
        ShRigExpect(mode_, what, "R0 at the delay slot word", rig_.State().gpr[0], kRoutineSlotOp);
    }

    void StoreIntoTheCodeHalts() {
        const char*       what   = "MOV.W into the on-chip routine";
        const std::string detail = rig_.RunToHalt({kMovWR4ToAtR2, kShRigNop}, Initial(), what, PointR2At(kRoutine));
        ShRigExpectHalt(mode_, what, detail, "to VA 0xE00001DE PA 0xE00001DE");
    }

    void CallOutsideTheCodeHalts() {
        const char*       what   = "JSR into the on-chip code area outside the routine";
        const std::string detail = rig_.RunToHalt({kJsrR2, kShRigNop}, Initial(), what, PointR2At(kUnbackedCode));
        ShRigExpectHalt(mode_, what, detail, "reads PA 0xE0000000, which no memory backs");
    }

    void CallAboveTheCodeAreaHalts() {
        const char*       what   = "JSR into the cache address array";
        const std::string detail = rig_.RunToHalt({kJsrR2, kShRigNop}, Initial(), what, PointR2At(kOnChipCodeEnd));
        ShRigExpectHalt(mode_, what, detail, "instruction fetch at PC 0xF0000000 in P4 is not modeled");
    }

    ShIrRig     bare_;
    ShIrRig     rig_;
    const char* mode_;
};

}

int main() {
    OnChipCodeCases(false, "plain").Run();
    OnChipCodeCases(true, "verified IR").Run();
    return 0;
}
