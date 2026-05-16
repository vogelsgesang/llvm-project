// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm < %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-unknown -emit-llvm < %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-windows-msvc -emit-llvm < %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-windows-msvc -emit-llvm < %s | FileCheck %s

// The coro_handle_fn calling convention attribute lowers to LLVM's
// fastcc, which is the same calling convention the LLVM coroutine
// passes use for the cloned resume/destroy bodies and their indirect
// call sites.
void do_resume(void *p) __attribute__((coro_handle_fn)) {
  // CHECK-LABEL: define {{(dso_local )?}}fastcc void @{{"?do_resume@@YTXPEAX@Z"|do_resume}}(
}

void do_destroy(void *p) __attribute__((coro_handle_fn)) {
  // CHECK-LABEL: define {{(dso_local )?}}fastcc void @{{"?do_destroy@@YTXPEAX@Z"|do_destroy}}(
}

typedef void (*resume_t)(void *) __attribute__((coro_handle_fn));

void call_through_pointer(resume_t r, void *p) {
  r(p);
  // CHECK: call fastcc void {{%[0-9a-zA-Z._]+}}({{i8|ptr}}{{[^,)]*}}
}
