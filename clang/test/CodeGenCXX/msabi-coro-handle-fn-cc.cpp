// RUN: %clang_cc1 -triple x86_64-unknown-windows-msvc -fdeclspec -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-windows-msvc -fdeclspec -emit-llvm %s -o - | FileCheck %s

void __attribute__((__coro_handle_fn__)) f(void *p) {}
// CHECK-DAG: @"?f@@YTXPEAX@Z"

void (__attribute__((__coro_handle_fn__)) *p)(void *);
// CHECK-DAG: @"?p@@3P6TXPEAX@ZEA

namespace {
void __attribute__((__coro_handle_fn__)) __attribute__((__used__)) f(void *q) { }
}
// CHECK-DAG: @"?f@?A0x{{[^@]*}}@@YTXPEAX@Z"

namespace n {
void __attribute__((__coro_handle_fn__)) f(void *q) {}
}
// CHECK-DAG: @"?f@n@@YTXPEAX@Z"

struct __declspec(dllexport) S {
  S(const S &) = delete;
  S & operator=(const S &) = delete;
  void __attribute__((__coro_handle_fn__)) m(void *q) { }
};
// CHECK-DAG: @"?m@S@@QEATXPEAX@Z"

void g(void (__attribute__((__coro_handle_fn__))(void *))) {}
// CHECK-DAG: @"?g@@YAXP6TXPEAX@Z@Z"
