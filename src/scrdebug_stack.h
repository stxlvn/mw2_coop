#pragma once
#include <stdint.h>

// Where a build's script VM keeps what the call stack needs (rvas from the image base; 0 = not known).
typedef struct {
	uint32_t get_handle;     // int Scr_GetFunctionHandle(const char *file, const char *name): the function's offset in the program buffer
	uint32_t program_buffer; // the pointer to the program buffer
	uint32_t vm_pos;         // the live code position of the running thread (a pointer into the program buffer)
	uint32_t depth;          // function_count (int32): the callers' saved positions are frames 0 .. depth - 1
	uint32_t frames;         // 64 bits: function_frame[] (the first member of each frame is its saved code position)
	uint32_t frame_ptr;      // 32 bits: the pointer one past the top frame (the frames are found below it)
	uint32_t frame_size;
} scrdebug_vm_t;

void scrdebug_stack_init(uint8_t *module_base, const scrdebug_vm_t *where);
void scrdebug_stack_build(void); // at the end of a script load, before the loaded-scripts table is freed
void scrdebug_stack_log(void);
