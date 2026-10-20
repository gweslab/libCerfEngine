#include "x86_emit.h"

#include <cstdio>

namespace cerfengine::x86 {

void ThrowModRmNeedsSib(uint8_t mod) {
    char text[96];
    std::snprintf(text, sizeof(text),
                  "x86 emit: r/m=100b in a memory form requires a SIB byte (mod=%u)", mod);
    throw EmitFault(text);
}

void ThrowModRmBareDisp32() {
    throw EmitFault("x86 emit: mod=00b r/m=101b encodes disp32, not [EBP]");
}

void ThrowRel8FixupRange(uint8_t opcode, ptrdiff_t disp) {
    char text[96];
    std::snprintf(text, sizeof(text),
                  "x86 emit: rel8 fixup out of range - opcode 0x%02X, displacement %d", opcode,
                  static_cast<int>(disp));
    throw EmitFault(text);
}

}
