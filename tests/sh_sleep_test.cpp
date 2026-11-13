#include "sh_fake_hosts.h"
#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <functional>

namespace {

using cerfengine::ShCpuState;

constexpr uint32_t kImask5     = 5u << cerfengine::kShSrImaskShift;
constexpr uint32_t kSrSleeping = cerfengine::kShSrMd | cerfengine::kShSrRb | cerfengine::kShSrBl | kImask5;
constexpr uint32_t kAboveMask  = 6u;
constexpr uint32_t kAtMask     = 5u;
constexpr uint16_t kBraOverOne = 0xA001u;

class SleepCases {
public:
    SleepCases(bool verify, const char* mode) : rig_(false, verify), mode_(mode) {
        rig_.LoadReturningHandler(kShRigInterruptVector);
        rig_.Interrupts().SetHost(&hosts_);
        rig_.Interrupts().SetSleepHost(&hosts_);
        rig_.HostRequests().RegisterListener([this] { OnHostRequest(); });
    }

    void Run() {
        WakesWithBlockSet();
        MaskedLevelKeepsSleeping();
        SleepInDelaySlot();
        StandbyStaysWithoutAnExit();
        StandbyExitTakesThePendingInterrupt();
        StandbyExitWithoutARequestWaitsInPowerDown();
        StandbyWakeLevelIgnoresBlock();
        HostRequestsCoalesceAtTheDispatcher();
        RequestPostedByAListenerReachesTheNextDispatch();
        HostRequestServicedInTheSleepWait();
        std::printf("%s: SH sleep passed\n", mode_);
    }

private:
    void Raise(uint32_t level) { hosts_.Raise(rig_.Interrupts(), level); }

    void OnHostRequest() {
        ++host_services_;
        if (request_on_first_ && host_services_ == 1u) rig_.HostRequests().Request();
        if (raise_on_service_) Raise(kAboveMask);
    }

    uint32_t HostRequestBit() const {
        return rig_.State().chain_exit_request & cerfengine::kShChainExitHostRequest;
    }

    void Reset() {
        Raise(0u);
        hosts_.accepted        = 0u;
        hosts_.standby         = false;
        hosts_.standby_level   = 0u;
        hosts_.standby_entries = 0u;
        hosts_.exited          = false;
        host_services_         = 0u;
        request_on_first_      = false;
        raise_on_service_      = false;
    }

    void ExpectStandby(const char* what, uint32_t entries) {
        const ShCpuState& s = rig_.State();
        ShRigExpect(mode_, what, "standby entries", hosts_.standby_entries, entries);
        ShRigExpect(mode_, what, "sleeping", s.sleeping, cerfengine::kShSleepStandby);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 0u);
    }

    std::function<void(ShCpuState&)> Prepare() {
        return [this](ShCpuState& s) {
            s.vbr     = kShRigVbr;
            s.gpr[13] = rig_.ReturnStubVa();
        };
    }

    ShCpuState Initial() const {
        ShCpuState s{};
        s.sr = kSrSleeping;
        return s;
    }

    void ExpectWake(const char* what, uint32_t spc) {
        const ShCpuState& s = rig_.State();
        ShRigExpect(mode_, what, "interrupt handler entries", s.gpr[9], 1u);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 1u);
        ShRigExpect(mode_, what, "INTEVT", s.intevt, kShRigFakeIntevt);
        ShRigExpect(mode_, what, "SPC", s.spc, spc);
        ShRigExpect(mode_, what, "SSR", s.ssr, kSrSleeping);
        ShRigExpect(mode_, what, "SR", s.sr, kSrSleeping);
        ShRigExpect(mode_, what, "sleeping", s.sleeping, 0u);
    }

    void WakesWithBlockSet() {
        const char* what = "SLEEP with SR.BL set and a level above the mask";
        Reset();
        Raise(kAboveMask);
        const uint32_t start = rig_.Run({kShRigSleep}, Initial(), what, Prepare());
        ExpectWake(what, start + 2u);
    }

    void MaskedLevelKeepsSleeping() {
        const char* what = "SLEEP with a level at the mask";
        Reset();
        Raise(kAtMask);
        const uint32_t start = rig_.RunSteps({kShRigSleep}, Initial(), 8u, what, Prepare());
        const ShCpuState& s  = rig_.State();
        ShRigExpect(mode_, what, "sleeping", s.sleeping, cerfengine::kShSleepMode);
        ShRigExpect(mode_, what, "PC", s.pc, start + 2u);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 0u);
        Raise(kAboveMask);
        rig_.Finish(what);
        ExpectWake(what, start + 2u);
    }

    void SleepInDelaySlot() {
        const char* what = "SLEEP in the delay slot of BRA over one instruction";
        Reset();
        Raise(kAboveMask);
        const uint32_t start = rig_.Run({kBraOverOne, kShRigSleep, kShRigNop}, Initial(), what, Prepare());
        ExpectWake(what, start + 6u);
    }

    uint32_t EnterStandbyWithARequest(const char* what) {
        Reset();
        hosts_.standby       = true;
        Raise(kAboveMask);
        const uint32_t start = rig_.RunSteps({kShRigSleep}, Initial(), 2u, what, Prepare());
        ExpectStandby(what, 1u);
        ShRigExpect(mode_, what, "PC", rig_.State().pc, start + 2u);
        return start;
    }

    void StandbyStaysWithoutAnExit() {
        const char* what     = "SLEEP with STBCR.STBY set and no standby exit";
        const uint32_t start = EnterStandbyWithARequest(what);
        rig_.StepWithoutHostExit(what);
        ExpectStandby(what, 2u);
        rig_.Step(1u, what);
        ExpectStandby(what, 2u);
        hosts_.exited = true;
        rig_.Finish(what);
        ExpectWake(what, start + 2u);
        ShRigExpect(mode_, what, "standby entries", hosts_.standby_entries, 2u);
    }

    void StandbyExitTakesThePendingInterrupt() {
        const char* what     = "a standby exit with a request above the mask";
        const uint32_t start = EnterStandbyWithARequest(what);
        hosts_.exited        = true;
        rig_.Finish(what);
        ExpectWake(what, start + 2u);
        ShRigExpect(mode_, what, "standby entries", hosts_.standby_entries, 1u);
    }

    void StandbyExitWithoutARequestWaitsInPowerDown() {
        const char* what     = "a standby exit after the request dropped";
        const uint32_t start = EnterStandbyWithARequest(what);
        Raise(0u);
        hosts_.exited = true;
        rig_.Step(1u, what);
        const ShCpuState& s = rig_.State();
        ShRigExpect(mode_, what, "sleeping", s.sleeping, cerfengine::kShSleepMode);
        ShRigExpect(mode_, what, "PC", s.pc, start + 2u);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 0u);
        Raise(kAboveMask);
        rig_.Finish(what);
        ExpectWake(what, start + 2u);
        ShRigExpect(mode_, what, "standby entries", hosts_.standby_entries, 1u);
    }

    void StandbyWakeLevelIgnoresBlock() {
        const char* what = "the standby wake level with SR.BL set";
        Reset();
        rig_.RunSteps({kShRigNop}, Initial(), 0u, what, Prepare());
        hosts_.standby_level = kAboveMask;
        ShRigExpect(mode_, what, "above the mask", rig_.Interrupts().StandbyWakePending() ? 1u : 0u, 1u);
        hosts_.standby_level = kAtMask;
        ShRigExpect(mode_, what, "at the mask", rig_.Interrupts().StandbyWakePending() ? 1u : 0u, 0u);
    }

    void HostRequestsCoalesceAtTheDispatcher() {
        const char* what = "two host requests before one dispatch";
        Reset();
        rig_.RunSteps({kShRigNop}, Initial(), 0u, what, Prepare());
        rig_.HostRequests().Request();
        rig_.HostRequests().Request();
        ShRigExpect(mode_, what, "host request bit before the dispatch", HostRequestBit(),
                    cerfengine::kShChainExitHostRequest);
        rig_.Step(1u, what);
        ShRigExpect(mode_, what, "listener runs", host_services_, 1u);
        ShRigExpect(mode_, what, "host request bit after the dispatch", HostRequestBit(), 0u);
        rig_.Finish(what);
        ShRigExpect(mode_, what, "listener runs at the end", host_services_, 1u);
    }

    void RequestPostedByAListenerReachesTheNextDispatch() {
        const char* what = "a host request a listener posts while the dispatcher services the last one";
        Reset();
        rig_.RunSteps({kShRigBraNextNop, kShRigNop}, Initial(), 0u, what, Prepare());
        request_on_first_ = true;
        rig_.HostRequests().Request();
        rig_.Step(1u, what);
        ShRigExpect(mode_, what, "listener runs after the first dispatch", host_services_, 1u);
        ShRigExpect(mode_, what, "host request bit after the first dispatch", HostRequestBit(),
                    cerfengine::kShChainExitHostRequest);
        rig_.Finish(what);
        ShRigExpect(mode_, what, "listener runs at the end", host_services_, 2u);
        ShRigExpect(mode_, what, "host request bit at the end", HostRequestBit(), 0u);
    }

    void HostRequestServicedInTheSleepWait() {
        const char* what = "a host request pending when the SLEEP wait polls";
        Reset();
        Raise(kAtMask);
        const uint32_t start = rig_.RunSteps({kShRigSleep}, Initial(), 8u, what, Prepare());
        ShRigExpect(mode_, what, "sleeping before the request", rig_.State().sleeping, cerfengine::kShSleepMode);
        raise_on_service_ = true;
        rig_.HostRequests().Request();
        rig_.Step(1u, what);
        ShRigExpect(mode_, what, "listener runs", host_services_, 1u);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 1u);
        rig_.Finish(what);
        ExpectWake(what, start + 2u);
    }

    ShIrRig        rig_;
    ShRigFakeHosts hosts_;
    const char* mode_;
    uint32_t    host_services_    = 0u;
    bool        request_on_first_ = false;
    bool        raise_on_service_ = false;
};

}

int main() {
    SleepCases(false, "plain").Run();
    SleepCases(true, "verified IR").Run();
    return 0;
}
