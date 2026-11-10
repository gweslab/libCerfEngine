#include "sh_ir_rig.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

using cerfengine::ShCpuState;

constexpr uint32_t kSrUser     = cerfengine::kShSrRb | cerfengine::kShSrT;
constexpr uint32_t kSrFromUser = kSrUser | cerfengine::kShSrMd | cerfengine::kShSrBl;

constexpr uint32_t kUserCodePa   = 0x00012000u;
constexpr uint32_t kUserCodeStep = 0x400u;
constexpr uint32_t kDataP1       = 0x80000000u | kShRigDataPa;
constexpr uint32_t kDataWord     = 0x13579BDFu;
constexpr uint32_t kFetchP1      = 0x80010000u;
constexpr uint32_t kBank0Fill    = 0xB0000000u;
constexpr uint32_t kBank1Fill    = 0xB1000000u;
constexpr uint32_t kGbr          = 0x6B6B0000u;

constexpr uint16_t kLoadLR1R8 = 0x6812u;
constexpr uint16_t kMovImm5R3 = 0xE305u;
constexpr uint16_t kJmpR1     = 0x412Bu;
constexpr uint16_t kStcGbrR0  = 0x0012u;
constexpr uint16_t kStcSrR0   = 0x0002u;

ShCpuState Initial() {
    ShCpuState s{};
    s.sr  = kShRigSrPrivileged;
    s.gbr = kGbr;
    for (uint32_t r = 0; r < 8u; ++r) s.gpr[r] = kBank1Fill + r;
    return s;
}

class UserModeCases {
public:
    UserModeCases(bool verify, const char* mode) : rig_(false, verify), mode_(mode) {
        rig_.LoadReturningHandler(kShRigGeneralVector);
        rig_.Host().LoadWords(kShRigDataPa, &kDataWord, 1u);
    }

    void Run() {
        BankZeroAndDataAddressError();
        FetchAddressError();
        GbrTransfersInUserMode();
        PrivilegedInstruction();
        PrivilegedInstructionInSlot();
        SleepInUserMode();
        RteSlotUsesRestoredUserMode();
        RteSlotPrivilegedInstructionUnderRestoredUserMode();
        RteSlotUsesRestoredPrivilegedMode();
        LoadBeforeRteRunsPrivileged();
        BlocksKeyedByProcessorMode();
        std::printf("%s: SH user mode passed\n", mode_);
    }

private:
    uint32_t LoadUser(std::vector<uint16_t> code) {
        const uint32_t va = next_user_pa_;
        next_user_pa_ += kUserCodeStep;
        rig_.LoadCode(va, std::move(code));
        return va;
    }

    std::function<void(ShCpuState&)> ToUser(uint32_t user_va, uint32_t bank0_r1) {
        return [this, user_va, bank0_r1](ShCpuState& s) {
            s.vbr     = kShRigVbr;
            s.ssr     = kSrUser;
            s.spc     = user_va;
            s.gpr[13] = rig_.ReturnStubVa();
            for (uint32_t r = 0; r < 8u; ++r) s.bank[r] = kBank0Fill + r;
            s.bank[1] = bank0_r1;
        };
    }

    void ExpectUserException(const char* what, uint32_t expevt, uint32_t spc) {
        const ShCpuState& s = rig_.State();
        ShRigExpect(mode_, what, "general handler entries", s.gpr[9], 1u);
        ShRigExpect(mode_, what, "EXPEVT", s.expevt, expevt);
        ShRigExpect(mode_, what, "SPC", s.spc, spc);
        ShRigExpect(mode_, what, "SSR", s.ssr, kSrUser);
        ShRigExpect(mode_, what, "SR", s.sr, kSrFromUser);
    }

    void ExpectUserAddressError(const char* what, uint32_t tea, uint32_t spc) {
        ExpectUserException(what, kShRigExpevtAddressRead, spc);
        ShRigExpect(mode_, what, "TEA", rig_.Mmu().ReadTea(), tea);
    }

    void BankZeroAndDataAddressError() {
        const char*                 what    = "user load from P1 with bank 0 active";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigNop};
        const uint32_t              user_va = LoadUser({kMovImm5R3, kShRigLoadLR1R2});
        rig_.Run(body, Initial(), what, ToUser(user_va, kDataP1));
        ExpectUserAddressError(what, kDataP1, user_va + 2u);
        const ShCpuState& s = rig_.State();
        for (uint32_t r = 0; r < 8u; ++r) ShRigExpect(mode_, what, "bank 1 register", s.gpr[r], kBank1Fill + r);
        ShRigExpect(mode_, what, "R0_BANK0", s.bank[0], kBank0Fill);
        ShRigExpect(mode_, what, "R1_BANK0", s.bank[1], kDataP1);
        ShRigExpect(mode_, what, "R2_BANK0", s.bank[2], kBank0Fill + 2u);
        ShRigExpect(mode_, what, "R3_BANK0", s.bank[3], 5u);
    }

    void FetchAddressError() {
        const char*                 what    = "user jump to P1";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigNop};
        const uint32_t              user_va = LoadUser({kJmpR1, kShRigNop});
        rig_.Run(body, Initial(), what, ToUser(user_va, kFetchP1));
        ExpectUserAddressError(what, kFetchP1, kFetchP1);
    }

    void GbrTransfersInUserMode() {
        const char*                 what    = "STC GBR in user mode";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigNop};
        const uint32_t              user_va = LoadUser({kStcGbrR0, kShRigLoadLR1R2});
        rig_.Run(body, Initial(), what, ToUser(user_va, kDataP1));
        ExpectUserAddressError(what, kDataP1, user_va + 2u);
        ShRigExpect(mode_, what, "R0_BANK0", rig_.State().bank[0], kGbr);
    }

    void PrivilegedInstruction() {
        const char*                 what    = "STC SR in user mode";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigNop};
        const uint32_t              user_va = LoadUser({kStcSrR0});
        rig_.Run(body, Initial(), what, ToUser(user_va, kDataP1));
        ExpectUserException(what, kShRigExpevtReservedInstruction, user_va);
        ShRigExpect(mode_, what, "R0_BANK0", rig_.State().bank[0], kBank0Fill);
    }

    void PrivilegedInstructionInSlot() {
        const char*                 what    = "STC SR in a user delay slot";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigNop};
        const uint32_t              user_va = LoadUser({kShRigBraNextNop, kStcSrR0});
        rig_.Run(body, Initial(), what, ToUser(user_va, kDataP1));
        ExpectUserException(what, kShRigExpevtIllegalSlot, user_va);
        ShRigExpect(mode_, what, "R0_BANK0", rig_.State().bank[0], kBank0Fill);
    }

    void SleepInUserMode() {
        const char*                 what    = "SLEEP in user mode";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigNop};
        const uint32_t              user_va = LoadUser({kShRigSleep});
        rig_.Run(body, Initial(), what, ToUser(user_va, kDataP1));
        ExpectUserException(what, kShRigExpevtReservedInstruction, user_va);
        ShRigExpect(mode_, what, "sleeping", rig_.State().sleeping, 0u);
    }

    void RteSlotUsesRestoredUserMode() {
        const char*                 what    = "RTE slot load from P1 under a restored user SR";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigLoadLR1R2};
        const uint32_t              user_va = LoadUser({kShRigNop});
        const std::string detail = rig_.RunToHalt(body, Initial(), what, ToUser(user_va, kDataP1));
        ShRigExpectHalt(mode_, what, detail, "exception 0x0E0 in the delay slot of RTE");
    }

    void RteSlotPrivilegedInstructionUnderRestoredUserMode() {
        const char*                 what    = "RTE slot STC SR under a restored user SR";
        const std::vector<uint16_t> body    = {kShRigRte, kStcSrR0};
        const uint32_t              user_va = LoadUser({kShRigNop});
        const std::string detail = rig_.RunToHalt(body, Initial(), what, ToUser(user_va, kDataP1));
        ShRigExpectHalt(mode_, what, detail, "exception 0x1A0 in the delay slot of RTE");
    }

    void RteSlotUsesRestoredPrivilegedMode() {
        const char*                 what    = "RTE slot load from P1 under a restored privileged SR";
        const std::vector<uint16_t> body    = {kShRigRte, kShRigLoadLR1R2};
        ShCpuState                  initial = Initial();
        initial.gpr[1]                      = kDataP1;
        rig_.Run(body, initial, what, [&](ShCpuState& s) {
            s.ssr = kShRigSrPrivileged;
            s.spc = rig_.ReturnStubVa();
        });
        ShRigExpect(mode_, what, "R2", rig_.State().gpr[2], kDataWord);
    }

    void LoadBeforeRteRunsPrivileged() {
        const char*                 what    = "P1 load before RTE in the same block";
        const std::vector<uint16_t> body    = {kLoadLR1R8, kShRigRte, kShRigNop};
        const uint32_t              user_va = LoadUser({kShRigLoadLR1R2});
        ShCpuState                  initial = Initial();
        initial.gpr[1]                      = kDataP1;
        rig_.Run(body, initial, what, ToUser(user_va, kDataP1));
        ShRigExpect(mode_, what, "R8", rig_.State().gpr[8], kDataWord);
        ExpectUserAddressError(what, kDataP1, user_va);
    }

    void BlocksKeyedByProcessorMode() {
        const char*                 what      = "the same code in privileged then user mode";
        const uint32_t              shared_va = LoadUser({kShRigLoadLR1R2, kShRigJmpR13, kShRigNop});
        const std::vector<uint16_t> entry     = {kShRigNop};
        ShCpuState                  initial   = Initial();
        initial.gpr[1]                        = kDataP1;
        rig_.Run(entry, initial, what, [&](ShCpuState& s) {
            s.gpr[13] = rig_.ReturnStubVa();
            s.pc      = shared_va;
        });
        ShRigExpect(mode_, what, "privileged R2", rig_.State().gpr[2], kDataWord);
        const std::vector<uint16_t> body = {kShRigRte, kShRigNop};
        rig_.Run(body, Initial(), what, ToUser(shared_va, kDataP1));
        ExpectUserAddressError(what, kDataP1, shared_va);
    }

    ShIrRig     rig_;
    const char* mode_;
    uint32_t    next_user_pa_ = kUserCodePa;
};

}

int main() {
    UserModeCases(false, "plain").Run();
    UserModeCases(true, "verified IR").Run();
    return 0;
}
