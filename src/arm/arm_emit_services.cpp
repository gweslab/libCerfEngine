#include "arm_emit_services.h"

#include <cerfengine/engine.h>
#include <cerfengine/arm/arm_cpu.h>
#include "arm_exception_frame.h"
#include <cerfengine/arm/arm_interrupt_channel.h>
#include <cerfengine/arm/arm_mmu.h>
#include "arm_neon.h"
#include "arm_neon_2reg_bitcount.h"
#include "arm_neon_2reg_bitwise_not.h"
#include "arm_neon_2reg_compare_zero.h"
#include "arm_neon_2reg_cvt_half_single.h"
#include "arm_neon_2reg_cvt_int_fp.h"
#include "arm_neon_2reg_narrow.h"
#include "arm_neon_2reg_pairwise_add_long.h"
#include "arm_neon_2reg_reciprocal.h"
#include "arm_neon_2reg_reverse.h"
#include "arm_neon_2reg_sat_abs_neg.h"
#include "arm_neon_2reg_scalar_mul.h"
#include "arm_neon_2reg_shuffle.h"
#include "arm_neon_2reg_swap.h"
#include "arm_neon_2reg_unary_arith.h"
#include "arm_neon_2regscalar.h"
#include "arm_neon_3difflen.h"
#include "arm_neon_3same_fp_abs_compare.h"
#include "arm_neon_3same_fp_arith.h"
#include "arm_neon_3same_fp_compare.h"
#include "arm_neon_3same_fp_fma.h"
#include "arm_neon_3same_fp_min_max.h"
#include "arm_neon_3same_fp_mul_acc.h"
#include "arm_neon_3same_fp_pair_add.h"
#include "arm_neon_3same_fp_pair_min_max.h"
#include "arm_neon_3same_fp_recip_step.h"
#include "arm_neon_one_reg_imm.h"
#include "arm_neon_sat.h"
#include "arm_neon_scalar_move.h"
#include "arm_neon_shift_imm.h"
#include "arm_neon_simd_3same.h"
#include "arm_neon_vext.h"
#include "arm_neon_vtbl.h"
#include "arm_routed_access.h"
#include "arm_translation_cache.h"
#include <cerfengine/arm/arm_translator.h>
#include "arm_unimplemented_halt.h"
#include "arm_vfp.h"
#include "arm_vfp_memory.h"

namespace cerfengine {

CERFENGINE_REGISTER(ArmEmitServices);

void ArmEmitServices::OnReady() {
    cpu_        = &engine_.Get<ArmCpu>();
    cpu_state_  = cpu_->State();
    mmu_        = &engine_.Get<ArmMmu>();
    translator_ = &engine_.Get<ArmTranslator>();

    system_           = engine_.GetSetup().arm_system;
    processor_config_ = &engine_.GetSetup().arm;

    translation_cache_ = &engine_.Get<ArmTranslationCache>();
    interrupt_channel_ = &engine_.Get<ArmInterruptChannel>();
    exception_frame_   = &engine_.Get<ArmExceptionFrame>();
    routed_access_     = &engine_.Get<ArmRoutedAccess>();
    unimplemented_halt_ = &engine_.Get<ArmUnimplementedHalt>();

    neon_                        = &engine_.Get<ArmNeon>();
    simd3same_                   = &engine_.Get<ArmNeonSimd3Same>();
    neon_sat_                    = &engine_.Get<ArmNeonSat>();
    neon_shift_imm_              = &engine_.Get<ArmNeonShiftImm>();
    neon_one_reg_imm_            = &engine_.Get<ArmNeonOneRegImm>();
    neon_scalar_move_            = &engine_.Get<ArmNeonScalarMove>();
    neon_vext_                   = &engine_.Get<ArmNeonVext>();
    neon_vtbl_                   = &engine_.Get<ArmNeonVtbl>();
    neon_2reg_scalar_            = &engine_.Get<ArmNeon2RegScalar>();
    neon_3difflen_               = &engine_.Get<ArmNeon3DiffLen>();
    neon_2reg_bitcount_          = &engine_.Get<ArmNeon2RegBitcount>();
    neon_2reg_bitwise_not_       = &engine_.Get<ArmNeon2RegBitwiseNot>();
    neon_2reg_compare_zero_      = &engine_.Get<ArmNeon2RegCompareZero>();
    neon_2reg_cvt_half_single_   = &engine_.Get<ArmNeon2RegCvtHalfSingle>();
    neon_2reg_cvt_int_fp_        = &engine_.Get<ArmNeon2RegCvtIntFp>();
    neon_2reg_narrow_            = &engine_.Get<ArmNeon2RegNarrow>();
    neon_2reg_pairwise_add_long_ = &engine_.Get<ArmNeon2RegPairwiseAddLong>();
    neon_2reg_reciprocal_        = &engine_.Get<ArmNeon2RegReciprocal>();
    neon_2reg_reverse_           = &engine_.Get<ArmNeon2RegReverse>();
    neon_2reg_sat_abs_neg_       = &engine_.Get<ArmNeon2RegSatAbsNeg>();
    neon_2reg_scalar_mul_        = &engine_.Get<ArmNeon2RegScalarMul>();
    neon_2reg_shuffle_           = &engine_.Get<ArmNeon2RegShuffle>();
    neon_2reg_swap_              = &engine_.Get<ArmNeon2RegSwap>();
    neon_2reg_unary_arith_       = &engine_.Get<ArmNeon2RegUnaryArith>();
    neon_3same_fp_abs_compare_   = &engine_.Get<ArmNeon3SameFpAbsCompare>();
    neon_3same_fp_arith_         = &engine_.Get<ArmNeon3SameFpArith>();
    neon_3same_fp_compare_       = &engine_.Get<ArmNeon3SameFpCompare>();
    neon_3same_fp_fma_           = &engine_.Get<ArmNeon3SameFpFma>();
    neon_3same_fp_min_max_       = &engine_.Get<ArmNeon3SameFpMinMax>();
    neon_3same_fp_mul_acc_       = &engine_.Get<ArmNeon3SameFpMulAcc>();
    neon_3same_fp_pair_add_      = &engine_.Get<ArmNeon3SameFpPairAdd>();
    neon_3same_fp_pair_min_max_  = &engine_.Get<ArmNeon3SameFpPairMinMax>();
    neon_3same_fp_recip_step_    = &engine_.Get<ArmNeon3SameFpRecipStep>();
    vfp_                         = &engine_.Get<ArmVfp>();
    vfp_mem_                     = &engine_.Get<ArmVfpMemory>();
}

}
