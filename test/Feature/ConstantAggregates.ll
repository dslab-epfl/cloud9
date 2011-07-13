; RUN: llvm-as %s -o %t1.bc
; RUN: %klee --no-output --exit-on-error %t1.bc

; ModuleID = 'IntrinsicGenerator.c.tmp1.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-f128:128:128-n8:16:32:64"
target triple = "x86_64-unknown-linux-gnu"


@.str = private unnamed_addr constant [22 x i8] c"ConstantAggregates.ll\00", align 8
@__PRETTY_FUNCTION__.1461 = internal unnamed_addr constant [5 x i8] c"main\00"
@.str1 = private unnamed_addr constant [31 x i8] c"ConstantStruct - first element\00", align 8
@.str2 = private unnamed_addr constant [32 x i8] c"ConstantStruct - second element\00", align 8
@.str3 = private unnamed_addr constant [31 x i8] c"ConstantVector - first element\00", align 8
@.str4 = private unnamed_addr constant [32 x i8] c"ConstantVector - second element\00", align 8

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

; ConstantStruct
	%v0 = extractvalue {i64, i64} {i64 12345, i64 67890}, 0
  %0 = icmp eq i64 %v0, 12345
  br i1 %0, label %bb1, label %bb

bb:                                               ; preds = %entry
  call void @__assert_fail(i8* getelementptr inbounds ([31 x i8]* @.str1, i64 0, i64 0), i8* getelementptr inbounds ([22 x i8]* @.str, i64 0, i64 0), i32 10, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

bb1:                                               ; preds = %enty
	%v1 = extractvalue {i64, i64} {i64 12345, i64 67890}, 1
  %1 = icmp eq i64 %v1, 67890
  br i1 %1, label %bb3, label %bb2

bb2:                                               ; preds = %bb1
  call void @__assert_fail(i8* getelementptr inbounds ([32 x i8]* @.str2, i64 0, i64 0), i8* getelementptr inbounds ([22 x i8]* @.str, i64 0, i64 0), i32 10, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

; ConstantVector
bb3:                                               ; preds = %bb1
	%v2 = extractelement <2 x i64> <i64 12345, i64 67890>, i32 0
  %2 = icmp eq i64 %v2, 12345
  br i1 %2, label %bb5, label %bb4

bb4:                                               ; preds = %entry
  call void @__assert_fail(i8* getelementptr inbounds ([31 x i8]* @.str1, i64 0, i64 0), i8* getelementptr inbounds ([22 x i8]* @.str, i64 0, i64 0), i32 10, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

bb5:                                               ; preds = %enty
	%v3 = extractelement <2 x i64> <i64 12345, i64 67890>, i32 1
  %3 = icmp eq i64 %v3, 67890
  br i1 %3, label %return, label %bb6

bb6:                                               ; preds = %bb1
  call void @__assert_fail(i8* getelementptr inbounds ([32 x i8]* @.str2, i64 0, i64 0), i8* getelementptr inbounds ([22 x i8]* @.str, i64 0, i64 0), i32 10, i8* getelementptr inbounds ([5 x i8]* @__PRETTY_FUNCTION__.1461, i64 0, i64 0)) noreturn nounwind
  unreachable

return:                                           ; preds = %bb1
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
