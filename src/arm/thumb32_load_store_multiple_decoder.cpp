#include "thumb32_load_store_multiple_decoder.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/block_context.h>

namespace cerfengine {

CERFENGINE_REGISTER(Thumb32LoadStoreMultipleDecoder);

bool Thumb32LoadStoreMultipleDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

bool Thumb32LoadStoreMultipleDecoder::DecodeBlockTransfer(DecodedInsn* insn,
                                                          uint32_t op,
                                                          bool increment) {
    const uint32_t load = (op >> 20) & 0x1u;
    const uint32_t w    = (op >> 21) & 0x1u;
    const uint32_t rn   = (op >> 16) & 0xFu;
    const uint32_t p    = (op >> 15) & 0x1u;
    const uint32_t m    = (op >> 14) & 0x1u;

    /* A6.1.1 p. A6-220: a non-zero (0) bit is UNPREDICTABLE; hw2[13] is (0)
       on every row of Table A6-16 and hw2[15] is (0) on the stores. */
    if (((op >> 13) & 0x1u) != 0u || (load == 0u && p != 0u)) {
        return false;
    }
    /* "if n == 15 || BitCount(registers) < 2 || (P == '1' && M == '1') then
       UNPREDICTABLE" (p. A8-396, p. A8-402). */
    if (load != 0u && p != 0u && m != 0u) {
        return false;
    }
    const uint32_t list =
        ((load != 0u ? p : 0u) << 15) | (m << 14) | (op & 0x1FFFu);
    if (rn == 15u || (list & (list - 1u)) == 0u) {
        return false;
    }
    if (w != 0u && ((list >> rn) & 0x1u) != 0u) {
        return false;
    }

    insn->register_list = static_cast<uint16_t>(list);
    insn->l             = load;
    insn->w             = w;
    insn->s             = 0u;
    insn->rn            = rn;
    insn->u             = increment ? 1u : 0u;
    insn->p             = increment ? 0u : 1u;
    insn->r15_modified  = (list & 0x8000u) != 0u;
    insn->kind          = ArmInsnKind::kBlockDataTransfer;
    return true;
}

/* DDI 0406C.c B9.3.13 RFE (p. B9-2000): encoding T1 is RFEDB with "increment
   = FALSE", encoding T2 is RFEIA with "increment = TRUE", both carrying
   "wordhigher = FALSE"; "if n == 15 then UNPREDICTABLE". */
bool Thumb32LoadStoreMultipleDecoder::DecodeReturnFromException(
    DecodedInsn* insn, uint32_t op, bool increment) {
    const uint32_t rn = (op >> 16) & 0xFu;
    if (rn == 15u) {
        return false;
    }
    insn->rn                  = rn;
    insn->w                   = (op >> 21) & 0x1u;
    insn->u                   = increment ? 1u : 0u;
    insn->p                   = increment ? 0u : 1u;
    insn->r15_modified        = true;
    insn->is_exception_return = true;
    insn->kind                = ArmInsnKind::kRfe;
    return true;
}

/* DDI 0406C.c B9.3.15 SRS (Thumb) (p. B9-2004): encoding T1 is SRSDB with
   "increment = FALSE", encoding T2 is SRSIA with "increment = TRUE", both
   carrying "wordhigher = FALSE"; mode is hw2[4:0]. */
bool Thumb32LoadStoreMultipleDecoder::DecodeStoreReturnState(
    DecodedInsn* insn, uint32_t op, bool increment) {
    insn->w         = (op >> 21) & 0x1u;
    insn->u         = increment ? 1u : 0u;
    insn->p         = increment ? 0u : 1u;
    insn->immediate = op & 0x1Fu;
    insn->kind      = ArmInsnKind::kSrs;
    return true;
}

/* DDI 0406C.c A6.3.5 and Table A6-16 p. A6-237: op = bits[24:23],
   W = bit[21], L = bit[20], Rn = bits[19:16]; these encodings are all
   available in ARMv6T2 and above. */
bool Thumb32LoadStoreMultipleDecoder::Decode(DecodedInsn* insn, uint32_t op) {
    const bool load = ((op >> 20) & 0x1u) != 0u;
    switch ((op >> 23) & 0x3u) {
    case 0x0u:
        return load ? DecodeReturnFromException(insn, op, false)
                    : DecodeStoreReturnState(insn, op, false);
    case 0x1u:
        return DecodeBlockTransfer(insn, op, true);
    case 0x2u:
        return DecodeBlockTransfer(insn, op, false);
    default:
        return load ? DecodeReturnFromException(insn, op, true)
                    : DecodeStoreReturnState(insn, op, true);
    }
}

}
