#include <cstdint>

#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

uint8_t* PlaceLoadStoreExtension(uint8_t*      cursor,
                                 DecodedInsn*  d,
                                 BlockContext* ctx) {
    if (d->op1 == 0) {
        return EmitSwap(cursor, d, ctx);
    }
    return EmitHalfwordSignedTransfer(cursor, d, ctx);
}

}
