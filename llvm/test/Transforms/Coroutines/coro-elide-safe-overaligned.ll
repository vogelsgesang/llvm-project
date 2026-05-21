; HALO (CoroAnnotationElide) for over-aligned-promise coroutines.
;
; CoroSplit emits a `.noalloc` ramp variant whose extra last parameter
; receives the *allocation pointer* (i.e. alloc_ptr, not the handle).
; Inside the ramp the post-CoroFrame `+HandleOffset` GEP off `coro.begin`
; — which CoroSplit rewrites to a GEP off the new parameter — produces
; the handle. The param attrs on that parameter therefore describe the
; full allocation (FrameSize, FrameAlign), not the degraded handle view,
; so CoroAnnotationElide can read `dereferenceable`/`align` directly and
; allocate enough bytes / alignment to cover the prefix.
;
; This test verifies the end-to-end chain for an over-aligned promise:
;   - CoroSplit emits `callee.noalloc(..., ptr align 64 dereferenceable(192))`.
;   - The .noalloc body GEPs `+48` off that arg to obtain the handle.
;   - CoroAnnotationElide allocates `[192 x i8] align 64` in the caller
;     and inlines the .noalloc ramp, eliding the heap alloc.
;
; RUN: opt < %s -S \
; RUN:   -passes='cgscc(coro-split),cgscc(coro-annotation-elide),simplifycfg' \
; RUN:   | FileCheck %s

%align64 = type { [64 x i8] }

declare void @use(ptr)
declare ptr @llvm.coro.free(token, ptr)
declare i32 @llvm.coro.size.i32()
declare i32 @llvm.coro.align.i32()
declare i8  @llvm.coro.suspend(token, i1)
declare token @llvm.coro.id(i32, ptr, ptr, ptr)
declare i1   @llvm.coro.alloc(token)
declare ptr  @llvm.coro.begin(token, ptr)
declare void @llvm.coro.end(ptr, i1, token)
declare noalias ptr @malloc(i32)
declare void @free(ptr)

define ptr @callee() presplitcoroutine {
entry:
  %p = alloca %align64, align 64
  %id = call token @llvm.coro.id(i32 0, ptr %p, ptr @callee, ptr null)
  %need.alloc = call i1 @llvm.coro.alloc(token %id)
  br i1 %need.alloc, label %dyn.alloc, label %coro.begin
dyn.alloc:
  %size = call i32 @llvm.coro.size.i32()
  %mem = call ptr @malloc(i32 %size)
  br label %coro.begin
coro.begin:
  %phi = phi ptr [ null, %entry ], [ %mem, %dyn.alloc ]
  %hdl = call ptr @llvm.coro.begin(token %id, ptr %phi)
  call void @use(ptr %p)
  %s = call i8 @llvm.coro.suspend(token none, i1 false)
  switch i8 %s, label %suspend [i8 0, label %resume i8 1, label %cleanup]
resume:
  call void @use(ptr %p)
  br label %cleanup
cleanup:
  %dealloc = call ptr @llvm.coro.free(token %id, ptr %hdl)
  %need.free = icmp ne ptr %dealloc, null
  br i1 %need.free, label %do.free, label %suspend
do.free:
  call void @free(ptr %dealloc)
  br label %suspend
suspend:
  call void @llvm.coro.end(ptr %hdl, i1 0, token none)
  ret ptr %hdl
}

define ptr @caller() presplitcoroutine {
entry:
  %task = call ptr @callee() coro_elide_safe
  ret ptr %task
}

; HALO must allocate [192 x i8] align 64 — covering the 48-byte prefix
; *and* the payload — and inline the .noalloc ramp. Bound the CHECK-NOT
; scope to the @caller body with the trailing `ret ptr` so we don't
; accidentally match `@free` calls inside the resume/destroy clones below.
;
; CHECK-LABEL: define ptr @caller()
; CHECK: alloca [192 x i8], align 64
; CHECK-NOT: call ptr @malloc
; CHECK-NOT: call void @free
; CHECK: ret ptr

; The .noalloc variant's frame-ptr param must advertise the *full* 192-byte
; / 64-byte-aligned allocation (not the degraded handle view), so HALO can
; recreate it. No `coro-frame-size`/`coro-frame-align` string param attrs
; needed here — the .noalloc param IS alloc_ptr (not the handle), so the
; standard `align`/`dereferenceable` already convey full info.
;
; CHECK-LABEL: define internal ptr @callee.noalloc(
; CHECK-SAME: ptr noundef nonnull align 64 dereferenceable(192) %{{.*}}) {
; CHECK-NOT: "coro-frame-size"
; CHECK-NOT: "coro-frame-align"
