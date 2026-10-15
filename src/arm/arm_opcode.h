#pragma once

#include <cstdint>

namespace cerfengine {

union ArmOpcode {
    uint32_t word;

    struct {
        uint32_t        : 4;
        uint32_t c      : 1;
        uint32_t        : 3;
        uint32_t opc    : 4;
        uint32_t        : 8;
        uint32_t size   : 2;
        uint32_t        : 1;
        uint32_t bit23  : 1;
        uint32_t u      : 1;
        uint32_t marker : 3;
        uint32_t        : 4;
    } neon_data_3reg;

    struct {
        uint32_t rm     : 4;
        uint32_t align  : 2;
        uint32_t size   : 2;
        uint32_t type   : 4;
        uint32_t vd     : 4;
        uint32_t rn     : 4;
        uint32_t        : 1;
        uint32_t l      : 1;
        uint32_t d      : 1;
        uint32_t a      : 1;
        uint32_t marker : 4;
        uint32_t        : 4;
    } neon_load_store;

    /* Single element to one lane, ARM encoding (DDI 0406C.c A8.8.321 A1
       p. A8-900). [9:8] holds Table A7-20/A7-21's B<1:0>. */
    struct {
        uint32_t rm          : 4;
        uint32_t index_align : 4;
        uint32_t n_minus1    : 2;
        uint32_t size        : 2;
        uint32_t vd          : 4;
        uint32_t rn          : 4;
        uint32_t             : 1;
        uint32_t l           : 1;
        uint32_t d           : 1;
        uint32_t             : 9;
    } neon_load_store_single;
};

static_assert(sizeof(ArmOpcode) == 4);

}
