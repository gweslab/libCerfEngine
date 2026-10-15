#include <cstdint>

#include <cerfengine/arm/place_fns.h>
#include "../../x86_emit.h"
#include "../../x86_emit_alu.h"

namespace cerfengine {

void EmitHalfwordAlignCheck(uint8_t*& cursor, bool sctlr_a,
                            uint8_t** align_fault_label,
                            uint8_t** cross_label) {
    using namespace x86;

    *align_fault_label = nullptr;
    *cross_label       = nullptr;

    if (sctlr_a) {
        EmitTestRegImm32(cursor, kEcx, 1u);
        *align_fault_label = EmitJnzLabel32(cursor);
        return;
    }

    /* A Tiny page maps 1 KB - Table D15-10 (p. D15-2609) descriptor 0b11
       gives its base in bits[31:10] - so an unaligned access at a 1 KB
       boundary can span two mappings (A3.2.3, p. A3-109). */
    EmitMovRegReg  (cursor, kEdx, kEcx);
    EmitAndRegImm32(cursor, kEdx, 0x3FFu);
    EmitCmpRegImm32(cursor, kEdx, 0x3FFu);
    *cross_label = EmitJzLabel32(cursor);
}

}
