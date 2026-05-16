// RUN: %clang_cc1 %s -fsyntax-only -triple x86_64-unknown-unknown -verify
// RUN: %clang_cc1 %s -fsyntax-only -triple aarch64-unknown-unknown -verify

typedef void typedef_fun_t(void *);

void __attribute__((coro_handle_fn)) good(void *ptr) {
}

void __attribute__((coro_handle_fn(1))) good1(void *ptr) { // expected-error {{'coro_handle_fn' attribute takes no arguments}}
}

void (__attribute__((coro_handle_fn)) *pgood1)(void *) = good;

void (__attribute__((cdecl)) *pgood2)(void *) = good; // expected-error {{cannot initialize a variable of type 'void (*)(void *) __attribute__((cdecl))' with an lvalue of type '__attribute__((coro_handle_fn)) void (void *)'}}
void (*pgood3)(void *) = good; // expected-error {{cannot initialize a variable of type 'void (*)(void *)' with an lvalue of type '__attribute__((coro_handle_fn)) void (void *)'}}

typedef_fun_t typedef_fun_good; // expected-note {{previous declaration is here}}
void __attribute__((coro_handle_fn)) typedef_fun_good(void *x) { } // expected-error {{function declared 'coro_handle_fn' here was previously declared without calling convention}}

struct type_test_good {} __attribute__((coro_handle_fn));  // expected-warning {{'coro_handle_fn' attribute only applies to functions and function pointers}}

// Signature-shape diagnostics.
int __attribute__((coro_handle_fn)) bad_return(void *p); // expected-error {{'coro_handle_fn' attribute requires a function with a 'void' return type}}

void __attribute__((coro_handle_fn)) bad_arity_zero(void); // expected-error {{'coro_handle_fn' attribute requires a function with exactly one parameter}}
void __attribute__((coro_handle_fn)) bad_arity_two(void *a, void *b); // expected-error {{'coro_handle_fn' attribute requires a function with exactly one parameter}}

void __attribute__((coro_handle_fn)) bad_arg_type(int x); // expected-error {{'coro_handle_fn' attribute requires a function with a pointer parameter}}

void __attribute__((coro_handle_fn)) bad_varargs(void *p, ...); // expected-error {{'coro_handle_fn' attribute requires a function with no variadic arguments}}

// Function pointer type signature checks.
typedef void (*good_fp_t)(void *) __attribute__((coro_handle_fn));
typedef int (*bad_fp_ret_t)(void *) __attribute__((coro_handle_fn)); // expected-error {{'coro_handle_fn' attribute requires a function with a 'void' return type}}
typedef void (*bad_fp_arity_t)(void) __attribute__((coro_handle_fn)); // expected-error {{'coro_handle_fn' attribute requires a function with exactly one parameter}}
typedef void (*bad_fp_arg_t)(int) __attribute__((coro_handle_fn)); // expected-error {{'coro_handle_fn' attribute requires a function with a pointer parameter}}
