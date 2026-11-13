#include "sh_fake_hosts.h"
#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>

namespace {

using cerfengine::ShAccess;
using cerfengine::ShCpuState;
using cerfengine::ShResetKind;
using cerfengine::ShTranslation;

constexpr uint32_t kResetPc            = 0xA0000000u;
constexpr uint32_t kResetSr            = 0x700000F0u;
constexpr uint32_t kPowerOnResetExpevt = 0x000u;
constexpr uint32_t kManualResetExpevt  = 0x020u;
constexpr uint32_t kMappedVa           = 0x00400000u;
constexpr uint32_t kImask5             = 5u << cerfengine::kShSrImaskShift;
constexpr uint32_t kSrAwake            = cerfengine::kShSrMd | cerfengine::kShSrRb | kImask5;
constexpr uint32_t kSrSleeping         = kSrAwake | cerfengine::kShSrBl;
constexpr uint32_t kAboveMask          = 6u;
constexpr uint32_t kAtMask             = 5u;
constexpr uint32_t kResetStops         = cerfengine::kShChainExitSleep | cerfengine::kShChainExitReset;

ShCpuState Initial(uint32_t sr) {
    ShCpuState s{};
    s.sr = sr;
    return s;
}

class ResetCases {
public:
    ResetCases(bool verify, const char* mode) : rig_(false, verify), mode_(mode) {
        rig_.Host().AcceptResets();
        rig_.Interrupts().SetHost(&hosts_);
        rig_.Interrupts().SetSleepHost(&hosts_);
        rig_.HostRequests().RegisterListener([this] {
            if (reset_on_host_request_) RequestReset(ShResetKind::kManual);
        });
    }

    void Run() {
        ManualResetKeepsTheTlb();
        PowerOnResetClearsTheTlb();
        ResetKindFallsBackToPowerOn();
        PendingInterruptWaitsForTheReset();
        ResetEndsSleep();
        ResetPostedInsideTheSleepWait();
        std::printf("%s: SH reset passed\n", mode_);
    }

private:
    void Start(uint16_t insn, uint32_t sr, uint32_t level, uint32_t steps, const char* what) {
        hosts_.Raise(rig_.Interrupts(), level);
        hosts_.accepted        = 0u;
        reset_on_host_request_ = false;
        rig_.RunSteps({insn}, Initial(sr), steps, what);
    }

    void StartAwake(uint32_t level, const char* what) { Start(kShRigNop, kSrAwake, level, 0u, what); }

    void StartAsleep(const char* what) {
        Start(kShRigSleep, kSrSleeping, kAtMask, 8u, what);
        ShRigExpect(mode_, what, "sleeping before the reset", rig_.State().sleeping, cerfengine::kShSleepMode);
    }

    void RequestReset(ShResetKind kind) {
        rig_.Cpu().SetPendingResetKind(kind);
        rig_.Guest().SetResetPending(false);
    }

    void ExpectRequested(const char* what, uint32_t requests, uint32_t delivered) {
        ShRigExpect(mode_, what, "reset requests", rig_.Host().ResetRequests(), requests);
        ShRigExpect(mode_, what, "reboot requests", rig_.Host().RebootRequests(), requests);
        ShRigExpect(mode_, what, "resets delivered before the dispatch", rig_.Host().ResetsDelivered(), delivered);
        ShRigExpect(mode_, what, "reset pending", rig_.Guest().ResetPending() ? 1u : 0u, 1u);
    }

    void DeliverAndExpect(const char* what, uint32_t expevt) {
        const uint32_t delivered = rig_.Host().ResetsDelivered();
        rig_.Step(1u, what);
        const ShCpuState& s = rig_.State();
        ShRigExpect(mode_, what, "PC", s.pc, kResetPc);
        ShRigExpect(mode_, what, "SR", s.sr, kResetSr);
        ShRigExpect(mode_, what, "VBR", s.vbr, 0u);
        ShRigExpect(mode_, what, "EXPEVT", s.expevt, expevt);
        ShRigExpect(mode_, what, "sleeping", s.sleeping, cerfengine::kShSleepNone);
        ShRigExpect(mode_, what, "sleep and reset stops", s.chain_exit_request & kResetStops, 0u);
        ShRigExpect(mode_, what, "MMUCR", rig_.Mmu().ReadMmucr(), 0u);
        ShRigExpect(mode_, what, "resets delivered", rig_.Host().ResetsDelivered(), delivered + 1u);
        ShRigExpect(mode_, what, "reset pending after the dispatch", rig_.Guest().ResetPending() ? 1u : 0u, 0u);
        ShRigExpect(mode_, what, "accepted interrupts", hosts_.accepted, 0u);
    }

    void Reset(ShResetKind kind, const char* what, uint32_t expevt) {
        const uint32_t requests  = rig_.Host().ResetRequests();
        const uint32_t delivered = rig_.Host().ResetsDelivered();
        RequestReset(kind);
        ExpectRequested(what, requests + 1u, delivered);
        DeliverAndExpect(what, expevt);
    }

    void LoadMapping(const char* what) {
        StartAwake(0u, what);
        rig_.ResetMmu(0u, kShRigAsid);
        rig_.Preload(0u, kMappedVa, kShRigAsid, kShRigDataPa | kPage4kRw);
        ShRigExpect(mode_, what, "mapped before the reset", Mapped() ? 1u : 0u, 1u);
    }

    bool Mapped() {
        cerfengine::ShMmu& mmu = rig_.Mmu();
        mmu.WritePteh(kShRigAsid);
        ShTranslation t{};
        return mmu.Probe(kMappedVa, ShAccess::kRead, &t) && t.pa == kShRigDataPa;
    }

    void ManualResetKeepsTheTlb() {
        const char* what = "a manual reset";
        LoadMapping(what);
        Reset(ShResetKind::kManual, what, kManualResetExpevt);
        ShRigExpect(mode_, what, "mapped after the reset", Mapped() ? 1u : 0u, 1u);
    }

    void PowerOnResetClearsTheTlb() {
        const char* what = "a power-on reset";
        LoadMapping(what);
        Reset(ShResetKind::kPowerOn, what, kPowerOnResetExpevt);
        ShRigExpect(mode_, what, "mapped after the reset", Mapped() ? 1u : 0u, 0u);
    }

    void ResetKindFallsBackToPowerOn() {
        const char* what = "a reset request that names no kind after a manual reset";
        StartAwake(0u, what);
        Reset(ShResetKind::kManual, what, kManualResetExpevt);
        const uint32_t requests = rig_.Host().ResetRequests();
        rig_.Guest().SetResetPending(false);
        ExpectRequested(what, requests + 1u, rig_.Host().ResetsDelivered());
        DeliverAndExpect(what, kPowerOnResetExpevt);
    }

    void PendingInterruptWaitsForTheReset() {
        const char* what = "a reset with an interrupt above the mask pending";
        StartAwake(kAboveMask, what);
        Reset(ShResetKind::kManual, what, kManualResetExpevt);
    }

    void ResetEndsSleep() {
        const char* what = "a reset requested while the CPU sleeps";
        StartAsleep(what);
        Reset(ShResetKind::kManual, what, kManualResetExpevt);
    }

    void ResetPostedInsideTheSleepWait() {
        const char* what = "a reset a host request listener posts inside the SLEEP wait";
        StartAsleep(what);
        const uint32_t requests  = rig_.Host().ResetRequests();
        const uint32_t delivered = rig_.Host().ResetsDelivered();
        reset_on_host_request_   = true;
        rig_.HostRequests().Request();
        rig_.StepWithoutHostExit(what);
        ShRigExpect(mode_, what, "sleeping after the wait", rig_.State().sleeping, cerfengine::kShSleepMode);
        ExpectRequested(what, requests + 1u, delivered);
        DeliverAndExpect(what, kManualResetExpevt);
    }

    ShIrRig        rig_;
    ShRigFakeHosts hosts_;
    const char*    mode_;
    bool           reset_on_host_request_ = false;
};

}

int main() {
    ResetCases(false, "plain").Run();
    ResetCases(true, "verified IR").Run();
    return 0;
}
