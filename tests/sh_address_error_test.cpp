#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using cerfengine::ShCpuState;

constexpr uint32_t kSrHandler  = kShRigSrPrivileged | cerfengine::kShSrBl;
constexpr uint32_t kR2Sentinel = 0x5A5AA5A5u;

constexpr uint16_t kStoreWR2R1 = 0x2121u;
constexpr uint16_t kStoreLR2R1 = 0x2122u;

struct Case {
    const char*           what;
    std::vector<uint16_t> body;
    uint32_t              r1;
    uint32_t              expevt;
    bool                  spc_is_target;
    bool                  links;
};

void RunCase(ShIrRig& rig, const Case& c, const char* mode) {
    ShCpuState initial{};
    initial.sr     = kShRigSrPrivileged;
    initial.gpr[1] = c.r1;
    initial.gpr[2]       = kR2Sentinel;
    const uint32_t start = rig.Run(c.body, initial, c.what, [&](ShCpuState& s) {
        s.vbr     = kShRigVbr;
        s.gpr[3]  = s.pc;
        s.gpr[13] = rig.ReturnStubVa();
        rig.Mmu().WritePteh(kShRigAsid);
    });
    const ShCpuState& s = rig.State();
    ShRigExpect(mode, c.what, "general handler entries", s.gpr[9], 1u);
    ShRigExpect(mode, c.what, "EXPEVT", s.expevt, c.expevt);
    ShRigExpect(mode, c.what, "TEA", rig.Mmu().ReadTea(), c.r1);
    ShRigExpect(mode, c.what, "PTEH", rig.Mmu().ReadPteh(), (c.r1 & kShRigVpnMask) | kShRigAsid);
    ShRigExpect(mode, c.what, "SPC", s.spc, c.spc_is_target ? c.r1 : start);
    ShRigExpect(mode, c.what, "SSR", s.ssr, kShRigSrPrivileged);
    ShRigExpect(mode, c.what, "SR", s.sr, kSrHandler);
    ShRigExpect(mode, c.what, "R2", s.gpr[2], kR2Sentinel);
    if (c.links) ShRigExpect(mode, c.what, "PR", s.pr, start + 4u);
    for (uint32_t i = 0; i < 8u; ++i) {
        ShRigExpect(mode, c.what, "data byte", rig.Host().Ram()[ShRigPa(kShRigDataVa) + i], 0u);
    }
}

void RunAll(bool verify, const char* mode) {
    ShIrRig rig(false, verify, true);
    rig.LoadReturningHandler(kShRigGeneralVector);
    const Case cases[] = {
        {"call to an odd address", {kShRigJsrR1, kShRigNop}, 0xFFFFFD1Fu, kShRigExpevtAddressRead, true, true},
        {"longword load from 4n + 2", {kShRigLoadLR1R2}, kShRigDataVa + 2u, kShRigExpevtAddressRead, false, false},
        {"word store to 4n + 1", {kStoreWR2R1}, kShRigDataVa + 1u, kShRigExpevtAddressWrite, false, false},
        {"longword store to 4n + 3", {kStoreLR2R1}, kShRigDataVa + 3u, kShRigExpevtAddressWrite, false, false},
        {"longword load in a BRA delay slot", {kShRigBraNextNop, kShRigLoadLR1R2}, kShRigDataVa + 2u,
         kShRigExpevtAddressRead, false, false},
        {"longword load in a JSR delay slot", {kShRigJsrR3, kShRigLoadLR1R2}, kShRigDataVa + 2u,
         kShRigExpevtAddressRead, false, true},
    };
    for (const Case& c : cases) RunCase(rig, c, mode);
    std::printf("%s: SH address errors passed\n", mode);
}

}

int main() {
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    return 0;
}
