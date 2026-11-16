#include "mips_ir_rig.h"

#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kNop         = 0u;
constexpr uint32_t kProbeWord   = 0x01090028u;
constexpr uint32_t kReservedBit = 1u << 7;

constexpr uint32_t JumpTo(uint32_t va) { return (0x02u << 26) | ((va >> 2) & 0x03FFFFFFu); }

struct Case {
    const char* name;
    uint32_t    word;
    uint64_t    rs;
    uint64_t    rt;
    uint64_t    lo;
    uint64_t    hi;
    uint64_t    expected_lo;
    uint64_t    expected_hi;
    uint64_t    expected_rd;
};

void Begin(MipsIrRig& rig, uint32_t word, const Case& k) {
    rig.Load(kMipsRigVaBase, {word, JumpTo(kMipsRigEndVa), kNop});
    cerfengine::MipsCpuState init{};
    init.pc      = kMipsRigVaBase;
    init.gpr[8]  = k.rs;
    init.gpr[9]  = k.rt;
    init.gpr[10] = 0u;
    init.lo      = k.lo;
    init.hi      = k.hi;
    rig.Begin(init);
}

void RunCase(MipsIrRig& rig, const Case& k, const char* mode) {
    Begin(rig, k.word, k);
    for (uint32_t step = 0; step < 50u && !rig.Finished(); ++step) {
        if (rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted) {
            TestFail("%s %s: halted: %s", mode, k.name, rig.Engine().LastHalt().detail);
        }
    }
    const cerfengine::MipsCpuState& s = rig.State();
    if (!rig.Finished() || rig.Events().exceptions != 0u || s.lo != k.expected_lo ||
        s.hi != k.expected_hi || s.gpr[10] != k.expected_rd || s.gpr[0] != 0u) {
        TestFail("%s %s: lo %016llX hi %016llX r10 %016llX, expected lo %016llX hi %016llX "
                 "r10 %016llX",
                 mode, k.name, static_cast<unsigned long long>(s.lo),
                 static_cast<unsigned long long>(s.hi), static_cast<unsigned long long>(s.gpr[10]),
                 static_cast<unsigned long long>(k.expected_lo),
                 static_cast<unsigned long long>(k.expected_hi),
                 static_cast<unsigned long long>(k.expected_rd));
    }
}

void ExpectHalt(MipsIrRig& rig, uint32_t word, const char* name, const char* mode) {
    Begin(rig, word, Case{});
    for (uint32_t step = 0; step < 50u && !rig.Finished(); ++step) {
        if (rig.Step() == cerfengine::GuestEngine::RunStatus::kHalted) return;
    }
    TestFail("%s %s: word %08X did not halt", mode, name, word);
}

cerfengine::MipsFeatures Vr4102Features() {
    cerfengine::MipsFeatures f;
    f.isa_level          = cerfengine::MipsIsaLevel::kMips3;
    f.vr41xx_power_modes = true;
    f.madd16_dmadd16     = true;
    f.phys_addr_mask     = 0x1FFFFFFFu;
    return f;
}

cerfengine::MipsFeatures Vr4122Features() {
    cerfengine::MipsFeatures f;
    f.isa_level          = cerfengine::MipsIsaLevel::kMips3;
    f.vr41xx_power_modes = true;
    f.mips16             = true;
    f.macc_dmacc         = true;
    f.phys_addr_mask     = 0x1FFFFFFFu;
    return f;
}

void RunCore(const cerfengine::MipsFeatures& features, bool verify, const std::vector<Case>& cases,
             const char* mode) {
    MipsIrRig rig(features, 10u, verify);
    rig.Load(kMipsRigEndVa, {0x1000FFFFu, 0u, 0u, 0u, 0x650017FFu});
    for (const Case& k : cases) RunCase(rig, k, mode);
}

void RunHalt(const cerfengine::MipsFeatures& features, bool verify, uint32_t word, const char* name,
             const char* mode) {
    MipsIrRig rig(features, 10u, verify);
    rig.Load(kMipsRigEndVa, {0x1000FFFFu, 0u, 0u, 0u, 0x650017FFu});
    ExpectHalt(rig, word, name, mode);
}

}

int main() {
    const std::vector<Case> cases = {
        {"macc probe of player.exe 0x4B674", 0x01090028u, 0x0000000012345678ull, 0x0000000012345678ull, 0x0000000000000000ull, 0x0000000000000000ull,
         0x000000001DF4D840ull, 0x00000000014B66DCull, 0x0000000000000000ull},
        {"macc negative product into rd", 0x01095028u, 0xFFFFFFFFFFFFFFFDull, 0x0000000000000005ull, 0x0000000000000010ull, 0x0000000000000000ull,
         0x0000000000000001ull, 0x0000000000000000ull, 0x0000000000000001ull},
        {"macc carry into hi", 0x01095028u, 0x000000007FFFFFFFull, 0x000000007FFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000003ull,
         0x0000000000000000ull, 0x0000000040000003ull, 0x0000000000000000ull},
        {"macc reads low words of 64-bit registers", 0x01095028u, 0x1234567800000003ull, 0xABCDEF0000000007ull, 0xDEAD000000000001ull, 0xBEEF000000000002ull,
         0x0000000000000016ull, 0x0000000000000002ull, 0x0000000000000016ull},
        {"macchi", 0x01095228u, 0xFFFFFFFF80000000ull, 0x0000000000000003ull, 0x0000000000000005ull, 0x0000000000000000ull,
         0xFFFFFFFF80000005ull, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFEull},
        {"maccu", 0x01095068u, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000002ull, 0x0000000000000000ull, 0x0000000000000000ull,
         0xFFFFFFFFFFFFFFFEull, 0x0000000000000001ull, 0xFFFFFFFFFFFFFFFEull},
        {"macchiu", 0x01095268u, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
         0x0000000000000000ull, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFEull},
        {"maccs in range", 0x01095428u, 0x0000000000000100ull, 0xFFFFFFFFFFFFFF00ull, 0x0000000000000010ull, 0x0000000000000000ull,
         0xFFFFFFFFFFFF0010ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFF0010ull},
        {"maccs overflow", 0x01095428u, 0x0000000000007FFFull, 0x0000000000007FFFull, 0x000000007FFFFFFFull, 0x0000000000000000ull,
         0x000000007FFFFFFFull, 0x0000000000000000ull, 0x000000007FFFFFFFull},
        {"maccs underflow", 0x01095428u, 0xFFFFFFFFFFFF8000ull, 0x0000000000007FFFull, 0xFFFFFFFF80000000ull, 0xFFFFFFFFFFFFFFFFull,
         0xFFFFFFFF80000000ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF80000000ull},
        {"macchis underflow", 0x01095628u, 0xFFFFFFFFFFFF8000ull, 0x0000000000007FFFull, 0xFFFFFFFF80000000ull, 0xFFFFFFFFFFFFFFFFull,
         0xFFFFFFFF80000000ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull},
        {"maccus overflow", 0x01095468u, 0x000000000000FFFFull, 0x000000000000FFFFull, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000000ull,
         0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull},
        {"maccus in range", 0x01095468u, 0x000000000000FFFFull, 0x0000000000000002ull, 0x0000000000000010ull, 0x0000000000000000ull,
         0x000000000002000Eull, 0x0000000000000000ull, 0x000000000002000Eull},
        {"macchius overflow", 0x01095668u, 0x000000000000FFFFull, 0x000000000000FFFFull, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000000ull,
         0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull},
        {"dmacc", 0x01095029u, 0xFFFFFFFFFFFFFFFEull, 0x0000000040000000ull, 0x0000000100000000ull, 0x1111111111111111ull,
         0x0000000080000000ull, 0x1111111111111111ull, 0x0000000080000000ull},
        {"dmacc wraps 64 bits", 0x01095029u, 0x000000007FFFFFFFull, 0x000000007FFFFFFFull, 0x7FFFFFFFFFFFFFFFull, 0x2222222222222222ull,
         0xBFFFFFFF00000000ull, 0x2222222222222222ull, 0xBFFFFFFF00000000ull},
        {"dmacchi copies the old hi", 0x01095229u, 0x0000000000000003ull, 0x0000000000000004ull, 0x0000000000000005ull, 0x3333333333333333ull,
         0x0000000000000011ull, 0x3333333333333333ull, 0x3333333333333333ull},
        {"dmaccu", 0x01095069u, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000001ull, 0x0000000000000000ull,
         0xFFFFFFFE00000002ull, 0x0000000000000000ull, 0xFFFFFFFE00000002ull},
        {"dmacchiu", 0x01095269u, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000002ull, 0x0000000000000007ull, 0x4444444444444444ull,
         0x0000000200000005ull, 0x4444444444444444ull, 0x4444444444444444ull},
        {"dmaccs overflow", 0x01095429u, 0x0000000000007FFFull, 0x0000000000007FFFull, 0x000000007FFFFFFFull, 0x5555555555555555ull,
         0x000000007FFFFFFFull, 0x5555555555555555ull, 0x000000007FFFFFFFull},
        {"dmaccs underflow", 0x01095429u, 0xFFFFFFFFFFFF8000ull, 0x0000000000007FFFull, 0xFFFFFFFF80000000ull, 0x6666666666666666ull,
         0xFFFFFFFF80000000ull, 0x6666666666666666ull, 0xFFFFFFFF80000000ull},
        {"dmaccs in range", 0x01095429u, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000003ull, 0x0000000000000002ull, 0x7777777777777777ull,
         0xFFFFFFFFFFFFFFFFull, 0x7777777777777777ull, 0xFFFFFFFFFFFFFFFFull},
        {"dmaccus overflow", 0x01095469u, 0x000000000000FFFFull, 0x000000000000FFFFull, 0x00000000FFFFFFFFull, 0x0000000000000000ull,
         0xFFFFFFFFFFFFFFFFull, 0x0000000000000000ull, 0xFFFFFFFFFFFFFFFFull},
        {"dmacchius in range", 0x01095669u, 0x000000000000FFFFull, 0x0000000000000002ull, 0x0000000000000010ull, 0x8888888888888888ull,
         0x000000000002000Eull, 0x8888888888888888ull, 0x8888888888888888ull},
        {"dmacchis overflow", 0x01095629u, 0x0000000000007FFFull, 0x0000000000007FFFull, 0x000000007FFFFFFFull, 0x9999999999999999ull,
         0x000000007FFFFFFFull, 0x9999999999999999ull, 0x9999999999999999ull},
    };
    const std::vector<Case> madd16_cases = {
        {"madd16 positive", 0x01090028u, 0x0000000000000003ull, 0x0000000000000005ull, 0x0000000000000000ull, 0x0000000000000000ull,
         0x000000000000000Full, 0x0000000000000000ull, 0x0000000000000000ull},
        {"madd16 negative product", 0x01090028u, 0xFFFFFFFFFFFFFFFDull, 0x0000000000000005ull, 0x0000000000000000ull, 0x0000000000000000ull,
         0xFFFFFFFFFFFFFFF1ull, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000000ull},
        {"madd16 sum crosses bit 31 of a positive product", 0x01090028u, 0x0000000000000020ull, 0x0000000000000001ull, 0x000000007FFFFFF0ull, 0x0000000000000000ull,
         0x0000000080000010ull, 0x0000000000000000ull, 0x0000000000000000ull},
        {"madd16 positive sum of a negative product", 0x01090028u, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000001ull, 0x0000000000000100ull, 0x0000000000000000ull,
         0xFFFFFFFF000000FFull, 0x0000000000000000ull, 0x0000000000000000ull},
        {"madd16 carry into hi", 0x01090028u, 0x0000000000007FFFull, 0x0000000000007FFFull, 0xFFFFFFFFFFFFFFFFull, 0x0000000000000002ull,
         0x000000003FFF0000ull, 0x0000000000000003ull, 0x0000000000000000ull},
        {"dmadd16", 0x01090029u, 0x0000000000000003ull, 0xFFFFFFFFFFFFFFFCull, 0x0000000100000000ull, 0x1234567812345678ull,
         0x00000000FFFFFFF4ull, 0x1234567812345678ull, 0x0000000000000000ull},
        {"dmadd16 most negative", 0x01090029u, 0xFFFFFFFFFFFF8000ull, 0x0000000000007FFFull, 0x0000000000000005ull, 0xAAAAAAAAAAAAAAAAull,
         0xFFFFFFFFC0008005ull, 0xAAAAAAAAAAAAAAAAull, 0x0000000000000000ull},
    };
    cerfengine::MipsFeatures without = Vr4122Features();
    without.macc_dmacc               = false;
    constexpr uint32_t kRd10         = 10u << 11;
    constexpr uint32_t kSa1          = 1u << 6;
    for (bool verify : {true, false}) {
        const char* mode = verify ? "verified" : "unverified";
        RunCore(Vr4122Features(), verify, cases, mode);
        RunHalt(Vr4122Features(), verify, kProbeWord | kReservedBit, "macc with a reserved bit", mode);
        RunHalt(Vr4122Features(), verify, (kProbeWord + 1u) | (kReservedBit << 1),
                "dmacc with a reserved bit", mode);
        RunHalt(without, verify, kProbeWord, "macc on a core without it", mode);
        RunCore(Vr4102Features(), verify, madd16_cases, mode);
        RunHalt(Vr4102Features(), verify, kProbeWord | kRd10, "madd16 with rd set", mode);
        RunHalt(Vr4102Features(), verify, kProbeWord | kSa1, "madd16 with sa set", mode);
        RunHalt(Vr4102Features(), verify, (kProbeWord + 1u) | kRd10, "dmadd16 with rd set", mode);
    }
    std::printf("mips_macc_test passed\n");
    return 0;
}
