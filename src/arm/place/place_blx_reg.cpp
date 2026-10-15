#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

uint8_t* PlaceBlxReg(uint8_t*      cursor,
                     DecodedInsn*  d,
                     BlockContext* ctx) {
    return PlaceBxImpl(cursor, d, ctx, true);
}

}
