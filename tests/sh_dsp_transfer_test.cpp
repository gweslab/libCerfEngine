#include "sh_dsp_rig.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using cerfengine::ShCpuState;

constexpr uint16_t kMovxIncX0   = 0xF008u;
constexpr uint16_t kMovxIncX1   = 0xF088u;
constexpr uint16_t kMovyIncY0   = 0xF002u;
constexpr uint16_t kMovyIncY1   = 0xF042u;
constexpr uint16_t kMovyIndexY0 = 0xF003u;
constexpr uint16_t kMovyA0Index = 0xF013u;
constexpr uint16_t kLoadX0AtR4  = 0x004u;
constexpr uint16_t kStoreA0AtR4 = 0x024u;
constexpr uint16_t kStoreA1Both = 0x0F5u;
constexpr uint16_t kPaddX0Y0A0  = 0xB107u;
constexpr uint16_t kPcopyX0A0   = 0xD907u;
constexpr uint16_t kBraSkipWide = 0xA001u;
constexpr uint16_t kBtsSkipAdd  = 0x8D02u;

constexpr uint16_t kMovsWLoadIncA0     = 0xF478u;
constexpr uint16_t kMovsLStoreDecA1    = 0xF753u;
constexpr uint16_t kMovsWLoadA1g       = 0xF6D4u;
constexpr uint16_t kMovsLStoreIncA0g   = 0xF5FBu;
constexpr uint16_t kMovsWStoreIndexX0  = 0xF48Du;
constexpr uint16_t kMovsLLoadM0        = 0xF7C6u;
constexpr uint16_t kMovsWStoreA1g      = 0xF6D5u;
constexpr uint16_t kMovsLLoadDecX0     = 0xF482u;
constexpr uint16_t kMovsReservedDs     = 0xF460u;
constexpr uint32_t kMovsData           = kShRigDataVa;
constexpr uint16_t kPwsbX0Y0A0         = 0xA407u;
constexpr uint16_t kPwadX0Y0A0         = 0xB407u;

constexpr uint32_t kXyRam      = 0x7000u;
constexpr uint32_t kModuloMod  = 0x70047000u;
constexpr uint32_t kModuloBase = 0x08017000u;
constexpr uint32_t kPageVa     = 0x00720000u;
constexpr uint32_t kPagePa     = 0x0001B000u;
constexpr uint32_t kTailVa     = kPageVa + kShRigPageSize;
constexpr uint32_t kTailPa     = 0x0001C800u;
constexpr uint32_t kWideVa     = kTailVa - 2u;

struct Context {
    ShIrRig&    rig;
    const char* mode;
};

void ExpectGpr(const Context& t, const char* what, uint32_t r, uint32_t want) {
    const std::string field = "R" + std::to_string(r);
    ShRigExpect(t.mode, what, field.c_str(), t.rig.State().gpr[r], want);
}

void ExpectWord(const Context& t, const char* what, bool x, uint32_t address, uint16_t want) {
    ShRigExpect(t.mode, what, x ? "X memory" : "Y memory", DspXyWord(t.rig, x, address), want);
}

void RunPointerMove(const Context& t, const char* what, uint16_t op, uint32_t gpr, uint32_t pointer, uint32_t index,
                    uint32_t want_pointer) {
    ShCpuState in{};
    in.sr       = kDspSr;
    in.gpr[gpr] = pointer;
    in.gpr[9]   = index;
    t.rig.Run({op}, in, what);
    ExpectGpr(t, what, gpr, want_pointer);
}

void RunManualMoves(const Context& t) {
    ShCpuState in{};
    in.sr     = kDspSr;
    in.gpr[4] = 0x08010000u | kXyRam;
    DspSetXyPair(t.rig, true, kXyRam, 0x5555u, 0u);
    t.rig.Run({kMovxIncX0}, in, "MOVX.W @R4+,X0", [](ShCpuState& s) { DspLoad(s, {.x0 = 0x12345678u}); });
    ExpectGpr(t, "MOVX.W @R4+,X0", 4u, 0x08010002u | kXyRam);
    DspExpect(t.mode, "MOVX.W @R4+,X0", t.rig.State(), {.x0 = 0x55550000u});

    in        = ShCpuState{};
    in.sr     = kDspSr;
    in.gpr[6] = 0x08020000u | kXyRam;
    in.gpr[9] = 6u;
    t.rig.Run({kMovyA0Index}, in, "MOVY.W A0,@R6+R9", [](ShCpuState& s) { DspLoad(s, {.a0 = 0x123456789Aull}); });
    ExpectGpr(t, "MOVY.W A0,@R6+R9", 6u, 0x08020006u | kXyRam);
    ExpectWord(t, "MOVY.W A0,@R6+R9", false, kXyRam, 0x3456u);

    RunPointerMove(t, "MOVX.W @R4+ carrying into the upper word", kMovxIncX0, 4u, 0x0801FFFEu, 0u, 0x08020000u);
    RunPointerMove(t, "MOVY.W @R6+ keeping the upper word", kMovyIncY0, 6u, 0x0802FFFEu, 0u, 0x08020000u);
    RunPointerMove(t, "MOVY.W @R6+R9 with R9 = -2 keeping the upper word", kMovyIndexY0, 6u, 0x08020000u, 0xFFFFFFFEu,
                   0x0802FFFEu);
}

void RunModulo(const Context& t, const char* what, bool x, uint32_t sr_extra, uint32_t want_first,
               uint32_t want_second, uint32_t want_pointer) {
    const uint32_t pointer = x ? 4u : 6u;
    DspSetXyPair(t.rig, x, kXyRam, 0x1111u, 0x2222u);
    DspSetXyPair(t.rig, x, kXyRam + 4u, 0x3333u, 0x4444u);
    ShCpuState in{};
    in.sr           = kDspSr | sr_extra;
    in.gpr[pointer] = kModuloBase;
    const std::vector<uint16_t> body =
        x ? std::vector<uint16_t>{kMovxIncX0, kMovxIncX1, kMovxIncX0, kMovxIncX1}
          : std::vector<uint16_t>{kMovyIncY0, kMovyIncY1, kMovyIncY0, kMovyIncY1};
    t.rig.Run(body, in, what, [](ShCpuState& s) { s.mod = kModuloMod; });
    const ShCpuState& s = t.rig.State();
    ShRigExpect(t.mode, what, "first register", x ? s.x0 : s.y0, want_first);
    ShRigExpect(t.mode, what, "second register", x ? s.x1 : s.y1, want_second);
    ExpectGpr(t, what, pointer, want_pointer);
}

void RunOrdering(const Context& t) {
    ShCpuState in{};
    in.sr     = kDspSr;
    in.gpr[4] = kXyRam + 0x100u;
    DspSetXyPair(t.rig, true, kXyRam + 0x100u, 0x1234u, 0u);
    const DspRegs sum{.x0 = 0x00010000u, .y0 = 0x00020000u};
    t.rig.Run(DspParallel(kLoadX0AtR4, kPaddX0Y0A0), in, "MOVX.W @R4,X0 with PADD X0,Y0,A0",
              [&](ShCpuState& s) { DspLoad(s, sum); });
    DspExpect(t.mode, "MOVX.W @R4,X0 with PADD X0,Y0,A0", t.rig.State(),
              {.a0 = 0x30000ull, .x0 = 0x12340000u, .y0 = 0x00020000u, .dsr = 0x80u});
    ExpectGpr(t, "MOVX.W @R4,X0 with PADD X0,Y0,A0", 4u, kXyRam + 0x100u);

    in.gpr[4] = kXyRam + 0x200u;
    t.rig.Run(DspParallel(kStoreA0AtR4, kPaddX0Y0A0), in, "MOVX.W A0,@R4 with PADD X0,Y0,A0", [&](ShCpuState& s) {
        DspLoad(s, {.a0 = 0x00ABCD0000ull, .x0 = 0x00010000u, .y0 = 0x00020000u});
    });
    ExpectWord(t, "MOVX.W A0,@R4 with PADD X0,Y0,A0", true, kXyRam + 0x200u, 0xABCDu);

    in.gpr[4] = kXyRam + 0x300u;
    in.gpr[6] = kXyRam + 0x400u;
    t.rig.Run(DspParallel(kStoreA1Both, kPaddX0Y0A0), in, "MOVX.W A1,@R4 MOVY.W A1,@R6 with PADD", [&](ShCpuState& s) {
        DspLoad(s, {.a1 = 0x00CAFE0000ull, .x0 = 0x00010000u, .y0 = 0x00020000u});
    });
    ExpectWord(t, "MOVX.W A1,@R4", true, kXyRam + 0x300u, 0xCAFEu);
    ExpectWord(t, "MOVY.W A1,@R6", false, kXyRam + 0x400u, 0xCAFEu);
    DspExpect(t.mode, "MOVX.W A1,@R4 MOVY.W A1,@R6 with PADD", t.rig.State(),
              {.a0 = 0x30000ull, .a1 = 0x00CAFE0000ull, .x0 = 0x00010000u, .y0 = 0x00020000u, .dsr = 0x80u});
}

void RunConflict(const Context& t, const char* what, std::vector<uint16_t> body) {
    ShCpuState in{};
    in.sr                    = kDspSr;
    in.gpr[4]                = kXyRam;
    const std::string detail = t.rig.RunToHalt(std::move(body), in, what);
    ShRigExpectHalt(t.mode, what, detail, "names one destination register in two parallel operations");
}

void RunUnspecified(const Context& t, const char* what, uint16_t field_b, const char* halt) {
    ShCpuState in{};
    in.sr                    = kDspSr;
    const std::string detail = t.rig.RunToHalt(DspParallel(0u, field_b), in, what);
    ShRigExpectHalt(t.mode, what, detail, halt);
}

uint32_t DataWord(const Context& t, uint32_t va) { return cerfengine::le::U32(&t.rig.Host().Ram()[ShRigPa(va)]); }

uint16_t DataHalf(const Context& t, uint32_t va) { return cerfengine::le::U16(&t.rig.Host().Ram()[ShRigPa(va)]); }

void SetDataWord(const Context& t, uint32_t va, uint32_t value) { t.rig.Host().LoadWords(ShRigPa(va), &value, 1u); }

void RunMovsCase(const Context& t, const char* what, uint16_t op, uint32_t gpr, uint32_t pointer,
                 const DspRegs& before, const DspRegs& after, uint32_t want_pointer) {
    ShCpuState in{};
    in.sr       = kDspSr;
    in.gpr[gpr] = pointer;
    in.gpr[8]   = 6u;
    t.rig.Run({op}, in, what, [&](ShCpuState& s) { DspLoad(s, before); });
    DspExpect(t.mode, what, t.rig.State(), after);
    ExpectGpr(t, what, gpr, want_pointer);
}

void RunMovs(const Context& t) {
    SetDataWord(t, kMovsData, 0x00008765u);
    RunMovsCase(t, "MOVS.W @R4+,A0", kMovsWLoadIncA0, 4u, kMovsData, {.a0 = 0x123456789Aull},
                {.a0 = 0xFF87650000ull}, kMovsData + 2u);
    RunMovsCase(t, "MOVS.L A1,@-R3", kMovsLStoreDecA1, 3u, kMovsData + 0x800u, {.a1 = 0x123456789Aull},
                {.a1 = 0x123456789Aull}, kMovsData + 0x7FCu);
    ShRigExpect(t.mode, "MOVS.L A1,@-R3", "memory", DataWord(t, kMovsData + 0x7FCu), 0x3456789Au);
    SetDataWord(t, kMovsData + 0x10u, 0x0000AB12u);
    RunMovsCase(t, "MOVS.W @R2,A1G", kMovsWLoadA1g, 2u, kMovsData + 0x10u, {}, {.a1 = 0x1200000000ull},
                kMovsData + 0x10u);
    RunMovsCase(t, "MOVS.L A0G,@R5+", kMovsLStoreIncA0g, 5u, kMovsData + 0x20u, {.a0 = 0x8000000000ull},
                {.a0 = 0x8000000000ull}, kMovsData + 0x24u);
    ShRigExpect(t.mode, "MOVS.L A0G,@R5+", "memory", DataWord(t, kMovsData + 0x20u), 0xFFFFFF80u);
    RunMovsCase(t, "MOVS.W X0,@R4+R8", kMovsWStoreIndexX0, 4u, kMovsData + 0x30u, {.x0 = 0xCAFE1234u},
                {.x0 = 0xCAFE1234u}, kMovsData + 0x36u);
    ShRigExpect(t.mode, "MOVS.W X0,@R4+R8", "memory", DataHalf(t, kMovsData + 0x30u), 0xCAFEu);
    SetDataWord(t, kMovsData + 0x40u, 0x11223344u);
    RunMovsCase(t, "MOVS.L @R3,M0", kMovsLLoadM0, 3u, kMovsData + 0x40u, {}, {.m0 = 0x11223344u},
                kMovsData + 0x40u);
    RunMovsCase(t, "MOVS.W A1G,@R2", kMovsWStoreA1g, 2u, kMovsData + 0x50u, {.a1 = 0x8000000000ull},
                {.a1 = 0x8000000000ull}, kMovsData + 0x50u);
    ShRigExpect(t.mode, "MOVS.W A1G,@R2", "memory", DataHalf(t, kMovsData + 0x50u), 0xFF80u);

    const char* what = "MOVS.L @-R4,X0 to a misaligned address";
    ShCpuState  in{};
    in.sr     = kDspSr;
    in.gpr[4] = kMovsData + 2u;
    t.rig.Run({kMovsLLoadDecX0}, in, what, [&](ShCpuState& s) {
        s.vbr     = kShRigVbr;
        s.gpr[13] = t.rig.ReturnStubVa();
    });
    ShRigExpect(t.mode, what, "EXPEVT", t.rig.State().expevt, kShRigExpevtAddressRead);
    ShRigExpect(t.mode, what, "TEA", t.rig.Mmu().State()->tea, kMovsData - 2u);
    ExpectGpr(t, what, 4u, kMovsData + 2u);
}

void RunException(const Context& t, const char* what, std::vector<uint16_t> body, uint32_t sr, uint32_t expevt) {
    ShCpuState in{};
    in.sr                = sr;
    const uint32_t start = t.rig.Run(std::move(body), in, what, [&](ShCpuState& s) {
        s.vbr     = kShRigVbr;
        s.gpr[13] = t.rig.ReturnStubVa();
    });
    const ShCpuState& s = t.rig.State();
    ShRigExpect(t.mode, what, "general handler entries", s.gpr[9], 1u);
    ShRigExpect(t.mode, what, "EXPEVT", s.expevt, expevt);
    ShRigExpect(t.mode, what, "SPC", s.spc, start);
}

void RunSlots(const Context& t) {
    const DspRegs in_regs{.x0 = 0x22222222u, .y0 = 0x33333333u};
    const DspRegs out_regs{.a0 = 0x0055555555ull, .x0 = 0x22222222u, .y0 = 0x33333333u, .dsr = 0x80u};
    ShCpuState    in{};
    in.sr = kDspSr;
    t.rig.Run({kBraSkipWide, 0xF800u, kPaddX0Y0A0}, in, "PADD in the delay slot of BRA",
              [&](ShCpuState& s) { DspLoad(s, in_regs); });
    DspExpect(t.mode, "PADD in the delay slot of BRA", t.rig.State(), out_regs);
    for (uint32_t taken = 0; taken < 2u; ++taken) {
        const char* what = taken != 0u ? "PADD in the slot of a taken BT/S" : "PADD in the slot of a BT/S not taken";
        in.sr            = kDspSr | (taken != 0u ? cerfengine::kShSrT : 0u);
        in.gpr[10]       = 0u;
        t.rig.Run({kBtsSkipAdd, 0xF800u, kPaddX0Y0A0, kShRigAddOneR10}, in, what,
                  [&](ShCpuState& s) { DspLoad(s, in_regs); });
        DspExpect(t.mode, what, t.rig.State(), out_regs);
        ExpectGpr(t, what, 10u, taken != 0u ? 0u : 1u);
    }
}

void RunPageStraddle(bool verify, const char* mode, bool tail_mapped) {
    const char* what = tail_mapped ? "32-bit instruction across two mapped 1 KB pages"
                                   : "32-bit instruction whose second halfword misses the TLB";
    ShIrRig     rig(true, verify);
    rig.LoadReturningHandler(kShRigTlbMissVector);
    rig.LoadCode(kPagePa + kShRigPageSize - 4u, {kShRigNop, 0xF800u});
    rig.LoadCode(kTailPa, {kPcopyX0A0, kShRigRts, kShRigNop});
    ShCpuState in{};
    in.sr     = kDspSr;
    in.gpr[1] = kWideVa - 2u;
    rig.Run({kShRigJsrR1, kShRigNop}, in, what, [&](ShCpuState& s) {
        s.vbr     = kShRigVbr;
        s.gpr[13] = rig.ReturnStubVa();
        rig.ResetMmu(0u, kShRigAsid);
        rig.Preload(0u, kPageVa, kShRigAsid, kPagePa | kPage1kRw);
        if (tail_mapped) rig.Preload(1u, kTailVa, kShRigAsid, kTailPa | kPage1kRw);
        DspLoad(s, {.x0 = 0x12345678u});
    });
    const ShCpuState& s = rig.State();
    if (tail_mapped) {
        ShRigExpect(mode, what, "handler entries", s.gpr[9], 0u);
        DspExpect(mode, what, s, {.a0 = 0x0012345678ull, .x0 = 0x12345678u, .dsr = 0x80u});
        return;
    }
    ShRigExpect(mode, what, "handler entries", s.gpr[9], 1u);
    ShRigExpect(mode, what, "EXPEVT", s.expevt, kShRigExpevtTlbRead);
    ShRigExpect(mode, what, "SPC", s.spc, kWideVa);
    ShRigExpect(mode, what, "TEA", rig.Mmu().State()->tea, kTailVa);
}

void RunAll(bool verify, const char* mode) {
    ShIrRig rig(true, verify, true);
    DspMapXyMemory(rig);
    rig.LoadReturningHandler(kShRigGeneralVector);
    const Context t{rig, mode};
    RunManualMoves(t);
    RunModulo(t, "X modulo addressing", true, cerfengine::kShSrDmx, 0x33330000u, 0x11110000u, 0x08017002u);
    RunModulo(t, "Y modulo addressing", false, cerfengine::kShSrDmy, 0x33330000u, 0x11110000u, 0x08017002u);
    RunModulo(t, "X addressing with DMX and DMY set", true, cerfengine::kShSrDmx | cerfengine::kShSrDmy, 0x33330000u,
              0x44440000u, 0x08017008u);
    RunOrdering(t);
    RunSlots(t);
    RunConflict(t, "MOVX.W @R4,X0 with PADD X1,Y0,X0",
                DspParallel(kLoadX0AtR4, DspOp3(0xB1u, kDspSxX1, kDspSyY0, kDspX0)));
    RunConflict(t, "PADD A0,M0,A0 with PMULS X0,Y0,A0",
                DspParallel(0u, DspMultiply(7u, kDspSeX0, kDspSfY0, kDspSxA0, kDspSyM0, kDspDgA0, kDspDuA0)));
    RunUnspecified(t, "PWSB X0,Y0,A0", kPwsbX0Y0A0, "PWSB / PWAD field B 0xA407 has no defined operation");
    RunUnspecified(t, "PWAD X0,Y0,A0", kPwadX0Y0A0, "PWSB / PWAD field B 0xB407 has no defined operation");
    RunException(t, "PWSB with SR.DSP clear", DspParallel(0u, kPwsbX0Y0A0), kShRigSrPrivileged,
                 kShRigExpevtReservedInstruction);
    RunException(t, "PWSB with a reserved Dz code", DspParallel(0u, DspOp3(0xA4u, kDspSxX0, kDspSyY0, 0u)), kDspSr,
                 kShRigExpevtReservedInstruction);
    RunException(t, "PWAD with an undefined NOPX field", DspParallel(0xA0u, kPwadX0Y0A0), kDspSr,
                 kShRigExpevtReservedInstruction);
    RunMovs(t);
    RunException(t, "MOVS with a reserved Ds code", {kMovsReservedDs}, kDspSr, kShRigExpevtReservedInstruction);
    RunException(t, "MOVS with SR.DSP clear", {kMovsWLoadIncA0}, kShRigSrPrivileged, kShRigExpevtReservedInstruction);
    RunException(t, "reserved field B", DspParallel(0u, 0xAC00u), kDspSr, kShRigExpevtReservedInstruction);
    RunException(t, "undefined NOPX field", {0xF0A0u}, kDspSr, kShRigExpevtReservedInstruction);
    RunException(t, "H'FC00", {0xFC00u}, kDspSr, kShRigExpevtReservedInstruction);
    RunException(t, "PADD with SR.DSP clear", DspParallel(0u, kPaddX0Y0A0), kShRigSrPrivileged,
                 kShRigExpevtReservedInstruction);
    RunException(t, "PADD with SR.DSP clear in the slot of BRA", {kBraSkipWide, 0xF800u, kPaddX0Y0A0},
                 kShRigSrPrivileged, kShRigExpevtIllegalSlot);
    RunPageStraddle(verify, mode, true);
    RunPageStraddle(verify, mode, false);
    std::printf("%s: SH-DSP data transfers passed\n", mode);
}

}

int main() {
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    return 0;
}
