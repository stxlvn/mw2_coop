// Code patches of the game's functions: a jump over the start of a function (14 bytes absolute in the 64-bit
// build, 5 bytes relative in the 32-bit one, where every address is within reach), and a trampoline that runs
// the function's own first bytes and then goes on with the rest of it.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "hook.h"

#ifdef _WIN64
#define JUMP_SIZE 14
#else
#define JUMP_SIZE 5
#endif
#define MAX_LEN 32

#ifdef _WIN64
void hook_jump_bytes(uint8_t out[JUMP_SIZE], const void *to)
{
	static const uint8_t jmp_rip[6] = {0xff, 0x25, 0, 0, 0, 0};
	uint64_t address = (uint64_t)(uintptr_t)to;
	memcpy(out, jmp_rip, sizeof(jmp_rip));
	memcpy(out + 6, &address, 8);
}
#endif

// The jump instruction as it will stand at `at` (the 32-bit one is relative to that place).
static void jump_bytes(uint8_t out[JUMP_SIZE], const uint8_t *at, const void *to)
{
#ifdef _WIN64
	(void)at;
	hook_jump_bytes(out, to);
#else
	int32_t rel = (int32_t)((uintptr_t)to - ((uintptr_t)at + 5));
	out[0] = 0xe9;
	memcpy(out + 1, &rel, 4);
#endif
}

uint8_t *hook_code(const uint8_t *code, size_t size)
{
	uint8_t *page = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!page)
		return NULL;
	memcpy(page, code, size);
	FlushInstructionCache(GetCurrentProcess(), page, size);
	DWORD old;
	if (!VirtualProtect(page, size, PAGE_EXECUTE_READ, &old)) {
		VirtualFree(page, 0, MEM_RELEASE);
		return NULL;
	}
	return page;
}

void hook_jump(uint8_t *target, size_t len, const void *replacement, uint8_t filler)
{
	if (len < JUMP_SIZE || len > MAX_LEN)
		return;
	uint8_t patch[MAX_LEN];
	memset(patch, filler, len);
	jump_bytes(patch, target, replacement);
	DWORD old;
	VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old);
	memcpy(target, patch, len);
	VirtualProtect(target, len, old, &old);
	FlushInstructionCache(GetCurrentProcess(), target, len);
}

// The trampoline of hook_function: the first `len` bytes of `target`, then a jump to target + len. It is
// built in place because the 32-bit jump is relative to where it stands.
static uint8_t *make_trampoline(const uint8_t *target, size_t len)
{
	size_t size = len + JUMP_SIZE;
	uint8_t *page = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!page)
		return NULL;
	memcpy(page, target, len);
	jump_bytes(page + len, page + len, target + len);
	FlushInstructionCache(GetCurrentProcess(), page, size);
	DWORD old;
	if (!VirtualProtect(page, size, PAGE_EXECUTE_READ, &old)) {
		VirtualFree(page, 0, MEM_RELEASE);
		return NULL;
	}
	return page;
}

int hook_function(uint8_t *target, const uint8_t *expected, size_t len, void *replacement, void **original)
{
	if (len < JUMP_SIZE || len > MAX_LEN || memcmp(target, expected, len) != 0)
		return 0;
	uint8_t *trampoline = make_trampoline(target, len);
	if (!trampoline)
		return 0;
	*original = trampoline; // before the patch, not after it
	hook_jump(target, len, replacement, 0xcc); // never run: the jump leaves, the trampoline resumes after them
	return 1;
}

#ifdef _WIN64
// A page within +-2 GB of `where`, read-write, or NULL.
uint8_t *hook_page_near(const uint8_t *where)
{
	uintptr_t center = (uintptr_t)where & ~(uintptr_t)0xffff;
	for (uintptr_t d = 0x10000; d < 0x70000000; d += 0x10000) {
		uint8_t *p = VirtualAlloc((void *)(center - d), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!p)
			p = VirtualAlloc((void *)(center + d), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (p)
			return p;
	}
	return NULL;
}

int hook_call(uint8_t *site, void *replacement, void **original)
{
	if (site[0] != 0xe8)
		return 0;
	int32_t rel;
	memcpy(&rel, site + 1, 4);
	uint8_t *page = hook_page_near(site);
	if (!page)
		return 0;
	hook_jump_bytes(page, replacement);
	DWORD old;
	if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old))
		return 0;
	int64_t to_stub = (int64_t)(page - (site + 5));
	if (to_stub > INT32_MAX || to_stub < INT32_MIN)
		return 0;
	*original = site + 5 + rel;
	int32_t new_rel = (int32_t)to_stub;
	VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
	memcpy(site + 1, &new_rel, 4);
	VirtualProtect(site, 5, old, &old);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return 1;
}
#endif

#ifndef _WIN64
// 32 bits: every address is within reach of a relative call, so the call site goes straight to the replacement.
int hook_call(uint8_t *site, void *replacement, void **original)
{
	if (site[0] != 0xe8)
		return 0;
	int32_t rel;
	memcpy(&rel, site + 1, 4);
	*original = site + 5 + rel;
	int32_t new_rel = (int32_t)((uint8_t *)replacement - (site + 5));
	DWORD old;
	VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
	memcpy(site + 1, &new_rel, 4);
	VirtualProtect(site, 5, old, &old);
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	return 1;
}
#endif
