#include <cstdint>

#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

uint8_t* PlaceNeonUnimplemented(uint8_t*      cursor,
                                DecodedInsn*  d,
                                BlockContext* ctx) {
    return PlaceArmUnimplemented(cursor, d, ctx);
}

}
