#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <random>
#include <vector>

namespace {

using cerfengine::kShSrM;
using cerfengine::kShSrQ;
using cerfengine::kShSrT;

constexpr uint32_t kRn = 1u;
constexpr uint32_t kRm = 2u;

enum class Kind { kAdd, kAddc, kAddv, kSub, kSubc, kSubv, kNeg, kNegc, kDt, kDiv0s, kDiv0u, kDiv1,
                  kExtsB, kExtsW, kExtuB, kExtuW, kAddImm };

struct Insn {
    const char* name;
    Kind        kind;
    uint16_t    pattern;
};

constexpr Insn kInsns[] = {
    {"ADD", Kind::kAdd, 0x300Cu},     {"ADDC", Kind::kAddc, 0x300Eu},   {"ADDV", Kind::kAddv, 0x300Fu},
    {"SUB", Kind::kSub, 0x3008u},     {"SUBC", Kind::kSubc, 0x300Au},   {"SUBV", Kind::kSubv, 0x300Bu},
    {"NEG", Kind::kNeg, 0x600Bu},     {"NEGC", Kind::kNegc, 0x600Au},   {"DT", Kind::kDt, 0x4010u},
    {"DIV0S", Kind::kDiv0s, 0x2007u}, {"DIV0U", Kind::kDiv0u, 0x0019u}, {"DIV1", Kind::kDiv1, 0x3004u},
    {"EXTS.B", Kind::kExtsB, 0x600Eu}, {"EXTS.W", Kind::kExtsW, 0x600Fu}, {"EXTU.B", Kind::kExtuB, 0x600Cu},
    {"EXTU.W", Kind::kExtuW, 0x600Du}, {"ADD #imm", Kind::kAddImm, 0x7000u},
};

constexpr uint32_t kEdges[] = {0u, 1u, 2u, 0x7Fu, 0x80u, 0xFFu, 0x7FFFu, 0x8000u, 0xFFFFu,
                               0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu};

struct Result {
    uint32_t rn;
    uint32_t sr;
};

uint32_t WithT(uint32_t sr, bool t) { return (sr & ~kShSrT) | (t ? kShSrT : 0u); }

bool SignedOverflow(int64_t wide) { return wide > INT32_MAX || wide < INT32_MIN; }

Result Div1Reference(uint32_t rn, uint32_t rm, uint32_t sr) {
    const bool     old_q = (sr & kShSrQ) != 0u;
    const bool     m     = (sr & kShSrM) != 0u;
    const bool     q     = (rn & 0x80000000u) != 0u;
    const uint32_t x     = (rn << 1) | (sr & kShSrT);
    uint32_t       r     = 0;
    bool           out   = false;
    if (old_q == m) {
        r   = x - rm;
        out = rm > x;
    } else {
        const uint64_t wide = static_cast<uint64_t>(x) + rm;
        r                   = static_cast<uint32_t>(wide);
        out                 = (wide >> 32) != 0u;
    }
    const bool q_new = m ? !(q != out) : (q != out);
    uint32_t   s     = sr & ~(kShSrQ | kShSrT);
    if (q_new) s |= kShSrQ;
    if (q_new == m) s |= kShSrT;
    return {r, s};
}

Result Reference(Kind kind, uint32_t rn, uint32_t rm, uint32_t sr, uint32_t imm) {
    const uint32_t t  = sr & kShSrT;
    const int64_t  sn = static_cast<int32_t>(rn);
    const int64_t  sm = static_cast<int32_t>(rm);
    switch (kind) {
    case Kind::kAdd: return {rn + rm, sr};
    case Kind::kAddc: {
        const uint64_t wide = static_cast<uint64_t>(rn) + rm + t;
        return {static_cast<uint32_t>(wide), WithT(sr, (wide >> 32) != 0u)};
    }
    case Kind::kAddv: return {rn + rm, WithT(sr, SignedOverflow(sn + sm))};
    case Kind::kSub: return {rn - rm, sr};
    case Kind::kSubc: return {rn - rm - t, WithT(sr, static_cast<uint64_t>(rm) + t > rn)};
    case Kind::kSubv: return {rn - rm, WithT(sr, SignedOverflow(sn - sm))};
    case Kind::kNeg: return {0u - rm, sr};
    case Kind::kNegc: return {0u - rm - t, WithT(sr, static_cast<uint64_t>(rm) + t > 0u)};
    case Kind::kDt: return {rn - 1u, WithT(sr, rn == 1u)};
    case Kind::kDiv0s: {
        const bool q = (rn & 0x80000000u) != 0u;
        const bool m = (rm & 0x80000000u) != 0u;
        uint32_t   s = sr & ~(kShSrM | kShSrQ | kShSrT);
        if (q) s |= kShSrQ;
        if (m) s |= kShSrM;
        if (q != m) s |= kShSrT;
        return {rn, s};
    }
    case Kind::kDiv0u: return {rn, sr & ~(kShSrM | kShSrQ | kShSrT)};
    case Kind::kDiv1: return Div1Reference(rn, rm, sr);
    case Kind::kExtsB: return {ShRigSignExtend(rm, 1u), sr};
    case Kind::kExtsW: return {ShRigSignExtend(rm, 2u), sr};
    case Kind::kExtuB: return {rm & 0xFFu, sr};
    case Kind::kExtuW: return {rm & 0xFFFFu, sr};
    case Kind::kAddImm: return {rn + ShRigSignExtend(imm, 1u), sr};
    }
    TestFail("unknown kind");
}

uint16_t Encode(const Insn& insn, uint32_t n, uint32_t m, uint32_t imm) {
    if (insn.kind == Kind::kDiv0u) return insn.pattern;
    if (insn.kind == Kind::kDt) return static_cast<uint16_t>(insn.pattern | (n << 8));
    if (insn.kind == Kind::kAddImm) return static_cast<uint16_t>(insn.pattern | (n << 8) | (imm & 0xFFu));
    return static_cast<uint16_t>(insn.pattern | (n << 8) | (m << 4));
}

void RunCase(ShIrRig& rig, const Insn& insn, bool alias, uint32_t a, uint32_t b, uint32_t sr, uint32_t imm,
             const char* mode) {
    cerfengine::ShCpuState init{};
    init.sr       = sr;
    init.gpr[kRn] = a;
    init.gpr[kRm] = b;
    rig.Run({Encode(insn, kRn, alias ? kRn : kRm, imm)}, init, insn.name);
    const Result                  want = Reference(insn.kind, a, alias ? a : b, sr, imm);
    const cerfengine::ShCpuState& s    = rig.State();
    if (s.gpr[kRn] != want.rn || s.sr != want.sr || s.gpr[kRm] != b) {
        TestFail("%s %s%s R1=%08X R2=%08X SR=%08X imm=%02X: got R1=%08X R2=%08X SR=%08X, want R1=%08X R2=%08X SR=%08X",
                 mode, insn.name, alias ? " (Rn=Rm=R1)" : "", a, b, sr, imm, s.gpr[kRn], s.gpr[kRm], s.sr, want.rn,
                 b, want.sr);
    }
}

uint32_t SrVariant(uint32_t bits) {
    return kShRigSrBase | ((bits & 1u) != 0u ? kShSrT : 0u) | ((bits & 2u) != 0u ? kShSrQ : 0u) |
           ((bits & 4u) != 0u ? kShSrM : 0u);
}

void RunDivideChain(ShIrRig& rig, bool signed_start, uint32_t dividend, uint32_t divisor, const char* mode) {
    std::vector<uint16_t> program;
    program.push_back(signed_start ? static_cast<uint16_t>(0x2007u | (kRn << 8) | (kRm << 4)) : 0x0019u);
    for (uint32_t i = 0; i < 16u; ++i) program.push_back(static_cast<uint16_t>(0x3004u | (kRn << 8) | (kRm << 4)));
    cerfengine::ShCpuState init{};
    init.sr       = kShRigSrBase | kShSrT | kShSrQ;
    init.gpr[kRn] = dividend;
    init.gpr[kRm] = divisor;
    rig.Run(program, init, "DIV1 chain");
    Result r = signed_start ? Reference(Kind::kDiv0s, dividend, divisor, init.sr, 0u)
                            : Reference(Kind::kDiv0u, dividend, divisor, init.sr, 0u);
    for (uint32_t i = 0; i < 16u; ++i) r = Div1Reference(r.rn, divisor, r.sr);
    const cerfengine::ShCpuState& s = rig.State();
    if (s.gpr[kRn] != r.rn || s.sr != r.sr) {
        TestFail("%s DIV1 chain (%s) %08X / %08X: got Rn=%08X SR=%08X, want Rn=%08X SR=%08X", mode,
                 signed_start ? "DIV0S" : "DIV0U", dividend, divisor, s.gpr[kRn], s.sr, r.rn, r.sr);
    }
}

void RunAll(bool verify, const char* mode) {
    std::mt19937 rng(0x5348u);
    uint32_t     blocks = 0;
    for (const Insn& insn : kInsns) {
        ShIrRig rig(false, verify);
        for (uint32_t a : kEdges) {
            for (uint32_t b : kEdges) {
                for (uint32_t bits = 0; bits < 8u; ++bits) {
                    RunCase(rig, insn, false, a, b, SrVariant(bits), b & 0xFFu, mode);
                }
            }
            RunCase(rig, insn, true, a, a, SrVariant(a & 7u), a & 0xFFu, mode);
            RunCase(rig, insn, true, a, a, SrVariant(~a & 7u), a & 0xFFu, mode);
        }
        for (uint32_t i = 0; i < 400u; ++i) {
            const uint32_t a    = rng();
            const uint32_t b    = rng();
            const uint32_t bits = rng();
            const uint32_t imm  = rng();
            RunCase(rig, insn, (i & 15u) == 0u, a, b, SrVariant(bits), imm, mode);
        }
        blocks += rig.BlocksCompiled();
    }
    ShIrRig rig(false, verify);
    for (uint32_t i = 0; i < 200u; ++i) {
        const uint32_t dividend = rng();
        const uint32_t divisor  = rng() | 1u;
        RunDivideChain(rig, (i & 1u) != 0u, dividend, divisor, mode);
    }
    blocks += rig.BlocksCompiled();
    const uint32_t runs = static_cast<uint32_t>(std::size(kInsns)) *
                              (static_cast<uint32_t>(std::size(kEdges) * std::size(kEdges)) * 8u +
                               static_cast<uint32_t>(std::size(kEdges)) * 2u + 400u) +
                          200u;
    std::printf("%s: %u SH arithmetic runs passed, %u blocks compiled\n", mode, runs, blocks);
    TEST_CHECK(blocks == runs);
}

}

int main() {
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    return 0;
}
