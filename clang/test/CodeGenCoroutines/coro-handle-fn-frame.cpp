// The point of this test: with [[clang::coro_handle_fn]] the user-written
// resume/destroy functions are emitted with LLVM's fastcc, matching the
// calling convention CoroEarly forces on indirect resume/destroy call
// sites. When the optimizer is able to fold the resume function pointer to
// a direct callee, the CC must match so InstCombine does not lower the
// call to `unreachable`.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 \
// RUN:   -emit-llvm %s -o - | FileCheck %s

#include "Inputs/coroutine.h"

typedef void (*coro_fn_t)(void *) [[clang::coro_handle_fn]];

struct frame {
  coro_fn_t resume;
  coro_fn_t destroy;
};

[[clang::coro_handle_fn]] static void do_resume(void *p) {
  // Touch the parameter so codegen doesn't drop everything.
  asm volatile("" : : "r"(p) : "memory");
}

[[clang::coro_handle_fn]] static void do_destroy(void *p) {
  asm volatile("" : : "r"(p) : "memory");
}

// Static frame whose resume/destroy slots have known compile-time values
// of do_resume / do_destroy. This is the configuration where the optimizer
// can constant-fold the indirect call to a direct call -- and where the
// pre-fix bug surfaced as InstCombine turning the call into `unreachable`.
static frame g_frame = {&do_resume, &do_destroy};

extern "C" void resume_known() {
  std::coroutine_handle<>::from_address(&g_frame).resume();
}

extern "C" void destroy_known() {
  std::coroutine_handle<>::from_address(&g_frame).destroy();
}

// The resume/destroy call sites use fastcc and survive optimization
// instead of being lowered to `unreachable` by InstCombine. We don't care
// whether the call is direct (devirtualized) or indirect; the important
// invariant is that the call still exists and has the correct CC.
// CHECK-LABEL: define {{(dso_local )?}}void @resume_known(
// CHECK-NOT:     unreachable
// CHECK:         {{call|tail call|musttail call}} fastcc void
// CHECK-NOT:     unreachable
// CHECK:       ret void

// CHECK-LABEL: define {{(dso_local )?}}void @destroy_known(
// CHECK-NOT:     unreachable
// CHECK:         {{call|tail call|musttail call}} fastcc void
// CHECK-NOT:     unreachable
// CHECK:       ret void

// do_resume / do_destroy are emitted with fastcc, matching the CC the
// coroutine lowering forces on the indirect call sites.
// CHECK-DAG: define {{(dso_local )?}}internal fastcc void @_ZL9do_resumePv(
// CHECK-DAG: define {{(dso_local )?}}internal fastcc void @_ZL10do_destroyPv(
