; Issue #58397: when the `coro-legacy-promise-layout` function attribute is
; present (Clang sets this for `-fclang-abi-compat <= 21`), CoroFrame /
; CoroEarly fall back to the pre-fix layout: promise at
; alignTo(2*ptrsize, PromiseAlign), no padding before resume_fn.
;
; This is the cross-TU compatibility hatch — frames laid out by old Clang
; releases still link against TUs translated with new Clang under the legacy
; flag.
;
; RUN: opt < %s -passes='cgscc(coro-split),simplifycfg,early-cse' -S \
; RUN:   | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%align64 = type { [64 x i8] }

declare void @use(ptr)
declare ptr @llvm.coro.free(token, ptr)
declare i32 @llvm.coro.size.i32()
declare i8  @llvm.coro.suspend(token, i1)
declare token @llvm.coro.id(i32, ptr, ptr, ptr)
declare ptr  @llvm.coro.begin(token, ptr)
declare void @llvm.coro.end(ptr, i1, token)
declare noalias ptr @malloc(i32)
declare void @free(ptr)

define ptr @f_legacy() presplitcoroutine "coro-legacy-promise-layout" {
entry:
  %p = alloca %align64, align 64
  %id = call token @llvm.coro.id(i32 0, ptr %p, ptr @f_legacy, ptr null)
  %size = call i32 @llvm.coro.size.i32()
  %mem = call ptr @malloc(i32 %size)
  %hdl = call ptr @llvm.coro.begin(token %id, ptr %mem)
  call void @use(ptr %p)
  %s = call i8 @llvm.coro.suspend(token none, i1 false)
  switch i8 %s, label %suspend [i8 0, label %resume i8 1, label %cleanup]
resume:
  call void @use(ptr %p)
  br label %cleanup
cleanup:
  %dealloc = call ptr @llvm.coro.free(token %id, ptr %hdl)
  call void @free(ptr %dealloc)
  br label %suspend
suspend:
  call void @llvm.coro.end(ptr %hdl, i1 0, token none)
  ret ptr %hdl
}

; Legacy path: no padding GEP, no null-preserving select, promise placed at
; alignTo(2*ptrsize, 64) = 64 (NOT 16 from frame_ptr).
; CHECK-LABEL: define ptr @f_legacy()
; CHECK:         [[ALLOC:%.*]] = call ptr @malloc(i32 128)
; CHECK-NEXT:    [[HDL:%.*]] = call noalias nonnull ptr @llvm.coro.begin(token {{.*}}, ptr [[ALLOC]])
; CHECK-NEXT:    store ptr @f_legacy.resume, ptr [[HDL]]
; CHECK-NEXT:    [[DESTROY_ADDR:%.*]] = getelementptr inbounds i8, ptr [[HDL]], i64 8
; CHECK-NEXT:    store ptr @f_legacy.destroy, ptr [[DESTROY_ADDR]]
; Legacy: promise at +64 (alignTo(16, 64)), not +16.
; CHECK-NEXT:    [[PROMISE:%.*]] = getelementptr inbounds i8, ptr [[HDL]], i64 64
; CHECK-NEXT:    call void @use(ptr [[PROMISE]])
; The handle is returned unchanged — no frame_ptr GEP at all.
; CHECK:         ret ptr [[HDL]]

; CHECK-LABEL: define internal void @f_legacy.resume(
; Legacy: frame_ptr == alloc_ptr, full alignment + dereferenceable preserved.
; CHECK-SAME:    ptr noundef nonnull align 64 dereferenceable(128) [[HDL:%.*]])
; Legacy: free uses the param directly, no -Padding GEP, no select.
; CHECK:         [[MEM:%.*]] = call ptr @llvm.coro.free(token poison, ptr [[HDL]])
; CHECK-NEXT:    call void @free(ptr [[MEM]])
; CHECK-NOT:     select
