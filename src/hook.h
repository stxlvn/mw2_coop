#pragma once
#include <stddef.h>
#include <stdint.h>

// Code patches of the game's functions, in one place (the six hooks of the DLL used to carry a copy each).

// Puts the first `len` bytes of the function at `target`, which must be `expected`, into a trampoline that
// goes on at target + len, and makes the function jump to `replacement` first. `len` is JUMP_SIZE (14 in the
// 64-bit build, 5 in the 32-bit one) to 32 bytes, and ends between two instructions. The
// trampoline (call it to run the original) is stored in *original BEFORE the function is patched: the game's
// other threads call hot functions (asset lookups), and one that reaches the replacement the instant the
// jump is in place must find the original to call. Returns 1, or 0 (nothing changed) when the bytes are not
// the ones the build has. The trampoline page is written once and then read-execute only: this DLL leaves
// no memory that can be both written and run.
int hook_function(uint8_t *target, const uint8_t *expected, size_t len, void *replacement, void **original);

// The same for code that cannot be copied as it stands (a relative call in it): `code` is the trampoline
// as built by the caller, copied into a read-execute page that is returned (NULL when none could be made).
uint8_t *hook_code(const uint8_t *code, size_t size);

// Overwrites the first `len` bytes (JUMP_SIZE to 32, as above) of `target` with a jump to `replacement`, the
// rest with `filler`. For functions the DLL rewrites whole (the original is not called).
void hook_jump(uint8_t *target, size_t len, const void *replacement, uint8_t filler);

#ifdef _WIN64
// The 14 bytes of `jmp [rip+0]; dq to`, for callers that build a trampoline by hand.
void hook_jump_bytes(uint8_t out[14], const void *to);
#endif

// Redirects one `call rel32` instruction (e8 xx xx xx xx) at `site` to `replacement`: the call goes to a stub in a page within reach of the site, which jumps to the replacement. The
// callee itself stays untouched (its first bytes may hold relative instructions that hook_function could not copy), so *original is simply its address. Returns 1, or 0 when the byte at
// `site` is not a call or no page near it could be had.
int hook_call(uint8_t *site, void *replacement, void **original);

#ifdef _WIN64
// A fresh read-write page within +-2 GB of `where` (so that rip-relative displacements reach it), or NULL.
uint8_t *hook_page_near(const uint8_t *where);
#endif
