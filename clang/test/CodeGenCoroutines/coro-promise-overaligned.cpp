// Issue #58397: the C++20 coroutine ABI requires the promise to live at
// offset 2*ptrsize from the coroutine handle, regardless of the promise's
// alignment. Before the fix, Clang/LLVM placed the promise at
// `alignTo(2*ptrsize, alignof(promise))`, breaking
// `coroutine_handle::promise()` / `from_promise()` for promises with
// alignment > 2*ptrsize across TU boundaries (and against ABI-aware tools).
//
// This test mirrors the godbolt reproducer in the issue
// (https://godbolt.org/z/WhG6z5czo) and exercises both the default
// (post-fix) layout and the legacy `-fclang-abi-compat=22` opt-out.
// (Clang 22 shipped with the buggy layout; the fix lands in Clang 23.)
//
// What we check:
//   - The ramp function carries `coro-legacy-promise-layout` *iff*
//     `-fclang-abi-compat <= 22`.
//   - A non-coroutine caller using `coroutine_handle::from_promise(...)`
//     ends up with a GEP of `-16` (= -2*ptrsize, fixed) or `-64`
//     (= -alignTo(16, 64), legacy) on the promise pointer.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fcoro-aligned-allocation -O1 -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefixes=COMMON,FIXED %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fcoro-aligned-allocation -fclang-abi-compat=22 -O1 -emit-llvm \
// RUN:   %s -o - | FileCheck --check-prefixes=COMMON,LEGACY %s

#include "Inputs/coroutine.h"

struct alignas(64) overaligned_promise_base {
  char pad[64];
};

struct task {
  struct promise_type : overaligned_promise_base {
    task get_return_object() { return {std::coroutine_handle<promise_type>::from_promise(*this)}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
  std::coroutine_handle<promise_type> h;
};

// COMMON-LABEL: define{{.*}} @ramp(
//   FIXED-NOT: "coro-legacy-promise-layout"
//  LEGACY-SAME: #[[#RAMP_ATTRS:]]
extern "C" task ramp() {
  co_return;
}

// Cross-TU pattern: lowers to a `__builtin_coro_promise(&p, 64, /*from*/1)`
// which becomes `gep(&p, -ABI_OFFSET)`. ABI_OFFSET = 16 in the fixed layout
// (the new default) and 64 in the legacy layout.
//
// COMMON-LABEL: define{{.*}} @caller(
//  FIXED: getelementptr inbounds i8, ptr %{{.*}}, i64 -16
// LEGACY: getelementptr inbounds i8, ptr %{{.*}}, i64 -64
extern "C" void *caller(task::promise_type &p) {
  return std::coroutine_handle<task::promise_type>::from_promise(p).address();
}

// LEGACY: attributes #[[#RAMP_ATTRS]] = {{.*}}"coro-legacy-promise-layout"
