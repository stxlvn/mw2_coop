// The script call stack of a runtime error or a traced builtin, for both builds: GSC function names are taken from the source text the game compiled (scrdebug_note_source),
// their start offsets in the program buffer from Scr_GetFunctionHandle, and a code position (the live one and the callers' saved ones) is mapped back to file::function.
// Each build says where its VM keeps these things (scrdebug_stack_init). Positions point into the program buffer; functions are compiled one after another, so a position
// belongs to the function with the greatest start offset at or below it. Scr_GetFunctionHandle only works while the loaded-scripts table exists: the owner of the build's
// Scr_EndLoadScripts hook calls scrdebug_stack_build right before it frees that table.
#include <windows.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "scrdebug.h"
#include "scrdebug_stack.h"

static uint8_t *base;
static scrdebug_vm_t vm;

void scrdebug_stack_init(uint8_t *module_base, const scrdebug_vm_t *where)
{
	base = module_base;
	vm = *where;
}

// Function names per script file, from the source text the game compiled.
#define MAX_FILES 512
static struct {
	char *name;  // "maps/_coop", as Scr_GetFunctionHandle wants it
	char **funcs;
	int count;
} files[MAX_FILES];
static int file_count;

// Built at the end of each script load: start offset -> file/function.
static struct func_start {
	uint32_t off;
	uint16_t file;
	uint16_t func;
} *starts;
static int start_count;

static int is_ident(int c)
{
	return isalnum(c) || c == '_';
}

void scrdebug_note_source(const char *ext, const char *text, int len)
{
	char name[MAX_PATH];
	lstrcpynA(name, ext, sizeof(name));
	int n = lstrlenA(name);
	if (n > 4 && !lstrcmpiA(name + n - 4, ".gsc"))
		name[n - 4] = 0;
	int f;
	for (f = 0; f < file_count && lstrcmpiA(files[f].name, name); f++)
		;
	if (f == file_count) {
		if (file_count >= MAX_FILES)
			return;
		files[f].name = _strdup(name);
		file_count++;
	}
	for (int i = 0; i < files[f].count; i++)
		free(files[f].funcs[i]);
	files[f].count = 0;
	int cap = 0;

	// Top-level "name(" is a function definition: GSC has nothing else at brace depth 0 but
	// #include and #using_animtree lines.
	int depth = 0;
	for (int i = 0; i < len && text[i]; i++) {
		char c = text[i];
		if (c == '/' && i + 1 < len && text[i + 1] == '/') {
			while (i < len && text[i] && text[i] != '\n')
				i++;
		} else if (c == '/' && i + 1 < len && text[i + 1] == '*') {
			for (i += 2; i + 1 < len && text[i] && !(text[i] == '*' && text[i + 1] == '/'); i++)
				;
			i++;
		} else if (c == '"') {
			for (i++; i < len && text[i] && text[i] != '"'; i++)
				if (text[i] == '\\')
					i++;
		} else if (c == '{') {
			depth++;
		} else if (c == '}') {
			depth--;
		} else if (depth == 0 && c == '#') {
			while (i < len && text[i] && text[i] != '\n')
				i++;
		} else if (depth == 0 && (isalpha((unsigned char)c) || c == '_') && (i == 0 || !is_ident((unsigned char)text[i - 1]))) {
			int s = i;
			while (i < len && is_ident((unsigned char)text[i]))
				i++;
			int e = i;
			while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n'))
				i++;
			if (i < len && text[i] == '(') {
				if (files[f].count == cap) {
					cap = cap ? cap * 2 : 64;
					files[f].funcs = realloc(files[f].funcs, cap * sizeof(char *));
				}
				char *fn = malloc(e - s + 1);
				for (int k = 0; k < e - s; k++)
					fn[k] = (char)tolower((unsigned char)text[s + k]);
				fn[e - s] = 0;
				files[f].funcs[files[f].count++] = fn;
			}
			i--;
		}
	}
}

static int cmp_start(const void *a, const void *b)
{
	uint32_t x = ((const struct func_start *)a)->off, y = ((const struct func_start *)b)->off;
	return x < y ? -1 : x > y;
}

void scrdebug_stack_build(void)
{
	typedef int (*get_handle_t)(const char *file, const char *name);
	if (!vm.get_handle)
		return;
	get_handle_t get_handle = (get_handle_t)(base + vm.get_handle);
	int total = 0;
	for (int f = 0; f < file_count; f++)
		total += files[f].count;
	free(starts);
	starts = malloc((total ? total : 1) * sizeof(*starts));
	start_count = 0;
	for (int f = 0; f < file_count; f++)
		for (int i = 0; i < files[f].count; i++) {
			int off = get_handle(files[f].name, files[f].funcs[i]);
			if (off > 0)
				starts[start_count++] = (struct func_start){(uint32_t)off, (uint16_t)f, (uint16_t)i};
		}
	qsort(starts, start_count, sizeof(*starts), cmp_start);
	log_printf("scrdebug: %d script functions mapped", start_count);
}

static void describe_pos(const char *what, const char *pos)
{
	const char *program = *(const char *const *)(base + vm.program_buffer);
	if (!pos || !program || pos < program) {
		log_printf("    %s: (no position)", what);
		return;
	}
	uint32_t off = (uint32_t)(pos - program);
	int lo = 0, hi = start_count - 1, best = -1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (starts[mid].off <= off) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	if (best < 0) {
		log_printf("    %s: program+0x%x", what, off);
		return;
	}
	log_printf("    %s: %s::%s +0x%x", what, files[starts[best].file].name,
		   files[starts[best].file].funcs[starts[best].func], off - starts[best].off);
}

void scrdebug_stack_log(void)
{
	if (!vm.vm_pos)
		return;
	describe_pos("at", *(const char *const *)(base + vm.vm_pos));
	int count = *(const int32_t *)(base + vm.depth);
	for (int i = count - 1, shown = 0; i >= 0 && shown < 8; i--, shown++)
		{
		const uint8_t *frame = vm.frame_ptr ? *(const uint8_t *const *)(base + vm.frame_ptr) - (int64_t)(count - i) * vm.frame_size : base + vm.frames + (int64_t)i * vm.frame_size;
		describe_pos("from", *(const char *const *)frame);
	}
}

// Method tracing (QA): "!trace <rva>" in commands.txt points a builtin method table entry
// ({const char *name; void (*func)(scr_entref_t); int developer}, e.g. unlink at rva 0x342c88)
// at a wrapper that logs the entity and the GSC call stack, then calls the original. Scripts
// compiled after that call the wrapper.
#define TRACE_SLOTS 4
#define TRACE_MAX_HITS 40
typedef void (*method_t)(uintptr_t entref);
static method_t trace_orig[TRACE_SLOTS];
static const char *trace_name[TRACE_SLOTS];
static volatile LONG trace_hits[TRACE_SLOTS];
static int trace_count;

static void trace_log(int i, uintptr_t entref)
{
	LONG n = InterlockedIncrement(&trace_hits[i]);
	if (n > TRACE_MAX_HITS)
		return;
	log_printf("TRACE %s #%ld on entity %u (class %u)", trace_name[i], n, (unsigned)(entref & 0xffff),
		   (unsigned)((entref >> 16) & 0xffff));
	scrdebug_stack_log();
}

#define TRACE_WRAP(i)                                    \
	static void trace_wrap##i(uintptr_t entref)      \
	{                                                \
		trace_log(i, entref);                    \
		trace_orig[i](entref);                   \
	}
TRACE_WRAP(0)
TRACE_WRAP(1)
TRACE_WRAP(2)
TRACE_WRAP(3)
static const method_t trace_wraps[TRACE_SLOTS] = {trace_wrap0, trace_wrap1, trace_wrap2, trace_wrap3};

void scrdebug_trace_method(uintptr_t entry_rva)
{
	if (trace_count >= TRACE_SLOTS) {
		log_printf("trace: no free slot");
		return;
	}
	uint8_t *entry = base + entry_rva;
	const char *name = *(const char **)entry;
	method_t *func = (method_t *)(entry + 8);
	if ((uint8_t *)name < base || (uint8_t *)*func < base) {
		log_printf("trace: rva 0x%llx is not a method table entry", (unsigned long long)entry_rva);
		return;
	}
	int i = trace_count++;
	trace_name[i] = name;
	trace_orig[i] = *func;
	DWORD old;
	VirtualProtect(func, sizeof(*func), PAGE_READWRITE, &old);
	*func = trace_wraps[i];
	VirtualProtect(func, sizeof(*func), old, &old);
	log_printf("trace: method %s (rva 0x%llx) is traced", name, (unsigned long long)((uint8_t *)trace_orig[i] - base));
}
