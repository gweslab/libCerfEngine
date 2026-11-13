#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <random>
#include <vector>

namespace {

using cerfengine::ShCpuState;

constexpr uint16_t kMacLR4R5 = 0x054Fu;
constexpr uint16_t kMacWR4R5 = 0x454Fu;
constexpr uint16_t kMacLR4R4 = 0x044Fu;
constexpr uint16_t kMacWR4R4 = 0x444Fu;
constexpr uint16_t kBraSkip  = 0xA001u;
constexpr uint16_t kJmpR1    = 0x412Bu;
constexpr uint16_t kBrafR1   = 0x0123u;

constexpr uint32_t kTailOffset = 8u;

constexpr uint32_t kRmData = kShRigDataVa;
constexpr uint32_t kRnData = kShRigDataVa + 0x10u;

constexpr int64_t kMax48 = 0x00007FFFFFFFFFFFll;
constexpr int64_t kMin48 = -0x0000800000000000ll;

struct Vector {
    uint64_t mac;
    uint32_t rm_operand;
    uint32_t rn_operand;
    uint64_t without_saturation;
    uint64_t with_saturation;
};

constexpr Vector kLongVectors[] = {
    {0x0000000000000000ull, 0x00000003u, 0x00000005u, 0x000000000000000Full, 0x000000000000000Full},
    {0x0000123400000000ull, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x4000123300000001ull, 0x00007FFFFFFFFFFFull},
    {0xFFFFF00000000000ull, 0x80000000u, 0x00000002u, 0xFFFFEFFF00000000ull, 0xFFFFEFFF00000000ull},
    {0x1234800000000000ull, 0x00000001u, 0xFFFFFFFFu, 0x12347FFFFFFFFFFFull, 0xFFFF800000000000ull},
    {0x00007FFFFFFFFFF0ull, 0x00000010u, 0x00000001u, 0x0000800000000000ull, 0x00007FFFFFFFFFFFull},
    {0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x0000000000000000ull, 0x0000000000000000ull},
};

constexpr Vector kWordVectors[] = {
    {0x0000000100000010ull, 0x0003u, 0xFFFEu, 0x000000010000000Aull, 0x000000010000000Aull},
    {0x555500007FFFFFF0ull, 0x0100u, 0x0100u, 0x555500008000FFF0ull, 0x555500017FFFFFFFull},
    {0x0000000280000005ull, 0x8000u, 0x0001u, 0x000000027FFF8005ull, 0x0000000380000000ull},
    {0xFFFFFFFFFFFFFFF0ull, 0x7FFFu, 0x7FFFu, 0x000000003FFEFFF1ull, 0xFFFFFFFF3FFEFFF1ull},
};

uint64_t ReferenceLong(uint64_t mac, uint32_t rm_operand, uint32_t rn_operand, bool saturate) {
    const int64_t product =
        static_cast<int64_t>(static_cast<int32_t>(rn_operand)) * static_cast<int64_t>(static_cast<int32_t>(rm_operand));
    if (!saturate) return mac + static_cast<uint64_t>(product);
    int64_t sum = (static_cast<int64_t>(mac << 16) >> 16) + product;
    if (sum > kMax48) sum = kMax48;
    if (sum < kMin48) sum = kMin48;
    return static_cast<uint64_t>(sum);
}

uint64_t ReferenceWord(uint64_t mac, uint32_t rm_operand, uint32_t rn_operand, bool saturate) {
    const int64_t product =
        static_cast<int64_t>(static_cast<int16_t>(rn_operand)) * static_cast<int64_t>(static_cast<int16_t>(rm_operand));
    if (!saturate) return mac + static_cast<uint64_t>(product);
    uint64_t      mach = mac >> 32;
    const int64_t sum  = static_cast<int64_t>(static_cast<int32_t>(mac)) + product;
    uint32_t      macl = static_cast<uint32_t>(sum);
    if (sum > INT32_MAX || sum < INT32_MIN) {
        macl = sum > INT32_MAX ? 0x7FFFFFFFu : 0x80000000u;
        mach |= 1u;
    }
    return (mach << 32) | macl;
}

struct Context {
    ShIrRig&    rig;
    const char* mode;
};

void SetData(const Context& t, uint32_t va, uint32_t value) { t.rig.Host().LoadWords(ShRigPa(va), &value, 1u); }

void ExpectMac(const Context& t, const char* what, uint64_t want) {
    ShRigExpect(t.mode, what, "MACH", t.rig.State().mach, static_cast<uint32_t>(want >> 32));
    ShRigExpect(t.mode, what, "MACL", t.rig.State().macl, static_cast<uint32_t>(want));
}

ShCpuState MacState(uint64_t mac, bool saturate) {
    ShCpuState s{};
    s.sr     = kShRigSrBase | (saturate ? cerfengine::kShSrS : 0u);
    s.mach   = static_cast<uint32_t>(mac >> 32);
    s.macl   = static_cast<uint32_t>(mac);
    s.gpr[4] = kRmData;
    s.gpr[5] = kRnData;
    return s;
}

void RunCase(const Context& t, const char* what, uint16_t op, uint32_t bytes, uint64_t mac, uint32_t rm_operand,
             uint32_t rn_operand, bool saturate, uint64_t want) {
    SetData(t, kRmData, rm_operand);
    SetData(t, kRnData, rn_operand);
    t.rig.Run({op}, MacState(mac, saturate), what);
    ExpectMac(t, what, want);
    ShRigExpect(t.mode, what, "R4", t.rig.State().gpr[4], kRmData + bytes);
    ShRigExpect(t.mode, what, "R5", t.rig.State().gpr[5], kRnData + bytes);
}

void RunVectors(const Context& t) {
    for (const Vector& v : kLongVectors) {
        RunCase(t, "MAC.L vector", kMacLR4R5, 4u, v.mac, v.rm_operand, v.rn_operand, false, v.without_saturation);
        RunCase(t, "MAC.L vector, S", kMacLR4R5, 4u, v.mac, v.rm_operand, v.rn_operand, true, v.with_saturation);
    }
    for (const Vector& v : kWordVectors) {
        RunCase(t, "MAC.W vector", kMacWR4R5, 2u, v.mac, v.rm_operand, v.rn_operand, false, v.without_saturation);
        RunCase(t, "MAC.W vector, S", kMacWR4R5, 2u, v.mac, v.rm_operand, v.rn_operand, true, v.with_saturation);
    }
}

uint32_t RunEdgeSweep(const Context& t) {
    constexpr uint64_t kMacEdges[] = {0x0000000000000000ull, 0xFFFFFFFFFFFFFFFFull, 0x00007FFFFFFFFFFFull,
                                      0xFFFF800000000000ull, 0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull,
                                      0x000000007FFFFFFFull, 0x0000000080000000ull, 0x12345678ABCDEF01ull,
                                      0xFFFF7FFFFFFFFFFFull, 0x0000800000000000ull};
    constexpr uint32_t kOperandEdges[] = {0x00000000u, 0x00000001u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u,
                                          0x00007FFFu, 0x00008000u, 0xFFFF8000u, 0x0000FFFFu, 0x12345678u};
    uint32_t runs = 0;
    for (uint64_t mac : kMacEdges) {
        for (uint32_t a : kOperandEdges) {
            for (uint32_t b : kOperandEdges) {
                for (bool s : {false, true}) {
                    RunCase(t, "MAC.L sweep", kMacLR4R5, 4u, mac, a, b, s, ReferenceLong(mac, a, b, s));
                    RunCase(t, "MAC.W sweep", kMacWR4R5, 2u, mac, a, b, s, ReferenceWord(mac, a, b, s));
                    runs += 2u;
                }
            }
        }
    }
    return runs;
}

uint32_t RunRandomSweep(const Context& t, std::mt19937_64& rng) {
    uint32_t runs = 0;
    for (uint32_t i = 0; i < 2000u; ++i) {
        const uint64_t mac = rng();
        const uint32_t a   = static_cast<uint32_t>(rng());
        const uint32_t b   = static_cast<uint32_t>(rng());
        const bool     s   = (rng() & 1u) != 0u;
        RunCase(t, "MAC.L random", kMacLR4R5, 4u, mac, a, b, s, ReferenceLong(mac, a, b, s));
        RunCase(t, "MAC.W random", kMacWR4R5, 2u, mac, a, b, s, ReferenceWord(mac, a, b, s));
        runs += 2u;
    }
    return runs;
}

void RunSameRegister(const Context& t) {
    SetData(t, kRmData, 0x00000003u);
    SetData(t, kRmData + 4u, 0xFFFFFFF9u);
    t.rig.Run({kMacLR4R4}, MacState(0x0000000000000010ull, false), "MAC.L @R4+,@R4+");
    ExpectMac(t, "MAC.L @R4+,@R4+", 0xFFFFFFFFFFFFFFFBull);
    ShRigExpect(t.mode, "MAC.L @R4+,@R4+", "R4", t.rig.State().gpr[4], kRmData + 8u);

    SetData(t, kRmData, 0xFFFB0006u);
    t.rig.Run({kMacWR4R4}, MacState(0x0000000000000010ull, false), "MAC.W @R4+,@R4+");
    ExpectMac(t, "MAC.W @R4+,@R4+", 0xFFFFFFFFFFFFFFF2ull);
    ShRigExpect(t.mode, "MAC.W @R4+,@R4+", "R4", t.rig.State().gpr[4], kRmData + 4u);
}

void RunAddressError(const Context& t, const char* what, std::vector<uint16_t> body, uint32_t rm, uint32_t rn,
                     uint32_t tea) {
    ShCpuState in = MacState(0x1122334455667788ull, false);
    in.sr         = kShRigSrPrivileged;
    in.gpr[4]     = rm;
    in.gpr[5]     = rn;
    const uint32_t start = t.rig.Run(std::move(body), in, what, [&](ShCpuState& s) {
        s.vbr     = kShRigVbr;
        s.gpr[13] = t.rig.ReturnStubVa();
    });
    const ShCpuState& s = t.rig.State();
    ShRigExpect(t.mode, what, "handler entries", s.gpr[9], 1u);
    ShRigExpect(t.mode, what, "EXPEVT", s.expevt, kShRigExpevtAddressRead);
    ShRigExpect(t.mode, what, "SPC", s.spc, start);
    ShRigExpect(t.mode, what, "TEA", t.rig.Mmu().State()->tea, tea);
    ShRigExpect(t.mode, what, "R4", s.gpr[4], rm);
    ShRigExpect(t.mode, what, "R5", s.gpr[5], rn);
    ExpectMac(t, what, 0x1122334455667788ull);
}

void RunAddressErrors(const Context& t) {
    RunAddressError(t, "MAC.L, @Rm misaligned", {kMacLR4R5}, kRmData + 2u, kRnData, kRmData + 2u);
    RunAddressError(t, "MAC.L, both misaligned", {kMacLR4R5}, kRmData + 2u, kRnData + 2u, kRnData + 2u);
    RunAddressError(t, "MAC.W, @Rm misaligned", {kMacWR4R5}, kRmData + 1u, kRnData, kRmData + 1u);
    RunAddressError(t, "MAC.W, both misaligned", {kMacWR4R5}, kRmData + 1u, kRnData + 1u, kRnData + 1u);
    RunAddressError(t, "MAC.L in a delay slot, @Rm misaligned", {kBraSkip, kMacLR4R5, kShRigNop}, kRmData + 2u,
                    kRnData, kRmData + 2u);
}

void RunDelaySlot(const Context& t) {
    SetData(t, kRmData, 0x00000005u);
    SetData(t, kRnData, 0xFFFFFFFDu);
    t.rig.Run({kBraSkip, kMacLR4R5, kShRigAddOneR10}, MacState(0x0000000100000000ull, false),
              "MAC.L in a delay slot");
    ExpectMac(t, "MAC.L in a delay slot", 0x00000000FFFFFFF1ull);
    ShRigExpect(t.mode, "MAC.L in a delay slot", "R4", t.rig.State().gpr[4], kRmData + 4u);
    ShRigExpect(t.mode, "MAC.L in a delay slot", "R5", t.rig.State().gpr[5], kRnData + 4u);
    ShRigExpect(t.mode, "MAC.L in a delay slot", "R10", t.rig.State().gpr[10], 0u);

    SetData(t, kRmData, 0x00000100u);
    SetData(t, kRnData, 0x00000100u);
    t.rig.Run({kBraSkip, kMacWR4R5, kShRigAddOneR10}, MacState(0x555500007FFFFFF0ull, true),
              "MAC.W in a delay slot, S");
    ExpectMac(t, "MAC.W in a delay slot, S", 0x555500017FFFFFFFull);
}

void RunLiveTargetSlots(const Context& t) {
    SetData(t, kRmData, 0x00000100u);
    SetData(t, kRnData, 0x00000100u);
    t.rig.Run({kJmpR1, kMacWR4R5, kShRigAddOneR10}, MacState(0x555500007FFFFFF0ull, true), "MAC.W in a JMP slot, S",
              [](ShCpuState& s) { s.gpr[1] = s.pc + kTailOffset; });
    ExpectMac(t, "MAC.W in a JMP slot, S", 0x555500017FFFFFFFull);
    ShRigExpect(t.mode, "MAC.W in a JMP slot, S", "R10", t.rig.State().gpr[10], 0u);

    SetData(t, kRmData, 0x7FFFFFFFu);
    SetData(t, kRnData, 0x7FFFFFFFu);
    t.rig.Run({kShRigRts, kMacLR4R5, kShRigAddOneR10}, MacState(0x0000123400000000ull, true),
              "MAC.L in an RTS slot, S", [](ShCpuState& s) { s.pr = s.pc + kTailOffset; });
    ExpectMac(t, "MAC.L in an RTS slot, S", 0x00007FFFFFFFFFFFull);
    ShRigExpect(t.mode, "MAC.L in an RTS slot, S", "R10", t.rig.State().gpr[10], 0u);

    SetData(t, kRmData, 0x00000005u);
    SetData(t, kRnData, 0xFFFFFFFDu);
    t.rig.Run({kBrafR1, kMacLR4R5, kShRigAddOneR10}, MacState(0x0000000100000000ull, false), "MAC.L in a BRAF slot",
              [](ShCpuState& s) { s.gpr[1] = kTailOffset - 4u; });
    ExpectMac(t, "MAC.L in a BRAF slot", 0x00000000FFFFFFF1ull);
    ShRigExpect(t.mode, "MAC.L in a BRAF slot", "R10", t.rig.State().gpr[10], 0u);
}

void RunCycles(const Context& t) {
    SetData(t, kRmData, 1u);
    SetData(t, kRnData, 1u);
    t.rig.Run({kShRigNop}, MacState(0u, false), "NOP");
    const uint32_t nop = t.rig.State().guest_cycle_counter;
    for (uint16_t op : {kMacLR4R5, kMacWR4R5}) {
        t.rig.Run({op}, MacState(0u, false), "MAC cycles");
        ShRigExpect(t.mode, "MAC cycles", "cycles", t.rig.State().guest_cycle_counter, nop + 1u);
    }
}

void RunAll(bool dsp, bool verify, const char* mode) {
    uint32_t runs = 0;
    {
        ShIrRig       rig(dsp, verify);
        const Context t{rig, mode};
        rig.LoadReturningHandler(kShRigGeneralVector);
        RunVectors(t);
        runs += RunEdgeSweep(t);
        RunSameRegister(t);
        RunDelaySlot(t);
        RunLiveTargetSlots(t);
        RunAddressErrors(t);
        RunCycles(t);
    }
    {
        ShIrRig         rig(dsp, verify);
        const Context   t{rig, mode};
        std::mt19937_64 rng(0x4D4143u);
        runs += RunRandomSweep(t, rng);
    }
    std::printf("%s: SH MAC vectors and %u sweep runs passed\n", mode, runs);
}

}

int main() {
    RunAll(false, false, "plain");
    RunAll(false, true, "verified IR");
    RunAll(true, false, "SH3-DSP");
    return 0;
}
