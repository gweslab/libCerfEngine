#pragma once

#include <cstdint>
#include <vector>

#include <cerfengine/force_inline.h>
#include "cerfir_op.h"
#include "cerfir_types.h"

namespace cerfengine::cerfir {

enum class AlignPolicy : uint8_t {
    kNone,
    kFault,
    kLegacyRotate,
    kCrossCheck,
    kUndefinedIfUnaligned,
    kIgnoreLowBits,
};

constexpr int32_t kNoSlot = -1;

struct MemAttr {
    uint8_t     width_bytes        = 4;
    bool        sign               = false;
    bool        unpriv             = false;
    bool        complete           = false;
    bool        back_out           = false;
    AlignPolicy align              = AlignPolicy::kNone;
    bool        stop_after_io      = false;
    bool        writeback_on_fault = false;
    uint16_t    cross_granule      = 0;
    uint32_t    slow_mask          = 0;
    int32_t     result_slot        = kNoSlot;
    int32_t     result_slot2       = kNoSlot;
    int32_t     writeback_slot     = kNoSlot;
    int32_t     base_slot          = kNoSlot;
    int32_t     offset             = 0;
    bool        pre_index          = true;
    uint32_t    guest_pc           = 0;
    uint32_t    next_pc            = 0;
    uint32_t    raw                = 0;
};

inline constexpr MemAttr kDefaultMemAttr{};

constexpr uint32_t kMaxWords = 16;

struct WordList {
    bool     store              = false;
    uint8_t  count              = 0;
    uint32_t wb_add             = 0;
    int32_t  offset[kMaxWords]  = {};
    int32_t  slot[kMaxWords]    = {};
    uint32_t imm[kMaxWords]     = {};
};

inline bool IsMemoryOp(Op op) {
    return op == Op::kLoad || op == Op::kStore || op == Op::kSwap || op == Op::kTransferWords;
}

inline uint32_t MemoryArgMask(Op op, const MemAttr& attr) {
    if (op == Op::kTransferWords) return 0u;
    const bool based = attr.base_slot != kNoSlot;
    uint32_t   mask  = based ? 0u : 1u;
    if (op != Op::kLoad) mask |= 2u;
    if (!based && attr.writeback_slot != kNoSlot) mask |= op == Op::kLoad ? 2u : 4u;
    return mask;
}

enum class CallConv : uint8_t {
    kCdecl,
    kFastcall,
};

enum class CallArgKind : uint8_t { kImm = 0u, kHostWord = 1u, kHostAddress = 2u };

struct CallArg {
    CallArgKind kind;
    uintptr_t   value;

    CallArg() = default;
    constexpr CallArg(uint32_t imm) : kind(CallArgKind::kImm), value(imm) {}
    CERFENGINE_FORCEINLINE CallArg(CallArgKind host_kind, const void* host)
        : kind(host_kind), value(reinterpret_cast<uintptr_t>(host)) {}

    CERFENGINE_FORCEINLINE const void* Host() const { return reinterpret_cast<const void*>(value); }
};

CERFENGINE_FORCEINLINE CallArg CallHostAddress(const void* address) {
    return CallArg(CallArgKind::kHostAddress, address);
}

CERFENGINE_FORCEINLINE CallArg CallHostWord(const void* address) {
    return CallArg(CallArgKind::kHostWord, address);
}

CERFENGINE_FORCEINLINE bool AnyHostWord(CallArg a0, CallArg a1, CallArg a2, CallArg a3) {
    return ((static_cast<uint32_t>(a0.kind) | static_cast<uint32_t>(a1.kind) |
             static_cast<uint32_t>(a2.kind) | static_cast<uint32_t>(a3.kind)) &
            static_cast<uint32_t>(CallArgKind::kHostWord)) != 0u;
}

constexpr uint32_t kMaxCallExtraArgs = 9;
constexpr uint32_t kMaxConstCallArgs = 4;

constexpr uint16_t kHostWidthMask = 0x00FFu;
constexpr uint16_t kHostSigned    = 0x0100u;
constexpr uint16_t kExitSlotMask  = 0x00FFu;
constexpr uint16_t kExitProbe     = 0x0100u;
constexpr uint16_t kExitStorePc   = 0x8000u;

enum class SlotPredicate : uint8_t {
    kNonZero,
    kAndNot,
    kNotEqual,
    kOrNotEqual,
    kZero32,
    kEqual32,
    kZero64,
    kEqual64,
};

struct SlotTest {
    SlotPredicate pred    = SlotPredicate::kNonZero;
    bool          if_true = true;
    int32_t       disp[3] = {};
};

struct FlagSlots {
    bool    set     = false;
    int32_t disp[4] = {};
};

struct CallDesc {
    const void*  fn;
    CallConv     conv;
    uint8_t      num_args;
    uint8_t      num_extra = 0;
    CallArg      extra[kMaxCallExtraArgs] = {};
    bool         store_pc = false;
    uint32_t     pc       = 0;
};

struct CallSpec {
    const void* fn;
    CallConv    conv;
    Type        result;
    bool        store_pc = false;
    uint32_t    pc       = 0;
};

[[noreturn]] void BlockLimitExceeded(const char* what);

constexpr uint8_t kValueSkip          = 1u << 3;
constexpr uint8_t kValueIsValueOp     = 1u << 4;
constexpr uint8_t kValueStoresFlags   = 1u << 5;
constexpr uint8_t kValueDestSext      = 1u << 7;

constexpr uint16_t kSetSlotSext = 1u;

struct ValueInfo {
    uint16_t uses;
    uint8_t  meta;
    uint8_t  wide;
};

struct SlotEpochs {
    static constexpr uint32_t kGranules = kSlotRefBytes / 4u;
    static constexpr uint32_t kRebaseAt = 0xF0000000u;

    uint32_t read[kGranules]  = {};
    uint32_t write[kGranules] = {};
    uint32_t all_write        = 0;
    uint32_t now              = 0;

    CERFENGINE_FORCEINLINE uint32_t Tick() { return ++now; }

    CERFENGINE_FORCEINLINE bool WrittenSinceRead(uint32_t g) const {
        const uint32_t last_write = write[g] > all_write ? write[g] : all_write;
        return last_write > read[g];
    }

    CERFENGINE_FORCEINLINE void NoteWrite(int32_t disp, uint32_t bytes) {
        if (static_cast<uint32_t>(disp) >= kSlotRefBytes) return;
        const uint32_t tick  = Tick();
        const uint32_t first = static_cast<uint32_t>(disp) >> 2;
        if (bytes <= 4u && (static_cast<uint32_t>(disp) & 3u) <= 4u - bytes) {
            write[first] = tick;
            return;
        }
        if (bytes == 8u && (static_cast<uint32_t>(disp) & 3u) == 0u && first + 1u < kGranules) {
            write[first]      = tick;
            write[first + 1u] = tick;
            return;
        }
        uint32_t last = (static_cast<uint32_t>(disp) + bytes - 1u) >> 2;
        if (last >= kGranules) last = kGranules - 1u;
        for (uint32_t g = first; g <= last; ++g) write[g] = tick;
    }

    void RebaseIfNear() {
        if (now < kRebaseAt) return;
        for (uint32_t g = 0; g < kGranules; ++g) {
            read[g]  = 0u;
            write[g] = 0u;
        }
        all_write = 0u;
        now       = 0u;
    }
};

class Block {
public:
    Block() : insts_(kMaxInsts), info_(kMaxInsts) {}

    CERFENGINE_FORCEINLINE void Reset() {
        size_            = 0;
        const_size_      = 0;
        mem_attr_count_  = 0;
        word_list_count_ = 0;
        call_count_      = 0;
        slot_test_count_ = 0;
        label_count_     = 0;
    }

    CERFENGINE_FORCEINLINE Ref Append(Op op, Type type, Ref a, Ref b, Ref c, uint16_t aux16,
                                      uint32_t aux, uint8_t meta, uint16_t uses) {
        if (size_ == kMaxInsts) BlockLimitExceeded("instruction");
        Inst& inst   = insts_[size_];
        inst.op      = op;
        inst.type    = type;
        inst.args[0] = a;
        inst.args[1] = b;
        inst.args[2] = c;
        inst.aux16   = aux16;
        inst.dest    = kNoDest;
        inst.aux     = aux;
        info_[size_] = ValueInfo{uses, meta, 0u};
        return static_cast<Ref>(size_++);
    }

    ValueInfo&       Info(Ref r)       { return info_[r]; }
    const ValueInfo& Info(Ref r) const { return info_[r]; }
    ValueInfo*       InfoData()        { return info_.data(); }

    CERFENGINE_FORCEINLINE static Ref HiHalf(Ref value) { return static_cast<Ref>(value + 1u); }

    CERFENGINE_FORCEINLINE Ref Const(Type type, uint64_t value) {
        if (const_size_ == const_capacity_) GrowConsts();
        ConstEntry& e = consts_[const_size_];
        e.type        = type;
        e.value       = value & TypeMask(type);
        return ConstRef(static_cast<uint16_t>(const_size_++));
    }

    CERFENGINE_FORCEINLINE Ref Const32(uint32_t value) {
        if (const_size_ == const_capacity_) GrowConsts();
        ConstEntry& e = consts_[const_size_];
        e.type        = Type::kI32;
        e.value       = value;
        return ConstRef(static_cast<uint16_t>(const_size_++));
    }

    CERFENGINE_FORCEINLINE Ref ConstPtr(const void* p) {
        return Const(Type::kPtr, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)));
    }

    CERFENGINE_FORCEINLINE Ref ConstArg(const CallArg& a) {
        return a.kind == CallArgKind::kHostAddress ? Const(Type::kPtr, a.value)
                                                   : Const32(static_cast<uint32_t>(a.value));
    }

    CERFENGINE_FORCEINLINE const Inst* Insts() const { return insts_.data(); }
    CERFENGINE_FORCEINLINE Inst&       At(Ref r) { return insts_[r]; }
    CERFENGINE_FORCEINLINE const Inst& At(Ref r) const { return insts_[r]; }
    CERFENGINE_FORCEINLINE uint64_t    ConstValue(Ref r) const { return consts_[ConstIndex(r)].value; }
    CERFENGINE_FORCEINLINE Type        ConstType(Ref r) const { return consts_[ConstIndex(r)].type; }
    CERFENGINE_FORCEINLINE Type        TypeOf(Ref r) const {
        return IsConstRef(r) ? ConstType(r) : IsSlotRef(r) ? Type::kI32 : insts_[r].type;
    }
    SlotEpochs& Epochs() { return epochs_; }

    uint32_t Size() const { return size_; }
    uint32_t ConstCount() const { return const_size_; }

    CERFENGINE_FORCEINLINE MemAttr& NewMemAttr(uint32_t* index) {
        if (mem_attr_count_ == mem_attr_capacity_) mem_attr_capacity_ = GrowSideTable(mem_attrs_);
        *index     = mem_attr_count_;
        MemAttr& m = mem_attrs_[mem_attr_count_++];
        m          = kDefaultMemAttr;
        return m;
    }
    CERFENGINE_FORCEINLINE WordList& NewWordList(uint32_t* index) {
        if (word_list_count_ == word_list_capacity_) word_list_capacity_ = GrowSideTable(word_lists_);
        *index      = word_list_count_;
        WordList& w = word_lists_[word_list_count_++];
        w.count     = 0;
        return w;
    }
    const WordList& WordListAt(uint32_t i) const { return word_lists_[i]; }

    CERFENGINE_FORCEINLINE uint32_t AddCall(const CallDesc& call) {
        CallDesc& d = NewCall(call.fn, call.conv, call.num_args, call.num_extra);
        for (uint32_t i = 0; i < call.num_extra; ++i) d.extra[i] = call.extra[i];
        d.store_pc = call.store_pc;
        d.pc       = call.pc;
        return call_count_ - 1u;
    }
    CERFENGINE_FORCEINLINE CallDesc& NewCall(const void* fn, CallConv conv, uint8_t num_args,
                                             uint8_t num_extra) {
        if (call_count_ == call_capacity_) call_capacity_ = GrowSideTable(calls_);
        CallDesc& d = calls_[call_count_++];
        d.fn        = fn;
        d.conv      = conv;
        d.num_args  = num_args;
        d.num_extra = num_extra;
        d.store_pc  = false;
        return d;
    }
    uint32_t CallCount() const { return call_count_; }
    CERFENGINE_FORCEINLINE uint32_t AddSlotTest(const SlotTest& test) {
        if (slot_test_count_ == slot_test_capacity_) GrowSlotTests();
        slot_tests_[slot_test_count_] = test;
        return slot_test_count_++;
    }
    uint32_t NewLabel() { return label_count_++; }

    const MemAttr&  MemAttrAt(uint32_t i) const { return mem_attrs_[i]; }
    const CallDesc& CallAt(uint32_t i) const { return calls_[i]; }
    const SlotTest& SlotTestAt(uint32_t i) const { return slot_tests_[i]; }
    uint32_t        SlotTestCount() const { return slot_test_count_; }

    void SetFlagSlots(int32_t n, int32_t z, int32_t c, int32_t v) {
        flag_slots_.set     = true;
        flag_slots_.disp[0] = n;
        flag_slots_.disp[1] = z;
        flag_slots_.disp[2] = c;
        flag_slots_.disp[3] = v;
        flag_contiguous_    = z == n + 1 && c == z + 1 && v == c + 1;
        flag_granule_       = -1;
        ++flag_layout_;
        const int32_t g     = n >> 2;
        for (int32_t d : flag_slots_.disp) {
            if (d < 0 || d >= static_cast<int32_t>(kSlotRefBytes) || (d >> 2) != g) return;
        }
        flag_granule_ = g;
    }
    const FlagSlots& FlagSlotsOf() const { return flag_slots_; }
    uint32_t         FlagLayout() const { return flag_layout_; }
    int32_t          FlagGranule() const { return flag_granule_; }
    bool             FlagSlotsContiguous() const { return flag_contiguous_; }

    void    SetPcSlot(int32_t disp) { pc_slot_ = disp; }
    int32_t PcSlot() const { return pc_slot_; }
    uint32_t        LabelCount() const { return label_count_; }

private:
    struct ConstEntry {
        Type     type;
        uint64_t value;
    };

    void GrowConsts();
    void GrowSlotTests();

    template <typename T>
    static uint32_t GrowSideTable(std::vector<T>& table) {
        table.resize(table.empty() ? 16u : table.size() * 2u);
        return static_cast<uint32_t>(table.size());
    }

    std::vector<Inst>       insts_;
    std::vector<ValueInfo>  info_;
    uint32_t                size_     = 0;
    std::vector<ConstEntry> consts_;
    uint32_t                const_size_     = 0;
    uint32_t                const_capacity_ = 0;
    std::vector<MemAttr>    mem_attrs_;
    uint32_t                mem_attr_count_     = 0;
    uint32_t                mem_attr_capacity_  = 0;
    std::vector<WordList>   word_lists_;
    uint32_t                word_list_count_    = 0;
    uint32_t                word_list_capacity_ = 0;
    std::vector<CallDesc>   calls_;
    uint32_t                call_count_         = 0;
    uint32_t                call_capacity_      = 0;
    std::vector<SlotTest>   slot_tests_;
    uint32_t                slot_test_count_    = 0;
    uint32_t                slot_test_capacity_ = 0;
    FlagSlots               flag_slots_;
    int32_t                 flag_granule_ = -1;
    uint32_t                flag_layout_  = 0;
    bool                    flag_contiguous_ = false;
    int32_t                 pc_slot_      = kNoSlot;
    SlotEpochs              epochs_;
    uint32_t                label_count_  = 0;
};

}
