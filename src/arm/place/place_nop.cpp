#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

uint8_t* PlaceNop(uint8_t*      cursor,
                  DecodedInsn*,
                  BlockContext*) {
    return cursor;
}

}
