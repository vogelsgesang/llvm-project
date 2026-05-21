; Regression test for the BUG-1 hazard found during code review of the
; issue #58397 fix: when a switch-ABI coroutine has no suspend points,
; CoroSplit takes the `handleNoSuspendCoroutine` path which calls
; `coro::elideCoroFree(CoroBegin)` to rewrite each `coro.free` to null and
; then replaces `coro.begin` with a stack alloca.
;
; Before the fix to `coro::elideCoroFree`, that routine only walked
; CoroBegin->users(). The +HandleOffset GEP inserted by buildCoroutineFrame
; for over-aligned promises sits *between* CoroBegin and `coro.free`, so the
; coro.free was never rewritten to null. CoroSplit then replaced CoroBegin
; with the alloca, the null-preserving select unwrapped to
; `alloca + Padding - Padding == alloca`, and the user's
; `if (mem) operator_delete(mem)` ended up `free()`ing the stack alloca.
;
; The fix: `coro::elideCoroFree` now also walks one hop through GEP users of
; the frame pointer, so over-aligned promises take the same elide path as
; aligned ones.
;
; What we check: after the no-suspend lowering, the `@free` call site in
; the ramp receives the null constant (not a stack pointer). The
; null-preserving select must constant-fold to null on this path.
;
; RUN: opt < %s -passes='cgscc(coro-split),simplifycfg,early-cse' -S \
; RUN:   | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%align64 = type { [64 x i8] }

declare void @use(ptr)
declare ptr @llvm.coro.free(token, ptr)
declare i32 @llvm.coro.size.i32()
declare token @llvm.coro.id(i32, ptr, ptr, ptr)
declare i1   @llvm.coro.alloc(token)
declare ptr  @llvm.coro.begin(token, ptr)
declare void @llvm.coro.end(ptr, i1, token)
declare noalias ptr @malloc(i32)
declare void @free(ptr)

define ptr @f_no_suspend() presplitcoroutine {
entry:
  %p = alloca %align64, align 64
  %id = call token @llvm.coro.id(i32 0, ptr %p, ptr @f_no_suspend, ptr null)
  %need_alloc = call i1 @llvm.coro.alloc(token %id)
  br i1 %need_alloc, label %dyn_alloc, label %coro_begin
dyn_alloc:
  %size = call i32 @llvm.coro.size.i32()
  %mem = call ptr @malloc(i32 %size)
  br label %coro_begin
coro_begin:
  %phi = phi ptr [ null, %entry ], [ %mem, %dyn_alloc ]
  %hdl = call ptr @llvm.coro.begin(token %id, ptr %phi)
  call void @use(ptr %p)
  br label %cleanup
cleanup:
  %dealloc = call ptr @llvm.coro.free(token %id, ptr %hdl)
  %nonnull = icmp ne ptr %dealloc, null
  br i1 %nonnull, label %do_free, label %end
do_free:
  call void @free(ptr %dealloc)
  br label %end
end:
  call void @llvm.coro.end(ptr %hdl, i1 false, token none)
  ret ptr %hdl
}

; CHECK-LABEL: define ptr @f_no_suspend()
; The frame should be a single stack alloca, with no `malloc`. The
; `coro.free` result must constant-fold to null (so the user's
; `if (mem) free(mem)` skips the deallocation) — *not* to the stack alloca
; offset by -Padding, which would `free()` the caller's stack.
; CHECK-NOT: call ptr @malloc
; CHECK-NOT: call void @free(ptr %{{[^)]*}})
; CHECK:     ret ptr
