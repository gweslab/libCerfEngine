#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../cerfir_block.h"
#include "../cerfir_guest_model.h"
#include "x86_memory_tails.h"
#include "x86_reg_alloc.h"

namespace cerfengine::cerfir {

class X86SoftTlb;

struct X86GuestTarget {
    const GuestModel* model = nullptr;
    const BlockLink*  link  = nullptr;
    const X86SoftTlb* tlb   = nullptr;
};

struct X86WideHalf {
    Ref      ref;
    bool     imm;
    uint32_t value;
};

enum class X86FlagKind : uint8_t { kNone, kEflags, kReg };

struct X86FlagSource {
    Ref         producer = kNoRef;
    X86FlagKind kind[4]  = {};
    uint8_t     where[4] = {};
};

constexpr uint32_t kFlagIndexN = 0;
constexpr uint32_t kFlagIndexZ = 1;
constexpr uint32_t kFlagIndexC = 2;
constexpr uint32_t kFlagIndexV = 3;

struct X86ArenaFull {};

struct X86ProbeStamp {
    const ExitModel* model      = nullptr;
    const void*      jump_cache = nullptr;
    int32_t          key_disp   = 0;
    int32_t          pc_slot    = 0;
    bool             mode_set   = false;
    uint32_t         length     = 0;
    uint8_t          bytes[192] = {};
};

class X86Lowering {
public:
    static constexpr ptrdiff_t kOpReserveBytes = 4096;

    X86Lowering() : regs_(cursor_, spill_area_) {}
    X86Lowering(const X86Lowering&) = delete;
    X86Lowering& operator=(const X86Lowering&) = delete;

    void     Begin(Block& block, uint8_t* code, uint8_t* code_end, const X86GuestTarget& target);
    void     Lower(Ref ref);
    uint8_t* EmitCycleAdd(uint32_t cycles);
    uint8_t* EmitCycleAddSlow(uint32_t cycles);
    void     Drop(Ref r);
    void     DropHalf(Ref r, bool hi);
    size_t   Finish();

    CERFENGINE_FORCEINLINE void Undefine(Ref r) { regs_.Undefine(r); }

    void EmitSetSlot(Ref value, uint32_t disp, bool sext);
    void EmitSetSlotConst(uint32_t disp, uint32_t value, bool sext);
    void EmitLabel(uint32_t label);
    void EmitBranch(Ref cond, uint32_t label, bool if_true);
    void EmitBranchSlots(const SlotTest& test, uint32_t label);
    uint8_t* EmitGuardBranch(const SlotTest& test);
    void     EmitSetSlotTest(const SlotTest& test, uint32_t dest);
    void     EmitGuardBind(uint8_t* site);
    void EmitExitDirect(uint32_t target, uint16_t flags);
    void EmitExitIndirect();
    void EmitExitDispatcher();
    void EmitFusedAlu(Op op, Ref a, Ref b, uint32_t mask, uint32_t dest, bool sext);
    bool EmitSlotImm(Op op, uint32_t a_disp, uint32_t imm, uint32_t dest, bool sext);
    void EmitSlotSlot(Op op, uint32_t a_disp, uint32_t b_disp, uint32_t dest, bool sext);
    void EmitSlotImmFlags(Op op, uint32_t a_disp, uint32_t imm, uint32_t mask, uint32_t dest);
    void EmitSlotSlotFlags(Op op, uint32_t a_disp, uint32_t b_disp, uint32_t mask, uint32_t dest);
    bool EmitShiftedSlotAlu(Op op, uint32_t a_disp, uint32_t b_disp, Op shift, uint32_t amount,
                            uint32_t mask, uint32_t dest);
    void EmitLoad(Ref address, Ref writeback, uint32_t attr);
    void EmitStore(Ref address, Ref value, Ref writeback, uint32_t attr);
    void EmitTransferWords(uint32_t attr, uint32_t list);
    void EmitCallDirect(const void* fn, CallConv conv, uint32_t num_args, Ref a, Ref b, Ref c,
                        const CallArg* extra, uint32_t num_extra, bool store_pc, uint32_t pc);
    void EmitCallConst(const CallSpec& spec, uint32_t num_args, CallArg a0, CallArg a1,
                       CallArg a2, CallArg a3);
    CERFENGINE_NOINLINE void EmitCallConstBusy(const CallSpec& spec, uint32_t num_args, CallArg a0,
                                               CallArg a1, CallArg a2, CallArg a3);
    void EmitSlotCopy32(uint32_t dest, uint32_t src, bool sext);
    void EmitSlotCopy64(uint32_t dest, uint32_t src);
    bool EmitLeanLoad(const MemAttr& m);
    bool EmitLeanStore(const MemAttr& m, Ref value);
    bool EmitSimpleLoad(uint32_t width, bool sign, int32_t base_slot, int32_t offset,
                        int32_t result_slot, int32_t result_slot2, uint32_t guest_pc);
    bool EmitSimpleStore(uint32_t width, int32_t base_slot, int32_t offset, Ref value,
                         uint32_t guest_pc);

    const Block&          GetBlock() const { return *block_; }
    uint8_t*&             Cursor() { return cursor_; }
    bool                  HasRoom() const { return code_end_ - cursor_ >= kOpReserveBytes; }
    bool                  HasRoomFor(ptrdiff_t bytes) const { return code_end_ - cursor_ >= bytes; }
    X86RegAlloc&          Regs() { return regs_; }
    X86MemoryTails&       Tails() { return tails_; }
    const X86GuestTarget& Target() const { return *target_; }
    X86FlagSource&        FlagSource() { return flag_source_; }
    X86ProbeStamp&        ProbeStamp(uint32_t i) { return probe_stamps_[i]; }

    CERFENGINE_FORCEINLINE uint8_t WantedFlags(Ref producer) const {
        const uint32_t aux16 = block_->At(producer).aux16;
        return static_cast<uint8_t>((aux16 | (aux16 >> 4)) & 0x0Fu);
    }

    void ClearFlagSource() {
        flag_source_.producer = kNoRef;
        std::memset(flag_source_.kind, 0, sizeof(flag_source_.kind));
    }

    void SetFlag(Ref producer, uint32_t index, X86FlagKind kind, uint8_t where) {
        if (flag_source_.producer != producer) {
            ClearFlagSource();
            flag_source_.producer = producer;
        }
        flag_source_.kind[index]  = kind;
        flag_source_.where[index] = where;
    }

    CERFENGINE_FORCEINLINE void AddLabelFixup(uint32_t label, uint8_t* site) {
        fixups_.push_back(Fixup{label, site});
    }
    CERFENGINE_FORCEINLINE void BindLabel(uint32_t label) {
        for (size_t i = fixups_.size(); i-- > 0u;) {
            if (fixups_[i].label != label) continue;
            x86::FixupLabel32(fixups_[i].site, cursor_);
            fixups_[i] = fixups_.back();
            fixups_.pop_back();
        }
    }
    static Ref  HiHalf(Ref value) { return Block::HiHalf(value); }
    X86WideHalf Half(Ref value, bool hi) const;

private:
    struct Fixup {
        uint32_t label;
        uint8_t* site;
    };

    struct FlagStamp {
        uint8_t  bytes[32];
        uint32_t length;
    };

    void LowerValueOp(uint32_t i);
    void EmitSlotAluFast(Op op, uint8_t reg, uint32_t a_disp, const X86RegAlloc::Operand& b,
                         uint32_t dest, bool sext);
    void EmitSlotAluFlags(Op op, uint8_t reg, uint32_t a_disp, const X86RegAlloc::Operand& b,
                          uint32_t mask, uint32_t dest);
    void EmitFlagSetccs(Op op, uint32_t mask);
    CERFENGINE_NOINLINE void BuildFlagStamps(const Block& block);
    [[noreturn]] static void FlagStampFault();
    static constexpr size_t kCycleAddBytes     = 7;
    static constexpr size_t kCycleAddImmOffset = 6;

    void PrepareCycleAdd(int32_t disp);
    void SkipDead(const Inst& inst);
    void ConsumeArg(Ref r);

    CERFENGINE_FORCEINLINE void StoreDest(Ref value, int32_t disp) {
        regs_.BeforeSlotWrite(disp, 4u);
        const X86RegAlloc::Loc& loc = regs_.At(value);
        if (loc.kind == X86RegAlloc::Kind::kReg && loc.wide) {
            x86::EmitMovBaseDisp32Reg(cursor_, x86::kStateReg, disp, loc.reg);
            regs_.NoteStored(value, disp);
            regs_.Consume(value);
            return;
        }
        StoreDestSlow(value, disp);
    }
    void StoreDestSlow(Ref value, int32_t disp);
    void StoreDestSext(Ref value, int32_t disp);

    uint8_t*              cursor_     = nullptr;
    uint8_t*              code_start_ = nullptr;
    uint8_t*              code_end_   = nullptr;
    uint8_t               cycle_add_[8]{};
    int32_t               cycle_add_disp_ = INT32_MIN;
    uint32_t              spill_area_[X86RegAlloc::kSpillSlots]{};
    X86RegAlloc           regs_;
    Block*                block_  = nullptr;
    ValueInfo*            info_   = nullptr;
    const X86GuestTarget* target_ = nullptr;
    X86MemoryTails        tails_;
    std::vector<Fixup>    fixups_;
    X86FlagSource         flag_source_;
    X86ProbeStamp         probe_stamps_[2];
    FlagStamp             flag_stamps_[2][16]{};
    const Block*          flag_stamp_block_  = nullptr;
    uint32_t              flag_stamp_layout_ = 0;
};

}
