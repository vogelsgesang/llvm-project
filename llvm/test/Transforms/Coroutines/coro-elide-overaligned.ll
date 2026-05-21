; CoroElide for over-aligned-promise frames.
;
; CoroSplit emits the resume/destroy clones of an over-aligned-promise
; coroutine with the frame_ptr param degraded to handle-relative geometry
; (only `FrameSize - HandleOffset` bytes are dereferenceable, alignment is
; `min(FrameAlign, ctz(HandleOffset))`). The full allocation geometry is
; stashed in the string-valued `"coro-frame-size"` / `"coro-frame-align"`
; param attributes on the handle argument. CoroElide must:
;
;   1. Read those string param attrs to determine the *full* alloca size
;      and alignment, NOT the degraded `dereferenceable`/`align`.
;   2. Elide the heap allocation (no `@CustomAlloc`, no `@CustomFree`).
;   3. Swap the destroy clone for the cleanup clone (skips the dealloc).
;   4. Preserve the post-CoroFrame `+HandleOffset` GEP, which now turns
;      the alloca base into the handle.
;
; This file also covers the BUG-3 hazard: the destroy → cleanup swap must
; only happen when elision actually succeeds. If `getFrameLayout` cannot
; recover sizes (no string attrs *and* no `dereferenceable` either), the
; coroutine must keep the heap-freeing destroy clone.
;
; RUN: opt < %s -S -passes='cgscc(inline,function(coro-elide,simplifycfg))' | FileCheck %s

declare void @print(i32) nounwind
declare ptr @CustomAlloc(i32, i32)
declare void @CustomFree(ptr)

; Over-aligned (PromiseAlign=64) coroutine. HandleOffset = 48,
; FrameSize = 192, FrameAlign = 64.
define fastcc void @over.resume(
    ptr noundef nonnull align 16 dereferenceable(144)
        "coro-frame-size"="192" "coro-frame-align"="64" %hdl) {
  tail call void @print(i32 0)
  ret void
}
define fastcc void @over.destroy(ptr %hdl) {
  %alloc = getelementptr inbounds i8, ptr %hdl, i64 -48
  tail call void @CustomFree(ptr %alloc)
  ret void
}
define fastcc void @over.cleanup(ptr %hdl) {
  ret void
}
@over.resumers = internal constant [3 x ptr]
    [ptr @over.resume, ptr @over.destroy, ptr @over.cleanup]

define ptr @over() personality ptr null {
entry:
  %id = call token @llvm.coro.id(i32 0, ptr null, ptr @over, ptr @over.resumers)
  %need.alloc = call i1 @llvm.coro.alloc(token %id)
  br i1 %need.alloc, label %dyn.alloc, label %coro.begin
dyn.alloc:
  %alloc = call ptr @CustomAlloc(i32 192, i32 64)
  br label %coro.begin
coro.begin:
  %phi = phi ptr [ null, %entry ], [ %alloc, %dyn.alloc ]
  %cb = call ptr @llvm.coro.begin(token %id, ptr %phi)
  %hdl = getelementptr inbounds i8, ptr %cb, i64 48
  ret ptr %hdl
}

; Elision must allocate the FULL 192-byte / 64-aligned frame, kill the
; CustomAlloc, and swap destroy → cleanup.
;
; CHECK-LABEL: define void @over.caller(
; CHECK: alloca [192 x i8], align 64
; CHECK-NOT: call ptr @CustomAlloc
; CHECK-NOT: call void @CustomFree
; CHECK: call fastcc void @over.resume(
; CHECK-NOT: call fastcc void @over.destroy(
; CHECK: call fastcc void @over.cleanup(
; CHECK-NOT: call fastcc void @over.destroy(
define void @over.caller() {
entry:
  %hdl = call ptr @over()
  %resume_fn = call ptr @llvm.coro.subfn.addr(ptr %hdl, i8 0)
  call fastcc void %resume_fn(ptr %hdl)
  %destroy_fn = call ptr @llvm.coro.subfn.addr(ptr %hdl, i8 1)
  call fastcc void %destroy_fn(ptr %hdl)
  ret void
}

; BUG-3 regression: a coroutine whose resume function carries no layout
; information at all (no `dereferenceable`, no `coro-frame-size`) cannot
; be elided. The destroy clone must NOT be swapped for the cleanup clone
; in that case, or the caller would leak the heap frame.
define fastcc void @unknown.resume(ptr %hdl) {
  tail call void @print(i32 0)
  ret void
}
define fastcc void @unknown.destroy(ptr %hdl) {
  tail call void @CustomFree(ptr %hdl)
  ret void
}
define fastcc void @unknown.cleanup(ptr %hdl) {
  ret void
}
@unknown.resumers = internal constant [3 x ptr]
    [ptr @unknown.resume, ptr @unknown.destroy, ptr @unknown.cleanup]

define ptr @unknown() {
entry:
  %id = call token @llvm.coro.id(i32 0, ptr null, ptr @unknown, ptr @unknown.resumers)
  %alloc = call i1 @llvm.coro.alloc(token %id)
  %hdl = call ptr @llvm.coro.begin(token %id, ptr null)
  ret ptr %hdl
}

; CHECK-LABEL: define void @unknown.caller(
; CHECK: call fastcc void @unknown.destroy(
; CHECK-NOT: call fastcc void @unknown.cleanup(
define void @unknown.caller() {
entry:
  %hdl = call ptr @unknown()
  %resume_fn = call ptr @llvm.coro.subfn.addr(ptr %hdl, i8 0)
  call fastcc void %resume_fn(ptr %hdl)
  %destroy_fn = call ptr @llvm.coro.subfn.addr(ptr %hdl, i8 1)
  call fastcc void %destroy_fn(ptr %hdl)
  ret void
}

declare token @llvm.coro.id(i32, ptr, ptr, ptr)
declare i1 @llvm.coro.alloc(token)
declare ptr @llvm.coro.begin(token, ptr)
declare ptr @llvm.coro.subfn.addr(ptr, i8)
