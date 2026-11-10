#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using cerfengine::ShCpuState;

constexpr uint32_t kJsrTarget   = 0xA0018000u;
constexpr uint32_t kNoException = 0u;
constexpr uint32_t kPrSentinel  = 0x5A5AA5A5u;

constexpr uint16_t kBtsNext   = 0x8D00u;
constexpr uint16_t kLdcR0Sr   = 0x400Eu;
constexpr uint16_t kUndefined = 0xF000u;
constexpr uint16_t kLdsR0Dsr  = 0x406Au;

struct Case {
    const char*           what;
    std::vector<uint16_t> body;
    uint32_t              sr_extra;
    uint32_t              expevt;
    bool                  links;
};

void RunCase(ShIrRig& rig, const Case& c, const char* mode) {
    ShCpuState initial{};
    initial.sr           = kShRigSrPrivileged | c.sr_extra;
    initial.gpr[3]       = kJsrTarget;
    const uint32_t start = rig.Run(c.body, initial, c.what, [&](ShCpuState& s) {
        s.vbr     = kShRigVbr;
        s.pr      = kPrSentinel;
        s.gpr[13] = rig.ReturnStubVa();
    });
    const ShCpuState& s = rig.State();
    if (c.expevt == kNoException) {
        ShRigExpect(mode, c.what, "general handler entries", s.gpr[9], 0u);
        return;
    }
    ShRigExpect(mode, c.what, "general handler entries", s.gpr[9], 1u);
    ShRigExpect(mode, c.what, "EXPEVT", s.expevt, c.expevt);
    ShRigExpect(mode, c.what, "SPC", s.spc, start);
    ShRigExpect(mode, c.what, "SSR", s.ssr, initial.sr);
    ShRigExpect(mode, c.what, "SR", s.sr, initial.sr | cerfengine::kShSrBl);
    ShRigExpect(mode, c.what, "PR", s.pr, c.links ? start + 4u : kPrSentinel);
}

void RunRteIllegalSlot(ShIrRig& rig, const char* mode) {
    const char* what = "BRA in the delay slot of RTE";
    ShCpuState  initial{};
    initial.sr               = kShRigSrPrivileged;
    const std::string detail = rig.RunToHalt({kShRigRte, kShRigBraNextNop}, initial, what, [](ShCpuState& s) {
        s.vbr = kShRigVbr;
        s.ssr = kShRigSrPrivileged;
        s.spc = s.pc;
    });
    ShRigExpectHalt(mode, what, detail, "exception 0x1A0 in the delay slot of RTE");
}

void RunAll(bool dsp, bool verify, const char* mode, const std::vector<Case>& cases) {
    ShIrRig rig(dsp, verify);
    rig.LoadReturningHandler(kShRigGeneralVector);
    for (const Case& c : cases) RunCase(rig, c, mode);
    RunRteIllegalSlot(rig, mode);
    std::printf("%s: SH illegal instructions passed\n", mode);
}

}

int main() {
    const std::vector<Case> sh3 = {
        {"BRA in the delay slot of BRA", {kShRigBraNextNop, kShRigBraNextNop}, 0u, kShRigExpevtIllegalSlot, false},
        {"LDC SR in the delay slot of JSR", {kShRigJsrR3, kLdcR0Sr}, 0u, kShRigExpevtIllegalSlot, true},
        {"BRA in the delay slot of a taken BT/S", {kBtsNext, kShRigBraNextNop}, cerfengine::kShSrT,
         kShRigExpevtIllegalSlot, false},
        {"BRA after a BT/S that is not taken", {kBtsNext, kShRigBraNextNop, kShRigNop}, 0u, kNoException, false},
        {"H'Fxxx outside a delay slot", {kUndefined}, 0u, kShRigExpevtReservedInstruction, false},
        {"H'Fxxx in the delay slot of BRA", {kShRigBraNextNop, kUndefined}, 0u, kShRigExpevtIllegalSlot, false},
    };
    const std::vector<Case> sh3_dsp = {
        {"LDS DSR with SR.DSP clear", {kLdsR0Dsr}, 0u, kShRigExpevtReservedInstruction, false},
        {"LDS DSR with SR.DSP clear in the delay slot of BRA", {kShRigBraNextNop, kLdsR0Dsr}, 0u,
         kShRigExpevtIllegalSlot, false},
        {"LDS DSR with SR.DSP set", {kLdsR0Dsr}, cerfengine::kShSrDsp, kNoException, false},
    };
    RunAll(false, false, "plain", sh3);
    RunAll(false, true, "verified IR", sh3);
    RunAll(true, false, "SH3-DSP plain", sh3_dsp);
    RunAll(true, true, "SH3-DSP verified IR", sh3_dsp);
    return 0;
}
