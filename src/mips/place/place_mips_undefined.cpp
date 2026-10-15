#include "../mips_place_fns.h"

#include <cstdint>

#include "../mips_block_context.h"
#include "../mips_block_compiler.h"
#include "../../x86_emit.h"

namespace cerfengine {

uint8_t* PlaceMipsUndefined(uint8_t* cursor, MipsDecodedInsn* d, MipsBlockContext* ctx) {
    using namespace x86;
    EmitPush32(cursor, d->raw);
    EmitPush32(cursor, d->guest_address);
    EmitPush32(cursor, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx->compiler)));
    EmitCall(cursor, reinterpret_cast<void*>(&MipsBlockCompiler::UnimplementedHelper));
    return cursor;
}

}
