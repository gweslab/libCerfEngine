#pragma once

#include <cstdint>

#include <cerfengine/engine_setup.h>

namespace cerfengine {

struct ArmCpsrWriteMasks {
    uint32_t user;
    uint32_t priv;
    uint32_t state_trip;
    bool     nmfi_gate;
};

/* ARM DDI 0100I A4.1.39 (p. A4-77): field_mask bit 0 -> 0x000000FF,
   1 -> 0x0000FF00, 2 -> 0x00FF0000, 3 -> 0xFF000000. */
inline uint32_t ArmPsrByteMask(uint32_t field_mask) {
    uint32_t byte_mask = 0;
    if (field_mask & 1u) byte_mask |= 0x000000FFu;
    if (field_mask & 2u) byte_mask |= 0x0000FF00u;
    if (field_mask & 4u) byte_mask |= 0x00FF0000u;
    if (field_mask & 8u) byte_mask |= 0xFF000000u;
    return byte_mask;
}

inline uint32_t ArmSpsrWriteMask(const ArmFeatures& f, uint32_t field_mask) {
    const uint32_t byte_mask = ArmPsrByteMask(field_mask);
    if (f.arch_v7) {
        uint32_t spsr_mask = 0;
        if (field_mask & 1u) spsr_mask |= 0x000000FFu;
        if (field_mask & 2u) spsr_mask |= 0x0000FF00u;
        if (field_mask & 4u) spsr_mask |= 0x000F0000u;
        if (field_mask & 8u) spsr_mask |= 0xFF000000u;
        return spsr_mask;
    }
    if (f.arch_v6) return byte_mask & 0xF90F03FFu;
    if (f.dsp) return byte_mask & 0xF80000FFu;
    if (f.thumb) return byte_mask & 0xF00000FFu;
    return byte_mask & 0xF00000DFu;
}

inline ArmCpsrWriteMasks ArmCpsrWriteMasksFor(const ArmFeatures& f, uint32_t field_mask) {
    const uint32_t byte_mask = ArmPsrByteMask(field_mask);
    if (f.arch_v7) {
        return {byte_mask & 0xF80F0200u, byte_mask & 0xF80F03DFu, 0u, true};
    }
    if (f.arch_v6) {
        return {byte_mask & 0xF80F0200u, byte_mask & 0xF80F03DFu, 0x01000020u, false};
    }
    if (f.dsp) return {byte_mask & 0xF8000000u, byte_mask & 0xF80000DFu, 0x00000020u, false};
    if (f.thumb) return {byte_mask & 0xF0000000u, byte_mask & 0xF00000DFu, 0x00000020u, false};
    return {byte_mask & 0xF0000000u, byte_mask & 0xF00000DFu, 0u, false};
}

}
