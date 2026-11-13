#include "sh_dsp_rig.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

using cerfengine::ShCpuState;

constexpr uint32_t kSrS = cerfengine::kShSrS;

constexpr uint16_t kPaddX0Y0A0  = DspOp3(0xB1u, kDspSxX0, kDspSyY0, kDspA0);
constexpr uint16_t kPaddA0Y0A0  = DspOp3(0xB1u, kDspSxA0, kDspSyY0, kDspA0);
constexpr uint16_t kPaddcX0Y0M0 = DspOp3(0xB0u, kDspSxX0, kDspSyY0, kDspM0);
constexpr uint16_t kPsubcX0Y0M0 = DspOp3(0xA0u, kDspSxX0, kDspSyY0, kDspM0);
constexpr uint16_t kPshaX0Y0A0  = DspOp3(0x91u, kDspSxX0, kDspSyY0, kDspA0);
constexpr uint16_t kPshlX0Y0A0  = DspOp3(0x81u, kDspSxX0, kDspSyY0, kDspA0);
constexpr uint16_t kPmulsX0Y0A0 = DspMultiply(4u, kDspSeX0, kDspSfY0, 0u, 0u, kDspDgA0, 0u);
constexpr uint16_t kPcmpX0Y0    = 0x8400u;

struct Case {
    const char* what;
    uint16_t    field_b;
    uint32_t    sr_extra;
    DspRegs     init;
    DspRegs     want;
};

const Case kCases[] = {
    {"PABS X0,M0", DspOp3(0x88u, kDspSxX0, 0u, kDspM0), 0u, {.x0 = 0x33333333u, .m0 = 0x12345678u},
     {.x0 = 0x33333333u, .m0 = 0x33333333u, .dsr = 0x80u}},
    {"PABS X1,X1", DspOp3(0x88u, kDspSxX1, 0u, kDspX1), 0u, {.x1 = 0xDDDDDDDDu}, {.x1 = 0x22222223u, .dsr = 0x81u}},
    {"PADD X0,Y0,A0", kPaddX0Y0A0, 0u, {.a0 = 0x123456789Aull, .x0 = 0x22222222u, .y0 = 0x33333333u},
     {.a0 = 0x0055555555ull, .x0 = 0x22222222u, .y0 = 0x33333333u, .dsr = 0x80u}},
    {"PADD A0,M0,A0 PMULS X0,Y0,M0", DspMultiply(7u, kDspSeX0, kDspSfY0, kDspSxA0, kDspSyM0, kDspDgM0, kDspDuA0), 0u,
     {.a0 = 0x0055555555ull, .x0 = 0x00020000u, .y0 = 0x00030000u, .m0 = 0x22222222u},
     {.a0 = 0x0077777777ull, .x0 = 0x00020000u, .y0 = 0x00030000u, .m0 = 0x0000000Cu, .dsr = 0x80u}},
    {"PADDC X0,Y0,M0 with DC clear", kPaddcX0Y0M0, 0u, {.x0 = 0xB3333333u, .y0 = 0x55555555u, .m0 = 0x12345678u},
     {.x0 = 0xB3333333u, .y0 = 0x55555555u, .m0 = 0x08888888u, .dsr = 0x81u}},
    {"PADDC X0,Y0,M0 with DC set", kPaddcX0Y0M0, 0u,
     {.x0 = 0x33333333u, .y0 = 0x55555555u, .m0 = 0x12345678u, .dsr = 0x01u},
     {.x0 = 0x33333333u, .y0 = 0x55555555u, .m0 = 0x88888889u, .dsr = 0x90u}},
    {"PAND X0,Y0,A0", DspOp3(0x95u, kDspSxX0, kDspSyY0, kDspA0), 0u,
     {.a0 = 0x123456789Aull, .x0 = 0x33333333u, .y0 = 0x55555555u},
     {.a0 = 0x0011110000ull, .x0 = 0x33333333u, .y0 = 0x55555555u, .dsr = 0x00u}},
    {"PCLR A0", DspOp3(0x8Du, 0u, 0u, kDspA0), 0u, {.a0 = 0xFF87654321ull}, {.dsr = 0x40u}},
    {"PCOPY X0,A0", DspOp3(0xD9u, kDspSxX0, 0u, kDspA0), 0u, {.a0 = 0xFFFFFFFFull, .x0 = 0x55555555u},
     {.a0 = 0x0055555555ull, .x0 = 0x55555555u, .dsr = 0x80u}},
    {"PDEC X0,M0", DspOp3(0x89u, kDspSxX0, 0u, kDspM0), 0u, {.x0 = 0x0052330Fu, .m0 = 0x12345678u},
     {.x0 = 0x0052330Fu, .m0 = 0x00510000u, .dsr = 0x80u}},
    {"PDEC X1,X1", DspOp3(0x89u, kDspSxX1, 0u, kDspX1), 0u, {.x1 = 0xFC342855u}, {.x1 = 0xFC330000u, .dsr = 0x20u}},
    {"PDMSB X0,M0", DspOp3(0x9Du, kDspSxX0, 0u, kDspM0), 0u, {.x0 = 0x0052330Fu, .m0 = 0x12345678u},
     {.x0 = 0x0052330Fu, .m0 = 0x00080000u, .dsr = 0x80u}},
    {"PDMSB X1,X1", DspOp3(0x9Du, kDspSxX1, 0u, kDspX1), 0u, {.x1 = 0xFC342855u}, {.x1 = 0x00050000u, .dsr = 0x80u}},
    {"PDMSB X0,A0 of zero", DspOp3(0x9Du, kDspSxX0, 0u, kDspA0), 0u, {.a0 = 0x123456789Aull},
     {.a0 = 0x00001F0000ull, .dsr = 0x80u}},
    {"PINC X0,M0", DspOp3(0x99u, kDspSxX0, 0u, kDspM0), 0u, {.x0 = 0x0052330Fu, .m0 = 0x12345678u},
     {.x0 = 0x0052330Fu, .m0 = 0x00530000u, .dsr = 0x80u}},
    {"PINC X1,X1", DspOp3(0x99u, kDspSxX1, 0u, kDspX1), 0u, {.x1 = 0xFC342855u}, {.x1 = 0xFC350000u, .dsr = 0x20u}},
    {"PLDS A0,MACH", DspOp3(0xEDu, 0u, 0u, kDspA0), 0u, {.a0 = 0x123456789Aull, .mach = 0x66666666u},
     {.a0 = 0x123456789Aull, .mach = 0x3456789Au}},
    {"PMULS X0,Y0,M0", DspMultiply(4u, kDspSeX0, kDspSfY0, 0u, 0u, kDspDgM0, 0u), 0u,
     {.x0 = 0x00010000u, .y0 = 0x00020000u, .m0 = 0x33333333u}, {.x0 = 0x00010000u, .y0 = 0x00020000u, .m0 = 4u}},
    {"PMULS X1,Y1,A0", DspMultiply(4u, kDspSeX1, kDspSfY1, 0u, 0u, kDspDgA0, 0u), 0u,
     {.a0 = 0x4444444444ull, .x1 = 0xFFFE2222u, .y1 = 0x0001AAAAu},
     {.a0 = 0xFFFFFFFFFCull, .x1 = 0xFFFE2222u, .y1 = 0x0001AAAAu}},
    {"PMULS H'8000 x H'8000", kPmulsX0Y0A0, 0u, {.x0 = 0x80000000u, .y0 = 0x80000000u},
     {.a0 = 0xFF80000000ull, .x0 = 0x80000000u, .y0 = 0x80000000u}},
    {"PMULS H'8000 x H'8000 with S", kPmulsX0Y0A0, kSrS, {.x0 = 0x80000000u, .y0 = 0x80000000u},
     {.a0 = 0x007FFFFFFFull, .x0 = 0x80000000u, .y0 = 0x80000000u}},
    {"PNEG X0,A0", DspOp3(0xC9u, kDspSxX0, 0u, kDspA0), 0u, {.a0 = 0xA987654321ull, .x0 = 0x55555555u},
     {.a0 = 0xFFAAAAAAABull, .x0 = 0x55555555u, .dsr = 0x21u}},
    {"PNEG X1,Y1", DspOp3(0xC9u, kDspSxX1, 0u, kDspY1), 0u, {.x1 = 0x99999999u},
     {.x1 = 0x99999999u, .y1 = 0x66666667u, .dsr = 0x81u}},
    {"PRND X0,M0", DspOp3(0x98u, kDspSxX0, 0u, kDspM0), 0u, {.x0 = 0x0052330Fu, .m0 = 0x12345678u},
     {.x0 = 0x0052330Fu, .m0 = 0x00520000u, .dsr = 0x80u}},
    {"PRND X1,X1", DspOp3(0x98u, kDspSxX1, 0u, kDspX1), 0u, {.x1 = 0xFC34C087u}, {.x1 = 0xFC350000u, .dsr = 0x20u}},
    {"PSHA X0,Y0,X0", DspOp3(0x91u, kDspSxX0, kDspSyY0, kDspX0), 0u, {.x0 = 0x33333333u, .y0 = 0xFFFF0000u},
     {.x0 = 0x19999999u, .y0 = 0xFFFF0000u, .dsr = 0x01u}},
    {"PSHA #-5,A1", DspShiftImmediate(2u, -5, kDspA1), 0u, {.a1 = 0xAAAAAAAAAAull},
     {.a1 = 0xFD55555555ull, .dsr = 0x30u}},
    {"PSHA X0,Y0,A0 shifting a bit into the guard", kPshaX0Y0A0, 0u, {.x0 = 0x80000000u, .y0 = 0x00010000u},
     {.a0 = 0xFF00000000ull, .x0 = 0x80000000u, .y0 = 0x00010000u, .dsr = 0x31u}},
    {"PSHL X0,Y0,A0", kPshlX0Y0A0, 0u, {.a0 = 0x123456789Aull, .x0 = 0x22222222u, .y0 = 0x00030000u},
     {.a0 = 0x0011100000ull, .x0 = 0x22222222u, .y0 = 0x00030000u, .dsr = 0x01u}},
    {"PSHL X1,Y1,X1", DspOp3(0x81u, kDspSxX1, kDspSyY1, kDspX1), 0u, {.x1 = 0xCCCCCCCCu, .y1 = 0xFFFE0000u},
     {.x1 = 0x33330000u, .y1 = 0xFFFE0000u, .dsr = 0x00u}},
    {"PSHL #7,A1", DspShiftImmediate(0u, 7, kDspA1), 0u, {.a1 = 0x55555555ull}, {.a1 = 0x00AA800000ull, .dsr = 0x20u}},
    {"PSTS MACH,A0", DspOp3(0xCDu, 0u, 0u, kDspA0), 0u, {.a0 = 0x123456789Aull, .mach = 0x88888888u},
     {.a0 = 0xFF88888888ull, .mach = 0x88888888u}},
    {"PSUB X0,Y0,A0", DspOp3(0xA1u, kDspSxX0, kDspSyY0, kDspA0), 0u,
     {.a0 = 0x123456789Aull, .x0 = 0x55555555u, .y0 = 0x33333333u},
     {.a0 = 0x0022222222ull, .x0 = 0x55555555u, .y0 = 0x33333333u, .dsr = 0x80u}},
    {"PSUB A0,M0,A0 PMULS X0,Y0,M0", DspMultiply(6u, kDspSeX0, kDspSfY0, kDspSxA0, kDspSyM0, kDspDgM0, kDspDuA0), 0u,
     {.a0 = 0x0022222222ull, .x0 = 0x00020000u, .y0 = 0xFFFE0000u, .m0 = 0x33333333u},
     {.a0 = 0xFFEEEEEEEFull, .x0 = 0x00020000u, .y0 = 0xFFFE0000u, .m0 = 0xFFFFFFF8u, .dsr = 0x21u}},
    {"PSUBC X0,Y0,M0 with DC clear", kPsubcX0Y0M0, 0u, {.x0 = 0x33333333u, .y0 = 0x55555555u, .m0 = 0x12345678u},
     {.x0 = 0x33333333u, .y0 = 0x55555555u, .m0 = 0xDDDDDDDEu, .dsr = 0x21u}},
    {"PSUBC X0,Y0,M0 with DC set", kPsubcX0Y0M0, 0u,
     {.x0 = 0x33333333u, .y0 = 0x55555555u, .m0 = 0x12345678u, .dsr = 0x01u},
     {.x0 = 0x33333333u, .y0 = 0x55555555u, .m0 = 0xDDDDDDDDu, .dsr = 0x21u}},
    {"PXOR X0,Y0,A0", DspOp3(0xA5u, kDspSxX0, kDspSyY0, kDspA0), 0u,
     {.a0 = 0x123456789Aull, .x0 = 0x33333333u, .y0 = 0x55555555u},
     {.a0 = 0x0066660000ull, .x0 = 0x33333333u, .y0 = 0x55555555u, .dsr = 0x00u}},
    {"PADD with S at the 32-bit limit", kPaddX0Y0A0, kSrS, {.x0 = 0x7FFFFFFFu, .y0 = 1u},
     {.a0 = 0x007FFFFFFFull, .x0 = 0x7FFFFFFFu, .y0 = 1u, .dsr = 0x80u}},
    {"PADD without S at the 32-bit limit", kPaddX0Y0A0, 0u, {.x0 = 0x7FFFFFFFu, .y0 = 1u},
     {.a0 = 0x0080000000ull, .x0 = 0x7FFFFFFFu, .y0 = 1u, .dsr = 0x90u}},
    {"PADD over range", kPaddA0Y0A0, 0u, {.a0 = 0x7FFFFFFFFFull, .y0 = 1u},
     {.a0 = 0x8000000000ull, .y0 = 1u, .dsr = 0xB1u}},
    {"PADD over range in GE mode", kPaddA0Y0A0, 0u, {.a0 = 0x7FFFFFFFFFull, .y0 = 1u, .dsr = 0x0Au},
     {.a0 = 0x8000000000ull, .y0 = 1u, .dsr = 0xBBu}},
    {"PADD over range with S", kPaddA0Y0A0, kSrS, {.a0 = 0x7FFFFFFFFFull, .y0 = 1u},
     {.a0 = 0xFF80000000ull, .y0 = 1u, .dsr = 0x21u}},
    {"DCT PADD with DC set", DspOp3(0xB2u, kDspSxX0, kDspSyY0, kDspA0), 0u,
     {.x0 = 0x22222222u, .y0 = 0x33333333u, .dsr = 0x01u},
     {.a0 = 0x0055555555ull, .x0 = 0x22222222u, .y0 = 0x33333333u, .dsr = 0x01u}},
    {"DCT PADD with DC clear", DspOp3(0xB2u, kDspSxX0, kDspSyY0, kDspA0), 0u,
     {.a0 = 0x123456789Aull, .x0 = 0x22222222u, .y0 = 0x33333333u},
     {.a0 = 0x123456789Aull, .x0 = 0x22222222u, .y0 = 0x33333333u}},
    {"DCF PCLR A1 with DC clear", DspOp3(0x8Fu, 0u, 0u, kDspA1), 0u, {.a1 = 0xFF87654321ull}, {.a1 = 0u}},
    {"DCF PCLR A1 with DC set", DspOp3(0x8Fu, 0u, 0u, kDspA1), 0u, {.a1 = 0xFF87654321ull, .dsr = 0x01u},
     {.a1 = 0xFF87654321ull, .dsr = 0x01u}},
};

constexpr uint32_t kPcmpDsr[8][2] = {{0x21u, 0x21u}, {0x23u, 0x23u}, {0x24u, 0x24u}, {0x26u, 0x26u},
                                     {0x28u, 0x28u}, {0x2Au, 0x2Au}, {0x2Cu, 0x2Du}, {0x2Eu, 0x2Fu}};

void RunCase(ShIrRig& rig, const char* mode, const char* what, uint16_t field_b, uint32_t sr_extra,
             const DspRegs& init, const DspRegs& want) {
    ShCpuState initial{};
    initial.sr = kDspSr | sr_extra;
    rig.Run(DspParallel(0u, field_b), initial, what, [&](ShCpuState& s) { DspLoad(s, init); });
    DspExpect(mode, what, rig.State(), want);
}

void RunConditionSelects(ShIrRig& rig, const char* mode) {
    for (uint32_t cs = 0; cs < 8u; ++cs) {
        for (uint32_t dc = 0; dc < 2u; ++dc) {
            const uint32_t dsr = (cs << 1) | dc;
            const DspRegs  in{.x0 = 0x22222222u, .y0 = 0x33333333u, .dsr = dsr};
            DspRegs        out = in;
            out.dsr            = kPcmpDsr[cs][dc];
            RunCase(rig, mode, "PCMP X0,Y0 under every condition select", kPcmpX0Y0, 0u, in, out);
        }
    }
}

void RunShiftRange(ShIrRig& rig, const char* mode, const char* what, uint16_t field_b, uint32_t y0,
                   const char* halt) {
    ShCpuState initial{};
    initial.sr               = kDspSr;
    const std::string detail = rig.RunToHalt(DspParallel(0u, field_b), initial, what, [&](ShCpuState& s) {
        DspLoad(s, DspRegs{.y0 = y0});
    });
    ShRigExpectHalt(mode, what, detail, halt);
}

void RunAll(bool verify, const char* mode) {
    ShIrRig rig(true, verify);
    for (const Case& c : kCases) RunCase(rig, mode, c.what, c.field_b, c.sr_extra, c.init, c.want);
    RunConditionSelects(rig, mode);
    RunShiftRange(rig, mode, "PSHA by +33", kPshaX0Y0A0, 0x00210000u, "shift amount 33 outside -32..+32");
    RunShiftRange(rig, mode, "PSHA by -33", kPshaX0Y0A0, 0x005F0000u, "shift amount -33 outside -32..+32");
    RunShiftRange(rig, mode, "PSHL by +17", kPshlX0Y0A0, 0x00110000u, "shift amount 17 outside -16..+16");
    std::printf("%s: SH-DSP data operations passed\n", mode);
}

}

int main() {
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    return 0;
}
