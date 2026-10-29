import re
p = 'src/native.c'
s = open(p, newline='').read()


def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    assert n >= 1, 'missing: ' + old[:90]
    if cnt == 0:
        s = s.replace(old, new)
    else:
        assert n == cnt, ('count %d != %d: ' % (n, cnt)) + old[:90]
        s = s.replace(old, new)


# ---- the register-convention functions of the 32-bit engine: asm stubs + original wrappers
rep('static void (*mouse_read_original)(long long, float *, float *);\nstatic void mouse_read_hook(long long state, float *x, float *y)',
    '''#ifdef _WIN64
static void (*mouse_read_original)(intptr_t, float *, float *);
#else
// 32 bits: the mouse read has the mouse state in ESI and two stack arguments (the caller cleans them): a stub below passes plain cdecl arguments to the hook,
// and the original is called through a wrapper that sets ESI (docs/32bit-native-map/clientctx.md)
static void *mouse_read_target, *mouse_read2_target;
static void mouse_read_original(intptr_t state, float *x, float *y);
static void mouse_read2_original(intptr_t state, float *x, float *y);
#endif
static void mouse_read_hook(intptr_t state, float *x, float *y)''')
rep('static void (*mouse_read2_original)(long long, float *, float *);\nstatic void mouse_read2_hook(long long state, float *x, float *y)',
    '''#ifdef _WIN64
static void (*mouse_read2_original)(intptr_t, float *, float *);
#endif
static void mouse_read2_hook(intptr_t state, float *x, float *y)''')
rep('static uint8_t *(*make_cmd_original)(uint8_t *, int);',
    '''#ifdef _WIN64
static uint8_t *(*make_cmd_original)(uint8_t *, int);
#else
// 32 bits: the usercmd maker takes its output buffer in EAX and the local client in EBX and returns the buffer in EAX
static void *make_cmd_target;
static uint8_t *make_cmd_original(uint8_t *out, int lc);
#endif''')

# the asm block goes before contexts_install
blk = r'''#ifndef _WIN64
// ---- 32 bits: the stubs the register-convention hooks go through, and the wrappers that call the originals (a C function cannot receive ESI or EAX/EBX)
void n32_mouse_hook(intptr_t state, float *x, float *y);
void n32_mouse2_hook(intptr_t state, float *x, float *y);
uint8_t *n32_make_cmd_hook(uint8_t *out, int lc);
void n32_mouse_hook(intptr_t state, float *x, float *y)
{
	mouse_read_hook(state, x, y);
}
void n32_mouse2_hook(intptr_t state, float *x, float *y)
{
	mouse_read2_hook(state, x, y);
}
uint8_t *n32_make_cmd_hook(uint8_t *out, int lc)
{
	return make_cmd_hook(out, lc);
}
__asm__(".text\n"
	".globl _n32_mouse_stub\n"
	"_n32_mouse_stub:\n"
	"	pushl 8(%esp)\n"
	"	pushl 8(%esp)\n"
	"	pushl %esi\n"
	"	call _n32_mouse_hook\n"
	"	addl $12, %esp\n"
	"	ret\n"
	".globl _n32_mouse2_stub\n"
	"_n32_mouse2_stub:\n"
	"	pushl 8(%esp)\n"
	"	pushl 8(%esp)\n"
	"	pushl %esi\n"
	"	call _n32_mouse2_hook\n"
	"	addl $12, %esp\n"
	"	ret\n"
	".globl _n32_make_cmd_stub\n"
	"_n32_make_cmd_stub:\n"
	"	pushl %ebx\n"
	"	pushl %eax\n"
	"	call _n32_make_cmd_hook\n"
	"	addl $8, %esp\n"
	"	ret\n");
extern char n32_mouse_stub[], n32_mouse2_stub[], n32_make_cmd_stub[];
static void mouse_read_original(intptr_t state, float *x, float *y)
{
	__asm__ volatile("pushl %3\n\tpushl %2\n\tcall *%1\n\taddl $8, %%esp" : : "S"(state), "r"(mouse_read_target), "r"(x), "r"(y) : "eax", "ecx", "edx", "memory", "cc");
}
static void mouse_read2_original(intptr_t state, float *x, float *y)
{
	__asm__ volatile("pushl %3\n\tpushl %2\n\tcall *%1\n\taddl $8, %%esp" : : "S"(state), "r"(mouse_read2_target), "r"(x), "r"(y) : "eax", "ecx", "edx", "memory", "cc");
}
static uint8_t *make_cmd_original(uint8_t *out, int lc)
{
	uint8_t *r;
	__asm__ volatile("call *%2" : "=a"(r) : "a"(out), "r"(make_cmd_target), "b"(lc) : "ecx", "edx", "memory", "cc");
	return r;
}

// the client-side count-of-local-players reads (docs/32bit-native-map/clientctx.md): VA, instruction length, offset of the absolute address
static const struct {
	uint32_t va;
	uint8_t length, disp;
} client_n_sites32[] = {
	{0x4818b9, 5, 1}, {0x43a926, 6, 2}, {0x55b7fa, 6, 2}, {0x55ea88, 6, 2}, {0x41685e, 6, 2}, {0x433f9a, 6, 2}, {0x434159, 6, 2}, {0x4341eb, 6, 2},
	{0x4342b4, 6, 2}, {0x434327, 6, 2}, {0x434378, 6, 2}, {0x56558b, 6, 2}, {0x5656be, 6, 2}, {0x4b74ed, 6, 2}, {0x454cb9, 6, 2}, {0x4b5594, 6, 2},
	{0x4978b4, 6, 2}, {0x497a44, 6, 2}, {0x497ac4, 6, 2}, {0x48190a, 6, 2}, {0x56e414, 6, 2}, {0x570bf9, 6, 2}, {0x4893d2, 6, 2}, {0x4e7632, 6, 2},
	{0x4b5062, 6, 2}, {0x471a64, 6, 2}, {0x471ae4, 6, 2}, {0x471b47, 6, 2}, {0x4da584, 6, 2}, {0x428666, 6, 2}, {0x4286ba, 6, 2}, {0x430868, 6, 2},
	{0x43282c, 6, 2}, {0x432888, 6, 2}, {0x4790ab, 6, 2},
};
static int32_t n_client_copy = 1; // what the client code reads instead of the number of local players
static int32_t n_zero_copy;       // what the server's entity list reads (a count of 0: no local player is left out)
static void absolute_patch(uint8_t *site, int disp, const void *to)
{
	DWORD prot;
	j_save(site, 8);
	VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &prot);
	uint32_t v = (uint32_t)(uintptr_t)to;
	memcpy(site + disp, &v, 4);
	VirtualProtect(site, 8, prot, &prot);
	FlushInstructionCache(GetCurrentProcess(), site, 8);
}
static void client_n_install(void)
{
	int done = 0;
	for (size_t i = 0; i < sizeof(client_n_sites32) / sizeof(client_n_sites32[0]); i++) {
		uint8_t *site = (uint8_t *)(uintptr_t)client_n_sites32[i].va - 0x400000 + (uintptr_t)base;
		uint32_t at;
		memcpy(&at, site + client_n_sites32[i].disp, 4);
		if (at != (uint32_t)(uintptr_t)(base + RVA_LOCAL_PLAYERS))
			continue; // not the instruction the build has
		absolute_patch(site, client_n_sites32[i].disp, &n_client_copy);
		done++;
	}
	log_printf("native: %d of %d client reads of the local player count point at a copy (1)", done, (int)(sizeof(client_n_sites32) / sizeof(client_n_sites32[0])));
}
static void list_players_install(void)
{
	uint8_t *site = base + 0x25cc10; // mov ebp, [count] in the entity list of the snapshot: the local players' own entities stay out of it
	uint32_t at;
	memcpy(&at, site + 2, 4);
	if (site[0] != 0x8b || site[1] != 0x2d || at != (uint32_t)(uintptr_t)(base + RVA_LOCAL_PLAYERS)) {
		log_printf("native: entity list patch: unexpected code");
		return;
	}
	absolute_patch(site, 2, &n_zero_copy);
}

static int hooks_apply32_contexts(void)
{
	static const struct {
		uint32_t site;
		void *hook;
		void **original;
	} sites[] = {
		{0x7af60, cg_init_all_hook, (void **)&cg_init_all_original},
		{0x55484, snapshot_take_hook, (void **)&snapshot_take_original},
		{0x25cab8, ps_publish_hook, (void **)&ps_publish_original},
		{0x2d155, server_cmds_hook, (void **)&server_cmds_original},
		{0xd6794, usercmd_step_hook, (void **)&usercmd_step_original},
		{0x25ccfa, snapshot_msg_hook, (void **)&snapshot_msg_original},
		{0x96dd7, client_frame_hook, (void **)&client_frame_original},
		{0x179be9, n32_make_cmd_stub, &make_cmd_target},
		{0x59d6c, server_step_hook, (void **)&server_step_original},
		{0x179000, n32_mouse_stub, &mouse_read_target},
		{0x1799e8, n32_mouse2_stub, &mouse_read2_target},
	};
	for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
		if (!jhook_call(base + sites[i].site, sites[i].hook, sites[i].original)) {
			log_printf("native: no call at rva 0x%x, context hook %zu off", sites[i].site, i);
			return 0;
		}
	swap_entities = cfg("swapent", 0);
	skip_entity = cfg("skipent", 1);
	log_entities = cfg("entdraw", 0);
	p1pad = cfg("p1pad", -1);
	pad_dev[0] = cfg("p0pad", -1);
	skip_hud_mask = cfg("skiphud", 0);
	skip_ctx = cfg("skipctx", 1);
	ctx_on = 1;
	log_printf("native: 32-bit client contexts on (call sites redirected)");
	return 1;
}
#endif

'''
a = s.index('static void contexts_install(void)')
# find the comment block before it, if any: insert directly before the function
s = s[:a] + blk + s[a:]

# the 64-bit client_n_install / list_players_install stubs for 32 bits go away
s = re.sub(r'#else\nstatic void client_n_install\(void\)\n\{\n\tlog_printf\("native: client_n_install is not ported to the 32-bit build yet"\);\n\}\n#endif\n', '#endif\n', s)
s = re.sub(r'#else\nstatic void list_players_install\(void\)\n\{\n\tlog_printf\("native: list_players_install is not ported to the 32-bit build yet"\);\n\}\n#endif\n', '#endif\n', s)

open(p, 'w', newline='').write(s)
print('ok')
