; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=thumbv8.1m.main -mattr=+mve.fp -verify-machineinstrs -o - %s | FileCheck %s

define arm_aapcs_vfpcc <4 x i32> @test_vaddq_u32(<4 x i32> %a, <4 x i32> %b) {
; CHECK-LABEL: test_vaddq_u32:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vadd.i32 q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %0 = add <4 x i32> %b, %a
  ret <4 x i32> %0
}

define arm_aapcs_vfpcc <4 x float> @test_vaddq_f32(<4 x float> %a, <4 x float> %b) {
; CHECK-LABEL: test_vaddq_f32:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vadd.f32 q0, q1, q0
; CHECK-NEXT:    bx lr
entry:
  %0 = fadd <4 x float> %b, %a
  ret <4 x float> %0
}

define arm_aapcs_vfpcc <8 x half> @test_vsubq_f16(<8 x half> %a, <8 x half> %b) {
; CHECK-LABEL: test_vsubq_f16:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vsub.f16 q0, q0, q1
; CHECK-NEXT:    bx lr
entry:
  %0 = fsub <8 x half> %a, %b
  ret <8 x half> %0
}

define arm_aapcs_vfpcc <8 x i16> @test_vsubq_s16(<8 x i16> %a, <8 x i16> %b) {
; CHECK-LABEL: test_vsubq_s16:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vsub.i16 q0, q0, q1
; CHECK-NEXT:    bx lr
entry:
  %0 = sub <8 x i16> %a, %b
  ret <8 x i16> %0
}

define arm_aapcs_vfpcc <16 x i8> @test_vaddq_m_s8(<16 x i8> %inactive, <16 x i8> %a, <16 x i8> %b, i16 zeroext %p) {
; CHECK-LABEL: test_vaddq_m_s8:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vmsr p0, r0
; CHECK-NEXT:    vpst
; CHECK-NEXT:    vaddt.i8 q0, q1, q2
; CHECK-NEXT:    bx lr
entry:
  %0 = zext i16 %p to i32
  %1 = tail call <16 x i1> @llvm.arm.mve.pred.i2v.v16i1(i32 %0)
  %2 = tail call <16 x i8> @llvm.arm.mve.add.predicated.v16i8.v16i1(<16 x i8> %a, <16 x i8> %b, <16 x i1> %1, <16 x i8> %inactive)
  ret <16 x i8> %2
}

declare <16 x i1> @llvm.arm.mve.pred.i2v.v16i1(i32)

declare <16 x i8> @llvm.arm.mve.add.predicated.v16i8.v16i1(<16 x i8>, <16 x i8>, <16 x i1>, <16 x i8>)

define arm_aapcs_vfpcc <8 x half> @test_vaddq_m_f16(<8 x half> %inactive, <8 x half> %a, <8 x half> %b, i16 zeroext %p) {
; CHECK-LABEL: test_vaddq_m_f16:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vmsr p0, r0
; CHECK-NEXT:    vpst
; CHECK-NEXT:    vaddt.f16 q0, q1, q2
; CHECK-NEXT:    bx lr
entry:
  %0 = zext i16 %p to i32
  %1 = tail call <8 x i1> @llvm.arm.mve.pred.i2v.v8i1(i32 %0)
  %2 = tail call <8 x half> @llvm.arm.mve.add.predicated.v8f16.v8i1(<8 x half> %a, <8 x half> %b, <8 x i1> %1, <8 x half> %inactive)
  ret <8 x half> %2
}

declare <8 x i1> @llvm.arm.mve.pred.i2v.v8i1(i32)

declare <8 x half> @llvm.arm.mve.add.predicated.v8f16.v8i1(<8 x half>, <8 x half>, <8 x i1>, <8 x half>)

define arm_aapcs_vfpcc <4 x float> @test_vsubq_m_f32(<4 x float> %inactive, <4 x float> %a, <4 x float> %b, i16 zeroext %p) {
; CHECK-LABEL: test_vsubq_m_f32:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vmsr p0, r0
; CHECK-NEXT:    vpst
; CHECK-NEXT:    vsubt.f32 q0, q1, q2
; CHECK-NEXT:    bx lr
entry:
  %0 = zext i16 %p to i32
  %1 = tail call <4 x i1> @llvm.arm.mve.pred.i2v.v4i1(i32 %0)
  %2 = tail call <4 x float> @llvm.arm.mve.sub.predicated.v4f32.v4i1(<4 x float> %a, <4 x float> %b, <4 x i1> %1, <4 x float> %inactive)
  ret <4 x float> %2
}

declare <4 x i1> @llvm.arm.mve.pred.i2v.v4i1(i32)

declare <4 x float> @llvm.arm.mve.sub.predicated.v4f32.v4i1(<4 x float>, <4 x float>, <4 x i1>, <4 x float>)

define arm_aapcs_vfpcc <4 x i32> @test_vsubq_m_u32(<4 x i32> %inactive, <4 x i32> %a, <4 x i32> %b, i16 zeroext %p) {
; CHECK-LABEL: test_vsubq_m_u32:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vmsr p0, r0
; CHECK-NEXT:    vpst
; CHECK-NEXT:    vsubt.i32 q0, q1, q2
; CHECK-NEXT:    bx lr
entry:
  %0 = zext i16 %p to i32
  %1 = tail call <4 x i1> @llvm.arm.mve.pred.i2v.v4i1(i32 %0)
  %2 = tail call <4 x i32> @llvm.arm.mve.sub.predicated.v4i32.v4i1(<4 x i32> %a, <4 x i32> %b, <4 x i1> %1, <4 x i32> %inactive)
  ret <4 x i32> %2
}

declare <4 x i32> @llvm.arm.mve.sub.predicated.v4i32.v4i1(<4 x i32>, <4 x i32>, <4 x i1>, <4 x i32>)
