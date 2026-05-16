// RUN: %clang_cc1 -ast-print -triple x86_64-unknown-unknown %s -o - | FileCheck %s

void (__attribute__((coro_handle_fn)) *handle_fp)(void *);

// CHECK: __attribute__((coro_handle_fn)) void (*handle_fp)(void *);
