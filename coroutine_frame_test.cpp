//===- coroutine_frame_test.cpp --------------------------------*- C++ -*-===//
//
// Smoke test for std::coroutine_frame. Exercises:
//   * Empty-promise size guarantees (void / monostate cost two pointers).
//   * The Itanium coroutine ABI invariant: promise at fixed offset
//     2*sizeof(void*) from the frame pointer, regardless of alignof(P).
//   * CRTP wiring -- derived resume/destroy fire, frame.promise() and
//     handle.promise() agree (for non-over-aligned promises; see
//     LLVM issue #58397 for the over-aligned case).
//   * mark_done() flips coroutine_handle::done().
//
// Build:
//   clang++ -std=c++20 -O2 coroutine_frame_test.cpp -o coroutine_frame_test
// Run:
//   ./coroutine_frame_test
//
//===----------------------------------------------------------------------===//

#include "coroutine_frame.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <variant>

namespace {

// ---------- compile-time layout invariants -------------------------------

// Tiny instantiable frames just to give the layout assertions something
// concrete to size. coroutine_frame requires a Derived to instantiate.
template <class _P>
struct GenericFrame : std::coroutine_frame<GenericFrame<_P>, _P> {
  void resume() {}
  void destroy() {}
};
struct VoidFrame : std::coroutine_frame<VoidFrame> {
  void resume() {}
  void destroy() {}
};

// Empty promise -- frame is exactly two function pointers wide.
static_assert(sizeof(VoidFrame) == 2 * sizeof(void*),
              "void promise must not consume storage");
static_assert(sizeof(GenericFrame<std::monostate>) == 2 * sizeof(void*),
              "empty promise must not consume storage");

// Over-aligned promise -- class alignment bumped, kLeadingPadding set.
struct alignas(64) BigAlignedPromise {
  int x = 0;
};
using BigFrame = GenericFrame<BigAlignedPromise>;
static_assert(alignof(BigFrame) >= 64);
static_assert(BigFrame::kLeadingPadding == 64 - 2 * sizeof(void*));
static_assert(GenericFrame<int>::kLeadingPadding == 0);
static_assert(VoidFrame::kLeadingPadding == 0);
static_assert(GenericFrame<std::monostate>::kLeadingPadding == 0);

// ---------- CRTP wiring fires resume/destroy -----------------------------

struct CountingPromise {
  int resumes  = 0;
  int destroys = 0;
};

// CRTP: pass the derived type as the 1st template parameter, the promise
// as the (optional) 2nd. The base's default ctor auto-wires.
struct CountingFrame
    : std::coroutine_frame<CountingFrame, CountingPromise> {
  void resume() { ++this->promise().resumes; }
  void destroy() { ++this->promise().destroys; }
};

void test_crtp_basic() {
  CountingFrame frame;
  std::coroutine_handle<CountingPromise> h = frame.get_coro_handle();
  assert(h);

  // For a non-over-aligned promise, frame.promise() and handle.promise()
  // agree (LLVM's coro.promise formula coincides with the ABI here).
  assert(&h.promise() == &frame.promise());

  h.resume();
  h.resume();
  assert(frame.promise().resumes == 2);

  h.destroy();
  assert(frame.promise().destroys == 1);

  // Type-erased conversion still resolves to the same frame pointer.
  std::coroutine_handle<> erased = h;
  assert(erased.address() == h.address());

  // Round-trip back to the owning frame from the handle's address.
  assert(CountingFrame::from_handle_ptr(h.address()) == &frame);
}

// ---------- mark_done() / done() -----------------------------------------

void test_mark_done() {
  CountingFrame frame;
  auto h = frame.get_coro_handle();

  assert(!h.done());
  frame.mark_done();
  assert(h.done()); // coro.done reads *(void**)handle == __resume_

  // destroy is still well-defined; the destroy slot is unaffected.
  h.destroy();
  assert(frame.promise().destroys == 1);
}

// ---------- ABI layout contract ------------------------------------------
//
// The Itanium coroutine ABI requires:
//
//   &promise - handle.address() == 2 * sizeof(void*)    (fixed!)
//
// for any alignof(P), with leading padding inserted *before* the resume
// slot to keep the promise on its required alignment. This is the rule
// GCC implements (PR c++/104177). LLVM violates it (issue #58397) by
// computing the offset as `alignTo(2*sizeof(void*), alignof(P))` in
// both directions, so handle.promise() round-trips against itself but
// the on-the-wire offset isn't fixed -- breaking debuggers and any
// consumer that looks at the frame layout directly.
//
// `verify_layout<P>` checks the ABI invariant via direct
// `frame.promise()` (which is unaffected by LLVM #58397). For non-over-
// aligned promises we additionally check `handle.promise()` because
// LLVM's buggy formula happens to agree with the ABI in that case.

template <class _P>
void verify_layout() {
  static_assert(!std::is_empty_v<_P>,
                "empty promises trade ABI offset for size; tested separately");
  GenericFrame<_P> frame;
  auto h = frame.get_coro_handle();

  // ABI invariant: promise at exactly 2*sizeof(void*) from the frame
  // pointer, regardless of alignof(P).
  const std::ptrdiff_t actual =
      reinterpret_cast<const char*>(&frame.promise()) -
      static_cast<const char*>(h.address());
  assert(actual == static_cast<std::ptrdiff_t>(2 * sizeof(void*)));

  // Promise must be aligned to alignof(P).
  assert(reinterpret_cast<std::uintptr_t>(&frame.promise()) % alignof(_P) ==
         0);

  // The frame pointer is the resume slot inside the C++ object, offset
  // by kLeadingPadding from the object start.
  assert(static_cast<const char*>(h.address()) -
             reinterpret_cast<const char*>(&frame) ==
         static_cast<std::ptrdiff_t>(GenericFrame<_P>::kLeadingPadding));

  // Forward + reverse via coroutine_handle: only valid for non-over-
  // aligned promises until LLVM #58397 is fixed.
  if constexpr (alignof(_P) <= 2 * sizeof(void*)) {
    assert(&h.promise() == &frame.promise());
    auto h2 = std::coroutine_handle<_P>::from_promise(frame.promise());
    assert(h2.address() == h.address());
  }
}

struct alignas(32) Aligned32Promise {
  int tag = 1;
};
struct alignas(64) Aligned64Promise {
  int tag = 2;
};
struct alignas(128) Aligned128Promise {
  int tag = 3;
};
struct OddSizePromise {
  char a;
  int b; // alignof = 4, sizeof = 8 (with padding)
};

void test_promise_layouts() {
  verify_layout<int>();                // alignof=4   -> padding 0
  verify_layout<long>();               // alignof=8   -> padding 0
  verify_layout<OddSizePromise>();     // alignof=4   -> padding 0
  verify_layout<Aligned32Promise>();   // alignof=32  -> padding 16
  verify_layout<Aligned64Promise>();   // alignof=64  -> padding 48
  verify_layout<Aligned128Promise>();  // alignof=128 -> padding 112
}

// ---------- empty-promise size invariant ---------------------------------
//
// The user-facing requirement: an empty promise (e.g. std::monostate)
// must not consume storage. Honoring this requires [[no_unique_address]]
// on the promise member, which lets the compiler collapse the empty
// subobject into another member's storage -- so its address does NOT
// land on the ABI's fixed `frame_ptr + 2*sizeof(void*)` for empty
// promises. That is harmless: an empty promise has no data to access,
// and `frame.promise()` / `handle.promise()` only ever return a valid
// reference to an empty subobject. The trade-off is documented; the
// frame still resumes/destroys correctly and `mark_done()` still works.
struct EmptyPromise {};
static_assert(sizeof(GenericFrame<EmptyPromise>) == 2 * sizeof(void*));
static_assert(sizeof(GenericFrame<std::monostate>) == 2 * sizeof(void*));

// ---------- monostate frame is round-trippable ---------------------------

struct MonostateFrame
    : std::coroutine_frame<MonostateFrame, std::monostate> {
  bool resumed   = false;
  bool destroyed = false;

  void resume() { resumed = true; }
  void destroy() { destroyed = true; }
};

void test_monostate_frame() {
  MonostateFrame frame;
  auto h = frame.get_coro_handle();
  h.resume();
  assert(frame.resumed);
  h.destroy();
  assert(frame.destroyed);
}

// ---------- private resume/destroy via friend declaration ----------------

struct PrivateFrame
    : std::coroutine_frame<PrivateFrame, CountingPromise> {
  // The injected base-class name resolves to
  // coroutine_frame<PrivateFrame, CountingPromise>, so a single-line
  // friend decl is enough to grant the base's thunks access.
  friend coroutine_frame;

private:
  void resume() { ++this->promise().resumes; }
  void destroy() { ++this->promise().destroys; }
};

void test_private_resume_destroy() {
  PrivateFrame frame;
  auto h = frame.get_coro_handle();
  h.resume();
  h.resume();
  assert(frame.promise().resumes == 2);
  h.destroy();
  assert(frame.promise().destroys == 1);
}

// ---------- void-promise frame -------------------------------------------

int g_void_resumes  = 0;
int g_void_destroys = 0;

struct PlainFrame : std::coroutine_frame<PlainFrame> {
  void resume() { ++g_void_resumes; }
  void destroy() { ++g_void_destroys; }
};

void test_void_frame() {
  PlainFrame frame;
  std::coroutine_handle<> h = frame.get_coro_handle();
  assert(h);
  assert(h.address() == &frame); // kLeadingPadding == 0

  h.resume();
  assert(g_void_resumes == 1);

  frame.mark_done();
  assert(h.done());

  h.destroy();
  assert(g_void_destroys == 1);
}

} // namespace

int main() {
  test_crtp_basic();
  test_mark_done();
  test_promise_layouts();
  test_monostate_frame();
  test_private_resume_destroy();
  test_void_frame();
  std::printf("OK\n");
  return 0;
}
