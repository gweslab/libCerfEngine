#include "arm_ir_rig.h"

#include <cerfengine/arm/arm_jit.h>
#include <cerfengine/arm/arm_mmu_state.h>

#include "arm/arm_translation_cache.h"

#include <array>

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocAction;
using cerfengine::ArmCoprocCase;
using cerfengine::ArmCoprocDecision;
using cerfengine::ArmCoprocKind;
using RunStatus = cerfengine::GuestEngine::RunStatus;

constexpr uint32_t kRegisters        = 8;
constexpr uint32_t kNoRegister       = 0xFFFFFFFFu;
constexpr uint32_t kSwitchValues[]   = {0u, 1u, 2u, 3u, 0xFFu};
constexpr uint32_t kSafeSctlrImm12[] = {0x000u, 0x002u, 0x501u};

struct Entry {
    ArmCoprocKind     kind;
    uint32_t          cp, opc1, crn, crm, opc2, crd;
    bool              read;
    uint32_t          reg_index;
    ArmCoprocDecision decision;
};

struct LogEntry {
    uint32_t what;
    uint32_t key;
    uint32_t value;
    uint32_t cycles;
    bool     operator==(const LogEntry&) const = default;
};

class Coprocessor : public TestCoprocessor {
public:
    explicit Coprocessor(const std::vector<Entry>& entries) : entries_(entries) {}

    void ObserveCycles(const uint32_t* counter) { cycles_ = counter; }

    ArmCoprocDecision Decide(const ArmCoprocAccess& a) override {
        for (const Entry& e : entries_) {
            if (!Matches(e, a)) continue;
            ArmCoprocDecision d = e.decision;
            if (e.reg_index != kNoRegister) d.reg = &regs[e.reg_index];
            if (e.decision.action == ArmCoprocAction::kRegister40) d.reg = &acc40[0];
            d.context = this;
            return d;
        }
        return ArmCoprocDecision{};
    }

    void Reset(uint32_t seed) {
        for (uint32_t i = 0; i < kRegisters; ++i) regs[i] = seed * 0x9E3779B1u + i * 0x85EBCA6Bu;
        acc40[0] = seed ^ 0xA5A5A5A5u;
        acc40[1] = seed >> 3;
        value_   = seed;
        log.clear();
    }

    bool SameAs(const Coprocessor& o) const {
        return regs == o.regs && acc40[0] == o.acc40[0] && acc40[1] == o.acc40[1] && log == o.log;
    }

    static uint32_t CERFENGINE_FASTCALL Read(void* self) {
        return static_cast<Coprocessor*>(self)->Note(1u, 0u, 0u);
    }
    static uint32_t CERFENGINE_FASTCALL ReadKeyed(void* self, uint32_t key) {
        return static_cast<Coprocessor*>(self)->Note(2u, key, 0u) ^ key;
    }
    static void CERFENGINE_FASTCALL Write(void* self, uint32_t value) {
        static_cast<Coprocessor*>(self)->Note(3u, 0u, value);
    }
    static void CERFENGINE_FASTCALL WriteKeyed(void* self, uint32_t key, uint32_t value) {
        static_cast<Coprocessor*>(self)->Note(4u, key, value);
    }
    static void CERFENGINE_FASTCALL Hook(void* self) {
        static_cast<Coprocessor*>(self)->Note(5u, 0u, 0u);
    }

    std::array<uint32_t, kRegisters> regs{};
    uint32_t                         acc40[2] = {};
    std::vector<LogEntry>            log;

private:
    static bool Matches(const Entry& e, const ArmCoprocAccess& a) {
        if (e.kind != a.kind || e.cp != a.cp_num) return false;
        switch (a.kind) {
        case ArmCoprocKind::kRegisterTransfer:
            return e.opc1 == a.opc1 && e.crn == a.crn && e.crm == a.crm && e.opc2 == a.opc2 &&
                   e.read == a.read;
        case ArmCoprocKind::kRegisterTransferDouble:
            return e.opc1 == a.opc1 && e.crm == a.crm && e.read == a.read;
        case ArmCoprocKind::kDataTransfer:
            return e.crd == a.crd;
        case ArmCoprocKind::kDataOperation:
            return e.opc1 == a.opc1 && e.crn == a.crn && e.crm == a.crm && e.opc2 == a.opc2 &&
                   e.crd == a.crd;
        }
        return false;
    }

    uint32_t Note(uint32_t what, uint32_t key, uint32_t value) {
        log.push_back(LogEntry{what, key, value, *cycles_});
        value_ = value_ * 1103515245u + 12345u;
        return value_;
    }

    const std::vector<Entry>& entries_;
    const uint32_t*           cycles_ = nullptr;
    uint32_t                  value_  = 0;
};

uint32_t GateMask(std::mt19937& rng) {
    const uint32_t pick = RigPick(rng, 8u);
    if (pick < 5u) return 0u;
    return pick < 7u ? 1u << RigPick(rng, 14u) : static_cast<uint32_t>(rng()) | 1u;
}

void FillDecision(Entry& e, std::mt19937& rng, uint32_t index) {
    ArmCoprocDecision& d = e.decision;
    d.user_allowed       = RigPick(rng, 2u) == 0u;
    d.gate_mask          = GateMask(rng);
    switch (d.action) {
    case ArmCoprocAction::kConstant:
        d.constant = static_cast<uint32_t>(rng());
        break;
    case ArmCoprocAction::kRegister:
    case ArmCoprocAction::kRegisterContextSwitch:
    case ArmCoprocAction::kRegisterFlushTlbs:
        e.reg_index = index % kRegisters;
        break;
    case ArmCoprocAction::kReadCallback:      d.read = &Coprocessor::Read; break;
    case ArmCoprocAction::kReadCallbackKeyed:
        d.read_keyed = &Coprocessor::ReadKeyed;
        d.key        = static_cast<uint32_t>(rng());
        break;
    case ArmCoprocAction::kWriteCallback:     d.write = &Coprocessor::Write; break;
    case ArmCoprocAction::kWriteCallbackKeyed:
        d.write_keyed = &Coprocessor::WriteKeyed;
        d.key         = static_cast<uint32_t>(rng());
        break;
    case ArmCoprocAction::kWriteValueSwitch:
        d.hook       = &Coprocessor::Hook;
        d.case_count = 2u + RigPick(rng, cerfengine::kArmCoprocMaxCases - 1u);
        for (uint32_t i = 0; i < d.case_count; ++i) {
            d.cases[i].value = kSwitchValues[(index + i) % 5u];
            d.cases[i].kind  = i == 0u ? ArmCoprocCase::kHook
                             : i == 1u ? ArmCoprocCase::kWaitForInterrupt
                                       : static_cast<ArmCoprocCase>(RigPick(rng, 3u));
        }
        break;
    case ArmCoprocAction::kInvalidateTlbAll:
    case ArmCoprocAction::kInvalidateTlbByMva:
        d.tlb_target = static_cast<cerfengine::ArmTlbTarget>(RigPick(rng, 3u));
        break;
    default:
        break;
    }
}

std::vector<Entry> BuildEntries(std::mt19937& rng) {
    static constexpr ArmCoprocAction kReads[] = {
        ArmCoprocAction::kUndefined, ArmCoprocAction::kUnimplemented, ArmCoprocAction::kNoOp,
        ArmCoprocAction::kConstant, ArmCoprocAction::kRegister,
        ArmCoprocAction::kRegisterContextSwitch, ArmCoprocAction::kRegisterFlushTlbs,
        ArmCoprocAction::kReadCallback, ArmCoprocAction::kReadCallbackKeyed,
        ArmCoprocAction::kSystemControl, ArmCoprocAction::kCoprocessorAccess,
        ArmCoprocAction::kContextId, ArmCoprocAction::kFcsePid};
    static constexpr ArmCoprocAction kWrites[] = {
        ArmCoprocAction::kUndefined, ArmCoprocAction::kUnimplemented, ArmCoprocAction::kNoOp,
        ArmCoprocAction::kRegister, ArmCoprocAction::kRegisterContextSwitch,
        ArmCoprocAction::kRegisterFlushTlbs, ArmCoprocAction::kWriteCallback,
        ArmCoprocAction::kWriteCallbackKeyed, ArmCoprocAction::kWriteValueSwitch,
        ArmCoprocAction::kWaitForInterrupt, ArmCoprocAction::kSystemControl,
        ArmCoprocAction::kCoprocessorAccess, ArmCoprocAction::kContextId,
        ArmCoprocAction::kFcsePid, ArmCoprocAction::kInvalidateTlbAll,
        ArmCoprocAction::kInvalidateTlbByMva, ArmCoprocAction::kInvalidateCode};
    static constexpr uint32_t kCps[] = {15u, 15u, 14u, 0u, 1u, 6u, 8u, 13u};

    std::vector<Entry> entries;
    uint32_t           index = 0;
    auto add = [&](ArmCoprocKind kind, ArmCoprocAction action, bool read, uint32_t cp) {
        Entry e{kind, cp, 0u, index & 0xFu, (index >> 4) & 0xFu, RigPick(rng, 8u),
                RigPick(rng, 16u), read, kNoRegister, ArmCoprocDecision{}};
        if (kind == ArmCoprocKind::kRegisterTransferDouble || kind == ArmCoprocKind::kDataOperation) {
            e.opc1 = RigPick(rng, 16u);
        }
        e.decision.action = action;
        FillDecision(e, rng, index);
        entries.push_back(e);
        ++index;
    };
    for (uint32_t round = 0; round < 3u; ++round) {
        for (ArmCoprocAction a : kReads) add(ArmCoprocKind::kRegisterTransfer, a, true, kCps[RigPick(rng, 8u)]);
        for (ArmCoprocAction a : kWrites) add(ArmCoprocKind::kRegisterTransfer, a, false, kCps[RigPick(rng, 8u)]);
        for (uint32_t extra = 0; extra < 2u; ++extra) {
            add(ArmCoprocKind::kRegisterTransfer, ArmCoprocAction::kWriteValueSwitch, false,
                kCps[RigPick(rng, 8u)]);
        }
        for (bool read : {true, false}) {
            add(ArmCoprocKind::kRegisterTransferDouble, ArmCoprocAction::kRegister40, read, kCps[3 + RigPick(rng, 5u)]);
            add(ArmCoprocKind::kRegisterTransferDouble, ArmCoprocAction::kUndefined, read, kCps[3 + RigPick(rng, 5u)]);
        }
        for (ArmCoprocAction a : {ArmCoprocAction::kUndefined, ArmCoprocAction::kUnimplemented}) {
            add(ArmCoprocKind::kDataTransfer, a, false, kCps[3 + RigPick(rng, 5u)]);
            add(ArmCoprocKind::kDataOperation, a, false, kCps[3 + RigPick(rng, 5u)]);
        }
    }
    return entries;
}

uint32_t Cond(std::mt19937& rng) {
    return (RigPick(rng, 4u) == 0u ? RigPick(rng, 14u) : 14u) << 28;
}

void PushRegisterTransfer(std::vector<uint32_t>& p, std::mt19937& rng, const Entry& e) {
    uint32_t rt = RigPick(rng, 15u);
    if (e.read && e.cp != 14u && e.cp != 15u && RigPick(rng, 6u) == 0u) rt = 15u;
    if (!e.read && e.decision.action == ArmCoprocAction::kSystemControl) {
        p.push_back(0xE3A00000u | (rt << 12) | kSafeSctlrImm12[RigPick(rng, 3u)]);
    } else if (!e.read && e.decision.action == ArmCoprocAction::kWriteValueSwitch &&
               RigPick(rng, 3u) != 0u) {
        const ArmCoprocDecision& d = e.decision;
        p.push_back(0xE3A00000u | (rt << 12) | d.cases[RigPick(rng, d.case_count)].value);
    } else if (!e.read && RigPick(rng, 4u) == 0u) {
        p.push_back(0xE3A00000u | (rt << 12) | RigPick(rng, 4096u));
    }
    p.push_back(Cond(rng) | 0x0E000010u | (e.opc1 << 21) | ((e.read ? 1u : 0u) << 20) |
                (e.crn << 16) | (rt << 12) | (e.cp << 8) | (e.opc2 << 5) | e.crm);
}

void PushDouble(std::vector<uint32_t>& p, std::mt19937& rng, const Entry& e) {
    const uint32_t rt  = RigPick(rng, 15u);
    uint32_t       rt2 = RigPick(rng, 15u);
    if (e.read && rt2 == rt) rt2 = (rt + 1u) % 15u;
    for (uint32_t reg : {rt, rt2}) {
        if (!e.read && RigPick(rng, 3u) == 0u) {
            p.push_back(0xE3A00000u | (reg << 12) | RigPick(rng, 4096u));
        }
    }
    p.push_back(Cond(rng) | 0x0C400000u | ((e.read ? 1u : 0u) << 20) | (rt2 << 16) | (rt << 12) |
                (e.cp << 8) | (e.opc1 << 4) | e.crm);
}

void PushDataTransfer(std::vector<uint32_t>& p, std::mt19937& rng, const Entry& e) {
    const uint32_t cond = Cond(rng);
    const uint32_t u    = RigPick(rng, 2u);
    const uint32_t d    = RigPick(rng, 2u);
    const uint32_t w    = RigPick(rng, 2u);
    const uint32_t l    = RigPick(rng, 2u);
    const uint32_t rn   = RigPick(rng, 16u);
    const uint32_t imm8 = RigPick(rng, 256u);
    p.push_back(cond | 0x0D000000u | (u << 23) | (d << 22) | (w << 21) | (l << 20) | (rn << 16) |
                (e.crd << 12) | (e.cp << 8) | imm8);
}

void PushDataOperation(std::vector<uint32_t>& p, std::mt19937& rng, const Entry& e) {
    p.push_back(Cond(rng) | 0x0E000000u | (e.opc1 << 20) | (e.crn << 16) | (e.crd << 12) |
                (e.cp << 8) | (e.opc2 << 5) | e.crm);
}

void PushInstruction(std::vector<uint32_t>& p, std::mt19937& rng, const std::vector<Entry>& es) {
    const uint32_t pick = RigPick(rng, 16u);
    if (pick == 0u) {
        const uint32_t rn = RigPick(rng, 15u);
        const uint32_t rm = RigPick(rng, 15u);
        p.push_back(0xE1500000u | (rn << 16) | rm);
        return;
    }
    if (pick == 1u) {
        const uint32_t vfp_cp = 10u + RigPick(rng, 2u);
        const uint32_t cond   = Cond(rng);
        const uint32_t l      = RigPick(rng, 2u);
        const uint32_t rt     = RigPick(rng, 15u);
        p.push_back(cond | 0x0E000010u | (l << 20) | (rt << 12) | (vfp_cp << 8));
        return;
    }
    const Entry& e = es[RigPick(rng, static_cast<uint32_t>(es.size()))];
    switch (e.kind) {
    case ArmCoprocKind::kRegisterTransfer:       PushRegisterTransfer(p, rng, e); return;
    case ArmCoprocKind::kRegisterTransferDouble: PushDouble(p, rng, e); return;
    case ArmCoprocKind::kDataTransfer:           PushDataTransfer(p, rng, e); return;
    case ArmCoprocKind::kDataOperation:          PushDataOperation(p, rng, e); return;
    }
}

struct Lane {
    std::unique_ptr<Coprocessor> coprocessor;
    std::unique_ptr<ArmIrRig>    rig;
};

void Prepare(Lane& lane) {
    lane.rig->System().SetCoprocessor(lane.coprocessor.get());
    lane.coprocessor->ObserveCycles(&lane.rig->State().guest_cycle_counter);
    lane.rig->Engine().Get<cerfengine::ArmJit>().SetIdleWake(true);
}

bool SameMmu(const cerfengine::ArmMmuState& a, const cerfengine::ArmMmuState& b) {
    return a.control_register.word == b.control_register.word &&
           a.coprocessor_access == b.coprocessor_access && a.contextidr == b.contextidr &&
           a.process_id == b.process_id && a.fcse_fold_id == b.fcse_fold_id;
}

bool EntryBlockCached(ArmIrRig& rig, uint32_t start_mode_cpsr) {
    const uint32_t privilege = (start_mode_cpsr & cerfengine::kArmCpsrModeMask) ==
                                       cerfengine::ArmMode::kUser
                                   ? cerfengine::kBlockUser
                                   : cerfengine::kBlockPrivileged;
    return rig.Engine().Get<cerfengine::ArmTranslationCache>().Space(false).JumpCacheLookup(
               privilege, kRigRamBase) != nullptr;
}

void TestCoprocessors(const char* name, const cerfengine::ArmFeatures& features, uint32_t seed) {
    static constexpr uint32_t kModes[] = {0u, 0x1D0u, 0x1DFu, 0x1D2u};
    std::mt19937             rng(seed);
    const std::vector<Entry> entries = BuildEntries(rng);
    std::array<Lane, 2> lanes;
    for (uint32_t r = 0; r < 2u; ++r) {
        lanes[r].coprocessor = std::make_unique<Coprocessor>(entries);
        lanes[r].rig = std::make_unique<ArmIrRig>(r == 0u, features);
        Prepare(lanes[r]);
    }

    constexpr uint32_t kPrograms = 6000u;
    uint32_t halts = 0, undefined = 0, exits = 0, flushed = 0;
    for (uint32_t n = 0; n < kPrograms; ++n) {
        std::vector<uint32_t> program;
        const uint32_t length = 1u + RigPick(rng, 5u);
        for (uint32_t i = 0; i < length; ++i) PushInstruction(program, rng, entries);
        program.push_back(kRigBranchSelf);
        cerfengine::ArmCpuState initial{};
        for (uint32_t i = 0; i < 15u; ++i) initial.gprs[i] = static_cast<uint32_t>(rng());
        initial.nf = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.zf = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.cf = static_cast<uint8_t>(RigPick(rng, 2u));
        initial.vf = static_cast<uint8_t>(RigPick(rng, 2u));
        const uint32_t cpacr = static_cast<uint32_t>(rng());
        const uint32_t mode  = kModes[RigPick(rng, 4u)];

        RunStatus status[2];
        for (uint32_t r = 0; r < 2u; ++r) {
            lanes[r].coprocessor->Reset(n);
            lanes[r].rig->Cpu().SetResetCoprocessorAccess(cpacr);
            status[r] = lanes[r].rig->Run(program, initial, 0u, mode, 0u);
        }
        ArmIrRig&  a    = *lanes[0].rig;
        ArmIrRig&  b    = *lanes[1].rig;
        const bool same = RigSameRun(a, status[0], b, status[1]) &&
                          lanes[0].coprocessor->SameAs(*lanes[1].coprocessor) &&
                          EntryBlockCached(a, mode) == EntryBlockCached(b, mode) &&
                          SameMmu(*a.Engine().Get<cerfengine::ArmMmu>().State(),
                                  *b.Engine().Get<cerfengine::ArmMmu>().State());
        if (!same) {
            std::printf("%s program %u (mode %X, cpacr %08X):", name, n, mode, cpacr);
            for (uint32_t word : program) std::printf(" %08X", word);
            std::printf("\nstatus verified=%d unverified=%d\n", static_cast<int>(status[0]),
                        static_cast<int>(status[1]));
            if (status[0] == RunStatus::kHalted) {
                std::printf("halt: %s\n", a.Engine().LastHalt().detail);
            }
            RigDump("verified  ", a.State());
            RigDump("unverified", b.State());
            TestFail("CerfIR differs between verify modes");
        }
        if (status[0] == RunStatus::kHalted) {
            ++halts;
            for (Lane& lane : lanes) {
                lane.rig->Restart();
                Prepare(lane);
            }
            continue;
        }
        if (!EntryBlockCached(*lanes[0].rig, mode)) ++flushed;
        const cerfengine::ArmCpuState& s = lanes[0].rig->State();
        if (s.cpsr.bits.mode == cerfengine::ArmMode::kUndefined) {
            ++undefined;
        } else if (s.gprs[15] != (program.size() - 1u) * 4u) {
            ++exits;
        }
    }
    std::printf("coprocessor %s: %u programs agree in both verify modes (exits %u, undefined %u, "
                "halts %u, entry block flushed %u)\n", name, kPrograms, exits, undefined, halts,
                flushed);
    RigPrintCompileStats("  cerfir verified  ", lanes[0].rig->Host());
    RigPrintCompileStats("  cerfir unverified", lanes[1].rig->Host());
}

}

int main() {
    cerfengine::ArmFeatures v4{};
    TestCoprocessors("v4", v4, 0x43503431u);
    cerfengine::ArmFeatures v5{};
    v5.thumb                     = true;
    v5.dsp                       = true;
    v5.armv5_unconditional_space = true;
    TestCoprocessors("v5", v5, 0x43503531u);
    cerfengine::ArmFeatures v6 = v5;
    v6.arch_v6                   = true;
    TestCoprocessors("v6", v6, 0x43503631u);
    cerfengine::ArmFeatures v7 = v6;
    v7.arch_v7                   = true;
    TestCoprocessors("v7", v7, 0x43503731u);
    std::printf("arm_ir_coproc_diff_test passed\n");
    return 0;
}
