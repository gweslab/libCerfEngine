#include "arm_neon_2reg_unary_decoder.h"

#include <cerfengine/engine.h>
#include "arm_neon_2reg_bitcount.h"
#include "arm_neon_2reg_compare_zero.h"
#include "arm_neon_2reg_cvt_half_single.h"
#include "arm_neon_2reg_cvt_int_fp.h"
#include "arm_neon_2reg_narrow.h"
#include "arm_neon_2reg_pairwise_add_long.h"
#include "arm_neon_2reg_reciprocal.h"
#include "arm_neon_2reg_reverse.h"
#include "arm_neon_2reg_sat_abs_neg.h"
#include "arm_neon_2reg_shuffle.h"
#include "arm_neon_2reg_swap.h"
#include "arm_neon_2reg_unary_arith.h"
#include "arm_opcode.h"
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>

namespace cerfengine {

CERFENGINE_REGISTER(ArmNeon2RegUnaryDecoder);

bool ArmNeon2RegUnaryDecoder::ShouldRegister() {
    return engine_.GetSetup().isa == cerfengine::Isa::kArm;
}

bool ArmNeon2RegUnaryDecoder::Decode(DecodedInsn* insn, ArmOpcode op) {
    const uint32_t a = (op.word >> 16) & 0x3u;

    if (a == 0u) {
        const uint32_t b_high = (op.word >> 7) & 0xFu;
        /* bits[10:7]=00xx → VREV (A8.8.386); xx picks 64/32/16/UND. */
        if ((b_high & 0xCu) == 0x0u) {
            const uint32_t op_field = b_high & 0x3u;
            /* ARM DDI 0406C.c Table A7-13 (p. A7-267): A = 00 allocates
               B = 0000x, 0001x, 0010x only; 0011x is unallocated. A7.4.5
               (p. A7-267): "Other encodings in this space are UNDEFINED." */
            if (op_field == 3u) {
                insn->cond      = 14;
                insn->immediate = op.word;
                insn->place_fn  = &EmitRaiseUndAndReturn;
                return true;
            }
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = op_field;
            insn->place_fn  = &PlaceNeonData2RegReverse;
            return true;
        }
        /* bits[10:7]=10xx → VCLS/VCLZ/VCNT/VMVN (A8.8.299/302/304/354). */
        if ((b_high & 0xCu) == 0x8u) {
            const uint32_t sel = b_high & 0x3u;
            insn->cond      = 14;
            insn->immediate = op.word;
            if (sel == 3u) {
                insn->place_fn = &PlaceNeonData2RegBitwiseNot;
            } else {
                if (sel == 0u)      insn->op1 = ArmNeon2RegBitcount::kCls;
                else if (sel == 1u) insn->op1 = ArmNeon2RegBitcount::kClz;
                else                insn->op1 = ArmNeon2RegBitcount::kCnt;
                insn->place_fn = &PlaceNeonData2RegBitcount;
            }
            return true;
        }
        /* bits[10:7]=010x → VPADDL (A8.8.364); bit[7]=U signed/unsigned. */
        if ((b_high & 0xEu) == 0x4u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegPairwiseAddLong::kPaddl;
            insn->place_fn  = &PlaceNeonData2RegPairwiseAddLong;
            return true;
        }
        /* bits[10:7]=110x → VPADAL (A8.8.361); bit[7]=U signed/unsigned. */
        if ((b_high & 0xEu) == 0xCu) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegPairwiseAddLong::kPadal;
            insn->place_fn  = &PlaceNeonData2RegPairwiseAddLong;
            return true;
        }
        /* bits[10:7]=1110 → VQABS (A8.8.369); 1111 → VQNEG (A8.8.375). */
        if (b_high == 0xEu) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegSatAbsNeg::kQabs;
            insn->place_fn  = &PlaceNeonData2RegSatAbsNeg;
            return true;
        }
        if (b_high == 0xFu) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegSatAbsNeg::kQneg;
            insn->place_fn  = &PlaceNeonData2RegSatAbsNeg;
            return true;
        }
        /* ARM DDI 0406C.c Table A7-13 (p. A7-267): A = 00 allocates B = 010xx
           VPADDL and then 1000x VCLS; B = 011xx is unallocated. A7.4.5
           (p. A7-267): "Other encodings in this space are UNDEFINED." */
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->place_fn  = &EmitRaiseUndAndReturn;
        return true;
    }

    if (a == 1u) {
        /* A7.4.5 A=01. bits[9:7] discriminates within this region (ARM ARM
           Table A7-13). */
        const uint32_t bits_9_7 = (op.word >> 7) & 0x7u;
        if (bits_9_7 == 0x0u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegCompareZero::kCgt;
            insn->place_fn  = &PlaceNeonData2RegCompareZero;
            return true;
        }
        if (bits_9_7 == 0x1u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegCompareZero::kCge;
            insn->place_fn  = &PlaceNeonData2RegCompareZero;
            return true;
        }
        if (bits_9_7 == 0x2u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegCompareZero::kCeq;
            insn->place_fn  = &PlaceNeonData2RegCompareZero;
            return true;
        }
        if (bits_9_7 == 0x3u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegCompareZero::kCle;
            insn->place_fn  = &PlaceNeonData2RegCompareZero;
            return true;
        }
        if (bits_9_7 == 0x4u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegCompareZero::kClt;
            insn->place_fn  = &PlaceNeonData2RegCompareZero;
            return true;
        }
        if (bits_9_7 == 0x6u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegUnaryArith::kAbs;
            insn->place_fn  = &PlaceNeonData2RegUnaryArith;
            return true;
        }
        if (bits_9_7 == 0x7u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegUnaryArith::kNeg;
            insn->place_fn  = &PlaceNeonData2RegUnaryArith;
            return true;
        }
        /* ARM DDI 0406C.c Table A7-13 (p. A7-267): A = 01 allocates B = x000x,
           x001x, x010x, x011x, x100x, x110x and x111x; x101x is unallocated.
           A7.4.5 (p. A7-267): "Other encodings in this space are UNDEFINED." */
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->place_fn  = &EmitRaiseUndAndReturn;
        return true;
    }

    if (a == 2u) {
        const uint32_t b_high = (op.word >> 7) & 0xFu;
        /* bits[10:7]=0000 → VSWP (A8.8.418). */
        if (b_high == 0x0u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->place_fn  = &PlaceNeonData2RegSwap;
            return true;
        }
        /* bits[10:7]=0001 → VTRN (A8.8.420). */
        if (b_high == 0x1u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegShuffle::kTrn;
            insn->place_fn  = &PlaceNeonData2RegShuffle;
            return true;
        }
        /* bits[10:7]=0010 → VUZP (A8.8.422). */
        if (b_high == 0x2u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegShuffle::kUzp;
            insn->place_fn  = &PlaceNeonData2RegShuffle;
            return true;
        }
        /* bits[10:7]=0011 → VZIP (A8.8.423). */
        if (b_high == 0x3u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = ArmNeon2RegShuffle::kZip;
            insn->place_fn  = &PlaceNeonData2RegShuffle;
            return true;
        }
        /* bits[10:7] ∈ {0x4, 0x5} = 5-bit B field 0100x or 0101x →
           VMOVN / VQMOVUN / VQMOVN.S / VQMOVN.U per A7.4.5 Table A7-13.
           bits[7:6] = op field directly selects the variant. */
        if (b_high == 0x4u || b_high == 0x5u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = (op.word >> 6) & 0x3u;
            insn->place_fn  = &PlaceNeonData2RegNarrow;
            return true;
        }
        /* bits[10:7]=0x6 + bit[6]=0 → VSHLL T2/A2 (A8.8.397). bit[6]=1
           is unallocated (Table A7-13 entry is exactly "01100") → UND. */
        if (b_high == 0x6u && ((op.word >> 6) & 1u) == 0u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->place_fn  = &PlaceNeonData2RegWiden;
            return true;
        }
        /* bits[10:7]=11x0 + bit[6]=0 → VCVT half↔single (A8.8.310). bit[8]=op
           direction. Table A7-13 row "10 11x00". */
        if ((b_high & 0xDu) == 0xCu && ((op.word >> 6) & 1u) == 0u) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = (op.word >> 8) & 1u;
            insn->place_fn  = &PlaceNeonData2RegCvtHalfSingle;
            return true;
        }
        /* ARM DDI 0406C.c Table A7-13 (p. A7-268): A = 10 allocates B = 0000x,
           0001x, 0010x, 0011x, 01000, 01001, 0101x, 01100 and 11x00 only.
           A7.4.5 (p. A7-267): "Other encodings in this space are UNDEFINED." */
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->place_fn  = &EmitRaiseUndAndReturn;
        return true;
    }

    if (a == 3u) {
        const uint32_t b_high = (op.word >> 7) & 0xFu;
        /* bits[10:9]=10 + bit[7]=op (0=VRECPE, 1=VRSQRTE), bit[8]=F.
           Table A7-13 row "11 10x0x VRECPE" / "10x1x VRSQRTE". */
        if ((b_high & 0xCu) == 0x8u) {
            const uint32_t op7 = b_high & 1u;
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = (op7 == 0u) ? ArmNeon2RegReciprocal::kRecpe
                                          : ArmNeon2RegReciprocal::kRsqrte;
            insn->place_fn  = &PlaceNeonData2RegReciprocal;
            return true;
        }
        /* bits[10:9]=11 → VCVT int↔fp (A8.8.305). bits[8:7]=op directly. */
        if ((b_high & 0xCu) == 0xCu) {
            insn->cond      = 14;
            insn->immediate = op.word;
            insn->op1       = b_high & 0x3u;
            insn->place_fn  = &PlaceNeonData2RegCvtIntFp;
            return true;
        }
        /* ARM DDI 0406C.c Table A7-13 (p. A7-268): A = 11 allocates B = 10x0x
           VRECPE, 10x1x VRSQRTE and 11xxx VCVT only. A7.4.5 (p. A7-267):
           "Other encodings in this space are UNDEFINED." */
        insn->cond      = 14;
        insn->immediate = op.word;
        insn->place_fn  = &EmitRaiseUndAndReturn;
        return true;
    }

    return false;
}

}
