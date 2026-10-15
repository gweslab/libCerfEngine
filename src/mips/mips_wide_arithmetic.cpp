#include "mips_wide_arithmetic.h"
#include <cerfengine/calling_convention.h>

#include <cstdint>

#include <cerfengine/engine.h>
#include <cerfengine/wide_multiply.h>
#include <cerfengine/mips/mips_cpu.h>
#include <cerfengine/mips/mips_cpu_state.h>

namespace cerfengine {

CERFENGINE_REGISTER(MipsWideArithmetic);

void MipsWideArithmetic::OnReady() {
    cpu_state_ = engine_.Get<MipsCpu>().State();
}

void CERFENGINE_FASTCALL MipsWideArithmetic::DmultuHelper(uint32_t rs, uint32_t rt, MipsWideArithmetic* w) {
    Mulu64(w->cpu_state_->gpr[rs], w->cpu_state_->gpr[rt],
           &w->cpu_state_->lo, &w->cpu_state_->hi);
}

void CERFENGINE_FASTCALL MipsWideArithmetic::DmultHelper(uint32_t rs, uint32_t rt, MipsWideArithmetic* w) {
    /* {HI,LO} = gpr[rs] * gpr[rt], full 128-bit SIGNED = unsigned product with the
       sign correction (QEMU host-utils.h muls64; gen_muldiv OPC_DMULT muls2_i64). */
    const uint64_t a = w->cpu_state_->gpr[rs];
    const uint64_t b = w->cpu_state_->gpr[rt];
    uint64_t lo, hi;
    Mulu64(a, b, &lo, &hi);
    if (static_cast<int64_t>(a) < 0) hi -= b;
    if (static_cast<int64_t>(b) < 0) hi -= a;
    w->cpu_state_->lo = lo;
    w->cpu_state_->hi = hi;
}

void CERFENGINE_FASTCALL MipsWideArithmetic::DdivHelper(uint32_t rs, uint32_t rt, MipsWideArithmetic* w) {
    const int64_t a = static_cast<int64_t>(w->cpu_state_->gpr[rs]);
    int64_t b = static_cast<int64_t>(w->cpu_state_->gpr[rt]);
    if (b == 0 || (a == static_cast<int64_t>(0x8000000000000000ULL) && b == -1)) {
        b = 1;
    }
    w->cpu_state_->lo = static_cast<uint64_t>(a / b);
    w->cpu_state_->hi = static_cast<uint64_t>(a % b);
}

void CERFENGINE_FASTCALL MipsWideArithmetic::DdivuHelper(uint32_t rs, uint32_t rt, MipsWideArithmetic* w) {
    const uint64_t a = w->cpu_state_->gpr[rs];
    uint64_t b = w->cpu_state_->gpr[rt];
    if (b == 0) {
        b = 1;
    }
    w->cpu_state_->lo = a / b;
    w->cpu_state_->hi = a % b;
}


void CERFENGINE_FASTCALL MipsWideArithmetic::DsllvHelper(uint32_t rd, uint32_t rt,
                                                         uint32_t rs,
                                                         MipsWideArithmetic* w) {
    if (rd == 0) {
        return;
    }
    const uint32_t n = static_cast<uint32_t>(w->cpu_state_->gpr[rs]) & 0x3fu;
    w->cpu_state_->gpr[rd] = w->cpu_state_->gpr[rt] << n;
}

void CERFENGINE_FASTCALL MipsWideArithmetic::DsrlvHelper(uint32_t rd, uint32_t rt,
                                                         uint32_t rs,
                                                         MipsWideArithmetic* w) {
    if (rd == 0) {
        return;
    }
    const uint32_t n = static_cast<uint32_t>(w->cpu_state_->gpr[rs]) & 0x3fu;
    w->cpu_state_->gpr[rd] = w->cpu_state_->gpr[rt] >> n;
}

void CERFENGINE_FASTCALL MipsWideArithmetic::DsravHelper(uint32_t rd, uint32_t rt,
                                                         uint32_t rs,
                                                         MipsWideArithmetic* w) {
    if (rd == 0) {
        return;
    }
    const uint32_t n = static_cast<uint32_t>(w->cpu_state_->gpr[rs]) & 0x3fu;
    w->cpu_state_->gpr[rd] = static_cast<uint64_t>(
        static_cast<int64_t>(w->cpu_state_->gpr[rt]) >> n);
}

}
