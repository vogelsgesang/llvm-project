// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s

// The Itanium ABI doesn't have a vendor mangling for fastcc-like calling
// conventions, so these should round-trip through normal mangling.

void __attribute__((coro_handle_fn)) f(void *p) {}
// CHECK-DAG: define {{(dso_local )?}}fastcc void @_Z1fPv(

void (__attribute__((coro_handle_fn)) *fp)(void *);
// CHECK-DAG: @fp = {{(dso_local )?}}global ptr null

namespace {
void __attribute__((coro_handle_fn)) __attribute__((__used__)) g(void *p) {}
}
// CHECK-DAG: define {{(dso_local )?}}internal fastcc void @_ZN12_GLOBAL__N_11gEPv(

namespace ns {
void __attribute__((coro_handle_fn)) h(void *p) {}
}
// CHECK-DAG: define {{(dso_local )?}}fastcc void @_ZN2ns1hEPv(

struct S {
  void __attribute__((coro_handle_fn)) m(void *p);
};
void S::m(void *p) {}
// CHECK-DAG: define {{(dso_local )?}}fastcc void @_ZN1S1mEPv(
