#include "sh_ir_rig.h"

#include "sh/sh_decoder.h"

#include <cstdint>
#include <cstdio>

namespace {

enum Cores : uint8_t { kBothCores, kDspOnly };

struct Opcode {
    const char* bits;
    Cores       cores;
};

constexpr Opcode kOpcodeMap[] = {
    {"0000000000001000", kBothCores},
    {"0000000000001001", kBothCores},
    {"0000000000001011", kBothCores},
    {"0000000000011000", kBothCores},
    {"0000000000011001", kBothCores},
    {"0000000000011011", kBothCores},
    {"0000000000101000", kBothCores},
    {"0000000000101011", kBothCores},
    {"0000000000111000", kBothCores},
    {"0000000001001000", kBothCores},
    {"0000000001011000", kBothCores},
    {"0000mmmm00000011", kBothCores},
    {"0000mmmm00100011", kBothCores},
    {"0000nnnn00000010", kBothCores},
    {"0000nnnn00001010", kBothCores},
    {"0000nnnn00010010", kBothCores},
    {"0000nnnn00011010", kBothCores},
    {"0000nnnn00100010", kBothCores},
    {"0000nnnn00101001", kBothCores},
    {"0000nnnn00101010", kBothCores},
    {"0000nnnn00110010", kBothCores},
    {"0000nnnn01000010", kBothCores},
    {"0000nnnn01010010", kDspOnly},
    {"0000nnnn01100010", kDspOnly},
    {"0000nnnn01101010", kDspOnly},
    {"0000nnnn01110010", kDspOnly},
    {"0000nnnn01111010", kDspOnly},
    {"0000nnnn10000010", kBothCores},
    {"0000nnnn10000011", kBothCores},
    {"0000nnnn10001010", kDspOnly},
    {"0000nnnn10010010", kBothCores},
    {"0000nnnn10011010", kDspOnly},
    {"0000nnnn10100010", kBothCores},
    {"0000nnnn10101010", kDspOnly},
    {"0000nnnn10110010", kBothCores},
    {"0000nnnn10111010", kDspOnly},
    {"0000nnnn11000010", kBothCores},
    {"0000nnnn11010010", kBothCores},
    {"0000nnnn11100010", kBothCores},
    {"0000nnnn11110010", kBothCores},
    {"0000nnnnmmmm0100", kBothCores},
    {"0000nnnnmmmm0101", kBothCores},
    {"0000nnnnmmmm0110", kBothCores},
    {"0000nnnnmmmm0111", kBothCores},
    {"0000nnnnmmmm1100", kBothCores},
    {"0000nnnnmmmm1101", kBothCores},
    {"0000nnnnmmmm1110", kBothCores},
    {"0000nnnnmmmm1111", kBothCores},
    {"0001nnnnmmmmdddd", kBothCores},
    {"0010nnnnmmmm0000", kBothCores},
    {"0010nnnnmmmm0001", kBothCores},
    {"0010nnnnmmmm0010", kBothCores},
    {"0010nnnnmmmm0100", kBothCores},
    {"0010nnnnmmmm0101", kBothCores},
    {"0010nnnnmmmm0110", kBothCores},
    {"0010nnnnmmmm0111", kBothCores},
    {"0010nnnnmmmm1000", kBothCores},
    {"0010nnnnmmmm1001", kBothCores},
    {"0010nnnnmmmm1010", kBothCores},
    {"0010nnnnmmmm1011", kBothCores},
    {"0010nnnnmmmm1100", kBothCores},
    {"0010nnnnmmmm1101", kBothCores},
    {"0010nnnnmmmm1110", kBothCores},
    {"0010nnnnmmmm1111", kBothCores},
    {"0011nnnnmmmm0000", kBothCores},
    {"0011nnnnmmmm0010", kBothCores},
    {"0011nnnnmmmm0011", kBothCores},
    {"0011nnnnmmmm0100", kBothCores},
    {"0011nnnnmmmm0101", kBothCores},
    {"0011nnnnmmmm0110", kBothCores},
    {"0011nnnnmmmm0111", kBothCores},
    {"0011nnnnmmmm1000", kBothCores},
    {"0011nnnnmmmm1010", kBothCores},
    {"0011nnnnmmmm1011", kBothCores},
    {"0011nnnnmmmm1100", kBothCores},
    {"0011nnnnmmmm1101", kBothCores},
    {"0011nnnnmmmm1110", kBothCores},
    {"0011nnnnmmmm1111", kBothCores},
    {"0100mmmm00000110", kBothCores},
    {"0100mmmm00000111", kBothCores},
    {"0100mmmm00001010", kBothCores},
    {"0100mmmm00001011", kBothCores},
    {"0100mmmm00001110", kBothCores},
    {"0100mmmm00010100", kDspOnly},
    {"0100mmmm00010110", kBothCores},
    {"0100mmmm00010111", kBothCores},
    {"0100mmmm00011010", kBothCores},
    {"0100mmmm00011110", kBothCores},
    {"0100mmmm00100110", kBothCores},
    {"0100mmmm00100111", kBothCores},
    {"0100mmmm00101010", kBothCores},
    {"0100mmmm00101011", kBothCores},
    {"0100mmmm00101110", kBothCores},
    {"0100mmmm00110111", kBothCores},
    {"0100mmmm00111110", kBothCores},
    {"0100mmmm01000111", kBothCores},
    {"0100mmmm01001110", kBothCores},
    {"0100mmmm01010111", kDspOnly},
    {"0100mmmm01011110", kDspOnly},
    {"0100mmmm01100110", kDspOnly},
    {"0100mmmm01100111", kDspOnly},
    {"0100mmmm01101010", kDspOnly},
    {"0100mmmm01101110", kDspOnly},
    {"0100mmmm01110110", kDspOnly},
    {"0100mmmm01110111", kDspOnly},
    {"0100mmmm01111010", kDspOnly},
    {"0100mmmm01111110", kDspOnly},
    {"0100mmmm10000110", kDspOnly},
    {"0100mmmm10000111", kBothCores},
    {"0100mmmm10001010", kDspOnly},
    {"0100mmmm10001110", kBothCores},
    {"0100mmmm10010110", kDspOnly},
    {"0100mmmm10010111", kBothCores},
    {"0100mmmm10011010", kDspOnly},
    {"0100mmmm10011110", kBothCores},
    {"0100mmmm10100110", kDspOnly},
    {"0100mmmm10100111", kBothCores},
    {"0100mmmm10101010", kDspOnly},
    {"0100mmmm10101110", kBothCores},
    {"0100mmmm10110110", kDspOnly},
    {"0100mmmm10110111", kBothCores},
    {"0100mmmm10111010", kDspOnly},
    {"0100mmmm10111110", kBothCores},
    {"0100mmmm11000111", kBothCores},
    {"0100mmmm11001110", kBothCores},
    {"0100mmmm11010111", kBothCores},
    {"0100mmmm11011110", kBothCores},
    {"0100mmmm11100111", kBothCores},
    {"0100mmmm11101110", kBothCores},
    {"0100mmmm11110111", kBothCores},
    {"0100mmmm11111110", kBothCores},
    {"0100nnnn00000000", kBothCores},
    {"0100nnnn00000001", kBothCores},
    {"0100nnnn00000010", kBothCores},
    {"0100nnnn00000011", kBothCores},
    {"0100nnnn00000100", kBothCores},
    {"0100nnnn00000101", kBothCores},
    {"0100nnnn00001000", kBothCores},
    {"0100nnnn00001001", kBothCores},
    {"0100nnnn00010000", kBothCores},
    {"0100nnnn00010001", kBothCores},
    {"0100nnnn00010010", kBothCores},
    {"0100nnnn00010011", kBothCores},
    {"0100nnnn00010101", kBothCores},
    {"0100nnnn00011000", kBothCores},
    {"0100nnnn00011001", kBothCores},
    {"0100nnnn00011011", kBothCores},
    {"0100nnnn00100000", kBothCores},
    {"0100nnnn00100001", kBothCores},
    {"0100nnnn00100010", kBothCores},
    {"0100nnnn00100011", kBothCores},
    {"0100nnnn00100100", kBothCores},
    {"0100nnnn00100101", kBothCores},
    {"0100nnnn00101000", kBothCores},
    {"0100nnnn00101001", kBothCores},
    {"0100nnnn00110011", kBothCores},
    {"0100nnnn01000011", kBothCores},
    {"0100nnnn01010011", kDspOnly},
    {"0100nnnn01100010", kDspOnly},
    {"0100nnnn01100011", kDspOnly},
    {"0100nnnn01110010", kDspOnly},
    {"0100nnnn01110011", kDspOnly},
    {"0100nnnn10000010", kDspOnly},
    {"0100nnnn10000011", kBothCores},
    {"0100nnnn10010010", kDspOnly},
    {"0100nnnn10010011", kBothCores},
    {"0100nnnn10100010", kDspOnly},
    {"0100nnnn10100011", kBothCores},
    {"0100nnnn10110010", kDspOnly},
    {"0100nnnn10110011", kBothCores},
    {"0100nnnn11000011", kBothCores},
    {"0100nnnn11010011", kBothCores},
    {"0100nnnn11100011", kBothCores},
    {"0100nnnn11110011", kBothCores},
    {"0100nnnnmmmm1100", kBothCores},
    {"0100nnnnmmmm1101", kBothCores},
    {"0100nnnnmmmm1111", kBothCores},
    {"0101nnnnmmmmdddd", kBothCores},
    {"0110nnnnmmmm0000", kBothCores},
    {"0110nnnnmmmm0001", kBothCores},
    {"0110nnnnmmmm0010", kBothCores},
    {"0110nnnnmmmm0011", kBothCores},
    {"0110nnnnmmmm0100", kBothCores},
    {"0110nnnnmmmm0101", kBothCores},
    {"0110nnnnmmmm0110", kBothCores},
    {"0110nnnnmmmm0111", kBothCores},
    {"0110nnnnmmmm1000", kBothCores},
    {"0110nnnnmmmm1001", kBothCores},
    {"0110nnnnmmmm1010", kBothCores},
    {"0110nnnnmmmm1011", kBothCores},
    {"0110nnnnmmmm1100", kBothCores},
    {"0110nnnnmmmm1101", kBothCores},
    {"0110nnnnmmmm1110", kBothCores},
    {"0110nnnnmmmm1111", kBothCores},
    {"0111nnnniiiiiiii", kBothCores},
    {"10000000nnnndddd", kBothCores},
    {"10000001nnnndddd", kBothCores},
    {"10000010iiiiiiii", kDspOnly},
    {"10000100mmmmdddd", kBothCores},
    {"10000101mmmmdddd", kBothCores},
    {"10001000iiiiiiii", kBothCores},
    {"10001001dddddddd", kBothCores},
    {"10001011dddddddd", kBothCores},
    {"10001100dddddddd", kDspOnly},
    {"10001101dddddddd", kBothCores},
    {"10001110dddddddd", kDspOnly},
    {"10001111dddddddd", kBothCores},
    {"1001nnnndddddddd", kBothCores},
    {"1010dddddddddddd", kBothCores},
    {"1011dddddddddddd", kBothCores},
    {"11000000dddddddd", kBothCores},
    {"11000001dddddddd", kBothCores},
    {"11000010dddddddd", kBothCores},
    {"11000011iiiiiiii", kBothCores},
    {"11000100dddddddd", kBothCores},
    {"11000101dddddddd", kBothCores},
    {"11000110dddddddd", kBothCores},
    {"11000111dddddddd", kBothCores},
    {"11001000iiiiiiii", kBothCores},
    {"11001001iiiiiiii", kBothCores},
    {"11001010iiiiiiii", kBothCores},
    {"11001011iiiiiiii", kBothCores},
    {"11001100iiiiiiii", kBothCores},
    {"11001101iiiiiiii", kBothCores},
    {"11001110iiiiiiii", kBothCores},
    {"11001111iiiiiiii", kBothCores},
    {"1101nnnndddddddd", kBothCores},
    {"1110nnnniiiiiiii", kBothCores},
};

enum class Class : uint8_t { kDecoded, kReserved, kUnimplemented };

constexpr const char* kClassNames[] = {"decoded", "reserved", "unimplemented"};

constexpr uint32_t kGuestAddress = 0x8C010000u;
constexpr uint32_t kCodeCount    = 0x10000u;
constexpr uint32_t kOpcodeBits   = 16u;
constexpr uint32_t kPrefixShift  = 12u;
constexpr uint32_t kFPrefix      = 0xFu;
constexpr uint32_t kReportLimit  = 16u;

bool Covers(const char* bits, uint32_t raw) {
    for (uint32_t i = 0; i < kOpcodeBits; ++i) {
        const uint32_t bit = (raw >> (kOpcodeBits - 1u - i)) & 1u;
        if ((bits[i] == '0' && bit != 0u) || (bits[i] == '1' && bit != 1u)) return false;
    }
    return true;
}

bool Defined(bool dsp, uint32_t raw) {
    if ((raw >> kPrefixShift) == kFPrefix) return false;
    for (const Opcode& op : kOpcodeMap) {
        if ((dsp || op.cores == kBothCores) && Covers(op.bits, raw)) return true;
    }
    return false;
}

Class Classify(cerfengine::ShInsnKind kind) {
    if (kind == cerfengine::ShInsnKind::kReserved) return Class::kReserved;
    return kind == cerfengine::ShInsnKind::kUnimplemented ? Class::kUnimplemented : Class::kDecoded;
}

uint32_t CheckCore(bool dsp) {
    ShIrRig     rig(dsp, false);
    const auto& decoder = rig.EngineService<cerfengine::ShDecoder>();
    const char* core    = dsp ? "SH3-DSP" : "SH-3";
    uint32_t    wrong   = 0u;
    for (uint32_t raw = 0; raw < kCodeCount; ++raw) {
        if (dsp && (raw >> kPrefixShift) == kFPrefix) continue;
        const Class want = Defined(dsp, raw) ? Class::kDecoded : Class::kReserved;
        const Class got  = Classify(decoder.Decode(static_cast<uint16_t>(raw), kGuestAddress).kind);
        if (got != want && ++wrong <= kReportLimit) {
            std::printf("%s H'%04X: %s, want %s\n", core, raw, kClassNames[static_cast<size_t>(got)],
                        kClassNames[static_cast<size_t>(want)]);
        }
    }
    return wrong;
}

}

int main() {
    const uint32_t wrong = CheckCore(false) + CheckCore(true);
    if (wrong != 0u) TestFail("%u opcodes classified differently from the SH-3 / SH3-DSP opcode map", wrong);
    return 0;
}
