#include "x86_host_abi.h"

namespace cerfengine::cerfir {

#if defined(_WIN64)
void X86RequireKnownCallAbi() {
    throw EmitFault("CerfIR x86 lowering: the Windows x64 calling convention has no lowering");
}
#endif

}
