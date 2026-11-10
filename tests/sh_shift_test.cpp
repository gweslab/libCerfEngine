#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <random>

namespace {

using cerfengine::kShSrT;

constexpr uint32_t kRn = 1u;
constexpr uint32_t kRm = 2u;

enum class Kind { kShll, kShlr, kShal, kShar, kShll2, kShlr2, kShll8, kShlr8, kShll16, kShlr16,
                  kRotl, kRotr, kRotcl, kRotcr, kShad, kShld };

struct Insn {
    const char* name;
    Kind        kind;
    uint16_t    pattern;
    bool        two_registers;
};

constexpr Insn kInsns[] = {
    {"SHLL", Kind::kShll, 0x4000u, false},     {"SHLR", Kind::kShlr, 0x4001u, false},
    {"SHAL", Kind::kShal, 0x4020u, false},     {"SHAR", Kind::kShar, 0x4021u, false},
    {"SHLL2", Kind::kShll2, 0x4008u, false},   {"SHLR2", Kind::kShlr2, 0x4009u, false},
    {"SHLL8", Kind::kShll8, 0x4018u, false},   {"SHLR8", Kind::kShlr8, 0x4019u, false},
    {"SHLL16", Kind::kShll16, 0x4028u, false}, {"SHLR16", Kind::kShlr16, 0x4029u, false},
    {"ROTL", Kind::kRotl, 0x4004u, false},     {"ROTR", Kind::kRotr, 0x4005u, false},
    {"ROTCL", Kind::kRotcl, 0x4024u, false},   {"ROTCR", Kind::kRotcr, 0x4025u, false},
    {"SHAD", Kind::kShad, 0x400Cu, true},      {"SHLD", Kind::kShld, 0x400Du, true},
};

constexpr uint32_t kEdges[] = {0u,          1u,          2u,          0x14u,       0x1Fu,       0x20u,
                               0x21u,       0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0x80180000u, 0xFFFFF801u,
                               0xFFFFFFE0u, 0xFFFFFFE1u, 0xFFFFFFECu, 0xFFFFFFFEu, 0xFFFFFFFFu};

struct Result {
    uint32_t rn;
    uint32_t sr;
};

uint32_t WithT(uint32_t sr, uint32_t t) { return (sr & ~kShSrT) | ((t & 1u) != 0u ? kShSrT : 0u); }

uint32_t RightShiftCount(uint32_t rm) { return ((~rm) & 0x1Fu) + 1u; }

Result Reference(Kind kind, uint32_t rn, uint32_t rm, uint32_t sr) {
    const uint32_t t = sr & kShSrT;
    switch (kind) {
    case Kind::kShll:
    case Kind::kShal: return {rn << 1, WithT(sr, rn >> 31)};
    case Kind::kShlr: return {rn >> 1, WithT(sr, rn)};
    case Kind::kShar: return {static_cast<uint32_t>(static_cast<int32_t>(rn) >> 1), WithT(sr, rn)};
    case Kind::kShll2: return {rn << 2, sr};
    case Kind::kShlr2: return {rn >> 2, sr};
    case Kind::kShll8: return {rn << 8, sr};
    case Kind::kShlr8: return {rn >> 8, sr};
    case Kind::kShll16: return {rn << 16, sr};
    case Kind::kShlr16: return {rn >> 16, sr};
    case Kind::kRotl: return {(rn << 1) | (rn >> 31), WithT(sr, rn >> 31)};
    case Kind::kRotr: return {(rn >> 1) | (rn << 31), WithT(sr, rn)};
    case Kind::kRotcl: return {(rn << 1) | t, WithT(sr, rn >> 31)};
    case Kind::kRotcr: return {(rn >> 1) | (t << 31), WithT(sr, rn)};
    case Kind::kShad: {
        if ((rm & 0x80000000u) == 0u) return {rn << (rm & 0x1Fu), sr};
        const uint32_t k = RightShiftCount(rm);
        if (k == 32u) return {(rn & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u, sr};
        return {static_cast<uint32_t>(static_cast<int32_t>(rn) >> k), sr};
    }
    case Kind::kShld: {
        if ((rm & 0x80000000u) == 0u) return {rn << (rm & 0x1Fu), sr};
        const uint32_t k = RightShiftCount(rm);
        return {k == 32u ? 0u : rn >> k, sr};
    }
    }
    TestFail("unknown kind");
}

uint16_t Encode(const Insn& insn, uint32_t n, uint32_t m) {
    const uint32_t rm_field = insn.two_registers ? (m << 4) : 0u;
    return static_cast<uint16_t>(insn.pattern | (n << 8) | rm_field);
}

void RunCase(ShIrRig& rig, const Insn& insn, bool alias, uint32_t a, uint32_t b, uint32_t sr, const char* mode) {
    cerfengine::ShCpuState init{};
    init.sr       = sr;
    init.gpr[kRn] = a;
    init.gpr[kRm] = b;
    rig.Run({Encode(insn, kRn, alias ? kRn : kRm)}, init, insn.name);
    const Result                  want = Reference(insn.kind, a, alias ? a : b, sr);
    const cerfengine::ShCpuState& s    = rig.State();
    if (s.gpr[kRn] != want.rn || s.sr != want.sr || s.gpr[kRm] != b) {
        TestFail("%s %s%s R1=%08X R2=%08X SR=%08X: got R1=%08X R2=%08X SR=%08X, want R1=%08X R2=%08X SR=%08X", mode,
                 insn.name, alias ? " (Rn=Rm=R1)" : "", a, b, sr, s.gpr[kRn], s.gpr[kRm], s.sr, want.rn, b, want.sr);
    }
}

void RunManualExamples(const char* mode) {
    struct Example {
        uint16_t opcode;
        uint32_t rm;
        uint32_t rn_before;
        uint32_t rn_after;
    };
    constexpr Example kExamples[] = {
        {0x412Cu, 0xFFFFFFECu, 0x80180000u, 0xFFFFF801u},
        {0x412Cu, 0x00000014u, 0xFFFFF801u, 0x80100000u},
        {0x412Du, 0xFFFFFFECu, 0x80180000u, 0x00000801u},
        {0x412Du, 0x00000014u, 0xFFFFF801u, 0x80100000u},
    };
    ShIrRig rig(false, false);
    for (const Example& e : kExamples) {
        cerfengine::ShCpuState init{};
        init.sr       = kShRigSrBase;
        init.gpr[kRn] = e.rn_before;
        init.gpr[kRm] = e.rm;
        rig.Run({e.opcode}, init, "manual example");
        if (rig.State().gpr[kRn] != e.rn_after) {
            TestFail("%s manual example %04X R2=%08X R1=%08X: got R1=%08X, want %08X", mode, e.opcode, e.rm,
                     e.rn_before, rig.State().gpr[kRn], e.rn_after);
        }
    }
}

void RunAll(bool verify, const char* mode) {
    std::mt19937 rng(0x5368u);
    uint32_t     blocks = 0;
    for (const Insn& insn : kInsns) {
        ShIrRig rig(false, verify);
        for (uint32_t a : kEdges) {
            for (uint32_t b : kEdges) {
                RunCase(rig, insn, false, a, b, kShRigSrBase, mode);
                RunCase(rig, insn, false, a, b, kShRigSrBase | kShSrT, mode);
            }
            RunCase(rig, insn, true, a, a, kShRigSrBase, mode);
            RunCase(rig, insn, true, a, a, kShRigSrBase | kShSrT, mode);
        }
        for (uint32_t count = 0; count < 64u; ++count) {
            const uint32_t rm = count < 32u ? count : 0u - (count - 31u);
            RunCase(rig, insn, false, rng(), rm, kShRigSrBase | ((count & 1u) != 0u ? kShSrT : 0u), mode);
        }
        for (uint32_t i = 0; i < 400u; ++i) {
            const uint32_t a = rng();
            const uint32_t b = rng();
            RunCase(rig, insn, (i & 15u) == 0u, a, b, kShRigSrBase | ((rng() & 1u) != 0u ? kShSrT : 0u), mode);
        }
        blocks += rig.BlocksCompiled();
    }
    const uint32_t runs = static_cast<uint32_t>(std::size(kInsns)) *
                          (static_cast<uint32_t>(std::size(kEdges) * std::size(kEdges)) * 2u +
                           static_cast<uint32_t>(std::size(kEdges)) * 2u + 64u + 400u);
    std::printf("%s: %u SH shift runs passed, %u blocks compiled\n", mode, runs, blocks);
    TEST_CHECK(blocks == runs);
}

}

int main() {
    RunManualExamples("plain");
    RunAll(false, "plain");
    RunAll(true, "verified IR");
    return 0;
}
