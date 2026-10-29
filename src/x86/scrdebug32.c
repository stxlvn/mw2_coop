// Script runtime errors in the 32-bit build. Scr_Error (0x457ae0) keeps its message (copied into a 1 KB buffer at 0x190ec18,
// pointed to by 0x18e3f90) and jumps to the error handler 0x612f50, which longjmps out of the failing script thread (or ends
// the game) without logging it. The hook logs the message first, then runs the handler as it is. The call stack (file::function of the
// live code position and of the callers' saved ones) comes from scrdebug_stack.c, as in the 64-bit build (not in the iw4x-sp profile,
// whose VM addresses are not mapped).
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "hook.h"
#include "log.h"
#include "scrdebug.h"
#include "scrdebug_stack.h"

#ifdef MW2COOP_IW4X // the 1.0.159 exe of iw4x-sp
#define RVA_ERROR_HANDLER 0x215790
#define RVA_ERROR_MESSAGE 0x14e7510
#define RVA_ERROR_FLAG_A  0x14e750f
#define RVA_ERROR_FLAG_B  0x118bfb4
#define ERROR_FLAG_ADDR   {0x0f, 0x75, 0x8e, 0x01}
#else
#define RVA_ERROR_HANDLER 0x212f50 // void (void); starts with cmp byte [0x18e3f8f], 0
#define RVA_ERROR_MESSAGE 0x14e3f90 // const char * (0x18e3f90)
#define RVA_ERROR_FLAG_A  0x14e3f8f // uint8
#define RVA_ERROR_FLAG_B  0x1188a34 // uint8 (0x1588a34)
#define ERROR_FLAG_ADDR   {0x8f, 0x3f, 0x8e, 0x01}
// the script VM (docs/32bit-native-map): function_count 0x190a878, the pointer one past the top call frame 0x190a87c (frames of 0x18 bytes, the saved code position first),
// the live code position 0x190a854, the program buffer pointer 0x18e3fe0, Scr_GetFunctionHandle(file, name) 0x5018f0 (cdecl), Scr_EndLoadScripts 0x490180
#define RVA_END_LOAD_SCRIPTS 0x90180
#define RVA_GET_FUNC_HANDLE  0x1018f0
#define RVA_PROGRAM_BUFFER   0x14e3fe0
#define RVA_VM_POS           0x150a854
#define RVA_VM_DEPTH         0x150a878
#define RVA_VM_FRAME_PTR     0x150a87c
#define VM_FRAME_SIZE        0x18
#endif

static uint8_t *base;
static void (*handler_original)(void);

static void error_hook(void)
{
	if (!base[RVA_ERROR_FLAG_A] && !base[RVA_ERROR_FLAG_B]) {
		const char *message = *(const char **)(base + RVA_ERROR_MESSAGE);
		MEMORY_BASIC_INFORMATION mbi;
		if (message && VirtualQuery(message, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT)
			log_printf("script error: %.400s", message);
		else
			log_printf("script error (no message)");
#ifndef MW2COOP_IW4X
		scrdebug_stack_log();
#endif
	}
	handler_original();
}

#ifndef MW2COOP_IW4X
// Scr_EndLoadScripts (VA 0x490180) starts with  53 / 6a 02 / e8 rel32 (call 0x4b3150)  and is entered by a call (rva 0xf921) and by a tail jump (rva 0x171dd), so the function
// itself is patched: its first 8 bytes (up to the end of the call) are replaced by a jump to end_load_scripts, whose trampoline runs them (the call made absolute) and goes on at +8.
static void (*end_load_original)(void);

static void end_load_scripts(void)
{
	scrdebug_stack_build();
	end_load_original();
}

static void hook_end_load(void)
{
	uint8_t *target = base + RVA_END_LOAD_SCRIPTS;
	if (target[0] != 0x53 || target[1] != 0x6a || target[2] != 0x02 || target[3] != 0xe8) {
		log_printf("scrdebug: unexpected bytes at Scr_EndLoadScripts, no script stacks");
		return;
	}
	int32_t rel;
	memcpy(&rel, target + 4, 4);
	uintptr_t callee = (uintptr_t)(target + 8) + (intptr_t)rel;
	uintptr_t resume = (uintptr_t)(target + 8);
	uint8_t code[32];
	uint8_t *p = code;
	*p++ = 0x53; // push ebx
	*p++ = 0x6a; // push 2
	*p++ = 0x02;
	*p++ = 0xb8; // mov eax, callee; call eax
	memcpy(p, &callee, 4);
	p += 4;
	*p++ = 0xff;
	*p++ = 0xd0;
	*p++ = 0xb8; // mov eax, resume; jmp eax (eax is loaded from memory right after, at +8)
	memcpy(p, &resume, 4);
	p += 4;
	*p++ = 0xff;
	*p++ = 0xe0;
	end_load_original = (void (*)(void))hook_code(code, (size_t)(p - code));
	if (!end_load_original)
		return;
	hook_jump(target, 8, end_load_scripts, 0x90);
}
#endif

void scrdebug_install(void)
{
	static const uint8_t flag_addr[4] = ERROR_FLAG_ADDR;
	const uint8_t expected[] = {0x80, 0x3d, flag_addr[0], flag_addr[1], flag_addr[2], flag_addr[3], 0x00};
	base = (uint8_t *)GetModuleHandleA(NULL);
	if (!hook_function(base + RVA_ERROR_HANDLER, expected, sizeof(expected), error_hook, (void **)&handler_original)) {
		log_printf("scrdebug: unexpected bytes at the script error handler, script errors are not logged");
		return;
	}
	log_printf("scrdebug: script errors are logged");
#ifndef MW2COOP_IW4X
	static const scrdebug_vm_t vm = {RVA_GET_FUNC_HANDLE, RVA_PROGRAM_BUFFER, RVA_VM_POS, RVA_VM_DEPTH, 0, RVA_VM_FRAME_PTR, VM_FRAME_SIZE};
	static const uint8_t handle_bytes[] = {0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x08, 0x56, 0x57, 0x50};
	if (memcmp(base + RVA_GET_FUNC_HANDLE, handle_bytes, sizeof(handle_bytes)) == 0) {
		scrdebug_stack_init(base, &vm);
		hook_end_load();
	} else {
		log_printf("scrdebug: unexpected bytes at Scr_GetFunctionHandle, no script stacks");
	}
#endif
}
