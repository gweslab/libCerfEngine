#pragma once

#include <cstdint>

#include "mips_decoded_insn.h"

namespace cerfengine {

class MipsDecoder {
public:
    void Configure(bool has_fpu, bool has_llsc, bool has_mips4,
                   bool has_vr41xx_power_modes, bool has_64bit, bool has_eret,
                   bool has_rfe, bool has_mips16, bool has_tx39_multiply_add) {
        has_fpu_   = has_fpu;
        has_llsc_  = has_llsc;
        has_mips4_ = has_mips4;
        has_vr41xx_power_modes_ = has_vr41xx_power_modes;
        has_64bit_ = has_64bit;
        has_eret_  = has_eret;
        has_rfe_   = has_rfe;
        has_mips16_ = has_mips16;
        has_tx39_multiply_add_ = has_tx39_multiply_add;
    }

    bool Decode(uint32_t word, uint32_t pc, MipsDecodedInsn* d);

private:
    bool has_fpu_   = false;
    bool has_llsc_  = false;
    bool has_mips4_ = false;
    bool has_vr41xx_power_modes_ = false;
    bool has_64bit_ = false;
    bool has_eret_  = false;
    bool has_rfe_   = false;
    bool has_mips16_ = false;
    bool has_tx39_multiply_add_ = false;
};

}
