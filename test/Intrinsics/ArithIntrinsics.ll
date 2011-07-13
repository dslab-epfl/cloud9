; RUN: llvm-as %s -o %t1.bc
; RUN: %klee --no-output --exit-on-error %t1.bc

; ModuleID = 'IntrinsicGenerator.c.tmp1.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-f128:128:128-n8:16:32:64"
target triple = "x86_64-unknown-linux-gnu"


@.str = private unnamed_addr constant [19 x i8] c"ArithIntrinsics.ll\00", align 8
@__PRETTY_FUNCTION__.1461 = internal unnamed_addr constant [5 x i8] c"main\00"
@.str1 = private unnamed_addr constant [19 x i8] c"uadd when overflow\00", align 8
@.str2 = private unnamed_addr constant [22 x i8] c"uadd when no overflow\00", align 8
@.str3 = private unnamed_addr constant [23 x i8] c"powi positive exponent\00", align 1
@.str4 = private unnamed_addr constant [23 x i8] c"powi negative exponent\00", align 1

declare {i16, i1} @llvm.uadd.with.overflow.i16(i16 %a, i16 %b)
declare {i32, i1} @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
declare {i64, i1} @llvm.uadd.with.overflow.i64(i64 %a, i64 %b)
declare float @llvm.powi.f32(float %Val, i32 %power)
declare double @llvm.powi.f64(double %Val, i32 %power)

define i32 @main() nounwind {
entry:
  %retval = alloca i32
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  %f = alloca float
  %e = alloca i32
  %"alloca point" = bitcast i32 0 to i32

; Start of tests

; uadd.with.overflow

; uadd16
  %0 = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 -1, i16 1)
	%obit = extractvalue {i16, i1} %0, 1
  %1 = icmp eq i1 %obit, 1
  br i1 %1, label %bb1, label %bb

bb:                                               ; preds = %entry
  call void @__assert_fail(i8* getelementptr inbounds ([19 x i8]* @.str1, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 10, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

bb1:                                              ; preds = %entry
  %2 = call {i16, i1} @llvm.uadd.with.overflow.i16(i16 -2, i16 1)
	%obit2 = extractvalue {i16, i1} %2, 1
  %3 = icmp eq i1 %obit2, 1
  br i1 %3, label %bb2, label %bb3

bb2:                                              ; preds = %bb1
  call void @__assert_fail(i8* getelementptr inbounds ([22 x i8]* @.str2, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 14, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

; uadd32
bb3:                                               ; preds = %entry
  %4 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 -1, i32 1)
	%obit3 = extractvalue {i32, i1} %4, 1
  %5 = icmp eq i1 %obit3, 1
  br i1 %5, label %bb5, label %bb4

bb4:                                               ; preds = %entry
  call void @__assert_fail(i8* getelementptr inbounds ([19 x i8]* @.str1, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 10, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

bb5:                                              ; preds = %entry
  %6 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 -2, i32 1)
	%obit4 = extractvalue {i32, i1} %6, 1
  %7 = icmp eq i1 %obit4, 1
  br i1 %7, label %bb6, label %bb7

bb6:                                              ; preds = %bb1
  call void @__assert_fail(i8* getelementptr inbounds ([22 x i8]* @.str2, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 14, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

; uadd64
bb7:                                              ; preds = %bb1
  %8 = call {i64, i1} @llvm.uadd.with.overflow.i64(i64 -1, i64 1)
	%obit5 = extractvalue {i64, i1} %8, 1
  %9 = icmp eq i1 %obit5, 1
  br i1 %9, label %bb9, label %bb8

bb8:                                               ; preds = %entry
  call void @__assert_fail(i8* getelementptr inbounds ([19 x i8]* @.str1, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 10, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

bb9:                                              ; preds = %entry
  %10 = call {i64, i1} @llvm.uadd.with.overflow.i64(i64 -2, i64 1)
	%obit6 = extractvalue {i64, i1} %10, 1
  %11 = icmp eq i1 %obit6, 1
  br i1 %11, label %bb10, label %bbf

bb10:                                              ; preds = %bb1
  call void @__assert_fail(i8* getelementptr inbounds ([22 x i8]* @.str2, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 14, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

; powi

; powi.f32 
bbf:                                              ; preds = %bb1
  %f0 = call float @llvm.powi.f32(float 2.000000e+00, i32 5)
  %f1 = fcmp une float %f0, 3.200000e+01
  br i1 %f1, label %bbf1, label %bbf2

bbf1:                                              ; preds = %bb3
  call void @__assert_fail(i8* getelementptr inbounds ([23 x i8]* @.str3, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 18, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

bbf2:                                              ; preds = %bb1
  %f2 = call float @llvm.powi.f32(float 2.000000e+00, i32 -1)
  %f3 = fcmp une float %f2, 0.500000e+00
  br i1 %f3, label %bbf3, label %bbf4

bbf3:                                              ; preds = %bb3
  call void @__assert_fail(i8* getelementptr inbounds ([23 x i8]* @.str4, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 18, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

; powi.f64 
bbf4:                                              ; preds = %bb1
  %f4 = call double @llvm.powi.f64(double 2.000000e+00, i32 5)
  %f5 = fcmp une double %f4, 3.200000e+01
  br i1 %f5, label %bbf5, label %bbf6

bbf5:                                              ; preds = %bb3
  call void @__assert_fail(i8* getelementptr inbounds ([23 x i8]* @.str3, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 18, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

bbf6:                                              ; preds = %bb1
  %f6 = call double @llvm.powi.f64(double 2.000000e+00, i32 -1)
  %f7 = fcmp une double %f6, 0.500000e+00
  br i1 %f7, label %bbf7, label %return

bbf7:                                              ; preds = %bb3
  call void @__assert_fail(i8* getelementptr inbounds ([23 x i8]* @.str4, i64 0, i64 0), i8* getelementptr inbounds ([19 x i8]* @.str, i64 0, i64 0), i32 18, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

return:                                           ; preds = %bb3
  %retval6 = load i32* %retval
  ret i32 %retval6
}

declare void @__assert_fail(i8*, i8*, i32, i8*) noreturn nounwind

declare void @llvm.dbg.value(metadata, i64, metadata) nounwind readnone

!llvm.dbg.sp = !{!0, !9}
!llvm.dbg.lv.memcpy = !{!18, !19, !20, !21, !25}
!llvm.dbg.lv.memmove = !{!28, !29, !30, !31, !35}

!0 = metadata !{i32 589870, i32 0, metadata !1, metadata !"memcpy", metadata !"memcpy", metadata !"memcpy", metadata !1, i32 12, metadata !3, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 true, null} ; [ DW_TAG_subprogram ]
!1 = metadata !{i32 589865, metadata !"memcpy.c", metadata !"/home/istvan/Work/cloud9-cpp/runtime/Intrinsic/", metadata !2} ; [ DW_TAG_file_type ]
!2 = metadata !{i32 589841, i32 0, i32 1, metadata !"memcpy.c", metadata !"/home/istvan/Work/cloud9-cpp/runtime/Intrinsic/", metadata !"4.2.1 (Based on Apple Inc. build 5658) (LLVM build 2.9)", i1 true, i1 true, metadata !"", i32 0} ; [ DW_TAG_compile_un
!3 = metadata !{i32 589845, metadata !1, metadata !"", metadata !1, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !4, i32 0, null} ; [ DW_TAG_subroutine_type ]
!4 = metadata !{metadata !5, metadata !5, metadata !5, metadata !6}
!5 = metadata !{i32 589839, metadata !1, metadata !"", metadata !1, i32 0, i64 64, i64 64, i64 0, i32 0, null} ; [ DW_TAG_pointer_type ]
!6 = metadata !{i32 589846, metadata !7, metadata !"size_t", metadata !7, i32 326, i64 0, i64 0, i64 0, i32 0, metadata !8} ; [ DW_TAG_typedef ]
!7 = metadata !{i32 589865, metadata !"stddef.h", metadata !"/home/istvan/tools/bin/../lib/gcc/x86_64-unknown-linux-gnu/4.2.1/include", metadata !2} ; [ DW_TAG_file_type ]
!8 = metadata !{i32 589860, metadata !1, metadata !"long unsigned int", metadata !1, i32 0, i64 64, i64 64, i64 0, i32 0, i32 7} ; [ DW_TAG_base_type ]
!9 = metadata !{i32 589870, i32 0, metadata !10, metadata !"memmove", metadata !"memmove", metadata !"memmove", metadata !10, i32 12, metadata !12, i1 false, i1 true, i32 0, i32 0, null, i32 256, i1 true, null} ; [ DW_TAG_subprogram ]
!10 = metadata !{i32 589865, metadata !"memmove.c", metadata !"/home/istvan/Work/cloud9-cpp/runtime/Intrinsic/", metadata !11} ; [ DW_TAG_file_type ]
!11 = metadata !{i32 589841, i32 0, i32 1, metadata !"memmove.c", metadata !"/home/istvan/Work/cloud9-cpp/runtime/Intrinsic/", metadata !"4.2.1 (Based on Apple Inc. build 5658) (LLVM build 2.9)", i1 true, i1 true, metadata !"", i32 0} ; [ DW_TAG_compile_
!12 = metadata !{i32 589845, metadata !10, metadata !"", metadata !10, i32 0, i64 0, i64 0, i64 0, i32 0, null, metadata !13, i32 0, null} ; [ DW_TAG_subroutine_type ]
!13 = metadata !{metadata !14, metadata !14, metadata !14, metadata !15}
!14 = metadata !{i32 589839, metadata !10, metadata !"", metadata !10, i32 0, i64 64, i64 64, i64 0, i32 0, null} ; [ DW_TAG_pointer_type ]
!15 = metadata !{i32 589846, metadata !16, metadata !"size_t", metadata !16, i32 326, i64 0, i64 0, i64 0, i32 0, metadata !17} ; [ DW_TAG_typedef ]
!16 = metadata !{i32 589865, metadata !"stddef.h", metadata !"/home/istvan/tools/bin/../lib/gcc/x86_64-unknown-linux-gnu/4.2.1/include", metadata !11} ; [ DW_TAG_file_type ]
!17 = metadata !{i32 589860, metadata !10, metadata !"long unsigned int", metadata !10, i32 0, i64 64, i64 64, i64 0, i32 0, i32 7} ; [ DW_TAG_base_type ]
!18 = metadata !{i32 590081, metadata !0, metadata !"destaddr", metadata !1, i32 12, metadata !5, i32 0} ; [ DW_TAG_arg_variable ]
!19 = metadata !{i32 590081, metadata !0, metadata !"srcaddr", metadata !1, i32 12, metadata !5, i32 0} ; [ DW_TAG_arg_variable ]
!20 = metadata !{i32 590081, metadata !0, metadata !"len", metadata !1, i32 12, metadata !6, i32 0} ; [ DW_TAG_arg_variable ]
!21 = metadata !{i32 590080, metadata !22, metadata !"dest", metadata !1, i32 13, metadata !23, i32 0} ; [ DW_TAG_auto_variable ]
!22 = metadata !{i32 589835, metadata !0, i32 12, i32 0, metadata !1, i32 0} ; [ DW_TAG_lexical_block ]
!23 = metadata !{i32 589839, metadata !1, metadata !"", metadata !1, i32 0, i64 64, i64 64, i64 0, i32 0, metadata !24} ; [ DW_TAG_pointer_type ]
!24 = metadata !{i32 589860, metadata !1, metadata !"char", metadata !1, i32 0, i64 8, i64 8, i64 0, i32 0, i32 6} ; [ DW_TAG_base_type ]
!25 = metadata !{i32 590080, metadata !22, metadata !"src", metadata !1, i32 14, metadata !26, i32 0} ; [ DW_TAG_auto_variable ]
!26 = metadata !{i32 589839, metadata !1, metadata !"", metadata !1, i32 0, i64 64, i64 64, i64 0, i32 0, metadata !27} ; [ DW_TAG_pointer_type ]
!27 = metadata !{i32 589862, metadata !1, metadata !"", metadata !1, i32 0, i64 8, i64 8, i64 0, i32 0, metadata !24} ; [ DW_TAG_const_type ]
!28 = metadata !{i32 590081, metadata !9, metadata !"dst", metadata !10, i32 12, metadata !14, i32 0} ; [ DW_TAG_arg_variable ]
!29 = metadata !{i32 590081, metadata !9, metadata !"src", metadata !10, i32 12, metadata !14, i32 0} ; [ DW_TAG_arg_variable ]
!30 = metadata !{i32 590081, metadata !9, metadata !"count", metadata !10, i32 12, metadata !15, i32 0} ; [ DW_TAG_arg_variable ]
!31 = metadata !{i32 590080, metadata !32, metadata !"a", metadata !10, i32 13, metadata !33, i32 0} ; [ DW_TAG_auto_variable ]
!32 = metadata !{i32 589835, metadata !9, i32 12, i32 0, metadata !10, i32 0} ; [ DW_TAG_lexical_block ]
!33 = metadata !{i32 589839, metadata !10, metadata !"", metadata !10, i32 0, i64 64, i64 64, i64 0, i32 0, metadata !34} ; [ DW_TAG_pointer_type ]
!34 = metadata !{i32 589860, metadata !10, metadata !"char", metadata !10, i32 0, i64 8, i64 8, i64 0, i32 0, i32 6} ; [ DW_TAG_base_type ]
!35 = metadata !{i32 590080, metadata !32, metadata !"b", metadata !10, i32 14, metadata !36, i32 0} ; [ DW_TAG_auto_variable ]
!36 = metadata !{i32 589839, metadata !10, metadata !"", metadata !10, i32 0, i64 64, i64 64, i64 0, i32 0, metadata !37} ; [ DW_TAG_pointer_type ]
!37 = metadata !{i32 589862, metadata !10, metadata !"", metadata !10, i32 0, i64 8, i64 8, i64 0, i32 0, metadata !34} ; [ DW_TAG_const_type ]
