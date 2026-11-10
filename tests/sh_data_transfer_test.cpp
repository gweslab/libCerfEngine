#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using cerfengine::kShSrT;
using cerfengine::ShCpuState;

constexpr uint32_t kDataBytes = 0x8000u;
constexpr uint32_t kIoVa      = 0xA0100000u;
constexpr uint32_t kMovaBody  = 8u;
constexpr uint32_t kLoadBody  = 32u;

struct Mode {
    const char* name;
    bool        dsp;
    bool        verify;
};

struct Ctx {
    Mode                 mode;
    std::mt19937         rng;
    std::vector<uint8_t> data;
    uint32_t             runs = 0u;
};

struct Form {
    const char* name;
    uint16_t    pattern;
    uint32_t    width;
};

uint32_t DataLoad(const Ctx& x, uint32_t va, uint32_t width) {
    TEST_CHECK(va >= kShRigDataVa && va - kShRigDataVa + width <= kDataBytes);
    return ShRigSignExtend(static_cast<uint32_t>(cerfengine::le::UN(&x.data[va - kShRigDataVa], width)), width);
}

std::unique_ptr<ShIrRig> NewRig(const Ctx& x) {
    auto                  rig = std::make_unique<ShIrRig>(x.mode.dsp, x.mode.verify);
    std::vector<uint32_t> words(kDataBytes / 4u, 0u);
    for (uint32_t i = 0; i < kDataBytes / 4u; ++i) words[i] = cerfengine::le::U32(x.data.data(), i * 4u);
    rig->Host().LoadWords(ShRigPa(kShRigDataVa), words.data(), words.size());
    rig->Host().EnableMmioLog();
    return rig;
}

ShCpuState Seeded(Ctx& x) { return ShRigSeededState(x.rng); }

uint32_t Exec(ShIrRig& rig, Ctx& x, const std::vector<uint16_t>& body, const ShCpuState& init, const char* what) {
    rig.Host().ResetMmioLog();
    ++x.runs;
    return rig.Run(body, init, what);
}

void VerifyRegisters(ShIrRig& rig, const Ctx& x, const ShCpuState& want, const char* what, uint16_t op) {
    ShRigCheckState(rig.State(), want, x.mode.name, what, op);
}

void Verify(ShIrRig& rig, const Ctx& x, const ShCpuState& want, const char* what, uint16_t op) {
    VerifyRegisters(rig, x, want, what, op);
    const auto& log = rig.Host().MmioLog();
    if (!log.empty()) {
        TestFail("%s %s %04X: %zu bus accesses, first PA %08X, want none", x.mode.name, what, op, log.size(),
                 log[0].pa);
    }
}

void VerifyStore(ShIrRig& rig, const Ctx& x, const ShCpuState& want, const char* what, uint16_t op, uint32_t va,
                 uint32_t value, uint32_t width) {
    VerifyRegisters(rig, x, want, what, op);
    const auto&    log = rig.Host().MmioLog();
    const uint32_t pa  = ShRigPa(va);
    const uint32_t v   = value & cerfengine::ByteWidthMask(width);
    if (log.size() != 1u || !log[0].write || log[0].pa != pa || log[0].value != v || log[0].width != width) {
        TestFail("%s %s %04X: %zu bus accesses, first write=%d PA %08X value %08X width %u; want one write PA %08X "
                 "value %08X width %u",
                 x.mode.name, what, op, log.size(), log.empty() ? 0 : log[0].write ? 1 : 0,
                 log.empty() ? 0u : log[0].pa, log.empty() ? 0u : log[0].value, log.empty() ? 0u : log[0].width, pa,
                 v, width);
    }
}

uint32_t RandomBase(Ctx& x, uint32_t area, uint32_t span, uint32_t width) {
    return area + ((x.rng() % span) & ~(width - 1u));
}

void ManualExamples(Ctx& x) {
    struct Example {
        uint16_t op;
        uint32_t r0;
        uint32_t r1_before;
        uint32_t r1_after;
    };
    constexpr Example kExamples[] = {
        {0x6108u, 0x12345678u, 0x00000000u, 0x12347856u},
        {0x6109u, 0x12345678u, 0x00000000u, 0x56781234u},
        {0x210Du, 0x01234567u, 0x89ABCDEFu, 0x456789ABu},
    };
    auto rig = NewRig(x);
    for (const Example& e : kExamples) {
        ShCpuState init = Seeded(x);
        init.gpr[0]     = e.r0;
        init.gpr[1]     = e.r1_before;
        ShCpuState want = init;
        want.gpr[1]     = e.r1_after;
        Exec(*rig, x, {e.op}, init, "manual example");
        Verify(*rig, x, want, "manual example", e.op);
    }
}

void DisplacementLoads(Ctx& x) {
    constexpr Form kR0Forms[] = {{"MOV.B @(disp,Rm),R0", 0x8400u, 1u}, {"MOV.W @(disp,Rm),R0", 0x8500u, 2u}};
    for (const Form& f : kR0Forms) {
        auto rig = NewRig(x);
        for (uint32_t m = 0; m < cerfengine::kShNumGpr; ++m) {
            for (uint32_t d = 0; d < 16u; ++d) {
                for (uint32_t i = 0; i < 3u; ++i) {
                    ShCpuState init     = Seeded(x);
                    init.gpr[m]         = RandomBase(x, kShRigDataVa, 0x4000u, f.width);
                    const uint16_t op   = static_cast<uint16_t>(f.pattern | (m << 4) | d);
                    ShCpuState     want = init;
                    want.gpr[0]         = DataLoad(x, init.gpr[m] + d * f.width, f.width);
                    Exec(*rig, x, {op}, init, f.name);
                    Verify(*rig, x, want, f.name, op);
                }
            }
        }
    }
    auto rig = NewRig(x);
    for (uint32_t n = 0; n < cerfengine::kShNumGpr; ++n) {
        for (uint32_t m = 0; m < cerfengine::kShNumGpr; ++m) {
            for (uint32_t d = 0; d < 16u; ++d) {
                ShCpuState init     = Seeded(x);
                init.gpr[m]         = RandomBase(x, kShRigDataVa, 0x4000u, 4u);
                const uint16_t op   = static_cast<uint16_t>(0x5000u | (n << 8) | (m << 4) | d);
                ShCpuState     want = init;
                want.gpr[n]         = DataLoad(x, init.gpr[m] + d * 4u, 4u);
                Exec(*rig, x, {op}, init, "MOV.L @(disp,Rm),Rn");
                Verify(*rig, x, want, "MOV.L @(disp,Rm),Rn", op);
            }
        }
    }
}

void DisplacementStores(Ctx& x) {
    constexpr Form kR0Forms[] = {{"MOV.B R0,@(disp,Rn)", 0x8000u, 1u}, {"MOV.W R0,@(disp,Rn)", 0x8100u, 2u}};
    for (const Form& f : kR0Forms) {
        auto rig = NewRig(x);
        for (uint32_t n = 0; n < cerfengine::kShNumGpr; ++n) {
            for (uint32_t d = 0; d < 16u; ++d) {
                for (uint32_t i = 0; i < 3u; ++i) {
                    ShCpuState init   = Seeded(x);
                    init.gpr[n]       = RandomBase(x, kIoVa, 0x10000u, f.width);
                    const uint16_t op = static_cast<uint16_t>(f.pattern | (n << 4) | d);
                    Exec(*rig, x, {op}, init, f.name);
                    VerifyStore(*rig, x, init, f.name, op, init.gpr[n] + d * f.width, init.gpr[0], f.width);
                }
            }
        }
    }
    auto rig = NewRig(x);
    for (uint32_t n = 0; n < cerfengine::kShNumGpr; ++n) {
        for (uint32_t m = 0; m < cerfengine::kShNumGpr; ++m) {
            for (uint32_t d = 0; d < 16u; ++d) {
                ShCpuState init   = Seeded(x);
                init.gpr[n]       = RandomBase(x, kIoVa, 0x10000u, 4u);
                const uint16_t op = static_cast<uint16_t>(0x1000u | (n << 8) | (m << 4) | d);
                Exec(*rig, x, {op}, init, "MOV.L Rm,@(disp,Rn)");
                VerifyStore(*rig, x, init, "MOV.L Rm,@(disp,Rn)", op, init.gpr[n] + d * 4u, init.gpr[m], 4u);
            }
        }
    }
}

void GbrForms(Ctx& x) {
    constexpr Form kLoads[]  = {{"MOV.B @(disp,GBR),R0", 0xC400u, 1u},
                                {"MOV.W @(disp,GBR),R0", 0xC500u, 2u},
                                {"MOV.L @(disp,GBR),R0", 0xC600u, 4u}};
    constexpr Form kStores[] = {{"MOV.B R0,@(disp,GBR)", 0xC000u, 1u},
                                {"MOV.W R0,@(disp,GBR)", 0xC100u, 2u},
                                {"MOV.L R0,@(disp,GBR)", 0xC200u, 4u}};
    for (uint32_t w = 0; w < 3u; ++w) {
        const Form& load  = kLoads[w];
        const Form& store = kStores[w];
        auto        rig   = NewRig(x);
        for (uint32_t d = 0; d < 256u; ++d) {
            for (uint32_t i = 0; i < 2u; ++i) {
                ShCpuState init     = Seeded(x);
                init.gbr            = RandomBase(x, kShRigDataVa, 0x4000u, load.width);
                uint16_t   op       = static_cast<uint16_t>(load.pattern | d);
                ShCpuState want     = init;
                want.gpr[0]         = DataLoad(x, init.gbr + d * load.width, load.width);
                Exec(*rig, x, {op}, init, load.name);
                Verify(*rig, x, want, load.name, op);

                init     = Seeded(x);
                init.gbr = RandomBase(x, kIoVa, 0x10000u, store.width);
                op       = static_cast<uint16_t>(store.pattern | d);
                Exec(*rig, x, {op}, init, store.name);
                VerifyStore(*rig, x, init, store.name, op, init.gbr + d * store.width, init.gpr[0], store.width);
            }
        }
    }
}

enum class Lead { kNone, kBra, kBtsTaken, kBtsNotTaken, kBraf };

constexpr Lead kLeads[] = {Lead::kNone, Lead::kBra, Lead::kBtsTaken, Lead::kBtsNotTaken, Lead::kBraf};

const char* LeadName(Lead lead) {
    switch (lead) {
    case Lead::kNone: return "";
    case Lead::kBra: return " in a BRA slot";
    case Lead::kBtsTaken: return " in a taken BT/S slot";
    case Lead::kBtsNotTaken: return " after a not-taken BT/S";
    case Lead::kBraf: return " in a BRAF slot";
    }
    TestFail("unknown lead");
}

uint16_t Filler(uint32_t i) {
    const uint32_t r = i & 15u;
    return static_cast<uint16_t>(i < 16u ? 0x0083u | (r << 8) : 0x6003u | (r << 8) | (r << 4));
}

struct Layout {
    std::vector<uint16_t> body;
    uint32_t              pc_offset;
    bool                  t;
    uint32_t              r3;
};

Layout BuildLayout(Lead lead, uint32_t p, uint32_t k, uint32_t length, uint16_t insn) {
    Layout l{{}, 2u * p + 4u, lead == Lead::kBtsTaken, 0u};
    for (uint32_t i = 0; i < length; ++i) l.body.push_back(Filler(i));
    if (lead == Lead::kNone) {
        l.body[p] = insn;
        return l;
    }
    l.body[p + 1u] = insn;
    l.pc_offset    = lead == Lead::kBtsNotTaken ? 2u * (p + 1u) + 4u : 2u * (p + 2u + k) + 2u;
    if (lead == Lead::kBra) l.body[p] = static_cast<uint16_t>(0xA000u | k);
    if (lead == Lead::kBtsTaken || lead == Lead::kBtsNotTaken) l.body[p] = static_cast<uint16_t>(0x8D00u | k);
    if (lead == Lead::kBraf) {
        l.body[p] = 0x0323u;
        l.r3      = 2u * k;
    }
    return l;
}

ShCpuState LayoutState(Ctx& x, const Layout& l) {
    ShCpuState init = Seeded(x);
    init.sr         = (init.sr & ~kShSrT) | (l.t ? kShSrT : 0u);
    init.gpr[3]     = l.r3;
    return init;
}

void PcRelativeMova(ShIrRig& rig, Ctx& x, Lead lead, uint32_t p, uint32_t k) {
    const std::string name = std::string("MOVA @(disp,PC),R0") + LeadName(lead);
    for (uint32_t d = 0; d < 256u; ++d) {
        const uint16_t   op   = static_cast<uint16_t>(0xC700u | d);
        const Layout     l    = BuildLayout(lead, p, k, kMovaBody, op);
        const ShCpuState init = LayoutState(x, l);
        const uint32_t   s    = Exec(rig, x, l.body, init, name.c_str());
        ShCpuState       want = init;
        want.gpr[0]           = ((s + l.pc_offset) & ~3u) + 4u * d;
        Verify(rig, x, want, name.c_str(), op);
    }
}

void PcRelativeLoads(ShIrRig& rig, Ctx& x, Lead lead, uint32_t p, uint32_t k) {
    for (uint32_t width : {2u, 4u}) {
        const std::string name = std::string(width == 4u ? "MOV.L @(disp,PC),R1" : "MOV.W @(disp,PC),R1") +
                                 LeadName(lead);
        for (uint32_t d = 0;; ++d) {
            const uint16_t op = static_cast<uint16_t>((width == 4u ? 0xD100u : 0x9100u) | d);
            const Layout   l  = BuildLayout(lead, p, k, kLoadBody, op);
            const uint32_t at = width == 4u ? (l.pc_offset & ~3u) + 4u * d : l.pc_offset + 2u * d;
            if (at + width > 2u * kLoadBody) break;
            const ShCpuState init = LayoutState(x, l);
            const uint32_t   s    = Exec(rig, x, l.body, init, name.c_str());
            TEST_CHECK((s & 3u) == 0u);
            const uint32_t lo   = l.body[at / 2u];
            ShCpuState     want = init;
            want.gpr[1]         = width == 4u ? lo | (static_cast<uint32_t>(l.body[at / 2u + 1u]) << 16)
                                              : ShRigSignExtend(lo, 2u);
            Verify(rig, x, want, name.c_str(), op);
        }
    }
}

void PcRelative(Ctx& x) {
    for (Lead lead : kLeads) {
        auto           rig     = NewRig(x);
        const uint32_t k_count = lead == Lead::kNone ? 1u : 2u;
        for (uint32_t p = 0; p < 2u; ++p) {
            for (uint32_t k = 1; k <= k_count; ++k) {
                PcRelativeMova(*rig, x, lead, p, k);
                PcRelativeLoads(*rig, x, lead, p, k);
            }
        }
    }
}

void Movt(Ctx& x) {
    auto rig = NewRig(x);
    for (uint32_t n = 0; n < cerfengine::kShNumGpr; ++n) {
        for (uint32_t i = 0; i < 8u; ++i) {
            const uint32_t t    = i & 1u;
            ShCpuState     init = Seeded(x);
            init.sr             = (init.sr & ~kShSrT) | (t != 0u ? kShSrT : 0u);
            const uint16_t op   = static_cast<uint16_t>(0x0029u | (n << 8));
            ShCpuState     want = init;
            want.gpr[n]         = t;
            Exec(*rig, x, {op}, init, "MOVT Rn");
            Verify(*rig, x, want, "MOVT Rn", op);
        }
    }
}

void Pref(Ctx& x) {
    constexpr uint32_t kTargets[] = {kIoVa, kIoVa + 1u, kShRigDataVa + 3u, 0x00000001u, 0x7FFFFFFEu, 0xFFFFFFFFu};
    auto               rig        = NewRig(x);
    for (uint32_t n = 0; n < cerfengine::kShNumGpr; ++n) {
        for (uint32_t target : kTargets) {
            ShCpuState init = Seeded(x);
            init.gpr[n]     = target;
            Exec(*rig, x, {kShRigNop}, init, "NOP");
            Verify(*rig, x, init, "NOP", kShRigNop);
            const uint32_t nop_cycles = rig->State().guest_cycle_counter;
            const uint16_t op         = static_cast<uint16_t>(0x0083u | (n << 8));
            Exec(*rig, x, {op}, init, "PREF @Rn");
            Verify(*rig, x, init, "PREF @Rn", op);
            const uint32_t extra = x.mode.dsp ? 1u : 0u;
            if (rig->State().guest_cycle_counter != nop_cycles + extra) {
                TestFail("%s PREF @R%u: %u cycles, NOP %u, want %u more", x.mode.name, n,
                         rig->State().guest_cycle_counter, nop_cycles, extra);
            }
        }
    }
}

uint32_t SwapBytes(uint32_t v) { return (v & 0xFFFF0000u) | ((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu); }

uint32_t SwapWords(uint32_t v) { return (v >> 16) | (v << 16); }

void RegisterTransfers(Ctx& x) {
    struct Transfer {
        const char* name;
        uint16_t    pattern;
    };
    constexpr Transfer kForms[] = {{"SWAP.B Rm,Rn", 0x6008u}, {"SWAP.W Rm,Rn", 0x6009u}, {"XTRCT Rm,Rn", 0x200Du}};
    constexpr uint32_t kEdges[] = {0x00000000u, 0xFFFFFFFFu, 0x12345678u, 0x80000000u,
                                   0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u};
    for (uint32_t f = 0; f < 3u; ++f) {
        auto rig = NewRig(x);
        for (uint32_t n = 0; n < cerfengine::kShNumGpr; ++n) {
            for (uint32_t m = 0; m < cerfengine::kShNumGpr; ++m) {
                for (uint32_t i = 0; i < 12u; ++i) {
                    ShCpuState init     = Seeded(x);
                    init.gpr[m]         = i < 8u ? kEdges[i] : x.rng();
                    const uint32_t rm   = init.gpr[m];
                    const uint32_t rn   = init.gpr[n];
                    const uint16_t op   = static_cast<uint16_t>(kForms[f].pattern | (n << 8) | (m << 4));
                    ShCpuState     want = init;
                    want.gpr[n] = f == 0u ? SwapBytes(rm) : f == 1u ? SwapWords(rm) : (rm << 16) | (rn >> 16);
                    Exec(*rig, x, {op}, init, kForms[f].name);
                    Verify(*rig, x, want, kForms[f].name, op);
                }
            }
        }
    }
}

}

int main() {
    constexpr Mode kModes[] = {{"plain", false, false}, {"verified IR", false, true}, {"SH3-DSP", true, false}};
    for (const Mode& mode : kModes) {
        Ctx x{mode, std::mt19937(0x5348u), std::vector<uint8_t>(kDataBytes, 0u)};
        for (uint8_t& byte : x.data) byte = static_cast<uint8_t>(x.rng());
        ManualExamples(x);
        DisplacementLoads(x);
        DisplacementStores(x);
        GbrForms(x);
        PcRelative(x);
        Movt(x);
        Pref(x);
        RegisterTransfers(x);
        std::printf("%s: %u SH data-transfer runs passed\n", mode.name, x.runs);
    }
    return 0;
}
