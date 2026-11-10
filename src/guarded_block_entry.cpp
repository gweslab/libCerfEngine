#include "guarded_block_entry.h"

#include <cerfengine/engine_host.h>

namespace cerfengine {

CERFENGINE_REGISTER(GuardedBlockEntry);

void GuardedBlockEntry::LogHostFault(const char* engine_name, const platform::HostFault& fault,
                                     uint32_t guest_pc) {
    CERFENGINE_LOG(engine_, kCaution, "%s: host exception 0x%08X at host address %p while running guest PC 0x%08X\n",
                   engine_name, fault.code, fault.address, guest_pc);
    char symbol[512];
    if (engine_.Host().Symbolize(fault.address, symbol, sizeof(symbol))) {
        CERFENGINE_LOG(engine_, kCaution, "%s:   host symbol %s\n", engine_name, symbol);
    }
    CERFENGINE_LOG(engine_, kCaution, "%s:   %s\n", engine_name, fault.registers);
}

void GuardedBlockEntry::PanicOnHostFault(const char* engine_name, uint32_t guest_pc) {
    engine_.Panic("%s: host exception while running guest PC 0x%08X", engine_name, guest_pc);
}

void GuardedBlockEntry::ReportHostFault(const char* engine_name, const platform::HostFault& fault,
                                        uint32_t guest_pc) {
    LogHostFault(engine_name, fault, guest_pc);
    PanicOnHostFault(engine_name, guest_pc);
}

void GuardedBlockEntry::PanicOnUnwindCode(uint32_t code, uint32_t guest_pc) {
    engine_.Panic("GuardedBlockEntry: unwind code 0x%08X while running guest PC 0x%08X", code, guest_pc);
}

void GuardedBlockEntry::PanicWithoutBackend(uint32_t guest_pc) {
    engine_.Panic("GuardedBlockEntry: guest PC 0x%08X reached dispatch in a build without an execution backend",
                  guest_pc);
}

}
