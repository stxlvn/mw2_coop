// Script runtime errors. The retail Scr_Error (rva 0x2226a0) drops the message and only
// longjmps out of the failing script thread:
//   if (!flag_a && !flag_b && depth) longjmp(&jmpbufs[index], -1);
// The message was formatted with va() just before, into one of two 1 KB slots of a
// thread-local block (va @0x273440 -> block = 0x203700(1); slot index at block+0x800).
// Our replacement logs both slots, then does exactly what the original does, with the game's
// own longjmp (rva 0x2e485c) on the game's own jmp_buf.
//
// It also logs where in GSC the error happened. The VM keeps its live code position at
// rva 0x1900b78 and the callers' saved positions in function_frame_start[] (rva 0x18f44c0,
// 0x28 bytes each, pos first; function_count at 0x18f44a0). Positions point into the program
// buffer (rva 0x15a03e0). Scr_GetFunctionHandle (rva 0x216920) returns a function's offset in
// that buffer, so the function names taken from each script's source text (scrdebug_note_source)
// map a position back to file::function. Functions are compiled one after another, so a position
// belongs to the function with the greatest start offset at or below it.
// Scr_GetFunctionHandle only works while the loaded-scripts table exists, so the offsets are
// read in a hook on Scr_EndLoadScripts (rva 0x2166d0), right before it frees that table.
#include <windows.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "scrdebug.h"
#include "scrdebug_stack.h"

#define RVA_SCR_ERROR        0x2226a0
#define RVA_SCR_ERR_FLAG_A   0x15a038b // uint8
#define RVA_SCR_ERR_FLAG_B   0x1463bce // uint8
#define RVA_SCR_ERR_DEPTH    0x18f44a0 // int32, scrVmPub.function_count
#define RVA_SCR_JMPBUF_INDEX 0x18fca44 // int32
#define RVA_SCR_JMPBUFS      0x18fca50 // jmp_buf[], 0x100 bytes each
#define RVA_LONGJMP          0x2e485c  // the game's CRT longjmp
#define RVA_TLS_BLOCK        0x203700  // void *Sys_GetValue(int)
#define RVA_VM_POS           0x1900b78 // const char *, live code position of the running thread
#define RVA_VM_FRAMES        0x18f44c0 // function_frame_t[], saved code position at +0
#define VM_FRAME_SIZE        0x28
#define RVA_PROGRAM_BUFFER   0x15a03e0 // const char *
#define RVA_GET_FUNC_HANDLE  0x216920  // int Scr_GetFunctionHandle(const char *file, const char *name)
#define RVA_END_LOAD_SCRIPTS 0x2166d0  // void Scr_EndLoadScripts(void)
#define RVA_END_LOAD_CALLEE  0x21a3b0  // its first call

static uint8_t *base;

static void (*end_load_original)(void);

static void end_load_scripts(void)
{
	scrdebug_stack_build();
	end_load_original();
}

// Scr_EndLoadScripts starts with (16 bytes, cut on an instruction boundary):
//   40 53 / 48 83 ec 20 / e8 rel32 (call 0x21a3b0) / b9 02 00 00 00
// The trampoline runs them with the call made absolute, then jumps back to +16.
static const uint8_t end_load_expected[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0xe8, 0xd5, 0x3c, 0x00, 0x00,
					    0xb9, 0x02, 0x00, 0x00, 0x00};

static void hook_end_load(void)
{
	uint8_t *target = base + RVA_END_LOAD_SCRIPTS;
	if (memcmp(target, end_load_expected, sizeof(end_load_expected)) != 0) {
		log_printf("scrdebug: unexpected bytes at Scr_EndLoadScripts, no script stacks");
		return;
	}
	uint8_t code[64];
	uint8_t *p = code;
	memcpy(p, target, 6); // push rbx; sub rsp, 0x20
	p += 6;
	uint64_t callee = (uint64_t)(uintptr_t)(base + RVA_END_LOAD_CALLEE);
	*p++ = 0x48; // mov rax, imm64; call rax
	*p++ = 0xb8;
	memcpy(p, &callee, 8);
	p += 8;
	*p++ = 0xff;
	*p++ = 0xd0;
	memcpy(p, target + 11, 5); // mov ecx, 2
	p += 5;
	hook_jump_bytes(p, target + 16); // back to +16
	p += 14;
	end_load_original = (void (*)(void))hook_code(code, (size_t)(p - code));
	if (!end_load_original)
		return;
	hook_jump(target, 16, end_load_scripts, 0x90);
}

// Per calling site (return address = the builtin or VM code that raised the error), log the
// first few hits and then the 10th, 100th, 1000th... ones. The Museum's civilians raise the same
// error about 6700 times a second (a game bug, also in single player): every 100th still wrote
// 67 entries with stacks a second, 175 MB in a quarter of an hour, and the game crawled.
#define MAX_SITES 64
static uintptr_t site_rva[MAX_SITES];
static volatile LONG site_hits[MAX_SITES];
static volatile LONG site_count;

static void log_error(uintptr_t rva)
{
	int i;
	for (i = 0; i < site_count && site_rva[i] != rva; i++)
		;
	if (i == site_count) {
		if (site_count >= MAX_SITES)
			return;
		site_rva[i] = rva;
		InterlockedIncrement(&site_count);
	}
	LONG n = InterlockedIncrement(&site_hits[i]);
	if (n > 3) {
		LONG p = 10;
		while (p < n && p < 1000000000)
			p *= 10;
		if (p != n)
			return;
	}
	const char *block = ((const char *(*)(int))(base + RVA_TLS_BLOCK))(1);
	int slot = *(const int32_t *)(block + 0x800);
	char last[160], prev[160];
	lstrcpynA(last, block + (slot ^ 1) * 0x400, sizeof(last));
	lstrcpynA(prev, block + slot * 0x400, sizeof(prev));
	log_printf("SCRIPT ERROR #%ld from rva 0x%llx (va slots: '%s' / '%s')", n, (unsigned long long)rva, last, prev);
	scrdebug_stack_log();
}

static void scr_error(void)
{
	if (base[RVA_SCR_ERR_FLAG_A] || base[RVA_SCR_ERR_FLAG_B] || !*(int32_t *)(base + RVA_SCR_ERR_DEPTH))
		return;
	log_error((uintptr_t)__builtin_return_address(0) - (uintptr_t)base);
	int index = *(int32_t *)(base + RVA_SCR_JMPBUF_INDEX);
	((void (*)(void *, int))(base + RVA_LONGJMP))(base + RVA_SCR_JMPBUFS + (int64_t)index * 0x100, -1);
}

static const uint8_t expected[] = {0x48, 0x83, 0xec, 0x28, 0x80, 0x3d, 0xe0, 0xdc, 0x37, 0x01, 0x00};

void scrdebug_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	uint8_t *target = base + RVA_SCR_ERROR;
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("scrdebug: unexpected bytes at Scr_Error, not hooking");
		return;
	}
	hook_jump(target, 14, scr_error, 0x90);
	log_printf("scrdebug: Scr_Error replaced (runtime script errors are logged)");
	static const scrdebug_vm_t vm = {RVA_GET_FUNC_HANDLE, RVA_PROGRAM_BUFFER, RVA_VM_POS, RVA_SCR_ERR_DEPTH, RVA_VM_FRAMES, 0, VM_FRAME_SIZE};
	scrdebug_stack_init(base, &vm);
	hook_end_load();
}
