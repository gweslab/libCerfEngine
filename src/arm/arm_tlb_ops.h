#pragma once

#include <cstdint>

#include <cerfengine/arm/arm_mmu_state.h>

namespace cerfengine {

void ArmTlbFlushAll(ArmTlbUnit* unit);

struct ArmTlbInvalidation {
    uint32_t base;
    uint32_t span_bytes;
};

/* ARM DDI 0406C.d B3.10.1; DDI 0406C.c B3.19.2. */
ArmTlbInvalidation ArmTlbInvalidateByVa(ArmTlbUnit* unit,
                                        uint32_t process_id, uint32_t va);

void ArmTlbInvalidateByPa(ArmTlbUnit* unit, uint32_t pa, uint32_t bytes);

void FillFastTlb(ArmTlbUnit* unit, uint32_t folded_va, uint8_t* host,
                 uint32_t pa, uint8_t asid, bool global, bool writable,
                 uint32_t span_bytes);

void FillFastTlbIo(ArmTlbUnit* unit, uint32_t folded_va, uint32_t pa,
                   uint8_t asid, bool global, bool writable,
                   uint32_t span_bytes);

void FillFastTlbSubpage(ArmTlbUnit* unit, uint32_t folded_va, uint8_t* host,
                        uint32_t pa, uint8_t asid, bool global, uint32_t access);

}
