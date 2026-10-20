#include "test_host.h"

namespace cerfengine::host {

uint8_t* CERFENGINE_FASTCALL PhysicalTranslate(void* context, uint32_t pa, bool,
                                               PhysicalPage& page) {
    return TestHost::PhysicalTranslate(context, pa, page);
}

bool CERFENGINE_FASTCALL PhysicalSpanUniform(void* context, uint32_t pa, uint32_t span) {
    return TestHost::PhysicalSpanUniform(context, pa, span);
}

bool CERFENGINE_FASTCALL MmioFindRoute(void* context, uint32_t pa, MmioRoute& route) {
    return TestHost::MmioFindRoute(context, pa, route);
}

uint32_t CERFENGINE_FASTCALL MmioReadSlow(void* context, uint32_t pa, MmioWidth width,
                                          MmioRoute& route) {
    if (!TestHost::MmioFindRoute(context, pa, route)) {
        TestFail("unexpected unrouted MMIO read at PA 0x%08X", pa);
    }
    return route.read(route.context, pa - route.base, static_cast<uint32_t>(width));
}

void CERFENGINE_FASTCALL MmioWriteSlow(void* context, uint32_t pa, uint32_t value,
                                       MmioWidth width, MmioRoute& route) {
    if (!TestHost::MmioFindRoute(context, pa, route)) {
        TestFail("unexpected unrouted MMIO write at PA 0x%08X", pa);
    }
    route.write(route.context, pa - route.base, value, static_cast<uint32_t>(width));
}

uint64_t CERFENGINE_FASTCALL MmioReadDword(void* context, uint32_t pa) {
    return TestHost::MmioReadDword(context, pa);
}

void CERFENGINE_FASTCALL MmioWriteDword(void* context, uint32_t pa, uint64_t value) {
    TestHost::MmioWriteDword(context, pa, value);
}

void CERFENGINE_FASTCALL ClockDispatch(ClockHost* clock) {
    static_cast<TestHost*>(clock)->DispatchClock();
}

template <ArmMmuAccess kAccess>
ArmTranslation CERFENGINE_FASTCALL ArmWalk(void* context, uint32_t mva, bool user, uint32_t) {
    constexpr bool kWrite = kAccess == ArmMmuAccess::kWrite || kAccess == ArmMmuAccess::kReadWrite;
    return static_cast<TestArmSystem*>(context)->Walk(mva, kWrite, user);
}

template ArmTranslation CERFENGINE_FASTCALL ArmWalk<ArmMmuAccess::kRead>(void*, uint32_t, bool,
                                                                         uint32_t);
template ArmTranslation CERFENGINE_FASTCALL ArmWalk<ArmMmuAccess::kWrite>(void*, uint32_t, bool,
                                                                          uint32_t);
template ArmTranslation CERFENGINE_FASTCALL ArmWalk<ArmMmuAccess::kReadWrite>(void*, uint32_t,
                                                                              bool, uint32_t);
template ArmTranslation CERFENGINE_FASTCALL ArmWalk<ArmMmuAccess::kExecute>(void*, uint32_t,
                                                                            bool, uint32_t);

bool CERFENGINE_FASTCALL ArmProbeWalk(void* context, uint32_t mva, uint32_t, uint32_t* pa) {
    return static_cast<TestArmSystem*>(context)->Probe(mva, pa);
}

void CERFENGINE_FASTCALL ArmAlignmentFault(void* context, uint32_t va, bool is_write) {
    static_cast<TestArmSystem*>(context)->RecordAlignmentFault(va, is_write);
}

}
