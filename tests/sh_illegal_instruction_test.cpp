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
constexpr uint16_t kUndefined  = 0xF000u;
constexpr uint16_t kUnassigned = 0x0050u;
constexpr uint16_t kLdsR0Dsr   = 0x406Au;
constexpr uint16_t kTrapa5     = 0xC305u;
constexpr uint16_t kSetrc1     = 0x8201u;
constexpr uint16_t kSetrcR0    = 0x4014u;
constexpr uint16_t kLdrs       = 0x8C00u;
constexpr uint32_t kTrapExpevt = 0x160u;
constexpr uint32_t kTra5       = 5u << 2;
constexpr uint32_t kTraBefore  = 0x3FCu;

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

void RunRteIllegalSlot(ShIrRig& rig, const char* mode, const char* what, uint32_t restored_sr) {
    ShCpuState initial{};
    initial.sr               = kShRigSrPrivileged;
    const std::string detail = rig.RunToHalt({kShRigRte, kShRigBraNextNop}, initial, what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        s.ssr = restored_sr;
        s.spc = s.pc;
    });
    ShRigExpectHalt(mode, what, detail, "exception 0x1A0 in the delay slot of RTE");
}

void RunBlockedTrap(ShIrRig& rig, const char* mode) {
    const char* what = "TRAPA #5 with SR.BL set";
    ShCpuState  initial{};
    initial.sr            = kShRigSrPrivileged | cerfengine::kShSrBl;
    const uint32_t resets = rig.Host().CpuResets();
    rig.RunSteps({kTrapa5}, initial, 1u, what, [&](ShCpuState& s) {
        s.vbr = kShRigVbr;
        s.tra = kTraBefore;
    });
    const ShCpuState& s = rig.State();
    ShRigExpect(mode, what, "PC", s.pc, kShRigResetPc);
    ShRigExpect(mode, what, "SR", s.sr, kShRigResetSr);
    ShRigExpect(mode, what, "TRA", s.tra, kTraBefore);
    ShRigExpect(mode, what, "general handler entries", s.gpr[9], 0u);
    ShRigExpect(mode, what, "CPU reset notices", rig.Host().CpuResets(), resets + 1u);
}

void RunTrap(ShIrRig& rig, const char* mode) {
    const char* what = "TRAPA #5";
    ShCpuState  initial{};
    initial.sr           = kShRigSrPrivileged;
    const uint32_t start = rig.Run({kTrapa5}, initial, what, [&](ShCpuState& s) {
        s.vbr     = kShRigVbr;
        s.gpr[13] = rig.ReturnStubVa();
    });
    const ShCpuState& s = rig.State();
    ShRigExpect(mode, what, "general handler entries", s.gpr[9], 1u);
    ShRigExpect(mode, what, "EXPEVT", s.expevt, kTrapExpevt);
    ShRigExpect(mode, what, "TRA", s.tra, kTra5);
    ShRigExpect(mode, what, "SPC", s.spc, start + 2u);
    ShRigExpect(mode, what, "SSR", s.ssr, initial.sr);
    ShRigExpect(mode, what, "SR", s.sr, initial.sr | cerfengine::kShSrBl);
}

void RunRepeatControlHalt(ShIrRig& rig, const char* mode) {
    const char* what = "SETRC R0 with SR.DSP set";
    ShCpuState  initial{};
    initial.sr               = kShRigSrPrivileged | cerfengine::kShSrDsp;
    const std::string detail = rig.RunToHalt({kSetrcR0}, initial, what, [](ShCpuState& s) { s.vbr = kShRigVbr; });
    ShRigExpectHalt(mode, what, detail, "repeat control instruction 0x4014");
}

void RunAll(bool dsp, bool verify, const char* mode, const std::vector<Case>& cases) {
    ShIrRig rig(dsp, verify);
    rig.LoadReturningHandler(kShRigGeneralVector);
    for (const Case& c : cases) RunCase(rig, c, mode);
    RunTrap(rig, mode);
    RunBlockedTrap(rig, mode);
    if (dsp) RunRepeatControlHalt(rig, mode);
    RunRteIllegalSlot(rig, mode, "BRA in the delay slot of RTE", kShRigSrPrivileged);
    RunRteIllegalSlot(rig, mode, "BRA in the delay slot of RTE that restores SR.BL",
                      kShRigSrPrivileged | cerfengine::kShSrBl);
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
        {"unassigned H'0050 outside a delay slot", {kUnassigned}, 0u, kShRigExpevtReservedInstruction, false},
        {"unassigned H'0050 in the delay slot of BRA", {kShRigBraNextNop, kUnassigned}, 0u, kShRigExpevtIllegalSlot,
         false},
        {"TRAPA in the delay slot of BRA", {kShRigBraNextNop, kTrapa5}, 0u, kShRigExpevtIllegalSlot, false},
        {"SETRC #1 on a core without the DSP unit", {kSetrc1}, 0u, kShRigExpevtReservedInstruction, false},
    };
    const std::vector<Case> sh3_dsp = {
        {"LDS DSR with SR.DSP clear", {kLdsR0Dsr}, 0u, kShRigExpevtReservedInstruction, false},
        {"LDS DSR with SR.DSP clear in the delay slot of BRA", {kShRigBraNextNop, kLdsR0Dsr}, 0u,
         kShRigExpevtIllegalSlot, false},
        {"LDS DSR with SR.DSP set", {kLdsR0Dsr}, cerfengine::kShSrDsp, kNoException, false},
        {"SETRC #1 with SR.DSP clear", {kSetrc1}, 0u, kShRigExpevtReservedInstruction, false},
        {"LDRS with SR.DSP clear in the delay slot of BRA", {kShRigBraNextNop, kLdrs}, 0u, kShRigExpevtIllegalSlot,
         false},
        {"unassigned H'0050 outside a delay slot", {kUnassigned}, 0u, kShRigExpevtReservedInstruction, false},
        {"unassigned H'0050 in the delay slot of BRA", {kShRigBraNextNop, kUnassigned}, 0u, kShRigExpevtIllegalSlot,
         false},
    };
    RunAll(false, false, "plain", sh3);
    RunAll(false, true, "verified IR", sh3);
    RunAll(true, false, "SH3-DSP plain", sh3_dsp);
    RunAll(true, true, "SH3-DSP verified IR", sh3_dsp);
    return 0;
}
