//===- coroutine_frame.hpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// std::coroutine_frame -- a hand-rolled coroutine frame, layout-compatible
// with std::coroutine_handle. References:
//   https://github.com/llvm/llvm-project/issues/91123
//   https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4126r0.pdf
//
// Frame layout follows the (still-draft, but agreed across vendors)
// Itanium coroutine ABI:
//
//   offset -kLeadingPadding:  start of the C++ object
//   offset 0:                 __resume_   <-- frame pointer == handle.address()
//   offset sizeof(void*):     __destroy_
//   offset 2*sizeof(void*):   __promise_  (always at this fixed offset)
//   offset 2*sizeof(void*)+sizeof(P): user data in derived classes
//
// where `kLeadingPadding == max(0, alignof(_Promise) - 2*sizeof(void*))`.
// When `alignof(_Promise) <= 2*sizeof(void*)` the leading padding is empty
// and the resume slot coincides with the start of the class. When the
// promise is over-aligned, the padding is inserted *before* the resume
// slot so the promise lands on its required alignment without disturbing
// the fixed `[resume][destroy][promise]` offsets the ABI mandates. See:
//
//   GCC PR c++/104177 ([PATCH 2/2] coroutines: handle (new-)extended
//                      alignment, landed 2024-09)
//   LLVM issue #58397 (still open, 2026-05): __builtin_coro_promise
//                      uses alignTo(2*sizeof(void*), alignof(P)) instead
//                      of the ABI-mandated fixed 2*sizeof(void*), so
//                      `coroutine_handle::promise()` returns the wrong
//                      address for over-aligned promises. `frame.promise()`
//                      below avoids that bug; `handle.promise()` should
//                      not be used until LLVM is fixed.
//
// Requires the [[clang::coro_handle_fn]] calling-convention attribute.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_COROUTINE_FRAME_HPP
#define _LIBCPP_COROUTINE_FRAME_HPP

#include <coroutine>
#include <cstddef>
#include <type_traits>

#if !defined(__clang__) || !__has_attribute(coro_handle_fn)
#  error "<coroutine_frame.hpp> requires Clang with [[clang::coro_handle_fn]]"
#endif

#if __cplusplus < 202002L
#  error "<coroutine_frame.hpp> requires C++20 or newer"
#endif

namespace std {

namespace __coroutine_frame_detail {

// Storage stand-in when _Promise is `void`: zero-sized, alignment 1.
struct __empty {};

template <class _Promise>
using __promise_storage_t =
    conditional_t<is_void_v<_Promise>, __empty, _Promise>;

// Bytes of leading padding required so the resume/destroy slots sit at
// offsets `2*sizeof(void*)` resp. `sizeof(void*)` *before* the promise,
// while the promise itself remains aligned to alignof(_Promise).
template <class _Promise>
constexpr size_t __leading_padding_size() {
  if constexpr (is_void_v<_Promise>) {
    return 0;
  } else if constexpr (alignof(_Promise) <= 2 * sizeof(void*)) {
    return 0;
  } else {
    return alignof(_Promise) - 2 * sizeof(void*);
  }
}

// Specialized at zero so [[no_unique_address]] can collapse it.
template <size_t _N>
struct __leading_padding {
  char __bytes[_N];
};
template <>
struct __leading_padding<0> {};

} // namespace __coroutine_frame_detail

// CRTP-shaped frame:
//
//   _Derived  -- the most-derived class. The ctor wires the resume and
//                destroy slots to thunks that forward to
//                `_Derived::resume()` / `_Derived::destroy()`. _Derived
//                must inherit publicly from this coroutine_frame
//                instantiation, must be the most-derived part of the
//                object, and must expose accessible
//                `void resume()` / `void destroy()` members. Single,
//                non-virtual inheritance only -- the thunk does a
//                `static_cast<_Derived*>` on the base pointer.
//
//   _Promise  -- promise type, defaulting to `void` (type-erased
//                handle). Determines the layout: over-aligned promises
//                trigger leading padding so the promise lands at the
//                ABI-fixed offset 2*sizeof(void*) from the frame
//                pointer.
//
// Usage:
//
//   struct my_frame : coroutine_frame<my_frame> {           // Promise = void
//     void resume();
//     void destroy();
//   };
//
//   struct my_frame : coroutine_frame<my_frame, my_promise> {
//     void resume();
//     void destroy();
//   };
//
// Private resume/destroy: the thunks live in this base, so private
// members are inaccessible unless _Derived grants friendship. The
// injected base-class name lets you write a single-line friend decl:
//
//   struct my_frame : coroutine_frame<my_frame, my_promise> {
//     friend coroutine_frame;     // grants access to private resume/destroy
//   private:
//     void resume();
//     void destroy();
//   };
template <class _Derived, class _Promise = void>
class coroutine_frame {
public:
  using promise_type = _Promise;

  // Bytes of padding the C++ object carries before the resume slot.
  // 0 unless `alignof(_Promise) > 2*sizeof(void*)`.
  static constexpr size_t kLeadingPadding =
      __coroutine_frame_detail::__leading_padding_size<_Promise>();

  // Convert the `void*` a coroutine_handle wraps (== handle.address()
  // == &__resume_ inside the frame) back to the owning _Derived*.
  // Identity cast at the machine level when `kLeadingPadding == 0`.
  static _Derived* from_handle_ptr(void* __p) noexcept {
    return static_cast<_Derived*>(reinterpret_cast<coroutine_frame*>(
        static_cast<char*>(__p) - kLeadingPadding));
  }

  // Default-construct. Auto-wires resume/destroy to thunks that
  // forward to `_Derived::resume()` / `_Derived::destroy()`.
  //
  // The is_base_of static_assert lives in the body (rather than as a
  // requires clause) because at the point a constraint is evaluated
  // _Derived may still be incomplete (the class is being used as a
  // base of _Derived). The body, by contrast, is only instantiated
  // when this ctor is actually called -- by which time _Derived is
  // complete (the call site sits inside _Derived's ctor).
  constexpr coroutine_frame() noexcept
      : __resume_(&__derived_resume_thunk),
        __destroy_(&__derived_destroy_thunk) {
    static_assert(
        is_base_of_v<coroutine_frame, _Derived>,
        "Derived (1st template parameter) must inherit from this "
        "coroutine_frame instantiation -- traditional CRTP shape: "
        "`struct my_frame : coroutine_frame<my_frame, my_promise> {...}`");
  }

  coroutine_frame(const coroutine_frame&) = delete;
  coroutine_frame& operator=(const coroutine_frame&) = delete;

  // Make `get_coro_handle().done()` return true. Resuming a done frame
  // is UB (matches `coro.done`'s contract: the resume slot is null at
  // the final suspend).
  void mark_done() noexcept { __resume_ = nullptr; }

  // The coroutine_handle's `address()` is the *frame pointer*: the
  // address of the resume slot. `coro.done` reads `*(void**)handle`
  // (== __resume_); indirect resume/destroy calls pass that pointer
  // as the argument to the thunk.
  coroutine_handle<_Promise> get_coro_handle() noexcept {
    return coroutine_handle<_Promise>::from_address(&__resume_);
  }

  // Direct promise access: ABI-correct (always at offset
  // 2*sizeof(void*) from the frame pointer), and unaffected by LLVM
  // issue #58397. Prefer this over `get_coro_handle().promise()` for
  // over-aligned promises.
  template <class _P = _Promise, class = enable_if_t<!is_void_v<_P>>>
  _P& promise() noexcept {
    return __promise_;
  }
  template <class _P = _Promise, class = enable_if_t<!is_void_v<_P>>>
  const _P& promise() const noexcept {
    return __promise_;
  }

private:
  // Function-pointer type for the resume/destroy slots. fastcc (via
  // [[clang::coro_handle_fn]]) is required so the indirect call lowered
  // out of `coro.resume`/`coro.destroy` matches the callee's CC.
  using fn_t = void (*)(void*) [[clang::coro_handle_fn]];

  [[clang::coro_handle_fn]] static void __derived_resume_thunk(void* __p) {
    from_handle_ptr(__p)->resume();
  }
  [[clang::coro_handle_fn]] static void __derived_destroy_thunk(void* __p) {
    from_handle_ptr(__p)->destroy();
  }

  using __promise_storage_t =
      __coroutine_frame_detail::__promise_storage_t<_Promise>;
  using __padding_t =
      __coroutine_frame_detail::__leading_padding<kLeadingPadding>;

  // ORDER MATTERS. Per the coroutine ABI, the layout must be:
  //   [leading padding][resume][destroy][promise]
  // with the promise sitting at exactly 2*sizeof(void*) past the
  // resume slot, regardless of alignof(_Promise). The compiler bumps
  // the class's overall alignment up via __promise_'s natural
  // alignment, so an over-aligned promise lands on its required
  // boundary without breaking the fixed offsets.
  [[no_unique_address]] __padding_t __padding_;
  fn_t __resume_  = nullptr;
  fn_t __destroy_ = nullptr;
  [[no_unique_address]] __promise_storage_t __promise_{};
};

} // namespace std

#endif // _LIBCPP_COROUTINE_FRAME_HPP
