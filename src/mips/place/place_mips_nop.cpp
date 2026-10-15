#include "../mips_place_fns.h"

namespace cerfengine {

uint8_t* PlaceMipsNop(uint8_t* cursor, MipsDecodedInsn*, MipsBlockContext*) {
    return cursor;
}

}
