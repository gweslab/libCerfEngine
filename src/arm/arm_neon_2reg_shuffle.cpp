#include "arm_neon_2reg_shuffle.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>
#include <cstring>

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegShuffle);

namespace {

/* VTRN - per A8.8.420: for each D-reg pair, swap
   Elem[D[d+r], 2*e+1] with Elem[D[m+r], 2*e] for every adjacent pair. */
inline void DoTrn(uint8_t* dp, uint8_t* mp, uint32_t ebytes,
                  uint32_t elements_per_dreg) {
    uint8_t tmp[8];
    for (uint32_t e = 0; e < elements_per_dreg / 2u; ++e) {
        const uint32_t off_d_odd  = (2u * e + 1u) * ebytes;
        const uint32_t off_m_even = (2u * e)       * ebytes;
        std::memcpy(tmp,             dp + off_d_odd,  ebytes);
        std::memcpy(dp + off_d_odd,  mp + off_m_even, ebytes);
        std::memcpy(mp + off_m_even, tmp,             ebytes);
    }
}

/* VUZP - per A8.8.422: zipped = Mreg:Dreg; new Dreg
   takes even-indexed elements of zipped, new Mreg takes odd-indexed. */
inline void DoUzp(uint8_t* dp, uint8_t* mp, uint32_t ebytes,
                  uint32_t total_bytes) {
    uint8_t zipped[32];
    const uint32_t half = total_bytes / 2u;
    std::memcpy(zipped,        dp, half);
    std::memcpy(zipped + half, mp, half);

    uint8_t out_d[16];
    uint8_t out_m[16];
    const uint32_t total_elements = total_bytes / ebytes;
    const uint32_t out_elements   = total_elements / 2u;
    for (uint32_t e = 0; e < out_elements; ++e) {
        std::memcpy(out_d + e * ebytes, zipped + (2u * e)      * ebytes, ebytes);
        std::memcpy(out_m + e * ebytes, zipped + (2u * e + 1u) * ebytes, ebytes);
    }
    std::memcpy(dp, out_d, half);
    std::memcpy(mp, out_m, half);
}

/* VZIP - per A8.8.423 (and D-form equivalent): interleave
   the elements of Dreg and Mreg into a 2x-wide buffer, then split halves
   back into the two destination registers. */
inline void DoZip(uint8_t* dp, uint8_t* mp, uint32_t ebytes,
                  uint32_t total_bytes) {
    uint8_t zipped[32];
    const uint32_t half = total_bytes / 2u;
    const uint32_t elements_per_input = half / ebytes;
    for (uint32_t e = 0; e < elements_per_input; ++e) {
        std::memcpy(zipped + (2u * e)      * ebytes, dp + e * ebytes, ebytes);
        std::memcpy(zipped + (2u * e + 1u) * ebytes, mp + e * ebytes, ebytes);
    }
    std::memcpy(dp, zipped,        half);
    std::memcpy(mp, zipped + half, half);
}

}

void ArmNeon2RegShuffle::HandleShuffle(uint32_t op_sel, uint32_t esize, uint32_t Q,
                                       uint32_t d_idx, uint32_t m_idx) {
    auto* state = engine_.Get<ArmCpu>().State();
    const uint32_t ebytes = esize / 8u;

    if (op_sel == kTrn) {
        const uint32_t regs = Q ? 2u : 1u;
        const uint32_t elements_per_dreg = 8u / ebytes;
        for (uint32_t r = 0; r < regs; ++r) {
            uint8_t* dp = reinterpret_cast<uint8_t*>(&state->vfp_d[d_idx + r]);
            uint8_t* mp = reinterpret_cast<uint8_t*>(&state->vfp_d[m_idx + r]);
            DoTrn(dp, mp, ebytes, elements_per_dreg);
        }
        return;
    }

    const uint32_t total_bytes = Q ? 32u : 16u;
    uint8_t* dp = reinterpret_cast<uint8_t*>(&state->vfp_d[d_idx]);
    uint8_t* mp = reinterpret_cast<uint8_t*>(&state->vfp_d[m_idx]);
    if (op_sel == kUzp) {
        DoUzp(dp, mp, ebytes, total_bytes);
    } else {
        DoZip(dp, mp, ebytes, total_bytes);
    }
}

void CERFENGINE_CDECL ArmNeon2RegShuffle::HandleShuffleHelper(ArmNeon2RegShuffle* svc,
                                                              uint32_t op_sel, uint32_t esize,
                                                              uint32_t Q, uint32_t d_idx,
                                                              uint32_t m_idx) {
    svc->HandleShuffle(op_sel, esize, Q, d_idx, m_idx);
}

}
