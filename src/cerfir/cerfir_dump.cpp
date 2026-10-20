#include "cerfir_dump.h"

#include <cstdarg>
#include <cstdio>

namespace cerfengine::cerfir {

namespace {

const char* TypeName(Type t) {
    switch (t) {
    case Type::kVoid: return "void";
    case Type::kI1:   return "i1";
    case Type::kI8:   return "i8";
    case Type::kI16:  return "i16";
    case Type::kI32:  return "i32";
    case Type::kI64:  return "i64";
    case Type::kPtr:  return "ptr";
    }
    return "?";
}

void Append(std::string& out, const char* fmt, ...) {
    char    text[160];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    out += text;
}

void AppendRef(std::string& out, const Block& block, Ref r) {
    if (IsConstRef(r)) {
        Append(out, "#0x%llX", static_cast<unsigned long long>(block.ConstValue(r)));
    } else if (IsSlotRef(r)) {
        Append(out, "@0x%X", static_cast<unsigned>(SlotRefDisp(r)));
    } else {
        Append(out, "%%%u", static_cast<unsigned>(r));
    }
}

}

std::string Dump(const Block& block) {
    std::string out;
    for (uint32_t i = 0; i < block.Size(); ++i) {
        const Inst&   inst = block.At(static_cast<Ref>(i));
        const OpInfo& info = InfoOf(inst.op);
        if (inst.type != Type::kVoid) {
            Append(out, "%%%u = %s.%s", i, info.name, TypeName(inst.type));
        } else {
            Append(out, "%s", info.name);
        }
        const char* sep = " ";
        for (Ref r : inst.args) {
            if (r == kNoRef) continue;
            out += sep;
            AppendRef(out, block, r);
            sep = ", ";
        }
        if (OpHas(inst.op, kOpPure) && !OpHas(inst.op, kOpFlagPseudo) && inst.aux16 != 0u) {
            Append(out, " flags 0x%X", inst.aux16);
        }
        switch (inst.op) {
        case Op::kExitDirect:
            Append(out, " pc 0x%08X slot %u%s%s", inst.aux,
                   static_cast<unsigned>(inst.aux16 & kExitSlotMask),
                   (inst.aux16 & kExitStorePc) != 0u ? " storepc" : "",
                   (inst.aux16 & kExitProbe) != 0u ? " probe" : "");
            break;
        case Op::kHostLoad:
        case Op::kHostStore:
            Append(out, " w%u%s", inst.aux16 & kHostWidthMask,
                   (inst.aux16 & kHostSigned) != 0u ? " s" : "");
            break;
        case Op::kGetSlot:
        case Op::kSetSlot:
        case Op::kSatAdd:
        case Op::kSatSub:
        case Op::kSignedSat:
        case Op::kOrSticky:
            Append(out, " @0x%X", inst.aux);
            break;
        case Op::kLabel:
        case Op::kBranchIf:
        case Op::kBranchIfNot:
            Append(out, " L%u", inst.aux);
            break;
        case Op::kBranchSlots: {
            const SlotTest& t = block.SlotTestAt(inst.aux16);
            Append(out, " pred%u %s @0x%X @0x%X @0x%X L%u", static_cast<unsigned>(t.pred),
                   t.if_true ? "true" : "false", t.disp[0], t.disp[1], t.disp[2], inst.aux);
            break;
        }
        case Op::kTransferWords: {
            const WordList& w = block.WordListAt(inst.aux16);
            Append(out, " {%s wb%+d", w.store ? "store" : "load", static_cast<int32_t>(w.wb_add));
            for (uint32_t k = 0; k < w.count; ++k) {
                if (w.slot[k] == kNoSlot) {
                    Append(out, " %+d:#0x%X", w.offset[k], w.imm[k]);
                } else {
                    Append(out, " %+d:@0x%X", w.offset[k], static_cast<unsigned>(w.slot[k]));
                }
            }
            Append(out, "}");
            [[fallthrough]];
        }
        case Op::kLoad:
        case Op::kStore:
        case Op::kSwap: {
            const MemAttr& m = block.MemAttrAt(inst.aux);
            Append(out, " {w%u%s%s%s%s align%u slow 0x%X cross %u pc 0x%08X", m.width_bytes,
                   m.sign ? " s" : "", m.unpriv ? " t" : "", m.complete ? " complete" : "",
                   m.back_out ? " backout" : "", static_cast<unsigned>(m.align), m.slow_mask,
                   static_cast<unsigned>(m.cross_granule), m.guest_pc);
            if (m.result_slot != kNoSlot) Append(out, " result @0x%X", m.result_slot);
            if (m.result_slot2 != kNoSlot) Append(out, " result2 @0x%X", m.result_slot2);
            if (m.writeback_slot != kNoSlot) Append(out, " writeback @0x%X", m.writeback_slot);
            if (m.base_slot != kNoSlot) {
                Append(out, " base @0x%X%+d%s", m.base_slot, m.offset, m.pre_index ? " pre" : " post");
            }
            Append(out, "}");
            break;
        }
        case Op::kCallHelper:
            Append(out, " {call %u args, %u extra}", block.CallAt(inst.aux).num_args,
                   block.CallAt(inst.aux).num_extra);
            if (block.CallAt(inst.aux).store_pc) {
                Append(out, " pc 0x%08X", static_cast<unsigned>(block.CallAt(inst.aux).pc));
            }
            break;
        default:
            break;
        }
        if (inst.op == Op::kSetSlot && (inst.aux16 & kSetSlotSext) != 0u) out += " sext";
        if (inst.dest != kNoDest) Append(out, " -> @0x%X", static_cast<unsigned>(inst.dest));
        if ((block.Info(static_cast<Ref>(i)).meta & kValueDestSext) != 0u) out += " sext";
        out += '\n';
    }
    return out;
}

}
