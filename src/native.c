// Native split screen, experiment 1 (docs/native-splitscreen-plan.md, phase 3 first): can the renderer take two views in one frame?
//
// The cgame's draw-active-frame (rva 0xbca00, local client number first) builds the view and queues the 3D scene; the renderer's view setup (rva 0x7e40, from
// rva 0x8150) reads the dvar r_subwindow, a vec4 (left, right, top, bottom: fractions of the picture), and corrects the projection for that rectangle. This
// hook runs the draw twice per frame, the first with the top half as r_subwindow and the second with the bottom half. settings.ini [native] experiment=1
// switches it on; with 2 the second view is turned around (yaw + 180) to show that the views are drawn separately. 64-bit only for now.
#define COBJMACROS
#include <windows.h>
#include <tlhelp32.h>
#include <math.h>
#include <d3d9.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "localize.h"
#include "menus_ui.h"
#include "native.h"
#include "padopts.h"
#include "padglyphs.h"
#include "rumble.h"
#include "shot.h"
#include "d3dcap.h"

// Both builds compile this file; the 64-bit addresses below are used by the 64-bit build, the 32-bit ones are found in docs/32bit-native-map/ and come in piece by piece
// (NATIVE32_READY in native_install says what is switched on).
#ifdef _WIN64
#define RVA_CG_DRAW 0x0bca00         // void (unsigned local_client, int, int): the cgame's frame; it calls the begin-frame below in the middle
#define RVA_BEGIN_FRAME 0x00c840     // void (void): swaps the renderer's two frame data sets and resets their command buffers: once per frame
#define RVA_SUBWINDOW_DVAR 0x4116158 // dvar_t *, the value (4 floats) at +0x10

static const uint8_t cg_draw_bytes[] = {0x48, 0x8b, 0xc4, 0x55, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0xc0, 0x00, 0x00, 0x00};
static const uint8_t begin_frame_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
#define RVA_CG_TIME 0x422434 // cg.time
#else
// The 32-bit official build (docs/32bit-native-map/render.md): the cgame's DrawActiveFrame is cdecl (local client, int, int); its first five bytes (sub esp,0xc; push ebx; push ebp) are clean to copy.
// The begin-frame is not hooked at its start (its first instructions are a push run and a relative call) but at its call in the cgame draw.
#define RVA_CG_DRAW XR(0x0666c0)
#define RVA_BEGIN_FRAME_CALL XR(0x066831) // the call of the begin-frame (VA 0x50d800) inside the cgame draw
#define RVA_SUBWINDOW_DVAR XR(0x1b19f98)
#define RVA_CG_TIME XR(0x374c20)
static const uint8_t cg_draw_bytes[] = {0x83, 0xec, 0x0c, 0x53, 0x55};
#endif
#define RVA_CG_PS_ORIGIN (RVA_CG_TIME + 0x28) // the origin in cg.ps (the camera of the live context): 0x1c into the playerState, which starts 0xc after cg.time, in both builds
static void (*cg_draw_original)(unsigned, int, int);
static void (*begin_frame_original)(void);
static uint8_t *base;
#ifdef _WIN64
#define CTX_IP(c) ((uintptr_t)(c).Rip)
#define CTX_SP(c) ((uintptr_t)(c).Rsp)
#else
#define CTX_IP(c) ((uintptr_t)(c).Eip)
#define CTX_SP(c) ((uintptr_t)(c).Esp)
#endif
#ifdef _WIN64
#define RVA_LOCAL_PLAYERS_STORE 0x1714a6
#define RVA_SV_ADD_COMMAND 0x232c30
#define RVA_COM_ERROR 0x1fb5f0 // void (int code, const char *format, ...)
#define RVA_SV_CLIENTS 0x19ada98
#define SV_CLIENT_STRIDE 0x1678
#define RVA_COOP_BYTE 0x52fb5c
#define RVA_LOCAL_PLAYERS 0x3c3eb0
#define RVA_COOP_SLOTS 0x540bb0 // slot p: type int at +p*0x2650
#define SLOT_STRIDE 0x2650
#define RVA_SV_CLIENT_THINK 0x22d000 // void (int lc, const usercmd_t *): copies the cmd into client_t[lc] + 0x1630 and runs the player's think
#define RVA_KEY_CATCHERS 0x533cb0
#define RVA_CLS_STATE 0x533cb8
#define RVA_ACC_PITCH 0x533d14 // the live view angle accumulators (pitch, then yaw)
#define ACC_IN_CLS (0x533d14 - 0x533cb8)
#define RVA_CG_CLIENTNUM 0x40caa0 // the cgame's copy of the client number; +4: its local client number
#define RVA_CL_PS 0x535d38 // the live cl block's playerstate
#define CL_RING_MASK 127
#define CL_CMDNUM 0x2000
#define CL_SENT 0x2004
#define CL_BLOCK_PS 0x2018
#define RVA_NOPREDICT_DVAR 0x51e028
#define RVA_FOV_DVAR 0x51e1a8
#define RVA_DRAWGUN_DVAR 0x51e120
#define RVA_WAIT_TYPE 0x300a0
#define RVA_CLIENT_INIT_A 0xb8830 // the per-client parts of the cgame init
#define RVA_CLIENT_INIT_B 0xb87b0
#define RVA_COOP_DVAR 0x1905308
#define MSG_T_SIZE 0x30
#define RVA_CENT 0x441070 // cg_entities
#define CENT_SIZE 0x1a8
#define CENT_TYPE 0xe0
#define CENT_FLAGS 0xec
#define CENT_VALID 0x18c
#define CF_YAW0 10 // float indices: yaw of the pose, of the current state, of the next state
#define CF_YAW1 42
#define CF_YAW2 73
#define CF_ORG0 6 // origin of the pose, of the current state, of the next state
#define CF_ORG1 32
#define CF_ORG2 63
#define P0_HDR 0x18 // the pose header a copy of the other player's entity keeps
#define P0_REST 0x30 // the rest of the pose
#define P0_REST_LEN 0x30
#define P0_TREE 0x170 // the two dwords with the animation tree reference
#define SLOT_END_PAST(q) ((q) > base + 0x545857)
#define RVA_AIM_GLOB 0x3cf180 // aaGlob
#define RVA_AIM_TARGET_GLOB 0x3d0530
#define RVA_LEVEL_NUM_ENT 0xd96e80
#define RVA_GENTITIES 0xd9e070
#define GENT_SIZE 0x2a0
#define GENT_SENTIENT 0x118
#define SENT_TEAM 8
#define GENT_HEALTH 0x164
#define GENT_SEEN 0x150
#define WD_AIM_RANGE 0x42c // the weapon def's aim assist ranges: aiming down the sight, auto aim
#define WD_AIM_RANGE_ADS 0x430
#define WD_AUTO_RANGE 0x428
#define WEAPON_DEF(w) (((const uint8_t *(*)(int64_t))(base + 0x85850))(w))
#define RVA_CG_ASPECT 0x43c640 // the cgame's aspect
#define RVA_PIXEL_ASPECT 0x411373c // the pixel aspect of the 2D
#define RVA_VIDEO_ASPECT 0x4113738
#define RVA_WAIT_ALPHA_SITE 0x94039
#define RVA_ENT_FLAGS 0x52c300 // the bit set of entities the snapshot transition flagged for the cleanup
#define RVA_CLIENT_DOBJ 0x1fece0 // DObj *(int entity): the client's DObj table
#define RVA_SERVER_DOBJ 0x1fee30
#define RVA_ENT_INFO 0x234520 // the server's entity info of an entity
#define RVA_SDOBJ_INFO 0x2968e0 // the first field of a DObj (the tree)
#define RVA_DOBJ_CLRBIT 0x1fefa0
#define DOBJ_NUMMODELS 0xd
#define DOBJ_MODELS 0xa8
#define P0GENERAL_RET(r) ((r) >= base + 0x9ea20 && (r) < base + 0x9ea20 + 1102)
#else
// the 32-bit official build: docs/32bit-native-map/clientctx.md
#define RVA_LOCAL_PLAYERS_STORE XR(0xbf9db)
#define RVA_SV_ADD_COMMAND XR(0xa550)
#define RVA_COM_ERROR XR(0x584f0)
#define RVA_SV_CLIENTS XR(0x1723988)
#define SV_CLIENT_STRIDE 0x1674
#define RVA_COOP_BYTE XR(0x48897c)
#define RVA_LOCAL_PLAYERS XR(0x338c6c)
#define RVA_COOP_SLOTS XR(0x48f560)
#define SLOT_STRIDE 0x263c
#define RVA_SV_CLIENT_THINK XR(0xe7750)
#define RVA_KEY_CATCHERS XR(0x525bb8)
#define RVA_CLS_STATE XR(0x525bc0)
#define RVA_ACC_PITCH XR(0x525c34)
#define ACC_IN_CLS (0x525c34 - 0x525bc0)
#define RVA_CG_CLIENTNUM XR(0x35f2a0)
#define RVA_CL_PS XR(0x526c58)
#define CL_RING_MASK 63
#define CL_CMDNUM 0x1000
#define CL_SENT 0x1004
#define CL_BLOCK_PS 0x1018
#define RVA_NOPREDICT_DVAR XR(0x45e8e8)
#define RVA_FOV_DVAR XR(0x45e968)
#define RVA_DRAWGUN_DVAR XR(0x35efec)
#define RVA_WAIT_TYPE XR(0x12ac60)
#define RVA_CLIENT_INIT_A XR(0x1dfa0)
#define RVA_CLIENT_INIT_B XR(0x956e0)
#define RVA_COOP_DVAR XR(0x167f0ec)
#define MSG_T_SIZE 0x28
#define RVA_CENT XR(0x392848)
#define CENT_SIZE 0x198
#define CENT_TYPE 0xd4
#define CENT_FLAGS 0xe0
#define CENT_VALID 0x180
#define CF_YAW0 9
#define CF_YAW1 39
#define CF_YAW2 70
#define CF_ORG0 5
#define CF_ORG1 29
#define CF_ORG2 60
#define P0_HDR 0x14
#define P0_REST 0x2c
#define P0_REST_LEN 0x2c
#define P0_TREE 0x164
#define SLOT_END_PAST(q) ((q) >= base + XR(0x4941e0))
#define RVA_AIM_GLOB XR(0x33ba90)
#define RVA_AIM_TARGET_GLOB XR(0x33cec0)
#define RVA_LEVEL_NUM_ENT XR(0xca3c18)
#define RVA_GENTITIES XR(0xaa76b8)
#define GENT_SIZE 0x270
#define GENT_SENTIENT 0x110
#define SENT_TEAM 4
#define GENT_HEALTH 0x14c
#define GENT_SEEN 0x138
#define WD_AIM_RANGE 0x2e4
#define WD_AIM_RANGE_ADS 0x2e8
#define WD_AUTO_RANGE 0x2e0
#define WEAPON_DEF(w) (*(const uint8_t *const *)(base + XR(0x1669d08) + (size_t)(w) * 4)) // the table of weapon def pointers
#define RVA_CG_ASPECT XR(0x35f018)
#define RVA_PIXEL_ASPECT XR(0x188edb8)
#define RVA_VIDEO_ASPECT XR(0x188edb4)
#define RVA_WAIT_ALPHA_SITE 0x15ef17 // (a call site: hook_calls32 translates it)
#define RVA_ENT_FLAGS XR(0x46b280)
#define RVA_CLIENT_DOBJ XR(0xf3c70)
#define RVA_SERVER_DOBJ XR(0x95740)
#define RVA_ENT_INFO XR(0xdd30)
#define RVA_SDOBJ_INFO XR(0x3f4e0)
#define RVA_DOBJ_CLRBIT XR(0x10f50)
#define DOBJ_NUMMODELS 9
#define DOBJ_MODELS 0x98
// the add-by-type cases that add the "general" entity types: 0x561d00 (case 0), 0x561d60 (case 2), 0x5621d0 (case 5)
#define P0GENERAL_RET(r) (((r) >= base + XR(0x161d00) && (r) < base + XR(0x161d5a)) || ((r) >= base + XR(0x161d60) && (r) < base + XR(0x161e17)) || ((r) >= base + XR(0x1621d0) && (r) < base + XR(0x162294)))
#endif
static char settings_path[MAX_PATH];
static int mode;
static int second_pass;

// The journal of the code this file patches: native_deactivate() puts every byte back, so a session that used the split screen can go on with the game's normal modes (the network
// co-op among them) untouched. Every patch goes through jhook_* or j_save().
static struct {
	uint8_t *at;
	uint8_t orig[32];
	size_t len;
	int group;
} journal[160];
static int njournal;
static int jgroup; // 1: the patches that make the game a two local players game (put back when the session ends), 0: the hooks that stay and do nothing while no session runs
static void j_save(uint8_t *at, size_t len)
{
	if (njournal >= (int)(sizeof(journal) / sizeof(journal[0])) || len > sizeof(journal[0].orig))
		return;
	for (int i = 0; i < njournal; i++)
		if (journal[i].at == at && journal[i].len >= len)
			return; // saved already (the first copy is the game's)
	journal[njournal].at = at, journal[njournal].len = len, journal[njournal].group = jgroup;
	memcpy(journal[njournal].orig, at, len);
	njournal++;
}
static void journal_restore(int group)
{
	int kept = 0;
	for (int i = njournal - 1; i >= 0; i--) {
		if (journal[i].group != group)
			continue;
		DWORD old;
		if (VirtualProtect(journal[i].at, journal[i].len, PAGE_EXECUTE_READWRITE, &old)) {
			memcpy(journal[i].at, journal[i].orig, journal[i].len);
			VirtualProtect(journal[i].at, journal[i].len, old, &old);
			FlushInstructionCache(GetCurrentProcess(), journal[i].at, journal[i].len);
		}
		journal[i].at = NULL;
	}
	for (int i = 0; i < njournal; i++)
		if (journal[i].at)
			journal[kept++] = journal[i];
	njournal = kept;
}
static int jhook_function(uint8_t *target, const uint8_t *expected, size_t len, void *replacement, void **original)
{
	j_save(target, len);
	return hook_function(target, expected, len, replacement, original);
}
static int jhook_call(uint8_t *site, void *replacement, void **original)
{
	j_save(site, 5);
	return hook_call(site, replacement, original);
}
static void jhook_jump(uint8_t *target, size_t len, const void *replacement, uint8_t filler)
{
	j_save(target, len);
	hook_jump(target, len, replacement, filler);
}

// The settings of the split screen: settings.ini [native] for the QA runs; a session started from the menu uses the values the feature ships with (from_menu) unless the file says otherwise
int native_active; // (read by the asm stub of the 32-bit view model hook)
#ifdef _WIN64
void *snd_ctx_ptr, *snd_count_orig; // (the sound counter's stub, see snd_count_stub)
volatile LONG snd_counts[2];
struct snd_name { const char *name; LONG n[2]; };
static struct snd_name snd_names[512];
#endif
static int from_menu;
static int menu_pad = -1, menu_pad0 = -1; // the lobby's devices of player 2 and player 1 (-1: the keyboard and mouse)
static int cfg(const char *key, int def)
{
	static const struct {
		const char *key;
		int value;
	} shipped[] = {{"experiment", 10}, {"players", 2}, {"coopdvar", 1}, {"p1cmd", 1}, {"listplayers", 1}, {"p1yawdeg", 0}, {"p0turn", 0}, {"ctx", 1}};
	if (from_menu) {
		if (!lstrcmpA(key, "p1pad"))
			return menu_pad;
		if (!lstrcmpA(key, "p0pad"))
			return menu_pad0;
		for (size_t i = 0; i < sizeof(shipped) / sizeof(shipped[0]); i++)
			if (!lstrcmpA(key, shipped[i].key))
				def = shipped[i].value;
	}
	return (int)GetPrivateProfileIntA("native", key, def, settings_path);
}
// cfg() reads settings.ini from the disk on every call; the per-frame paths read a key once per session with CFG_ONCE (a session start bumps cfg_gen)
static unsigned cfg_gen = 1;
#define CFG_ONCE(key, def) __extension__({ static unsigned g_; static int v_; if (g_ != cfg_gen) { v_ = cfg(key, def); g_ = cfg_gen; } v_; })

static float *subwindow(void)
{
	uint8_t *dvar = *(uint8_t **)(base + RVA_SUBWINDOW_DVAR);
	return dvar ? (float *)(dvar + 0x10) : NULL;
}

// ---- the compositor (mode 2): one scene per frame, the two halves kept between frames
// Each frame the cgame draws one player's view into one half of the picture (r_subwindow). The frame's end (EndScene of the game's device, on the render thread)
// copies that half into a texture of its own and fills the other half from the other texture, so the picture always has both views whole.
#define RVA_D3D_DEVICE 0x410fd20
static volatile LONG ring[8];
static volatile DWORD render_tid;
static volatile LONG ring_head, ring_tail; // frames queued by the main thread, and the halves they were drawn into, in order
// Mode 10 tells a frame's half on the render thread from the frame itself, not from a queue (a queue is off by one for good when a frame submitted before the split is still in flight
// as the split starts: the halves swapped for the whole session): the scene struct holds the cgame's time of its frame at +0x180; the main thread notes (time, half) of every split frame
// it draws, the render thread looks its frame's time up (the two contexts' times differ by a few ms and never match).
static struct {
	volatile int time;
	volatile int half;
} half_tbl[64];
static volatile LONG half_tbl_head;
static volatile int render_half = -1; // render thread only: the half of the frame being drawn, -1 when it is not a split frame
static void half_note(int time, int half)
{
	LONG i = InterlockedIncrement(&half_tbl_head) & 63;
	half_tbl[i].half = half;
	half_tbl[i].time = time;
}
static int half_find(int time)
{
	LONG head = half_tbl_head;
	for (int k = 0; k < 64; k++) {
		int i = (int)((head - k) & 63);
		if (half_tbl[i].time == time)
			return half_tbl[i].half;
	}
	return -1;
}
static IDirect3DDevice9 *comp_device;
static IDirect3DTexture9 *comp_tex[2];
static IDirect3DSurface9 *comp_surf[2];
static int comp_have[2];
static UINT comp_w, comp_h;
// mode 10: the engine's 2D pass of the scene (rva 0x63680) is drawn into a layer of its own (one per half), cleared to transparent; the compositor lays the top rows of it over the squeezed view
static IDirect3DTexture9 *lay_tex[2];
static IDirect3DSurface9 *lay_surf[2];
static int lay_have[2];
static IDirect3DStateBlock9 *comp_sb;
static volatile LONG layer_pass; // the render thread is inside the 2D pass
static volatile DWORD scope_tick[2]; // per half: when a scope's lens rectangle was last seen
static volatile int frame_scope; // render thread: a lens rectangle was set in the frame being drawn
static volatile DWORD split_tick; // when the picture was last split (the intro's title lines have random glyphs, which no comparison can match: for the first seconds the bands are cut out whatever the layers hold)
static IDirect3DPixelShader9 *dedupe_ps;
static int dedupe_tried;

static void comp_release(void)
{
	for (int i = 0; i < 2; i++) {
		if (comp_surf[i])
			IDirect3DSurface9_Release(comp_surf[i]);
		if (comp_tex[i])
			IDirect3DTexture9_Release(comp_tex[i]);
		comp_surf[i] = NULL, comp_tex[i] = NULL, comp_have[i] = 0;
		if (lay_surf[i])
			IDirect3DSurface9_Release(lay_surf[i]);
		if (lay_tex[i])
			IDirect3DTexture9_Release(lay_tex[i]);
		lay_surf[i] = NULL, lay_tex[i] = NULL, lay_have[i] = 0;
	}
	if (comp_sb)
		IDirect3DStateBlock9_Release(comp_sb);
	comp_sb = NULL;
	if (dedupe_ps)
		IDirect3DPixelShader9_Release(dedupe_ps);
	dedupe_ps = NULL;
	dedupe_tried = 0;
}

// The global texts (the objective, the subtitles, the title lines of an intro) are drawn by both contexts: in the bottom half they are left out where the two layers hold the same picture, in
// a few bands of the screen where those texts live (a pixel shader compares the layers; the per-player parts of the HUD differ between the layers or lie outside the bands, the crosshair is
// outside them). The shader is compiled from HLSL by the system's d3dcompiler_47.dll (target ps_2_0); without it the objective's strip is simply cut out.
static void dedupe_make(IDirect3DDevice9 *dev)
{
	if (dedupe_tried)
		return;
	dedupe_tried = 1;
	typedef HRESULT(WINAPI * compile_t)(const void *, SIZE_T, const char *, const void *, void *, const char *, const char *, UINT, UINT, void **, void **);
	HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
	compile_t compile = m ? (compile_t)GetProcAddress(m, "D3DCompile") : NULL;
	if (!compile) {
		log_printf("native: no d3dcompiler, the global texts are only cut out of the bottom half in the objective's strip");
		return;
	}
	static const char src[] =
		"sampler2D A : register(s0);\n"
		"sampler2D B : register(s1);\n"
		"float4 band1 : register(c0);\n"
		"float4 band2 : register(c1);\n"
		"float4 band3 : register(c2);\n"
		"float4 force : register(c3);\n"
		"float4 fill : register(c4);\n"
		"float core(float2 uv)\n"
		"{\n"
		"\tfloat4 t = tex2D(B, uv);\n"
		"\treturn step(0.5, t.a) * step(0.4, max(t.r, max(t.g, t.b)));\n"
		"}\n"
		"float4 main(float2 uv : TEXCOORD0) : COLOR\n"
		"{\n"
		"\tfloat4 a = tex2D(A, uv);\n"
		"\tfloat4 b = tex2D(B, uv);\n"
		"\tfloat2 px = float2(1.0 / 1920.0, 1.0 / 1080.0);\n"
		"\tfloat near = core(uv);\n"
		"\tnear = max(near, core(uv + px * float2(-2, 0)));\n"
		"\tnear = max(near, core(uv + px * float2(-1, 0)));\n"
		"\tnear = max(near, core(uv + px * float2(1, 0)));\n"
		"\tnear = max(near, core(uv + px * float2(2, 0)));\n"
		"\tnear = max(near, core(uv + px * float2(0, -2)));\n"
		"\tnear = max(near, core(uv + px * float2(0, -1)));\n"
		"\tnear = max(near, core(uv + px * float2(0, 1)));\n"
		"\tnear = max(near, core(uv + px * float2(0, 2)));\n"
		"\tnear = max(near, core(uv + px * float2(-1, -1)));\n"
		"\tnear = max(near, core(uv + px * float2(1, -1)));\n"
		"\tnear = max(near, core(uv + px * float2(-1, 1)));\n"
		"\tnear = max(near, core(uv + px * float2(1, 1)));\n"
		"\tnear = max(near, core(uv + px * float2(-3, 0)));\n"
		"\tnear = max(near, core(uv + px * float2(3, 0)));\n"
		"\tfloat4 d = abs(a - b);\n"
		"\tfloat equal = step(d.x + d.y + d.z + d.w, 0.06) * step(0.03, a.a) * step(0.03, b.a);\n"
		"\tfloat solid = step(0.25, a.a);\n"
		"\tfloat same = max(force.x * step(0.02, a.a), max(near * solid, equal));\n"
		"\tfloat in1 = step(band1.x, uv.x) * step(uv.x, band1.y) * step(band1.z, uv.y) * step(uv.y, band1.w);\n"
		"\tfloat in2 = step(band2.x, uv.x) * step(uv.x, band2.y) * step(band2.z, uv.y) * step(uv.y, band2.w);\n"
		"\tfloat in3 = step(band3.x, uv.x) * step(uv.x, band3.y) * step(band3.z, uv.y) * step(uv.y, band3.w);\n"
		"\tfloat kill = same * saturate(in1 + in2 + in3) * step(0.02, a.a);\n"
		"\treturn a * (1.0 - kill) + float4(0, 0, 0, 1) * (kill * fill.x);\n"
		"}\n";
	void *code = NULL, *errors = NULL;
	HRESULT hr = E_FAIL;
	static const char *const targets[] = {"ps_2_b", "ps_2_a", "ps_2_0"};
	for (int t = 0; t < 3 && (FAILED(hr) || !code); t++) {
		hr = compile(src, sizeof(src) - 1, "dedupe", NULL, NULL, "main", targets[t], 0, 0, &code, &errors);
		if (SUCCEEDED(hr) && code && FAILED(IDirect3DDevice9_CreatePixelShader(dev, (const DWORD *)((void *(*)(void *))(*(void ***)code)[3])(code), &dedupe_ps))) {
			((ULONG(*)(void *))(*(void ***)code)[2])(code); // this device does not take that target: the next one
			code = NULL;
			hr = E_FAIL;
		}
	}
	if (FAILED(hr) || !code) {
		log_printf("native: the de-duplication shader did not compile (%08lx)", (unsigned long)hr);
		if (errors) {
			void **vt = *(void ***)errors;
			log_printf("native: %s", (const char *)((void *(*)(void *))vt[3])(errors));
			((ULONG(*)(void *))vt[2])(errors);
		}
		return;
	}
	void **vt = *(void ***)code;
	if (!dedupe_ps) {
		const DWORD *bytes = (const DWORD *)((void *(*)(void *))vt[3])(code);
		if (FAILED(IDirect3DDevice9_CreatePixelShader(dev, bytes, &dedupe_ps)))
			log_printf("native: the de-duplication shader was refused by the device");
	}
	if (dedupe_ps)
		log_printf("native: de-duplication of the global texts on");
	((ULONG(*)(void *))vt[2])(code);
	if (errors) {
		void **ev = *(void ***)errors;
		((ULONG(*)(void *))ev[2])(errors);
	}
}

// The mission's title lines are drawn by context 0 with the full-screen 2D placement (hudelem_hook): the rows of its layer below its half hold them, and they are laid over the bottom
// half texture (once per frame of the bottom half, after its own 2D), so the lines come where and as big as they are in single player.
static volatile DWORD title_tick; // when a title line was last drawn
static void overlay_titles(IDirect3DDevice9 *dev)
{
	IDirect3DSurface9 *old_rt = NULL, *old_ds = NULL;
	if (!comp_sb && FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &comp_sb)))
		return;
	IDirect3DStateBlock9_Capture(comp_sb);
	IDirect3DDevice9_GetRenderTarget(dev, 0, &old_rt);
	IDirect3DDevice9_GetDepthStencilSurface(dev, &old_ds);
	IDirect3DDevice9_SetRenderTarget(dev, 0, comp_surf[1]);
	IDirect3DDevice9_SetDepthStencilSurface(dev, NULL);
	D3DVIEWPORT9 vp = {0, 0, comp_w, comp_h / 2, 0.0f, 1.0f};
	IDirect3DDevice9_SetViewport(dev, &vp);
	if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
		static const struct {
			D3DRENDERSTATETYPE s;
			DWORD v;
		} rs[] = {{D3DRS_ZENABLE, 0}, {D3DRS_ZWRITEENABLE, 0}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_ALPHATESTENABLE, 0}, {D3DRS_ALPHABLENDENABLE, 1},
			  {D3DRS_SRCBLEND, D3DBLEND_ONE}, {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {D3DRS_SEPARATEALPHABLENDENABLE, 0}, {D3DRS_BLENDOP, D3DBLENDOP_ADD},
			  {D3DRS_STENCILENABLE, 0}, {D3DRS_SCISSORTESTENABLE, 0}, {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FOGENABLE, 0}, {D3DRS_LIGHTING, 0}, {D3DRS_SRGBWRITEENABLE, 0}};
		for (size_t i = 0; i < sizeof(rs) / sizeof(rs[0]); i++)
			IDirect3DDevice9_SetRenderState(dev, rs[i].s, rs[i].v);
		IDirect3DDevice9_SetVertexShader(dev, NULL);
		IDirect3DDevice9_SetPixelShader(dev, NULL);
		IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
		IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)lay_tex[0]);
		IDirect3DDevice9_SetTexture(dev, 1, NULL);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, 0);
		float w = (float)comp_w - 0.5f, h = (float)(comp_h / 2) - 0.5f;
		float q[4][6] = {{-0.5f, -0.5f, 0, 1, 0, 0.5f}, {w, -0.5f, 0, 1, 1, 0.5f}, {-0.5f, h, 0, 1, 0, 1.0f}, {w, h, 0, 1, 1, 1.0f}}; // the bottom half of the layer
		IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(q[0]));
		IDirect3DDevice9_EndScene(dev);
	}
	IDirect3DDevice9_SetDepthStencilSurface(dev, old_ds);
	IDirect3DDevice9_SetRenderTarget(dev, 0, old_rt);
	IDirect3DStateBlock9_Apply(comp_sb);
	if (old_rt)
		IDirect3DSurface9_Release(old_rt);
	if (old_ds)
		IDirect3DSurface9_Release(old_ds);
}

// Lays the top half of a 2D layer (premultiplied alpha, see layer_fix) over the half texture with the fixed pipeline. Everything the game had set on the device is kept in a state block
// and put back, so the engine's own state caching is not fooled.
static int layer_full;
static void overlay_layer(IDirect3DDevice9 *dev, int half)
{
	IDirect3DSurface9 *old_rt = NULL, *old_ds = NULL;
	if (!comp_sb && FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &comp_sb)))
		return;
	IDirect3DStateBlock9_Capture(comp_sb);
	IDirect3DDevice9_GetRenderTarget(dev, 0, &old_rt);
	IDirect3DDevice9_GetDepthStencilSurface(dev, &old_ds);
	IDirect3DDevice9_SetRenderTarget(dev, 0, comp_surf[half]);
	IDirect3DDevice9_SetDepthStencilSurface(dev, NULL);
	D3DVIEWPORT9 vp = {0, 0, comp_w, comp_h / 2, 0.0f, 1.0f};
	IDirect3DDevice9_SetViewport(dev, &vp);
	if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
		static const struct {
			D3DRENDERSTATETYPE s;
			DWORD v;
		} rs[] = {{D3DRS_ZENABLE, 0}, {D3DRS_ZWRITEENABLE, 0}, {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_ALPHATESTENABLE, 0}, {D3DRS_ALPHABLENDENABLE, 1},
			  {D3DRS_SRCBLEND, D3DBLEND_ONE}, {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {D3DRS_SEPARATEALPHABLENDENABLE, 0}, {D3DRS_BLENDOP, D3DBLENDOP_ADD},
			  {D3DRS_STENCILENABLE, 0}, {D3DRS_SCISSORTESTENABLE, 0}, {D3DRS_COLORWRITEENABLE, 0xf}, {D3DRS_FOGENABLE, 0}, {D3DRS_LIGHTING, 0}, {D3DRS_SRGBWRITEENABLE, 0}};
		for (size_t i = 0; i < sizeof(rs) / sizeof(rs[0]); i++)
			IDirect3DDevice9_SetRenderState(dev, rs[i].s, rs[i].v);
		IDirect3DDevice9_SetVertexShader(dev, NULL);
		IDirect3DDevice9_SetPixelShader(dev, NULL);
		IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
		IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)lay_tex[half]);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, 0);
		float w = (float)comp_w - 0.5f, h = (float)(comp_h / 2) - 0.5f;
		float q[4][6] = {{-0.5f, -0.5f, 0, 1, 0, 0}, {w, -0.5f, 0, 1, 1, 0}, {-0.5f, h, 0, 1, 0, layer_full ? 1.0f : 0.5f}, {w, h, 0, 1, 1, layer_full ? 1.0f : 0.5f}}; // the top half of the layer (layerdebug=3: all of it)
		static int dedupe = -1;
		if (dedupe < 0)
			dedupe = cfg("dedupe", 0); // off: the global texts are drawn by one context each now (msgwin_hook, hudelem_hook), and the comparison cut holes in what both halves hold the same (the intro's black screen)
		DWORD now = GetTickCount();
		int scoped = 0; // (a scope's black overlay is opaque behind the texts: what is cut out of such a layer is filled with black, not left transparent)
		int one_scoped = now - scope_tick[0] < 400 || now - scope_tick[1] < 400; // somebody looks through a scope: the intro's title lines cannot be on screen, no forced cut-out
		if (dedupe && !scoped && lay_have[1 - half])
			dedupe_make(dev);
		if (dedupe && !scoped && dedupe_ps && lay_have[1 - half]) {
			// The global texts are drawn by both contexts and kept once: the objective (top left) in the top half, the subtitles and the intro's title lines (lower part of the screen) in the bottom half.
			// Each half drops, inside its bands, the pixels its layer holds the same as the other half's layer.
			static const float none[4] = {1.0f, 0.0f, 1.0f, 0.0f}; // an empty band
			static const float objective[4] = {0.0f, 0.6f, 0.0f, 0.0375f};       // layer coordinates: a half is the top half of its layer, v up to 0.5
			static const float subtitles[4] = {0.05f, 0.9f, 0.15f, 0.2375f};     // above the crosshair
			static const float lower[4] = {0.05f, 0.80f, 0.35f, 0.475f};         // intro lines (bottom left) and subtitles (bottom centre); the ammo counter (bottom right) is outside
			float bands[3][4];
			memcpy(bands[0], half == 1 ? objective : none, sizeof(bands[0]));
			memcpy(bands[1], none, sizeof(bands[1])); // (no known text lives above the crosshair: that band cut the scope lens)
			(void)subtitles;
			memcpy(bands[2], half == 0 ? lower : none, sizeof(bands[2]));
			IDirect3DDevice9_SetPixelShader(dev, dedupe_ps);
			IDirect3DDevice9_SetPixelShaderConstantF(dev, 0, &bands[0][0], 3);
			float force[4] = {GetTickCount() - split_tick < 14000 && !one_scoped && !title_tick ? 1.0f : 0.0f, 0, 0, 0}; // (not once the title lines go over the whole picture)
			IDirect3DDevice9_SetPixelShaderConstantF(dev, 3, force, 1);
			float fill[4] = {now - scope_tick[half] < 400 ? 1.0f : 0.0f, 0, 0, 0};
			IDirect3DDevice9_SetPixelShaderConstantF(dev, 4, fill, 1);
			IDirect3DDevice9_SetTexture(dev, 1, (IDirect3DBaseTexture9 *)lay_tex[1 - half]);
			for (int i = 0; i < 2; i++) {
				IDirect3DDevice9_SetSamplerState(dev, i, D3DSAMP_MINFILTER, D3DTEXF_POINT);
				IDirect3DDevice9_SetSamplerState(dev, i, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
				IDirect3DDevice9_SetSamplerState(dev, i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
				IDirect3DDevice9_SetSamplerState(dev, i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
				IDirect3DDevice9_SetSamplerState(dev, i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			}
			IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(q[0]));
		} else if (half == 1 && dedupe && !scoped) {
			// The objective text (top left) is global: both contexts draw it, the bottom one is left out. Two quads: the strip's rows without the left part, then the rest.
			float ry = (float)(comp_h / 2) * 0.075f, rx = (float)comp_w * 0.6f, v = ry / (float)comp_h;
			float a[4][6] = {{rx - 0.5f, -0.5f, 0, 1, rx / (float)comp_w, 0}, {w, -0.5f, 0, 1, 1, 0}, {rx - 0.5f, ry - 0.5f, 0, 1, rx / (float)comp_w, v}, {w, ry - 0.5f, 0, 1, 1, v}};
			float b[4][6] = {{-0.5f, ry - 0.5f, 0, 1, 0, v}, {w, ry - 0.5f, 0, 1, 1, v}, {-0.5f, h, 0, 1, 0, 0.5f}, {w, h, 0, 1, 1, 0.5f}};
			IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, a, sizeof(a[0]));
			IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, b, sizeof(b[0]));
		} else {
			IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(q[0]));
		}
		IDirect3DDevice9_EndScene(dev);
	}
	IDirect3DDevice9_SetDepthStencilSurface(dev, old_ds);
	IDirect3DDevice9_SetRenderTarget(dev, 0, old_rt);
	IDirect3DStateBlock9_Apply(comp_sb);
	if (old_rt)
		IDirect3DSurface9_Release(old_rt);
	if (old_ds)
		IDirect3DSurface9_Release(old_ds);
}

static void compose(IDirect3DDevice9 *dev, int half)
{
	IDirect3DSurface9 *back = NULL;
	D3DSURFACE_DESC d;
	if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
		return;
	IDirect3DSurface9_GetDesc(back, &d);
	if (dev != comp_device || d.Width != comp_w || d.Height != comp_h) {
		comp_release();
		comp_device = dev, comp_w = d.Width, comp_h = d.Height;
		for (int i = 0; i < 2; i++) {
			if (FAILED(IDirect3DDevice9_CreateTexture(dev, d.Width, d.Height / 2, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &comp_tex[i], NULL)) ||
			    FAILED(IDirect3DTexture9_GetSurfaceLevel(comp_tex[i], 0, &comp_surf[i]))) {
				log_printf("native: cannot make the half textures (%ux%u)", d.Width, d.Height / 2);
				comp_release();
				IDirect3DSurface9_Release(back);
				return;
			}
		}
		for (int i = 0; i < 2 && mode == 10; i++) {
			if (FAILED(IDirect3DDevice9_CreateTexture(dev, d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lay_tex[i], NULL)) ||
			    FAILED(IDirect3DTexture9_GetSurfaceLevel(lay_tex[i], 0, &lay_surf[i]))) {
				log_printf("native: cannot make the 2D layers (%ux%u)", d.Width, d.Height);
				comp_release();
				IDirect3DSurface9_Release(back);
				return;
			}
		}
		log_printf("native: compositor on, %ux%u picture, two %ux%u halves", d.Width, d.Height, d.Width, d.Height / 2);
	}
	if (mode == 10) { // the squeezed view, with the top rows of the 2D layer laid over it
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, NULL, comp_surf[half], NULL, D3DTEXF_LINEAR)))
			comp_have[half] = 1;
		{
			static int dbg = -1; // [native] layerdebug=1: the 3D alone, 2: the 2D layer alone on black
			if (dbg < 0)
				dbg = cfg("layerdebug", 0);
			layer_full = dbg == 3;
			if (dbg == 2 || dbg == 3) {
				D3DRECT all = {0, 0, (LONG)comp_w, (LONG)(comp_h / 2)};
				IDirect3DDevice9_ColorFill(dev, comp_surf[half], NULL, 0xff000000);
				(void)all;
			}
			if (lay_have[half] && comp_have[half] && dbg != 1)
				overlay_layer(dev, half);
			// (not while player 1 looks through a scope: its black overlay fills his layer below his half as well, and the title elements are still drawn, faded
			// out, long after the lines are gone: the bottom half went black whenever player 1 aimed with a scope)
			if (half == 1 && comp_have[1] && lay_have[0] && GetTickCount() - title_tick < 500 && GetTickCount() - scope_tick[0] >= 400 && dbg != 1)
				overlay_titles(dev);
		}
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		// a half not composed yet (the split's first frame of the other context) is black: the back buffer's own rows there are this frame's whole view without its 2D,
		// which flashed the level through the title's black screen for a frame
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		else
			IDirect3DDevice9_ColorFill(dev, back, &top, 0xff000000);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		else
			IDirect3DDevice9_ColorFill(dev, back, &bottom, 0xff000000);
		IDirect3DSurface9_Release(back);
		return;
	}
	if (mode == 9) { // the top rows hold the view recorded for a half-height screen: they are copied 1:1 into the half texture
		RECT rows = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, &rows, comp_surf[half], NULL, D3DTEXF_NONE)))
			comp_have[half] = 1;
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		IDirect3DSurface9_Release(back);
		return;
	}
	if (mode == 7 || mode == 8) { // the whole frame, drawn for the shape of a half, is squeezed into the half texture
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, NULL, comp_surf[half], NULL, D3DTEXF_LINEAR)))
			comp_have[half] = 1;
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		IDirect3DSurface9_Release(back);
		return;
	}
	if (mode == 6) { // the drawn view is the middle band of the picture; it fills the whole half texture, and both halves are put on the picture
		RECT band = {0, (LONG)(d.Height / 4), (LONG)d.Width, (LONG)(d.Height * 3 / 4)};
		if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, &band, comp_surf[half], NULL, D3DTEXF_LINEAR)))
			comp_have[half] = 1;
		RECT top = {0, 0, (LONG)d.Width, (LONG)(d.Height / 2)};
		RECT bottom = {0, (LONG)(d.Height / 2), (LONG)d.Width, (LONG)d.Height};
		if (comp_have[0])
			IDirect3DDevice9_StretchRect(dev, comp_surf[0], NULL, back, &top, D3DTEXF_NONE);
		if (comp_have[1])
			IDirect3DDevice9_StretchRect(dev, comp_surf[1], NULL, back, &bottom, D3DTEXF_NONE);
		IDirect3DSurface9_Release(back);
		return;
	}
	RECT drawn = {0, half ? (LONG)(d.Height / 2) : 0, (LONG)d.Width, half ? (LONG)d.Height : (LONG)(d.Height / 2)};
	RECT other = {0, half ? 0 : (LONG)(d.Height / 2), (LONG)d.Width, half ? (LONG)(d.Height / 2) : (LONG)d.Height};
	if (SUCCEEDED(IDirect3DDevice9_StretchRect(dev, back, &drawn, comp_surf[half], NULL, D3DTEXF_NONE)))
		comp_have[half] = 1;
	if (comp_have[1 - half])
		IDirect3DDevice9_StretchRect(dev, comp_surf[1 - half], NULL, back, &other, D3DTEXF_NONE);
	IDirect3DSurface9_Release(back);
}

static volatile LONG pops_total, pushes_total;
static volatile LONG present_hits[3]; // device Present, device PresentEx, swap chain Present: which one the game calls
static volatile LONG compose_via = -1;

typedef HRESULT(STDMETHODCALLTYPE *present_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(STDMETHODCALLTYPE *present_ex_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *sc_present_t)(IDirect3DSwapChain9 *, const RECT *, const RECT *, HWND, const RGNDATA *, DWORD);
static present_t present_orig;
static present_ex_t present_ex_orig;
static sc_present_t sc_present_orig;
static IDirect3DDevice9 *the_device;
static const GUID iid_device9ex = {0xb18b10ce, 0x2649, 0x405a, {0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a}};

// before the picture is shown: the half this frame was drawn into is kept and the other one filled in (once per frame, whichever way the game presents)
static void trace_present(void);
static int skip_hud_mask;
static int skip_ctx = 1; // [native] skipctx=<0|1>: the context whose HUD parts skiphud / skiprot drop
// Diagnostics ([native] skiprot=<seconds>): the HUD part skipped in context 1 changes every <seconds> through the candidates, and a frame is saved ("rot<k>") before the next change
static int split_on;
static int qa_ctx1_buttons;
static void skip_rotation(IDirect3DDevice9 *dev)
{
	static int rot = -1;
	static DWORD t0;
	static int shot_win = -1;
	if (rot < 0)
		rot = cfg("skiprot", 0);
	if (rot <= 0 || mode < 5 || !split_on)
		return;
	DWORD now = GetTickCount();
	if (!t0)
		t0 = now;
	unsigned period = (unsigned)rot * 1000, win = (now - t0) / period;
	int k = (int)(win % 11);
	skip_hud_mask = 1 << k;
	if ((int)win != shot_win && (now - t0) % period > period * 3 / 4 && win < 33) {
		char name[16];
		shot_win = (int)win;
		snprintf(name, sizeof(name), "rot%02d_%d", (int)win, k);
		shot_save_now(dev, name);
	}
}
// [native] perflog=1: where a second of split screen goes (milliseconds, QueryPerformanceCounter): the waits for the renderer's workers, the copying of the contexts' ranges,
// the two contexts' client frames, the gap between the engine's frames, and the compositor (render thread)
static LARGE_INTEGER perf_freq;
static volatile LONGLONG perf_wait, perf_copy, perf_ctx[2], perf_comp, perf_gap_sum, perf_step, perf_aim, perf_sync;
static volatile LONG perf_swaps, perf_ctx_n[2], perf_comp_n, perf_gap_n, perf_gap_long;
static LONGLONG perf_gap_max;
// smoothness ([native] perflog=1): how even the frames are. Buckets in ms: <2, <4, <6, <8, <12, <16, <24, <33, <50, more
#define PERF_BUCKETS 10
static volatile LONG perf_gap_hist[PERF_BUCKETS], perf_present_hist[PERF_BUCKETS];
static volatile LONG perf_td_n[2], perf_td_big[2];
static volatile LONGLONG perf_td_sum[2], perf_td_sq[2];
static volatile int perf_td_min[2] = {1 << 30, 1 << 30}, perf_td_max[2];
static int perf_bucket(double ms)
{
	static const double edge[PERF_BUCKETS - 1] = {2, 4, 6, 8, 12, 16, 24, 33, 50};
	for (int i = 0; i < PERF_BUCKETS - 1; i++)
		if (ms < edge[i])
			return i;
	return PERF_BUCKETS - 1;
}
static LONGLONG perf_now(void)
{
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return c.QuadPart;
}
static void perf_report(void)
{
	static LONGLONG last;
	LONGLONG now = perf_now();
	if (!perf_freq.QuadPart)
		QueryPerformanceFrequency(&perf_freq);
	if (!last)
		last = now;
	if (now - last < perf_freq.QuadPart)
		return;
	double sec = (double)(now - last) / (double)perf_freq.QuadPart, ms = 1000.0 / (double)perf_freq.QuadPart;
	LONG n0 = perf_ctx_n[0], n1 = perf_ctx_n[1], nc = perf_comp_n, ng = perf_gap_n, sw = perf_swaps;
	log_printf("native: perf per second: engine frames %.0f (gap %.2f ms), ctx0 frames %ld (%.2f ms each), ctx1 frames %ld (%.2f ms each), swaps %ld (waits %.1f ms, copies %.1f ms, per second in total), compositor %ld frames (%.2f ms each)",
		   ng / sec, ng ? (double)perf_gap_sum * ms / ng : 0.0, n0, n0 ? (double)perf_ctx[0] * ms / n0 : 0.0, n1, n1 ? (double)perf_ctx[1] * ms / n1 : 0.0, sw,
		   (double)perf_wait * ms / sec, (double)perf_copy * ms / sec, nc, nc ? (double)perf_comp * ms / nc : 0.0);
#ifdef _WIN64
	if (CFG_ONCE("sndlog", 0)) { // the engine's two sound listeners (rva 0x1a28f48, 0x3c bytes each: origin, active flag at +0x38)
		const uint8_t *l = base + 0x1a28f48;
		log_printf("native: sound listeners: 0 %s at %.0f %.0f %.0f, 1 %s at %.0f %.0f %.0f", l[0x38] ? "on" : "off", (double)((const float *)l)[0], (double)((const float *)l)[1], (double)((const float *)l)[2],
			   l[0x3c + 0x38] ? "on" : "off", (double)((const float *)(l + 0x3c))[0], (double)((const float *)(l + 0x3c))[1], (double)((const float *)(l + 0x3c))[2]);
		if (snd_ctx_ptr)
			log_printf("native: world sounds started per second: context 0 %ld, context 1 %ld", (long)InterlockedExchange(&snd_counts[0], 0), (long)InterlockedExchange(&snd_counts[1], 0));
		for (int i = 0; i < 512 && snd_ctx_ptr && snd_names[i].name; i++) {
			if (snd_names[i].n[0] + snd_names[i].n[1] > 0 && !IsBadReadPtr(snd_names[i].name, 4))
				log_printf("native:   sound %s: context 0 %ld, context 1 %ld", snd_names[i].name, (long)snd_names[i].n[0], (long)snd_names[i].n[1]);
			snd_names[i].n[0] = snd_names[i].n[1] = 0;
		}
	}
#elif !defined(MW2COOP_IW4X)
	if (CFG_ONCE("sndlog", 0)) {
		const uint8_t *l = base + 0x17f5b4c; // rva of the listener array in the official 32-bit build
		log_printf("native: sound listeners: 0 %s at %.0f %.0f %.0f, 1 %s at %.0f %.0f %.0f", l[0x38] ? "on" : "off", (double)((const float *)l)[0], (double)((const float *)l)[1], (double)((const float *)l)[2],
			   l[0x3c + 0x38] ? "on" : "off", (double)((const float *)(l + 0x3c))[0], (double)((const float *)(l + 0x3c))[1], (double)((const float *)(l + 0x3c))[2]);
	}
#endif
	{
		char line[400];
		int n = snprintf(line, sizeof(line), "native: perf smoothness: engine frame gaps <2/4/6/8/12/16/24/33/50/more ms:");
		for (int i = 0; i < PERF_BUCKETS; i++)
			n += snprintf(line + n, sizeof(line) - n, " %ld", (long)perf_gap_hist[i]), perf_gap_hist[i] = 0;
		n += snprintf(line + n, sizeof(line) - n, " | shown frames:");
		for (int i = 0; i < PERF_BUCKETS; i++)
			n += snprintf(line + n, sizeof(line) - n, " %ld", (long)perf_present_hist[i]), perf_present_hist[i] = 0;
		log_printf("%s", line);
		for (int k = 0; k < 2; k++) {
			LONG cnt = perf_td_n[k];
			double mean = cnt ? (double)perf_td_sum[k] / cnt : 0, var = cnt ? (double)perf_td_sq[k] / cnt - mean * mean : 0;
			log_printf("native: perf smoothness: view %d game time step per draw: mean %.2f ms, sd %.2f, min %d, max %d, steps over 12 ms %ld (of %ld)", k, mean, var > 0 ? sqrt(var) : 0.0,
				   cnt ? perf_td_min[k] : 0, perf_td_max[k], (long)perf_td_big[k], (long)cnt);
			perf_td_n[k] = perf_td_big[k] = 0;
			perf_td_sum[k] = perf_td_sq[k] = 0;
			perf_td_min[k] = 1 << 30, perf_td_max[k] = 0;
		}
	}
	log_printf("native: perf per second, inside the client frames (ms per second in total): server steps %.1f, aim targets %.1f, body sync %.1f; longest frame %.1f ms, "
		   "frames over 50 ms %ld", (double)perf_step * ms / sec, (double)perf_aim * ms / sec, (double)perf_sync * ms / sec, (double)perf_gap_max * ms, perf_gap_long);
	perf_gap_max = 0;
	perf_gap_long = 0;
	perf_step = perf_aim = perf_sync = 0;
	perf_wait = perf_copy = perf_ctx[0] = perf_ctx[1] = perf_comp = perf_gap_sum = 0;
	perf_swaps = perf_ctx_n[0] = perf_ctx_n[1] = perf_comp_n = perf_gap_n = 0;
	last = now;
}

// [native] pairpresent=1 (default): the picture is shown after the frame of the second view only. Each engine frame draws one view (the first one's, then the second's); showing both would
// give each view half of the display's rate (the first view's picture is only half new), and with the display's synchronisation on (the engine waits for the refresh in every present)
// 30 pictures a second per view on a 60 Hz screen. Both halves are composed in every frame anyway, so the picture after the second view's frame is whole and new in both.
static volatile int skip_this_present;
static void before_present_inner(int via, IDirect3DDevice9 *dev)
{
	trace_present();
	InterlockedIncrement(&present_hits[via]);
	render_tid = GetCurrentThreadId();
	if (CFG_ONCE("perflog", 0)) { // how evenly the pictures are shown: the time between two presents
		static LONGLONG prev;
		LARGE_INTEGER c, f;
		QueryPerformanceCounter(&c);
		QueryPerformanceFrequency(&f);
		if (prev)
			perf_present_hist[perf_bucket((double)(c.QuadPart - prev) * 1000.0 / (double)f.QuadPart)]++;
		prev = c.QuadPart;
	}
	{
		static DWORD last_log;
		DWORD now = GetTickCount();
		if (now - last_log > 3000 && present_hits[0] + present_hits[1] + present_hits[2] > 0 && pushes_total > 0) {
			last_log = now;
			log_printf("native: present hits %ld/%ld/%ld, via %ld, pushes %ld, ring %ld-%ld", (long)present_hits[0], (long)present_hits[1], (long)present_hits[2], (long)compose_via,
				   (long)pushes_total, (long)ring_tail, (long)ring_head);
		}
	}
	if (compose_via < 0)
		compose_via = via;
	int half;
	if (mode == 10) { // the frame knows its own half (see half_note)
		if (compose_via != via)
			return;
		half = render_half;
		render_half = -1;
		if (half < 0) {
			comp_have[0] = comp_have[1] = 0; // not a split frame: the halves of before are stale
			lay_have[0] = lay_have[1] = 0;
			return;
		}
		InterlockedExchange(&ring_tail, ring_head); // (the queue of the other modes is not used)
	} else {
		if (compose_via == via && ring_tail == ring_head)
			comp_have[0] = comp_have[1] = 0; // not a split frame: the halves of before are stale
		if (compose_via != via || ring_tail == ring_head)
			return;
		half = (int)ring[ring_tail & 7];
		InterlockedIncrement(&ring_tail);
	}
	LONG pops = InterlockedIncrement(&pops_total);
	{ // [native] hitchlog=1: a gap of more than 150 ms between two composed frames is logged (the stalls during the title's fade)
		static int on = -1;
		static LARGE_INTEGER freq, last;
		static unsigned logged;
		if (on < 0)
			on = cfg("hitchlog", 0), QueryPerformanceFrequency(&freq);
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if (on && last.QuadPart && logged < 100) {
			double ms = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart;
			if (ms > 150.0)
				logged++, log_printf("native: hitch %.0f ms before composed frame %ld (half %d)", ms, (long)pops, half);
		}
		last = now;
	}
	compose(dev, half);
	skip_rotation(dev);
	{
		static int pair = -1;
		if (pair < 0)
			pair = cfg("pairpresent", 1);
		skip_this_present = pair && half == 0 && mode == 10 && comp_have[0] && comp_have[1] ? 1 : 0;
	}
	static LONG seq_at = -1;
	if (seq_at < 0)
		seq_at = cfg("seqat", -1); // (QA: tools/qa_native.sh sets 3000; off for players: twelve 8 MB pictures per session)
	static LONG seq_step = -1;
	if (seq_step < 0)
		seq_step = cfg("seqstep", 1);
	if (seq_at >= 0 && pops >= seq_at && (pops - seq_at) % seq_step == 0 && (pops - seq_at) / seq_step < 12) { // the composed picture about once a second (QA of the experiment: "seq-<pops>.bmp")
		char name[32];
		snprintf(name, sizeof(name), "seq-%ld", (long)pops);
		shot_save_now(dev, name);
	}
	{ // QA: [native] burstat=<pops>: frames in a row without a stall per frame, "b<k>-NN.bmp"; burstn (40) frames of the bottom half (burstfull=1: 30 whole frames),
	  // burstcount (1) bursts, burstevery (600) composed frames apart. Counted in composed frames, so a slow level start does not move them.
		static LONG burst_at = -2, every, count;
		static int full;
		if (burst_at == -2) {
			burst_at = cfg("burstat", -1), every = cfg("burstevery", 600), count = cfg("burstcount", 1), full = cfg("burstfull", 0);
			if (every < 1)
				every = 1;
		}
		if (burst_at >= 0 && pops >= burst_at && (pops - burst_at) % every == 0 && (pops - burst_at) / every < count) {
			char name[16];
			snprintf(name, sizeof(name), "b%ld", (long)((pops - burst_at) / every));
			if (full)
				shot_burst_start(name, 30, 0, 0, (int)comp_w, (int)comp_h);
			else
				shot_burst_start(name, cfg("burstn", 40), 0, (int)(comp_h / 2), (int)comp_w, (int)(comp_h / 2));
		}
		shot_burst_tick(dev);
	}
	if (pops % 4 == 0) { // QA: a level script sets the dvar mw2coop_qa_frame to a new name: the composed picture (both halves) is saved as that name
		static char last[64];
		char name[64];
		if (game_dvar_raw("mw2coop_qa_frame", name, sizeof(name)) && name[0] && strcmp(name, last)) {
			lstrcpynA(last, name, sizeof(last));
			shot_save_now(dev, name);
		}
	}
	if (pops % 300 == 0)
		log_printf("native: %ld frames composed via %d, presents %ld/%ld/%ld, %ld views queued, queue %ld", (long)pops, via, (long)present_hits[0],
			   (long)present_hits[1], (long)present_hits[2], (long)pushes_total, (long)(ring_head - ring_tail));
}

static void before_present(int via, IDirect3DDevice9 *dev)
{
	skip_this_present = 0;
	static int on = -1;
	if (on < 0)
		on = cfg("perflog", 0);
	LONGLONG t0 = on ? perf_now() : 0;
	before_present_inner(via, dev);
	if (on) {
		perf_comp += perf_now() - t0;
		InterlockedIncrement(&perf_comp_n);
	}
}

static HRESULT STDMETHODCALLTYPE present_hook(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND c, const RGNDATA *d)
{
	before_present(0, dev);
	if (skip_this_present)
		return D3D_OK;
	return present_orig(dev, a, b, c, d);
}

static HRESULT STDMETHODCALLTYPE present_ex_hook(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND c, const RGNDATA *d, DWORD e)
{
	before_present(1, dev);
	if (skip_this_present)
		return D3D_OK;
	return present_ex_orig(dev, a, b, c, d, e);
}

static HRESULT STDMETHODCALLTYPE sc_present_hook(IDirect3DSwapChain9 *sc, const RECT *a, const RECT *b, HWND c, const RGNDATA *d, DWORD e)
{
	before_present(2, the_device);
	if (skip_this_present)
		return D3D_OK;
	return sc_present_orig(sc, a, b, c, d, e);
}

// The vtable slots of the D3D9 device and swap chain that this file patches. The Steam overlay (gameoverlayrenderer64.dll) hooks the same functions and puts its hooks back after
// a device Reset; two hooks on one slot that each saved the other's address call each other until the stack is gone (0xc00000fd inside the overlay). So around a Reset all of
// ours are taken out first and put back on top of whatever is there afterwards.
static struct {
	void **slot;
	void *hook;
	void **orig;
	int in;
} slots[24];
static int nslots;

static void patch_slot(void **slot, void *hook, void **orig)
{
	DWORD old;
	if (nslots >= 24 || !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old))
		return;
	*orig = *slot;
	*slot = hook;
	VirtualProtect(slot, sizeof(void *), old, &old);
	slots[nslots].slot = slot, slots[nslots].hook = hook, slots[nslots].orig = orig, slots[nslots].in = 1;
	nslots++;
}

static void slots_take_out(void)
{
	for (int i = 0; i < nslots; i++) {
		DWORD old;
		if (!slots[i].in || *slots[i].slot != slots[i].hook || !VirtualProtect(slots[i].slot, sizeof(void *), PAGE_READWRITE, &old))
			continue; // somebody else is on top of ours: left as it is
		*slots[i].slot = *slots[i].orig;
		VirtualProtect(slots[i].slot, sizeof(void *), old, &old);
		slots[i].in = 0;
	}
}

static void slots_put_back(void)
{
	for (int i = 0; i < nslots; i++) {
		DWORD old;
		if (slots[i].in || !VirtualProtect(slots[i].slot, sizeof(void *), PAGE_READWRITE, &old))
			continue;
		*slots[i].orig = *slots[i].slot; // whatever is there now is what ours calls
		*slots[i].slot = slots[i].hook;
		VirtualProtect(slots[i].slot, sizeof(void *), old, &old);
		slots[i].in = 1;
	}
}

// Reset (vtable 16) fails while default-pool resources of ours are alive: the half textures are let go first (a lost device, a vid_restart, a change of the desktop: "DirectX error")
typedef HRESULT(STDMETHODCALLTYPE *reset_t)(IDirect3DDevice9 *, void *);
static reset_t reset_orig;
static HRESULT STDMETHODCALLTYPE reset_hook(IDirect3DDevice9 *dev, void *params)
{
	log_printf("native: device reset: the half textures are released, the hooks taken out for it");
	comp_release();
	reset_t call = reset_orig;
	slots_take_out();
	HRESULT hr = call(dev, params);
	slots_put_back();
	return hr;
}
typedef HRESULT(STDMETHODCALLTYPE *reset_ex_t)(IDirect3DDevice9 *, void *, void *);
static reset_ex_t reset_ex_orig;
static HRESULT STDMETHODCALLTYPE reset_ex_hook(IDirect3DDevice9 *dev, void *params, void *mode)
{
	log_printf("native: device reset (Ex): the half textures are released, the hooks taken out for it");
	comp_release();
	reset_ex_t call = reset_ex_orig;
	slots_take_out();
	HRESULT hr = call(dev, params, mode);
	slots_put_back();
	return hr;
}

// Watchdog ([native] watchdog=1, default on): when the swap chain's Present has not been reached for 6 s while the main thread keeps queueing views, the render thread is stuck;
// its instruction pointer and the return addresses on its stack are logged as module+offset (once), to see what it waits for.
static DWORD WINAPI watchdog_thread(LPVOID unused)
{
	(void)unused;
	LONG last = -1;
	int stuck = 0;
	for (;;) {
		Sleep(2000);
		LONG now = present_hits[2] + present_hits[0] + present_hits[1];
		if (now == last && ring_head - ring_tail >= 5 && render_tid) {
			if (++stuck == 3) {
				HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, render_tid);
				if (th && SuspendThread(th) != (DWORD)-1) {
					CONTEXT ctx;
					memset(&ctx, 0, sizeof(ctx));
					ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
					if (GetThreadContext(th, &ctx)) {
						char line[1600];
						int n = 0;
						const uintptr_t *sp = (const uintptr_t *)CTX_SP(ctx);
						uintptr_t addrs[1 + 400];
						int na = 0;
						addrs[na++] = CTX_IP(ctx);
						for (int i = 0; i < 400 && na < 41; i++) {
							uintptr_t v = 0;
							if (!ReadProcessMemory(GetCurrentProcess(), sp + i, &v, sizeof(v), NULL))
								break;
							MEMORY_BASIC_INFORMATION mbi;
							if (v > 0x10000 && VirtualQuery((void *)v, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
								addrs[na++] = v;
						}
						n += snprintf(line + n, sizeof(line) - n, "native: WATCHDOG render thread %lu stuck, rip + stack:", (unsigned long)render_tid);
						for (int i = 0; i < na && n < (int)sizeof(line) - 80; i++) {
							HMODULE m = NULL;
							char name[MAX_PATH] = "?";
							if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)addrs[i], &m) && m) {
								GetModuleFileNameA(m, name, sizeof(name));
								const char *slash = strrchr(name, '\\');
								n += snprintf(line + n, sizeof(line) - n, " %s+%llx", slash ? slash + 1 : name, (unsigned long long)(addrs[i] - (uintptr_t)m));
							}
						}
						log_printf("%s", line);
					}
					ResumeThread(th);
				}
				if (th)
					CloseHandle(th);
			}
		} else {
			stuck = 0;
		}
		last = now;
	}
	return 0;
}

// ---- diagnostics ([native] d3dtrace=1): the render thread's calls of one frame (render targets, viewports, clears, stretches, draw counts), to see where the 3D ends and the 2D begins
static int trace_on;
static char trace_buf[400000];
static int trace_layer = -1; // [native] tracelayer=1: only the 2D pass's draws are traced
static int trace_len, trace_draws;
static IDirect3DDevice9 *trace_dev;
static char trace_key[192], trace_key_last[192], trace_stack[160], trace_stack_last[160];
static int bb_pass, bb_pass_done, bb_draws;
static void trace_flush_draws(void)
{
	if (trace_draws && trace_len < (int)sizeof(trace_buf) - 160)
		trace_len += snprintf(trace_buf + trace_len, sizeof(trace_buf) - trace_len, "draw x%d %s  @%s\n", trace_draws, trace_key_last, trace_stack_last);
	trace_draws = 0;
}
// one draw: the shaders and states it uses; a run of draws with the same ones is one line
static void trace_draw(IDirect3DDevice9 *dev)
{
	IDirect3DVertexShader9 *vs = NULL;
	IDirect3DPixelShader9 *ps = NULL;
	if (trace_layer < 0)
		trace_layer = cfg("tracelayer", 0);
	if (trace_layer && !layer_pass)
		return;
	DWORD z = 0, zw = 0, ab = 0, sb = 0, db = 0, sep = 0, sba = 0, dba = 0, cw = 0, sc = 0;
	IDirect3DDevice9_GetVertexShader(dev, &vs);
	IDirect3DDevice9_GetPixelShader(dev, &ps);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ZENABLE, &z);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ZWRITEENABLE, &zw);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ALPHABLENDENABLE, &ab);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_SRCBLEND, &sb);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_DESTBLEND, &db);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, &sep);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_SRCBLENDALPHA, &sba);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_DESTBLENDALPHA, &dba);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_COLORWRITEENABLE, &cw);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_SCISSORTESTENABLE, &sc);
	if (bb_pass && !bb_pass_done && bb_draws % 60 == 0 && bb_draws > 0) { // what the picture holds after this many draws of the pass that ends with the screen copy
		char name[24];
		trace_on = 0;
		snprintf(name, sizeof(name), "tr-d%03d", bb_draws);
		shot_save_now(dev, name);
		trace_on = 1;
	}
	bb_draws++;
	snprintf(trace_key, sizeof(trace_key), "vs=%p ps=%p z=%lu zw=%lu blend=%lu %lu/%lu sep=%lu %lu/%lu cw=%lx sc=%lu%s", (void *)vs, (void *)ps, (unsigned long)z, (unsigned long)zw,
		 (unsigned long)ab, (unsigned long)sb, (unsigned long)db, (unsigned long)sep, (unsigned long)sba, (unsigned long)dba, (unsigned long)cw, (unsigned long)sc,
		 layer_pass ? " LAYER" : "");
	if (vs)
		IDirect3DVertexShader9_Release(vs);
	if (ps)
		IDirect3DPixelShader9_Release(ps);
	{
		void *frames[8];
		USHORT n = CaptureStackBackTrace(2, 8, frames, NULL); // the callers of the engine's draw wrapper, as offsets into the game's image
		int len = 0;
		trace_stack[0] = 0;
		for (USHORT i = 0; i < n && len < (int)sizeof(trace_stack) - 12; i++) {
			uintptr_t off = (uintptr_t)frames[i] - (uintptr_t)base;
			if (off < 0x4000000)
				len += snprintf(trace_stack + len, sizeof(trace_stack) - len, "%lx,", (unsigned long)off);
		}
	}
	if (strcmp(trace_key, trace_key_last) || strcmp(trace_stack, trace_stack_last)) {
		trace_flush_draws();
		strcpy(trace_key_last, trace_key);
		strcpy(trace_stack_last, trace_stack);
	}
	trace_draws++;
}
static void trace_event(const char *fmt, ...)
{
	trace_flush_draws();
	if (trace_len > (int)sizeof(trace_buf) - 200)
		return;
	va_list ap;
	va_start(ap, fmt);
	trace_len += vsnprintf(trace_buf + trace_len, sizeof(trace_buf) - trace_len, fmt, ap);
	va_end(ap);
	trace_buf[trace_len++] = '\n';
}
static const char *trace_surf(IDirect3DDevice9 *dev, IDirect3DSurface9 *s)
{
	static char out[64];
	IDirect3DSurface9 *back = NULL;
	D3DSURFACE_DESC d = {0};
	if (s)
		IDirect3DSurface9_GetDesc(s, &d);
	if (SUCCEEDED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && back)
		IDirect3DSurface9_Release(back);
	snprintf(out, sizeof(out), "%p %ux%u fmt%d%s", (void *)s, d.Width, d.Height, (int)d.Format, s && s == back ? " BACKBUFFER" : "");
	return out;
}
// In the 2D layer the engine's alpha blending (source alpha, inverse source alpha) leaves the colour premultiplied by the alpha and the alpha squared; asking for "source alpha one,
// inverse source alpha" for the alpha channel alone makes the layer a proper premultiplied picture (blending states 206-208 are ones the engine never sets itself).
static void layer_fix(IDirect3DDevice9 *dev)
{
	DWORD on = 0, src = 0, dst = 0, cw = 0xf;
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ALPHABLENDENABLE, &on);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_SRCBLEND, &src);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_DESTBLEND, &dst);
	IDirect3DDevice9_GetRenderState(dev, D3DRS_COLORWRITEENABLE, &cw);
	if (!on || !(cw & 7)) { // a draw that writes alpha alone builds a mask for the next ones (the compass: a rectangle, multiplied by its round edge, then the map drawn by that
		// destination alpha): its own blending must reach the alpha, or the multiply is lost and the map covers the whole rectangle (a dark square around the compass)
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, 0);
	} else if (dst == D3DBLEND_INVSRCALPHA && (src == D3DBLEND_SRCALPHA || src == D3DBLEND_ONE)) { // "over": the alpha of the layer accumulates as coverage
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, 1);
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
		IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
	} else { // additive glows, multiplies and the like add or change colour only: they must not make the layer opaque (that was a black box behind the speaker's name)
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, 1);
		IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
		IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLENDALPHA, D3DBLEND_ONE);
	}
}
typedef HRESULT(STDMETHODCALLTYPE *set_rt_t)(IDirect3DDevice9 *, DWORD, IDirect3DSurface9 *);
typedef HRESULT(STDMETHODCALLTYPE *clear_t)(IDirect3DDevice9 *, DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT(STDMETHODCALLTYPE *stretch_t)(IDirect3DDevice9 *, IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const RECT *, D3DTEXTUREFILTERTYPE);
typedef HRESULT(STDMETHODCALLTYPE *viewport_t)(IDirect3DDevice9 *, const D3DVIEWPORT9 *);
typedef HRESULT(STDMETHODCALLTYPE *dip_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *dp_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE *dpup_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT(STDMETHODCALLTYPE *dipup_t)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT);
typedef HRESULT(STDMETHODCALLTYPE *scissor_t)(IDirect3DDevice9 *, const RECT *);
typedef HRESULT(STDMETHODCALLTYPE *scene_t)(IDirect3DDevice9 *);
static set_rt_t set_rt_orig;
static clear_t clear_orig;
static stretch_t stretch_orig;
static viewport_t viewport_orig;
static dip_t dip_orig;
static dp_t dp_orig;
static scissor_t scissor_orig;
// Mode 10, the scope: the engine clips the lens' 3D with a scissor rectangle worked out from the 2D placement (made for a half: a square of half the picture's height, centred in the half);
// the 3D itself is drawn over the whole picture and squeezed 2:1 into the half, so the rows of that rectangle are doubled for every full-size target but the 2D layer's (whose own
// pixels are the half's), and the lens comes out as the square it is in the half. A lens rectangle is recognised as one that lies in the upper half of a full-size target.
static volatile LONG rt_full; // the current render target (stage 0) is as big as the picture
static HRESULT STDMETHODCALLTYPE t_scissor(IDirect3DDevice9 *dev, const RECT *r)
{
	RECT fixed;
	if (r && mode == 10 && native_active && split_on && CFG_ONCE("scissorlog", 0)) { // QA: the scissor rectangles of split frames
		static LONG logged;
		if (!layer_pass && rt_full && comp_h && r->bottom <= (LONG)(comp_h / 2) && r->right - r->left > 64 && InterlockedIncrement(&logged) < 400)
			log_printf("native: scissor %ld,%ld %ld,%ld layer %d full %d", (long)r->left, (long)r->top, (long)r->right, (long)r->bottom, layer_pass, (int)rt_full);
	}
	if (r && mode == 10 && native_active && split_on && !layer_pass && rt_full && comp_h && r->bottom <= (LONG)(comp_h / 2) && r->right - r->left > 64 &&
	    CFG_ONCE("lensfix", 1)) {
		if (r->right - r->left < (LONG)(comp_w / 2))
			frame_scope = 1; // a lens rectangle: the frame being drawn looks through a scope (its half is known once the scene's 2D pass is reached)
		fixed = *r;
		fixed.top *= 2;
		fixed.bottom *= 2;
		r = &fixed;
	}
	if (trace_on && r)
		trace_event("SetScissorRect %ld,%ld %ld,%ld", (long)r->left, (long)r->top, (long)r->right, (long)r->bottom);
	return scissor_orig(dev, r);
}
static dpup_t dpup_orig;
static dipup_t dipup_orig;
static scene_t begin_scene_orig, end_scene_orig;
// Mode 10: the scene's last pass (rva 0x63680, see scene2d_hook) also draws 3D things with the depth test on: effects, particles, the glass and the dot of a red dot
// sight (the dot is a bone of the gun's model, tag_reticle_red_dot). Into the layer they came out where the half's 2D is laid out (the dot below the layer's rows,
// the glass and the snow as a veil over the top half), so a depth-tested draw of that pass goes to the half's picture with the 3D instead ([native] layer3d=0: as before)
static IDirect3DSurface9 *layer_scene_rt, *layer_rt;
static int layer_to_scene(IDirect3DDevice9 *dev)
{
	if (!layer_scene_rt || !layer_rt || !CFG_ONCE("layer3d", 1))
		return 0;
	DWORD z = 0;
	IDirect3DDevice9_GetRenderState(dev, D3DRS_ZENABLE, &z);
	if (!z)
		return 0;
	D3DVIEWPORT9 vp;
	IDirect3DDevice9_GetViewport(dev, &vp);
	set_rt_orig(dev, 0, layer_scene_rt);
	viewport_orig(dev, &vp);
	return 1;
}
static void layer_back(IDirect3DDevice9 *dev)
{
	D3DVIEWPORT9 vp;
	IDirect3DDevice9_GetViewport(dev, &vp);
	set_rt_orig(dev, 0, layer_rt);
	viewport_orig(dev, &vp);
}
static HRESULT STDMETHODCALLTYPE t_dpup(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, UINT pc, const void *v, UINT st)
{
	int moved = layer_pass ? layer_to_scene(dev) : 0;
	if (layer_pass && !moved)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	HRESULT hr = dpup_orig(dev, t, pc, v, st);
	if (moved)
		layer_back(dev);
	return hr;
}
static HRESULT STDMETHODCALLTYPE t_dipup(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, const void *i, D3DFORMAT f, const void *v, UINT st)
{
	int moved = layer_pass ? layer_to_scene(dev) : 0;
	if (layer_pass && !moved)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	HRESULT hr = dipup_orig(dev, t, mi, nv, pc, i, f, v, st);
	if (moved)
		layer_back(dev);
	return hr;
}
static HRESULT STDMETHODCALLTYPE t_set_rt(IDirect3DDevice9 *dev, DWORD i, IDirect3DSurface9 *s)
{
	if (i == 0) {
		D3DSURFACE_DESC d;
		InterlockedExchange(&rt_full, s && comp_w && SUCCEEDED(IDirect3DSurface9_GetDesc(s, &d)) && d.Width == comp_w && d.Height == comp_h);
	}
	if (trace_on && i == 0) {
		const char *d = trace_surf(dev, s);
		trace_event("SetRenderTarget %s", d);
		if (strstr(d, "BACKBUFFER") && !bb_pass_done)
			bb_pass = 1, bb_draws = 0;
	}
	return set_rt_orig(dev, i, s);
}
static HRESULT STDMETHODCALLTYPE t_clear(IDirect3DDevice9 *dev, DWORD n, const D3DRECT *r, DWORD f, D3DCOLOR c, float z, DWORD st)
{
	if (trace_on)
		trace_event("Clear flags %lx color %08lx rects %lu", (unsigned long)f, (unsigned long)c, (unsigned long)n);
	return clear_orig(dev, n, r, f, c, z, st);
}
static HRESULT STDMETHODCALLTYPE t_stretch(IDirect3DDevice9 *dev, IDirect3DSurface9 *a, const RECT *ra, IDirect3DSurface9 *b, const RECT *rb, D3DTEXTUREFILTERTYPE f)
{
	if (trace_on) {
		char s1[80];
		snprintf(s1, sizeof(s1), "%s", trace_surf(dev, a));
		trace_event("StretchRect %s -> %s", s1, trace_surf(dev, b));
		if (strstr(s1, "BACKBUFFER") && !strstr(trace_surf(dev, b), "1920x540")) {
			trace_on = 0;
			shot_save_now(dev, "tr-copy");
			trace_on = 1;
			bb_pass_done = 1;
			trace_event("(%d draws in the pass before the screen copy)", bb_draws);
		}
	}
	return stretch_orig(dev, a, ra, b, rb, f);
}
static HRESULT STDMETHODCALLTYPE t_viewport(IDirect3DDevice9 *dev, const D3DVIEWPORT9 *v)
{
	if (trace_on && v)
		trace_event("SetViewport %lu,%lu %lux%lu", (unsigned long)v->X, (unsigned long)v->Y, (unsigned long)v->Width, (unsigned long)v->Height);
	return viewport_orig(dev, v);
}
static HRESULT STDMETHODCALLTYPE t_dip(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, INT bv, UINT mi, UINT nv, UINT si, UINT pc)
{
	int moved = layer_pass ? layer_to_scene(dev) : 0;
	if (layer_pass && !moved)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	HRESULT hr = dip_orig(dev, t, bv, mi, nv, si, pc);
	if (moved)
		layer_back(dev);
	return hr;
}
static HRESULT STDMETHODCALLTYPE t_dp(IDirect3DDevice9 *dev, D3DPRIMITIVETYPE t, UINT sv, UINT pc)
{
	int moved = layer_pass ? layer_to_scene(dev) : 0;
	if (layer_pass && !moved)
		layer_fix(dev);
	if (trace_on)
		trace_draw(dev);
	HRESULT hr = dp_orig(dev, t, sv, pc);
	if (moved)
		layer_back(dev);
	return hr;
}
static HRESULT STDMETHODCALLTYPE t_begin(IDirect3DDevice9 *dev)
{
	if (trace_on)
		trace_event("BeginScene");
	return begin_scene_orig(dev);
}
static HRESULT STDMETHODCALLTYPE t_end(IDirect3DDevice9 *dev)
{
	if (trace_on)
		trace_event("EndScene");
	return end_scene_orig(dev);
}
static void trace_install(IDirect3DDevice9 *dev)
{
	void **vt = *(void ***)dev;
	patch_slot(&vt[37], (void *)t_set_rt, (void **)&set_rt_orig);
	patch_slot(&vt[43], (void *)t_clear, (void **)&clear_orig);
	patch_slot(&vt[34], (void *)t_stretch, (void **)&stretch_orig);
	patch_slot(&vt[47], (void *)t_viewport, (void **)&viewport_orig);
	patch_slot(&vt[82], (void *)t_dip, (void **)&dip_orig);
	patch_slot(&vt[81], (void *)t_dp, (void **)&dp_orig);
	patch_slot(&vt[83], (void *)t_dpup, (void **)&dpup_orig);
	patch_slot(&vt[84], (void *)t_dipup, (void **)&dipup_orig);
	patch_slot(&vt[75], (void *)t_scissor, (void **)&scissor_orig);
	patch_slot(&vt[41], (void *)t_begin, (void **)&begin_scene_orig);
	patch_slot(&vt[42], (void *)t_end, (void **)&end_scene_orig);
	trace_dev = dev;
}
// called at each Present of the render thread: the frame after the 900th composed one is traced
static void trace_present(void)
{
	static int armed = -1;
	if (armed < 0)
		armed = cfg("d3dtrace", 0);
	if (!armed || !trace_dev)
		return;
	if (trace_on) {
		trace_event("Present");
		shot_save_now(trace_dev, "tr-end");
		trace_on = 0;
		{
			char path[MAX_PATH];
			snprintf(path, sizeof(path), "%s", settings_path);
			char *slash = strrchr(path, '\\');
			if (slash)
				strcpy(slash + 1, "d3dtrace.txt");
			FILE *f = fopen(path, "wb");
			if (f) {
				fwrite(trace_buf, 1, trace_len, f);
				fclose(f);
			}
			log_printf("native: D3D TRACE of one frame written to %s (%d bytes)", path, trace_len);
		}
		armed = 0;
	} else if (pops_total >= cfg("tracepops", 600) && (mode == 10 ? render_half >= 0 && 1 - render_half == cfg("tracehalf", 0) : ring_head - ring_tail >= 2 && (int)ring[(ring_tail + 1) & 7] == cfg("tracehalf", 0))) { // the frame after this one is the wanted half's (the halves alternate)
		trace_len = 0, trace_draws = 0, bb_pass = 0, bb_pass_done = 0, bb_draws = 0;
		trace_on = 1;
	}
}

// Mode 10: the scene function's last pass (rva 0x63680, called at 0x5c5bf: the 2D texts and signs) draws into the half's layer instead of the picture, so the picture keeps the view alone
// (the glow and blur that follow work on it) and the 2D, laid out by the client for a half-height screen, stays whole.
#ifdef _WIN64
static void (*scene2d_original)(void *);
#else
// 32 bits: the scene's 2D pass and the frame's 2D command list take the scene in ESI
static void *scene2d_target, *final2d_target;
static void scene2d_original(void *vp)
{
	__asm__ volatile("call *%1" : "+S"(vp) : "r"(scene2d_target) : "eax", "ecx", "edx", "memory", "cc");
}
static void final2d_original(void *vp)
{
	__asm__ volatile("call *%1" : "+S"(vp) : "r"(final2d_target) : "eax", "ecx", "edx", "memory", "cc");
}
#endif
static void scene2d_hook(void *vp)
{
	{ // diagnostics ([native] scenescan=1): which dwords of the scene struct look like the cgame's time (rva 0x422434), to find a per-frame identity the render thread can read
		static int armed = -1, n;
		if (armed < 0)
			armed = cfg("scenescan", 0);
		if (armed && split_on && ++n >= 200 && n < 206) {
			int t = *(int *)(base + 0x422434);
			char line[900];
			int len = snprintf(line, sizeof(line), "native: scenescan %p t=%d half %d:", vp, t, ring_head != ring_tail ? (int)ring[ring_tail & 7] : -1);
			const int *d = (const int *)vp;
			for (int i = 0; i < 0x800 && len < (int)sizeof(line) - 30; i++)
				if (d[i] > t - 400 && d[i] < t + 400)
					len += snprintf(line + len, sizeof(line) - len, " +%x=%d", i * 4, d[i] - t);
			log_printf("%s", line);
		}
	}
	IDirect3DDevice9 *dev = the_device;
	render_half = mode == 10 && native_active ? half_find(*(int *)((uint8_t *)vp + 0x180)) : -1;
	if (render_half >= 0 && frame_scope)
		scope_tick[render_half & 1] = GetTickCount();
	frame_scope = 0;
	if (mode == 10 && native_active && split_on) { // how often a split frame's half is not found (the notes are late, or the frame has another time)
		static LONG seen, lost;
		static DWORD last;
		seen++;
		if (render_half < 0)
			lost++;
		DWORD now = GetTickCount();
		if (now - last > 10000) {
			last = now;
			log_printf("native: frames whose half was not found: %ld of %ld", (long)lost, (long)seen);
		}
	}
	if (mode != 10 || !dev || render_half < 0 || !lay_surf[0] || !lay_surf[1]) {
		scene2d_original(vp);
		return;
	}
	int half = render_half;
	IDirect3DSurface9 *old = NULL;
	IDirect3DDevice9_GetRenderTarget(dev, 0, &old);
	IDirect3DDevice9_SetRenderTarget(dev, 0, lay_surf[half]);
	IDirect3DDevice9_Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
	lay_have[half] = 1;
	layer_scene_rt = old, layer_rt = lay_surf[half];
	layer_pass = 1;
	scene2d_original(vp);
	layer_pass = 0;
	layer_scene_rt = layer_rt = NULL;
	IDirect3DDevice9_SetRenderTarget(dev, 0, old);
	if (old)
		IDirect3DSurface9_Release(old);
}

// The second 2D source: the frame's 2D command list (function 0x5c010, called at rva 0x4545b after the glow and blur): global texts, fades, the intro's title lines. It goes into the same
// layer (not cleared again).
#ifdef _WIN64
static void (*final2d_original)(void *);
#endif
static void final2d_hook(void *vp)
{
	IDirect3DDevice9 *dev = the_device;
	if (mode != 10 || !dev || render_half < 0 || !lay_surf[0] || !lay_surf[1]) {
		final2d_original(vp);
		return;
	}
	int half = render_half;
	IDirect3DSurface9 *old = NULL;
	IDirect3DDevice9_GetRenderTarget(dev, 0, &old);
	IDirect3DDevice9_SetRenderTarget(dev, 0, lay_surf[half]);
	lay_have[half] = 1;
	layer_pass = 1;
	final2d_original(vp);
	layer_pass = 0;
	IDirect3DDevice9_SetRenderTarget(dev, 0, old);
	if (old)
		IDirect3DSurface9_Release(old);
}

static void comp_hook(void)
{
	if (the_device)
		return;
#ifdef _WIN64
	IDirect3DDevice9 *dev = *(IDirect3DDevice9 **)(base + RVA_D3D_DEVICE);
#else
	IDirect3DDevice9 *dev = (IDirect3DDevice9 *)d3dcap_device(); // src/d3dcap.c: caught when the game created it
#endif
	if (!dev)
		return;
	the_device = dev;
	void **vt = *(void ***)dev;
	patch_slot(&vt[17], (void *)present_hook, (void **)&present_orig);
	if (cfg("resethook", 1))
		patch_slot(&vt[16], (void *)reset_hook, (void **)&reset_orig);
	IDirect3DDevice9Ex *ex = NULL;
	if (SUCCEEDED(IDirect3DDevice9_QueryInterface(dev, &iid_device9ex, (void **)&ex)) && ex) {
		patch_slot(&vt[121], (void *)present_ex_hook, (void **)&present_ex_orig); // PresentEx: the slot of the Ex interface
		if (cfg("resethook", 1))
			patch_slot(&vt[132], (void *)reset_ex_hook, (void **)&reset_ex_orig);
		IDirect3DDevice9Ex_Release(ex);
	}
	IDirect3DSwapChain9 *sc = NULL;
	if (SUCCEEDED(IDirect3DDevice9_GetSwapChain(dev, 0, &sc)) && sc) {
		void **svt = *(void ***)sc;
		patch_slot(&svt[3], (void *)sc_present_hook, (void **)&sc_present_orig);
		IDirect3DSwapChain9_Release(sc);
	}
	{
		HMODULE m1 = NULL, m2 = NULL;
		char n1[MAX_PATH] = "?", n2[MAX_PATH] = "?";
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)present_orig, &m1);
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)sc_present_orig, &m2);
		if (m1)
			GetModuleFileNameA(m1, n1, sizeof(n1));
		if (m2)
			GetModuleFileNameA(m2, n2, sizeof(n2));
		log_printf("native: device Present in %s, swap chain Present in %s", n1, n2);
	}
	if (mode == 10 || cfg("d3dtrace", 0))
		trace_install(dev);
	log_printf("native: Present hooks set (device %p, Ex %s)", (void *)dev, present_ex_orig ? "yes" : "no");
	if (cfg("watchdog", 1))
		CreateThread(NULL, 0, watchdog_thread, NULL, 0, NULL);
}

// the second draw of the frame must not start a new frame: it adds its view to the one the first draw began
static void begin_frame_hook(void)
{
	if (second_pass)
		return;
	begin_frame_original();
}

static unsigned cg_draw_count, hook_counts[8]; // QA counters: snapshot take, ps publish, server step, usercmd step, snapshot msg, server cmds
static int ctx_on;
static float cg_aspect_saved;
static int cg_aspect_have;
static float asp_saved[2];
static int asp_have;
static int split_on = 1; // the scripts set the dvar mw2coop_split_view once the intro is over; before that there is one picture
static int frame_bottom; // mode 5: this client frame is context 1's (the bottom half)
static int ctx1_ready;
static void ctx_swap(int to);
void native_unswap_entities(void);
static void ctx_after_draw(void);

// Diagnostics ([native] scanplace=1, once at the first split frame): where the 2D placement keeps the screen scale. The values the placement holds for a 1920x1080 screen are looked for in the
// game's image: the pair of floats (3.0, 2.25) (real pixels per virtual unit), (1/3, 0.4444), and the int pair (1920, 1080); every hit is logged with the floats around it.
static void scan_place(void)
{
#ifndef _WIN64
	return; // QA scan of the 64-bit placement array
#endif
	static int done;
	if (done || !cfg("scanplace", 0))
		return;
	done = 1;
	uint8_t *lo = base, *hi = base + ((IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew))->OptionalHeader.SizeOfImage;
	int hits = 0;
	for (uint8_t *a = lo; a < hi && hits < 60;) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery(a, &mbi, sizeof(mbi)))
			break;
		uint8_t *end = (uint8_t *)mbi.BaseAddress + mbi.RegionSize;
		if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD)) {
			for (uint8_t *q = (uint8_t *)mbi.BaseAddress; q + 8 <= end && hits < 60; q += 4) {
				float f0 = *(float *)q, f1 = *(float *)(q + 4);
				int i0 = *(int *)q, i1 = *(int *)(q + 4);
				const char *kind = NULL;
				if (f0 == 3.0f && f1 == 2.25f)
					kind = "scale 3.0 2.25";
				else if (f0 > 0.33332f && f0 < 0.33334f && f1 > 0.44443f && f1 < 0.44446f)
					kind = "inverse 1/3 0.4444";
				else if (i0 == 1920 && i1 == 1080)
					kind = "ints 1920 1080";
				if (kind) {
					hits++;
					char line[700];
					int n = snprintf(line, sizeof(line), "native: scanplace %s at rva 0x%llx:", kind, (unsigned long long)(q - base));
					for (int k = -2; k < 28 && n < (int)sizeof(line) - 24; k++)
						n += snprintf(line + n, sizeof(line) - n, " %g", (double)*(float *)(q + k * 4));
					log_printf("%s", line);
				}
			}
		}
		a = end;
	}
	log_printf("native: scanplace done, %d hits", hits);
}

static int ctx_current; // defined with the context ranges below
static unsigned p0cnt_draws, p0cnt_adds, p0cnt_flags1, p0cnt_flags0; // flags (4th argument) of the last add of entity 0 in context 1 / of entity 1 in context 0
static uint8_t p0dump_slot[2][CENT_SIZE], p0dump_dobj[2][0x200]; // [native] p0cnt=2: the entity slot and the DObj bytes of the other player as each context draws him (0: context 0's entity 1, 1: context 1's entity 0)
static int p0dump_have[2];
static void *p0ptr[6];
static unsigned p0ptr_cnt[6], p0ptr_b8[6], p0ptr_bc[6]; // the distinct DObjs added as entity 0 in context 1 since the last p0cnt line, how often, and their dwords +0xb8 / +0xbc
static void p0dump_diff(const char *what, const uint8_t *a, const uint8_t *b, int n)
{
	char buf[1200];
	int len = 0, cnt = 0;
	for (int i = 0; i + 4 <= n && len < (int)sizeof(buf) - 40; i += 4) {
		uint32_t x = *(const uint32_t *)(a + i), y = *(const uint32_t *)(b + i);
		if (x != y && ++cnt <= 60)
			len += snprintf(buf + len, sizeof(buf) - len, " %x:%x/%x", i, x, y);
	}
	buf[len] = 0;
	log_printf("native: p0dump %s %d dwords differ:%s", what, cnt, buf);
}
static float p0cnt_draw_org[3], p0cnt_draw_org_b[3]; // the two origins the last two adds of entity 0 were given (5th argument and the pose at rdx) // [native] p0cnt=1: context 1's draws, and the adds of entity 0's DObj to its scene, per second
static void cg_draw_hook_inner(unsigned local_client, int a, int b);
// [native] smoothlog=1: how evenly the camera of each view moves from one draw to the next while it walks (the draws in a row that did not move although it did, the speed per draw)
static void smooth_sample(int k)
{
	static float last[2][3];
	static LONGLONG last_t[2];
	static struct {
		long n, still, moving;
		double sum, sq;
		float max;
	} st[2];
	static LONGLONG report;
	LARGE_INTEGER c, f;
	QueryPerformanceCounter(&c);
	QueryPerformanceFrequency(&f);
	const float *o = (const float *)(base + RVA_CG_PS_ORIGIN);
	if (last_t[k]) {
		float dx = o[0] - last[k][0], dy = o[1] - last[k][1], dz = o[2] - last[k][2];
		float d = sqrtf(dx * dx + dy * dy + dz * dz);
		double ms = (double)(c.QuadPart - last_t[k]) * 1000.0 / (double)f.QuadPart;
		if (CFG_ONCE("smoothlog", 0) >= 2 && d > 0.05f && d < 100.0f) { // raw samples of a walking camera: the step, the wall time since the view's last draw, the game time step
			static int dumped;
			static int last_cgt[2];
			int cgt = *(const int *)(base + RVA_CG_TIME);
			if (dumped < 300)
				dumped++, log_printf("native: smooth raw view %d: step %.3f units in %.2f wall ms, game time step %d ms (%.3f units/ms of game time)", k, d, ms, cgt - last_cgt[k],
						     cgt > last_cgt[k] ? d / (cgt - last_cgt[k]) : 0.0f);
			last_cgt[k] = cgt;
		}
		if (d < 100.0f) {
			st[k].n++;
			if (d > 0.0005f) {
				double v = d / ms; // units per ms
				st[k].moving++, st[k].sum += v, st[k].sq += v * v;
				if ((float)v > st[k].max)
					st[k].max = (float)v;
			} else {
				st[k].still++;
			}
		}
	}
	memcpy(last[k], o, sizeof(last[k]));
	last_t[k] = c.QuadPart;
	if (!report)
		report = c.QuadPart;
	if (c.QuadPart - report >= f.QuadPart) {
		report = c.QuadPart;
		for (int i = 0; i < 2; i++) {
			double mean = st[i].moving ? st[i].sum / st[i].moving : 0, var = st[i].moving ? st[i].sq / st[i].moving - mean * mean : 0;
			if (st[i].n)
				log_printf("native: smooth view %d: %ld draws, %ld without camera movement, camera speed per draw mean %.4f sd %.4f max %.4f units/ms", i, st[i].n, st[i].still, mean,
					   var > 0 ? sqrt(var) : 0.0, (double)st[i].max);
			memset(&st[i], 0, sizeof(st[i]));
		}
	}
}
static void cg_draw_hook(unsigned local_client, int a, int b)
{
	cg_draw_hook_inner(local_client, a, b);
	if (CFG_ONCE("smoothlog", 0))
		smooth_sample(ctx_current == 1 ? 1 : 0);
}
static void cg_draw_hook_inner(unsigned local_client, int a, int b)
{
	cg_draw_count++;
	float *sw = subwindow();
	if (split_on)
		scan_place();
	// Time measurement ([native] timelog=1, 2026-10-28: the owner sees entities and players "as on ice" in the split): per cgame draw, the wall clock and the live context's
	// client times: cg.time (rva 0x422434), cg.frametime (0x422430), cg.frameInterpolation (0x42242c), cl.serverTime (0x51dca8) and the snapshot lerp fraction (0x51dcb0).
	if (CFG_ONCE("p0cnt", 0) && ctx_on && ctx_current == 1) {
		if (++p0cnt_draws >= 100) {
			const float *e = (const float *)(base + RVA_CENT);
			log_printf("native: p0cnt ctx1 draws %u adds of entity 0 %u | slot0 pos %.1f %.1f %.1f | cg.ps (p2) %.1f %.1f | last add: arg5 %.1f %.1f %.1f pose %.1f %.1f %.1f | flags ctx1 ent0 %08x ctx0 ent1 %08x", p0cnt_draws, p0cnt_adds, (double)e[63], (double)e[64], (double)e[65],
				   (double)*(const float *)(base + 0x42245c), (double)*(const float *)(base + 0x422460), (double)p0cnt_draw_org[0], (double)p0cnt_draw_org[1], (double)p0cnt_draw_org[2],
				   (double)p0cnt_draw_org_b[0], (double)p0cnt_draw_org_b[1], (double)p0cnt_draw_org_b[2], p0cnt_flags1, p0cnt_flags0);
			p0cnt_draws = p0cnt_adds = 0;
			{
				char b[300];
				int n = 0;
				for (int k = 0; k < 6 && p0ptr[k]; k++)
					n += snprintf(b + n, sizeof(b) - n, " [%p x%u b8 %x bc %x]", p0ptr[k], p0ptr_cnt[k], p0ptr_b8[k], p0ptr_bc[k]);
				b[n] = 0;
				log_printf("native: p0dobjs%s", b);
				memset(p0ptr, 0, sizeof(p0ptr));
				memset(p0ptr_cnt, 0, sizeof(p0ptr_cnt));
			}
			if (CFG_ONCE("p0cnt", 0) >= 2 && p0dump_have[0] && p0dump_have[1]) {
				static uint8_t prev_slot[CENT_SIZE], prev_dobj[0x200];
				p0dump_diff("slot ctx1ent0/ctx0ent1", p0dump_slot[1], p0dump_slot[0], sizeof(p0dump_slot[0]));
				p0dump_diff("dobj ctx1ent0/ctx0ent1", p0dump_dobj[1], p0dump_dobj[0], sizeof(p0dump_dobj[0]));
				p0dump_diff("slot ctx1 since last", p0dump_slot[1], prev_slot, sizeof(prev_slot));
				p0dump_diff("dobj ctx1 since last", p0dump_dobj[1], prev_dobj, sizeof(prev_dobj));
				memcpy(prev_slot, p0dump_slot[1], sizeof(prev_slot));
				memcpy(prev_dobj, p0dump_dobj[1], sizeof(prev_dobj));
			}
		}
	}
	// View angle check ([native] viewlog=1, 2026-10-28: a script's SetPlayerAngles turns player 2's body but not his camera): per context, every 25th draw, the live context's
	// playerState view angles (cg.ps rva 0x422440: pitch +0x108, yaw +0x10c, roll +0x110) and dwords +0x100..+0x120 (delta_angles are among them), the view angle accumulators
	// (rva 0x533d14 pitch, 0x533d18 yaw) and the cl block's playerState copy (rva RVA_CL_PS) yaw
	if (CFG_ONCE("viewlog", 0) && ctx_on) {
		static unsigned vn;
		if (++vn % 25 == 0 && vn < 20000) {
#ifdef _WIN64
			const uint8_t *ps = base + 0x422440;
#else
			const uint8_t *ps = base + RVA_CL_PS; // (32 bits: the cgame's own copy was not looked for; the client block's copy is the one the next frame is predicted from)
#endif
			const unsigned *d = (const unsigned *)(ps + 0x100);
			log_printf("native: viewlog origin ctx %d: %.1f %.1f %.1f", ctx_current, (double)*(float *)(ps + 0x1c), (double)*(float *)(ps + 0x20), (double)*(float *)(ps + 0x24));
			log_printf("native: viewlog ctx %d cg.ps pitch %.1f yaw %.1f roll %.1f | acc pitch %.1f yaw %.1f | cl.ps yaw %.1f | ps+100: %08x %08x %08x %08x %08x %08x %08x %08x", ctx_current,
				   (double)*(float *)(ps + 0x108), (double)*(float *)(ps + 0x10c), (double)*(float *)(ps + 0x110), (double)*(float *)(base + RVA_ACC_PITCH), (double)*(float *)(base + RVA_ACC_PITCH + 4),
				   (double)*(float *)(base + RVA_CL_PS + 0x10c), d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
		}
	}
	if (CFG_ONCE("timelog", 0) && ctx_on) {
		static unsigned seq;
		unsigned skip = (unsigned)CFG_ONCE("timelogskip", 0);
		if (seq >= skip && seq < skip + 6000) {
			LARGE_INTEGER pc, hz;
			QueryPerformanceCounter(&pc);
			QueryPerformanceFrequency(&hz);
			log_printf("native: timelog #%u wall %.1f ctx %d cg.time %d frametime %d interp %.3f cl.serverTime %d frac %.3f", seq, (double)pc.QuadPart * 1000.0 / (double)hz.QuadPart,
				   ctx_current, *(int *)(base + 0x422434), *(int *)(base + 0x422430), (double)*(float *)(base + 0x42242c), *(int *)(base + 0x51dca8), (double)*(float *)(base + 0x51dcb0));
			if (CFG_ONCE("timelog", 0) > 1) {
				// "as on ice": where player 2 is by his own context's playerState (cg.ps origin, rva 0x42245c, live in context 1) and where context 0's cgame has his entity (slot 1,
				// interpolated origin floats 63..65); both on the same cg.time scale
				const float *ps = (const float *)(base + 0x42245c);
				const float *e1 = (const float *)(base + RVA_CENT + CENT_SIZE);
				if (ctx_current == 1)
					log_printf("native: timelog p2ps cg.time %d pos %.1f %.1f %.1f", *(int *)(base + 0x422434), (double)ps[0], (double)ps[1], (double)ps[2]);
				else
					log_printf("native: timelog p2ent cg.time %d pos %.1f %.1f %.1f", *(int *)(base + 0x422434), (double)e1[63], (double)e1[64], (double)e1[65]);
			}
		}
		seq++;
	}
	if (!mode || !sw || second_pass || (mode >= 5 && !split_on)) {
		if ((mode == 8 || mode == 10) && cg_aspect_have) { // back from the split: the cgame's aspect as it was
			*(float *)(base + RVA_CG_ASPECT) = cg_aspect_saved;
			cg_aspect_have = 0;
		}
		if (mode == 7 && asp_have) { // back from the split: the video aspect as the game had it
			memcpy(base + RVA_VIDEO_ASPECT, asp_saved, sizeof(asp_saved));
			asp_have = 0;
		}
		cg_draw_original(local_client, a, b);
		if (ctx_on)
			ctx_after_draw();
		return;
	}
	float saved[4];
	memcpy(saved, sw, sizeof(saved));
	if (mode >= 2 && mode <= 10) { // alternate frames: one scene per frame, the top half on even frames and the bottom half on odd ones
		static unsigned frame;
		int bottom = mode >= 5 ? frame_bottom : (int)(frame++ & 1); // modes 5 and 6: the client frame (below) says whose frame it is
		sw[0] = 0, sw[1] = 1, sw[2] = bottom ? 0.5f : 0, sw[3] = bottom ? 1 : 0.5f;
		if (mode == 6) // the middle band of the full view in both halves: right proportions, half the vertical field
			sw[2] = 0.25f, sw[3] = 0.75f;
		if (mode == 9) // the views are recorded for a half-height screen: the whole viewport, in the top rows
			sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 1;
		if (mode == 8 || mode == 10) { // the console's way: the whole view, built for the shape of a half with the cgame's own aspect (rva RVA_CG_ASPECT, read by CG_CalcFov), drawn over the full frame and squeezed into the half
			sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 1;
			if (comp_w && comp_h) {
				float *aspect = (float *)(base + RVA_CG_ASPECT);
				if (!cg_aspect_have) {
					cg_aspect_saved = *aspect;
					cg_aspect_have = 1;
				}
				*aspect = (float)comp_w / ((float)comp_h * 0.5f);
			}
			{ // [native] pixelaspect=<f>: the pixel aspect the 2D is laid out with (rva RVA_PIXEL_ASPECT); the squeeze of the frame into its half flattens text 2:1, a factor of 2 or 0.5 here
			  // pre-stretches it. Kept while the picture is split.
				static float want = -1;
				static float saved;
				static int have;
				float *pa = (float *)(base + RVA_PIXEL_ASPECT);
				if (want < 0) {
					char v[32];
					GetPrivateProfileStringA("native", "pixelaspect", "0", v, sizeof(v), settings_path);
					want = (float)atof(v);
				}
				if (want > 0) {
					if (!have) {
						saved = *pa;
						have = 1;
					}
					*pa = want;
				}
			}
		}
		if (mode == 7) { // the whole view, built for the shape of a half (wide), drawn over the full frame: the compositor squeezes it into the half (rva RVA_VIDEO_ASPECT: aspect, RVA_PIXEL_ASPECT: pixel aspect)
			sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 1;
			if (comp_w && comp_h) {
				float *aspect = (float *)(base + RVA_VIDEO_ASPECT);
				if (!asp_have) {
					memcpy(asp_saved, aspect, sizeof(asp_saved));
					asp_have = 1;
				}
				aspect[0] = (float)comp_w / ((float)comp_h * 0.5f);
				aspect[1] = 1.0f;
			}
		}
		comp_hook();
		{
			static DWORD last_log;
			static unsigned draws;
			draws++;
			DWORD now = GetTickCount();
			if (now - last_log > 3000) {
				last_log = now;
				log_printf("native: split draws %u, device %p, pushes %ld, ring %ld-%ld, bottom %d", draws, (void *)the_device, (long)pushes_total, (long)ring_tail, (long)ring_head, bottom);
				if (ring_tail == 0 && draws > 100) { // no frame has ever been composed: are our hooks still in the tables?
					for (int i = 0; i < nslots; i++)
						log_printf("native: slot %d (%p): in %d, now %p, hook %p", i, (void *)slots[i].slot, slots[i].in, *slots[i].slot, slots[i].hook);
				}
			}
		}
		if (the_device && ring_head - ring_tail < 6) { // the half this frame is drawn into: the frame's end composes it
			ring[ring_head & 7] = bottom;
			InterlockedIncrement(&ring_head);
			InterlockedIncrement(&pushes_total);
		}
		if (mode == 4 && bottom && ctx1_ready) { // mode 4: the odd frames run the cgame in context 1 (the client statics swapped), as local client 0
			ctx_swap(1);
			cg_draw_original(0, a, b);
			ctx_swap(0);
		} else {
			cg_draw_original(mode == 3 && bottom ? 1u : local_client, a, b); // mode 3: the odd frames run the cgame for LOCAL CLIENT 1
		}
		half_note(*(int *)(base + RVA_CG_TIME), bottom);
		if (CFG_ONCE("perflog", 0)) { // the step of the game time between two draws of the same view
			static int last_t[2];
			int t = *(int *)(base + RVA_CG_TIME), k = bottom ? 1 : 0;
			if (last_t[k] && t >= last_t[k]) {
				int d = t - last_t[k];
				perf_td_n[k]++, perf_td_sum[k] += d, perf_td_sq[k] += (LONGLONG)d * d;
				if (d < perf_td_min[k])
					perf_td_min[k] = d;
				if (d > perf_td_max[k])
					perf_td_max[k] = d;
				if (d > 12)
					perf_td_big[k]++;
			}
			last_t[k] = t;
		}
		memcpy(sw, saved, sizeof(saved));
		if (ctx_on)
			ctx_after_draw();
		return;
	}
	sw[0] = 0, sw[1] = 1, sw[2] = 0, sw[3] = 0.5f; // the top half
	cg_draw_original(local_client, a, b);
	sw[2] = 0.5f, sw[3] = 1; // the bottom half
	second_pass = 1;
	cg_draw_original(local_client, a, b);
	second_pass = 0;
	memcpy(sw, saved, sizeof(saved));
	static int logged;
	if (!logged++)
		log_printf("native: the first frame was drawn twice (top and bottom half)");
}

// ---- experiment 4: the engine's own local-players configuration (docs/native-splitscreen-plan.md, "Packet flow")
// G_InitGame stores the number of local players (rva 0x3c3eb0) as the immediate 1 of `mov dword [rip+x], 1` (c7 05 <rel32> 01 00 00 00 at rva 0x1714a6) when the co-op byte is 0.
// settings.ini [native] players=2 makes it 2: the server then makes two clients and two player entities, with no network and no co-op session. The client side has one
// element of each array, so this only shows what the server side does alone.
static void sv_add_command(uint8_t *cl, const char *text)
{
	int idx = (int)((cl - (base + RVA_SV_CLIENTS)) / SV_CLIENT_STRIDE);
	int in_use = *(base + RVA_COOP_BYTE) ? *(int *)(base + RVA_COOP_SLOTS + idx * SLOT_STRIDE) == 2 : idx < *(int *)(base + RVA_LOCAL_PLAYERS);
	if (!in_use || (idx == 1 && !*(base + RVA_COOP_BYTE)))
		return;
	void (*com_error)(int, const char *, ...) = (void (*)(int, const char *, ...))(base + RVA_COM_ERROR);
	int *p = (int *)cl;
	int seq = p[2];
	if (seq - p[3] == 0x80) {
		com_error(1, "Server command overflow");
		seq = p[2];
	}
	p[2] = seq + 1;
	int len = (int)strlen(text);
	int off = p[1];
	if (0x1000 < off + 1 + len) {
		com_error(1, "Reliable command buffer overflow");
		off = p[1];
	}
	*(int *)(cl + 0x1010 + ((seq + 1) & 0x7f) * 4) = off;
	char *d = (char *)(cl + 0x10 + p[1]);
	for (int i = 0; i < len; i++)
		*d++ = text[i] == '%' ? '.' : text[i];
	*d = 0;
	p[1] += len + 1;
}

// Debug aid ([native] logcbuf=1): every text the engine adds to the command buffer, in the log.
static const uint8_t cbuf_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xf9, 0x48, 0x8b, 0xda};
static void (*cbuf_original)(int, const char *);
static void cbuf_hook(int lc, const char *text)
{
	char line[200];
	size_t i = 0;
	for (; text && text[i] && text[i] != '\n' && i < sizeof(line) - 1; i++)
		line[i] = text[i];
	line[i] = 0;
	log_printf("cbuf(%d): %s", lc, line);
	cbuf_original(lc, text);
}

// ---- experiment 5: a second client context (docs/native-splitscreen-plan.md): the engine's client code runs for local client 1 as local client 0, with the client statics swapped
// The arrays of the client side (cl, clc, cls, the cgame's cg/cgs) have one element, so the loops over the local clients (i < number of local players) would write local client 1's
// state over unrelated statics. Each call site of such a loop is redirected to a wrapper: for client 1 it swaps the client statics to context 1's copy, calls the function as
// client 0, and swaps back. The ranges are the first guess (cgame block, snapshots, cl, clc, cls, input); the ones the experiments show missing are added.
typedef struct {
	uint32_t rva, size;
} range_t;
#ifdef _WIN64
static const range_t ranges[] = {
    {0x404450, 0x44f000 - 0x404450}, // cgs, cg, cg_entities and neighbours
    {0x51b078, 0x51dcb0 - 0x51b078}, // cl snapshots and their pointers
    {0x52c400, 0x100},               // cleared by the cgame init for each client
    {0x52fbc0, 0x21c},               // input
    {0x533cb8, 0x68},                // cls state, the mouse state and the view angle accumulators (0x533cc0, 0x533d0c-0x533d18)
    {0x533d20, 0x540b60 - 0x533d20}, // cl: usercmd ring, cmdNumber, playerstate, packet history
    {0x545844, 0x3218},              // clc: reliable command ring
    {0x548ce8, 8},                   // the snapshot message's time and flag
    {0x3cf180, 0xe80},               // aim assist of local client 0 (aaGlob: the targets on screen, the aim_* regions): each context's cgame fills its own
    {0xbc7300, 0xcdb110 - 0xbc7300}, // the effects system of local client 0 (FX_InitSystem 0x119a00: header 0xb18, its pools from 0xbc7e80, 0x10ea80 bytes, then 0xcd6900 + 0x4810):
                                     // with one system both halves drew the muzzle flash and smoke of the first-person weapon of the other ([native] fxsplit=0 off)
};
#define RANGE_FX 9
#else
// 32 bits (docs/32bit-native-map/clientctx.md): the same eight kinds of state in the same order (the code indexes them), the cgame block in two parts: [0] is its main part
// (the first 135 entities included, as in 64 bits) and [9] the view model pointers behind the entity array
static const range_t ranges[] = {
    {0x35f280, 0x7a0000 - 0x75f280}, // cgs, cg, media tables, cg_entities 0..134
    {0x46798c, 0x2c30},              // cl snapshots and their pointers
    {0x46b180, 0x100},               // cleared by the cgame init for each client
    {0x4889b0, 0x21c},               // input
    {0x525bc0, 0x80},                // cls state, the mouse state and the view angle accumulators (0x925c34 pitch, 0x925c38 yaw)
    {0x525c40, 0xbe3c},              // cl: usercmd ring (64 deep), cmdNumber, playerstate (+0x1018), packet history
    {0x48c330, 0x3218},              // clc: reliable command ring
    {0x494424, 8},                   // the snapshot message's time and flag
    {0x33ba90, 0xe80},               // aim assist of local client 0 (aaGlob)
    {0x45e8a4, 0x2c},                // the cgame's view model pointers (2 hands x 0x14)
    {0x983e80, 0xa90c80 - 0x983e80}, // the effects system of local client 0 (FX_InitSystem VA 0x49e390: header VA 0xd83e80 (0xad0), the block 0xd84980 (0x4810), the pools from
                                     // 0xd89200 (0x107a80 bytes)): see the 64-bit range; [native] fxsplit=0 turns it off
};
#define RANGE_FX 10
#endif
#define NRANGES (sizeof(ranges) / sizeof(ranges[0]))
#define RANGE_RVA(i) XR(ranges[i].rva) // (iw4x-sp: the range's start in the 159 image, the same length)
static uint8_t *ctx_store[2][NRANGES];
static int ctx_current;

static void ctx_ensure(void)
{
	if (ctx_store[0][0])
		return;
	for (size_t i = 0; i < NRANGES; i++) { // first use: both copies start as what the live memory holds now
		ctx_store[0][i] = VirtualAlloc(NULL, ranges[i].size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		ctx_store[1][i] = VirtualAlloc(NULL, ranges[i].size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		memcpy(ctx_store[1][i], base + RANGE_RVA(i), ranges[i].size);
	}
}

// `mask`: the ranges (bit i = ranges[i]) that are swapped; a function that touches only the cl block need not expose the other threads to a swapped cgame block. A masked swap
// must be undone with the same mask, and not nest with another.
#define CTX_ALL ((1u << NRANGES) - 1)
#define CTX_CL (1u << 5)
#define CTX_CL_CLC ((1u << 5) | (1u << 6))
// The renderer's worker threads finish a frame's scene while the client goes on (types at rva 0x370340: "add scene ent", "skin model", the cell and shadow entities...):
// they read the cgame's entities and the skeletons through pointers into the live client statics. A swap rewrote those under them, and the bottom half's far NPCs were
// skinned with context 0's data for a frame (2026-10-28: gone with r_smp_worker 0, not with r_smp_backend 0). Before a swap, the engine's own wait for a type of worker
// command (rva 0x300a0: it helps the workers until the type's count of commands in flight is 0) runs for types 0-18; 19 and 20 are physics ([native] workerwait=0 off).
static void ctx_wait_workers(void)
{
	static int on = -1;
	if (on < 0)
		on = cfg("workerwait", 1);
	if (!on)
		return;
	void (*wait_type)(int) = (void (*)(int))(base + RVA_WAIT_TYPE);
	LONGLONG t0 = perf_now();
	for (int t = 0; t <= 18; t++)
		wait_type(t);
	perf_wait += perf_now() - t0;
}

static void ctx_swap_mask(int to, unsigned mask)
{
	if (to == ctx_current)
		return;
	ctx_ensure();
	ctx_wait_workers();
	LONGLONG t0 = perf_now();
	static int fx_on = -1;
	if (fx_on < 0)
		fx_on = cfg("fxsplit", 1);
	for (size_t i = 0; i < NRANGES; i++) {
		if (!(mask & (1u << i)))
			continue;
#ifdef RANGE_FX
		if (i == RANGE_FX && !fx_on)
			continue;
#endif
		uint8_t *live = base + RANGE_RVA(i);
		memcpy(ctx_store[ctx_current][i], live, ranges[i].size);
		memcpy(live, ctx_store[to][i], ranges[i].size);
	}
	perf_copy += perf_now() - t0;
	InterlockedIncrement(&perf_swaps);
	ctx_current = to;
}

static void ctx_swap(int to)
{
	ctx_swap_mask(to, CTX_ALL);
}

// The cgame block holds what the cgame registered for the whole process (the media handles the 2D drawing uses even while the cgame is still being initialised). Context 1 starts
// from context 0's block after its init, so its own init can draw the loading screen.
static void ctx_seed_cgame(void)
{
	static int seeded;
	if (seeded || ctx_current != 0)
		return;
	ctx_ensure();
	// Context 1 starts as a copy of context 0 as it is now (the cgame registered dvars and media for the whole process, the snapshot pointers point into their own block), except for
	// the playerstate the server published for client 1, which is its own.
	for (size_t i = 0; i < NRANGES; i++) {
		uint8_t *live = base + RANGE_RVA(i);
		if (i == 5) { // the cl block: everything but the playerstate at +0x2018 (32 bits: +0x1018)
			memcpy(ctx_store[1][i], live, CL_BLOCK_PS);
			memcpy(ctx_store[1][i] + CL_BLOCK_PS + 0xaca4, live + CL_BLOCK_PS + 0xaca4, ranges[i].size - CL_BLOCK_PS - 0xaca4);
		} else {
			memcpy(ctx_store[1][i], live, ranges[i].size);
		}
	}
	seeded = 1;
}

// The client code reads the number of local players (rva 0x3c3eb0) at these instructions (rva, length, offset of the rip-relative displacement). The server and the game module keep
// reading the real one (2); the client code is pointed at a copy that stays 1, so its loops over the local clients only ever touch client 0 and nothing of the arrays of one element.
static const struct {
	uint32_t rva;
	uint8_t length, disp;
} client_n_sites[] = {
	{0x6bae9, 6, 2},
	{0x85f44, 6, 2},
	{0x89e44, 6, 2},
	{0x93d94, 6, 2},
	{0xa08c2, 7, 3},
	{0xa6703, 7, 3},
	{0xa69cf, 6, 2},
	{0xa6a76, 6, 2},
	{0xa6b29, 7, 3},
	{0xa6c44, 6, 2},
	{0xa6c8c, 6, 2},
	{0xa8f25, 6, 2},
	{0xa905d, 6, 2},
	{0xa9248, 6, 2},
	{0xa936b, 6, 2},
	{0xb2154, 6, 2},
	{0xb60ec, 7, 3},
	{0xb629a, 6, 2},
	{0xb630a, 6, 2},
	{0xb71df, 6, 2},
	{0xb8e34, 6, 2},
	{0xbcb49, 6, 2},
	{0xc2835, 6, 2},
	{0xc3e5e, 6, 2},
	{0xc3eb8, 6, 2},
	{0xc9667, 6, 2},
	{0xc96f5, 6, 2},
	{0xc9754, 6, 2},
	{0xc9a04, 6, 2},
	{0xca135, 6, 2},
	{0xca184, 6, 2},
	{0xca607, 6, 2},
	{0xd22ac, 6, 2},
	{0xd2304, 6, 2},
	{0xd5595, 6, 2}
};

#ifdef _WIN64
static void client_n_install(void)
{
	int32_t *n_client = (int32_t *)hook_page_near(base + RVA_LOCAL_PLAYERS_STORE);
	if (!n_client) {
		log_printf("native: no page near the image for the client's player count");
		return;
	}
	*n_client = 1;
	int done = 0;
	for (size_t i = 0; i < sizeof(client_n_sites) / sizeof(client_n_sites[0]); i++) {
		uint8_t *site = base + client_n_sites[i].rva;
		int32_t old;
		memcpy(&old, site + client_n_sites[i].disp, 4);
		if ((uint8_t *)(site + client_n_sites[i].length + old) != base + 0x3c3eb0)
			continue; // not the instruction the build has
		int64_t to = (int64_t)((uint8_t *)n_client - (site + client_n_sites[i].length));
		if (to > INT32_MAX || to < INT32_MIN)
			continue;
		int32_t rel = (int32_t)to;
		DWORD prot;
		j_save(site, 8);
		VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &prot);
		memcpy(site + client_n_sites[i].disp, &rel, 4);
		VirtualProtect(site, 8, prot, &prot);
		done++;
	}
	FlushInstructionCache(GetCurrentProcess(), base, 0xe0000);
	log_printf("native: %d of %d client reads of the local player count point at a copy (1)", done, (int)(sizeof(client_n_sites) / sizeof(client_n_sites[0])));
}
#endif

// The server's entity list for the snapshot leaves out the local players' entities (they are fed through the playerstate); with two local players each context would never see the other
// one's body. The read of the player count at the start of the publisher (rva 0x23459a, `mov reg, [0x3c3eb0]`, used only for that exclusion) is pointed at a variable that is 0.
// [native] listplayers=1.
#ifdef _WIN64
static void list_players_install(void)
{
	uint8_t *site = base + 0x23459a;
	int32_t old;
	memcpy(&old, site + 3, 4);
	int32_t *zero = (int32_t *)hook_page_near(base + RVA_LOCAL_PLAYERS_STORE);
	if (!zero || (uint8_t *)(site + 7 + old) != base + 0x3c3eb0) {
		log_printf("native: entity list patch: unexpected code");
		return;
	}
	*zero = 0;
	int32_t rel = (int32_t)((uint8_t *)zero - (site + 7));
	DWORD prot;
	j_save(site, 8);
	VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &prot);
	memcpy(site + 3, &rel, 4);
	VirtualProtect(site, 8, prot, &prot);
	FlushInstructionCache(GetCurrentProcess(), site, 8);
	log_printf("native: the entity list keeps the local players' entities");
}
#endif

static void (*ps_publish_original)(unsigned, const void *);
static void (*server_cmds_original)(uintptr_t, int *);
static void (*usercmd_step_original)(int);
static void (*snapshot_msg_original)(void *, int);
static void (*cg_init_all_original)(void);
static void (*snapshot_take_original)(void);
#define RVA_LOCAL_PLAYERS_N 0x3c3eb0

// A client-side function that loops over the local clients (and calls others that touch the client statics without a client number): run for context 0 and, once context 1
// exists, for context 1, each time with the number of local players set to 1 so that every loop sees only client 0, which in context 1 is client 1.
static int cg_init_done;
static int draws_after_init;

static void run_both(void (*fn)(void))
{
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	fn();
	if (ctx1_ready) {
		ctx_swap(1);
		fn();
		ctx_swap(0);
	}
	if (was)
		ctx_swap(was);
}

// Hang watch ([native] hangwatch=0 off): after a level's cgame init, when no frame comes for 10 s, or frames stop for 6 s once they came, every other thread of the game is
// stopped for a moment and its instruction pointer and the code addresses on its stack are logged (module+offset), once per hang. The threads run again before anything is logged.
static volatile LONG hang_frames; // frames that reached the draw hooks (draw_hook)
static volatile DWORD hang_init_tick;
static void hang_dump_thread(const char *why, DWORD only) // only: one thread's id, 0: every thread but this one
{
	DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;
	static char lines[24][1400];
	int nl = 0;
	THREADENTRY32 te;
	te.dwSize = sizeof(te);
	for (BOOL ok = Thread32First(snap, &te); ok && nl < 24; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid || te.th32ThreadID == self || (only && te.th32ThreadID != only))
			continue;
		HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
		if (!th)
			continue;
		uintptr_t addrs[40];
		int na = 0;
		if (SuspendThread(th) != (DWORD)-1) {
			CONTEXT ctx;
			memset(&ctx, 0, sizeof(ctx));
			ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
			if (GetThreadContext(th, &ctx)) {
				addrs[na++] = CTX_IP(ctx);
				const uintptr_t *sp = (const uintptr_t *)CTX_SP(ctx);
				for (int i = 0; i < 600 && na < 40; i++) {
					uintptr_t v = 0;
					if (!ReadProcessMemory(GetCurrentProcess(), sp + i, &v, sizeof(v), NULL))
						break;
					MEMORY_BASIC_INFORMATION mbi;
					if (v > 0x10000 && VirtualQuery((void *)(uintptr_t)v, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE &&
					    (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
						addrs[na++] = v;
				}
			}
			ResumeThread(th);
		}
		CloseHandle(th);
		int n = snprintf(lines[nl], sizeof(lines[nl]), "native: HANG (%s) thread %lu:", why, (unsigned long)te.th32ThreadID);
		for (int i = 0; i < na && n < (int)sizeof(lines[nl]) - 60; i++) {
			HMODULE m = NULL;
			char name[MAX_PATH] = "?";
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)addrs[i], &m) && m) {
				GetModuleFileNameA(m, name, sizeof(name));
				const char *slash = strrchr(name, '\\');
				n += snprintf(lines[nl] + n, sizeof(lines[nl]) - n, " %s+%llx", slash ? slash + 1 : name, (unsigned long long)(addrs[i] - (uintptr_t)m));
			}
		}
		nl++;
	}
	CloseHandle(snap);
	for (int i = 0; i < nl; i++)
		log_printf("%s", lines[i]);
}
static void hang_dump(const char *why)
{
	hang_dump_thread(why, 0);
}
// Stall watch (QA, [native] stallwatch=<ms>, off by default): when the thread of the client frames has not started a frame for that long, its stack is logged in the middle of
// the stall (and once more at three times as long): where the engine spends a hitch. At most 80 per session.
static volatile DWORD stall_tid;
static volatile LONGLONG stall_frame_t;
static DWORD WINAPI stall_watch_thread(LPVOID p)
{
	const LONGLONG limit = (LONGLONG)(intptr_t)p;
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	LONGLONG seen = 0;
	int stage = 0, dumps = 0;
	for (;;) {
		Sleep(4);
		LONGLONG t = stall_frame_t;
		if (!t || !stall_tid || dumps >= 80)
			continue;
		if (t != seen)
			seen = t, stage = 0;
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		LONGLONG since = (now.QuadPart - t) * 1000 / f.QuadPart;
		if ((stage == 0 && since > limit) || (stage == 1 && since > 3 * limit)) {
			char why[48];
			snprintf(why, sizeof(why), "stall %lld ms", since);
			hang_dump_thread(why, stall_tid);
			stage++, dumps++;
		}
	}
	return 0;
}
static DWORD WINAPI hang_watch_thread(LPVOID unused)
{
	(void)unused;
	LONG last = -1;
	DWORD since = GetTickCount();
	int dumped = 0;
	for (;;) {
		Sleep(1000);
		DWORD now = GetTickCount();
		LONG f = hang_frames;
		if (f != last) {
			last = f, since = now, dumped = 0;
			continue;
		}
		if (dumped || !hang_init_tick)
			continue;
		if (f == 0 && now - hang_init_tick > 10000) // (hang_frames is zeroed at each cgame init)
			dumped = 1, hang_dump("no frame 10 s after the cgame init");
		else if (f > 0 && now - since > 6000)
			dumped = 1, hang_dump("no frame for 6 s");
	}
	return 0;
}

static void cg_init_all_hook(void)
{
	log_printf("native: cgame init (context 0; context 1 follows after the first frames), dvar ptr 0x52bfc0 = %p", *(void **)(base + 0x52bfc0));
	{
		static int started;
		if (!started && cfg("hangwatch", 1)) {
			started = 1;
			CreateThread(NULL, 0, hang_watch_thread, NULL, 0, NULL);
		}
		static int stall_started;
		int stall_ms = cfg("stallwatch", 0);
		if (!stall_started && stall_ms > 0) {
			stall_started = 1;
			CreateThread(NULL, 0, stall_watch_thread, (LPVOID)(intptr_t)stall_ms, 0, NULL);
		}
		hang_frames = 0;
		hang_init_tick = GetTickCount();
	}
	cg_init_all_original();
	cg_init_done = 1;
	if (native_active) { // a new level: context 1 starts again from this level's cgame
		ctx1_ready = 0;
		draws_after_init = 0;
	}
	log_printf("native: cgame init done, dvar ptr 0x52bfc0 = %p", *(void **)(base + 0x52bfc0));
}

static void snapshot_take_hook(void)
{
	hook_counts[0]++;
	run_both(snapshot_take_original);
}

// The cgame registers its dvars and media while it runs the first frames, and the 2D drawing of every thread reads them: context 1 can only start from a cgame block that has them.
// So its init runs a few frames after context 0's, from its first frame's hook, from a copy of context 0's block.
static void ctx_dump(const char *what)
{
#ifdef _WIN64
	static const uint32_t probes[] = {0x422434, 0x40caa0, 0x40cad0, 0x40cad8, 0x40cab8, 0x422440};
#else
	static const uint32_t probes[] = {0x374c20, 0x35f2a0, 0x35f2c8, 0x35f2bc, 0x35f2b8, 0x374c2c}; // cg.time, the cgame's client numbers and pointers, cg.ps
#endif
	uint8_t *r1_live = base + RANGE_RVA(0);
	uint8_t *r1 = ctx_current == 0 && ctx_store[1][0] ? ctx_store[1][0] : r1_live;
	char line[400];
	int n = snprintf(line, sizeof(line), "native: %s ctx1:", what);
	for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
		n += snprintf(line + n, sizeof(line) - n, " %x=%llx", XR(probes[i]), *(unsigned long long *)(r1 + XR(probes[i]) - RANGE_RVA(0)));
	n += snprintf(line + n, sizeof(line) - n, " | ctx0:");
	for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
		n += snprintf(line + n, sizeof(line) - n, " %x=%llx", XR(probes[i]), *(unsigned long long *)(r1_live + XR(probes[i]) - RANGE_RVA(0)));
	log_printf("%s", line);
}

static void ctx_jitter_log(void)
{
	static unsigned seen;
	if (ctx_current != 1 || !CFG_ONCE("jitterlog", 0))
		return;
	seen++;
	if (seen < 1500 || seen >= 1506)
		return;
	char line[1200];
	int n = snprintf(line, sizeof(line), "native: ctx1 ps frame %u:", seen);
	const float *f = (const float *)(base + 0x422440);
	for (int i = 0; i < 70; i++) {
		float v = f[i];
		if (v > -100000.0f && v < 100000.0f && (v > 0.0001f || v < -0.0001f))
			n += snprintf(line + n, sizeof(line) - n, " %x=%.1f", i * 4, v);
	}
	log_printf("%s", line);
}

// Diagnostics ([native] entlog=1): the entities of the live cgame near the camera (cg_entities at rva RVA_CENT, CENT_SIZE bytes each: eType +0xe0, origin +0x18, clientNum +0x160)
static void ctx_entity_log(void)
{
	static unsigned seen[2];
	if (!CFG_ONCE("entlog", 0))
		return;
	if (++seen[ctx_current] != 600)
		return;
	{
		const float *f = (const float *)(base + 0x422440);
		char line[1500];
		int n = snprintf(line, sizeof(line), "native: ctx %d cg ps floats:", ctx_current);
		for (int i = 0; i < 100; i++)
			if (f[i] > -400.0f && f[i] < 400.0f && (f[i] > 0.01f || f[i] < -0.01f))
				n += snprintf(line + n, sizeof(line) - n, " %x=%.1f", i * 4, f[i]);
		log_printf("%s", line);
	}
	{
		const float *f = (const float *)(base + 0x43c200);
		char line[1500];
		int n = snprintf(line, sizeof(line), "native: ctx %d refdef-ish floats @43c200:", ctx_current);
		for (int i = 0; i < 90; i++)
			if (f[i] > -1000000.0f && f[i] < 1000000.0f && (f[i] > 0.001f || f[i] < -0.001f))
				n += snprintf(line + n, sizeof(line) - n, " %x=%.2f", i * 4, f[i]);
		log_printf("%s", line);
	}
	const float *cam = (const float *)(base + 0x42245c);
	log_printf("native: ctx %d camera %.0f %.0f %.0f, clientNum %d", ctx_current, cam[0], cam[1], cam[2], *(int *)(base + 0x40caa0));
	for (int i = 0; i < 1024; i++) {
		const uint8_t *e = base + RVA_CENT + (size_t)i * CENT_SIZE;
		const float *o = (const float *)(e + 0x18);
		float dx = o[0] - cam[0], dy = o[1] - cam[1], dz = o[2] - cam[2];
		if (dx * dx + dy * dy + dz * dz < 200.0f * 200.0f && e[CENT_TYPE] != 0)
			log_printf("native:   ent %d type %d flags %08x clientNum %d at %.0f %.0f %.0f", i, e[CENT_TYPE], *(unsigned *)(e + CENT_FLAGS), *(unsigned short *)(e + 0x160), o[0], o[1], o[2]);
	}
}

// Diagnostics ([native] timelog=1): the cgame's time (rva 0x422434) at the end of each context's draw, a few dozen frames in a row
static void time_log(void)
{
	static int armed = -1, n;
	static char line[900];
	static int len;
	if (armed < 0)
		armed = cfg("timelog", 0);
	if (!armed || !split_on || !ctx1_ready)
		return;
	if (++n < 3000 || n >= 3040)
		return;
	len += snprintf(line + len, sizeof(line) - len, " %d:%d", ctx_current, *(int *)(base + 0x422434));
	if (n == 3039)
		log_printf("native: cg.time per drawn frame (context:time):%s", line);
}

static void ctx_after_draw(void)
{
	static int texts_done;
	InterlockedIncrement(&hang_frames);
	time_log();
	if (!texts_done) { // both players are on this machine: the co-op "waiting for the other player" texts have nothing to wait for (an asset lookup: not while the level is loading)
		texts_done = 1;
		localize_override("COOP_WAITINGFORPLAYER", " ");
		localize_override("SPECIAL_OPS_WAITING_OTHER_PLAYER", " ");
	}
	ctx_entity_log();
	ctx_jitter_log();
	static unsigned count;
	if (ctx1_ready && ++count % 240 == 0 && count < 1200)
		ctx_dump("state");
	static int logged;
	if (!logged++)
		log_printf("native: first draw after the hooks: cg_init_done %d ready %d", cg_init_done, ctx1_ready);
	if (ctx1_ready || !cg_init_done || ++draws_after_init < 5)
		return;
	log_printf("native: starting context 1");
	ctx_seed_cgame();
#ifdef _WIN64
	memset(ctx_store[1][0] + (0x440730 - RANGE_RVA(0)), 0, 0x40); // context 1 makes its own view model (see viewmodel_setup_hook)
#else
	memset(ctx_store[1][9], 0, ranges[9].size); // (32 bits: the view model pointers are a range of their own)
#endif
	if (cfg("p1cmd", 0)) {
		// player 1's view: the view angle accumulators (pitch 0x533d14, yaw 0x533d18, degrees) of context 1's block; the server turns them into his view with the
		// angles it set at his spawn, so this is relative to that (player 1 stood at yaw -77 with 0): [native] p1yaw=<degrees>
		float *acc = (float *)(ctx_store[1][4] + ACC_IN_CLS);
		acc[0] = 0;
		acc[1] = (float)cfg("p1yawdeg", 60);
	}
	if (cfg("p1cmd", 0)) {
		// and player 0's: turned once by [native] p0turn degrees (default 241: from the spawn yaw -78 to the direction of player 1, 163) through his own view angle accumulator
		float *acc0 = (float *)(base + RVA_ACC_PITCH);
		acc0[1] += (float)cfg("p0turn", 241);
	}
	ctx_swap(1);
	// not the whole cgame init (it also resets what is shared: effects, media, the renderer's per-level state): only its two per-client parts, on the copied block
	((void (*)(int))(base + RVA_CLIENT_INIT_A))(0);
	((void (*)(int))(base + RVA_CLIENT_INIT_B))(0);
	ctx_swap(0);
	// the cgame's own idea of its local client number (rva 0x40caa4): the draw hides "my" entity by it and answers per-client visibility masks by it; context 1 runs as local client 0
	// but is client 1
	*(int *)(ctx_store[1][0] + ((RVA_CG_CLIENTNUM + 4) - RANGE_RVA(0))) = cfg("ctx1lc", 1);
	ctx1_ready = 1;
	log_printf("native: context 1 initialised");
	ctx_dump("after init");
}

// The server's copy of a local player's playerstate into the cl block (rva RVA_CL_PS is +0x2018 into the block at 0x533d20): for client 1 straight into context 1's copy, so no other thread
// ever sees a swapped block for it.
#define PS_SIZE 0xaca4
static void ps_publish_hook(unsigned lc, const void *ps)
{
	hook_counts[1]++;
	if (lc == 0 && ctx_current == 0)
		return ps_publish_original(0, ps);
	ctx_ensure();
	// the playerstate of client lc belongs to context lc: into the live block when that context is the live one, else into its stored copy
	uint8_t *dest = (int)lc == ctx_current ? base + RVA_CL_PS : ctx_store[lc][5] + CL_BLOCK_PS;
	memcpy(dest, ps, PS_SIZE);
}

static void server_cmds_hook(uintptr_t lc, int *msg)
{
	hook_counts[5]++;
	if ((int)lc != 0)
		return; // client 1 gets no server commands yet
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	server_cmds_original(0, msg);
	if (was)
		ctx_swap(was);
}

// The server's snapshot message (entities, as shared lists) is parsed into the client's snapshot ring: once for each context, the message read again from its start.
static void snapshot_msg_hook(void *msg, int time)
{
	hook_counts[4]++;
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	uint8_t copy[MSG_T_SIZE]; // the engine's msg_t: 0x30 bytes in 64 bits, 0x28 in 32
	memcpy(copy, msg, sizeof(copy));
	snapshot_msg_original(msg, time);
	if (ctx1_ready) {
		memcpy(msg, copy, sizeof(copy));
		ctx_swap(1);
		snapshot_msg_original(msg, time);
		ctx_swap(0);
	}
	if (was)
		ctx_swap(was);
}

// ---- the players' devices: [native] p0pad (player 1, the top half) and p1pad (player 2): an XInput pad 0-3, or -1 for the keyboard and mouse. A player's pad is read in his
// own context's client frame, with his context live. Stick left: movement (analog, into the usercmd by make_cmd_hook), stick right: the view (the view angle accumulators of the
// live block, rva 0x533d14 pitch / 0x533d18 yaw, degrees), the buttons run the game's own actions for the live kbuttons (rva 0xd1810 name -> index in the action table at rva
// 0x3be770, 0xce450 runs it for the local client with a key number no keyboard sends). The layout is the console's: A jump, B stance (hold: prone, cl_stanceHoldTime),
// X reload (hold: use), Y next weapon, LB special grenade, RB frag, LT aim, RT fire, LS sprint / hold breath, RS melee, d-pad the action slots, Back objectives, Start pause.
// In a menu (key catchers 0x10) player 1's pad sends the keys a menu knows: d-pad / left stick arrows, A Enter, B / Start Escape; player 2's pad does nothing there.
typedef struct {
	WORD buttons;
	BYTE lt, rt;
	SHORT lx, ly, rx, ry;
} xin_gamepad_t;
typedef struct {
	DWORD packet;
	xin_gamepad_t pad;
} xin_state_t;
static DWORD(WINAPI *xinput_get)(DWORD, xin_state_t *);
static int pad_dev[2] = {-1, -1}; // per player: an XInput pad 0-3, or -1 (the keyboard and mouse)
#define p1pad pad_dev[1]
typedef struct {
	int forward, right; // -127..127, for the usercmd of this frame
	int active;         // the pad is connected
	WORD held;
	int rt, lt;
	LONGLONG last_count; // the performance counter at the last frame (the turn is per second)
	WORD menu_held;
	DWORD menu_repeat; // when a held direction in a menu sends its key again
} pad_player_t;
static pad_player_t pads[2];
// the player whose context gets the keyboard and the mouse: player 2 only when player 1 plays on a pad and player 2 on none
static int kb_player(void)
{
	return pad_dev[0] >= 0 && pad_dev[1] < 0 ? 1 : 0;
}

// an action of the table at rva 0x3be770 by its name ("+attack" / "-attack", "weapnext"), for local client 0 of the live context
#ifndef _WIN64
// 32 bits (docs/32bit-native-map/inputui.md): there is no action table; the buttons are console commands (+attack, -attack, weapnext ...). The command text goes into the command buffer
// of local client 0 and the buffer is executed at once (Cbuf_Execute, EBX = local client), so it acts on the live context's input block: the player's own, as in the 64-bit build.
static void cbuf_run_now(void)
{
	void *fn = base + XR(0x2001c0);
	__asm__ volatile("call *%0" : : "m"(fn), "b"(0) : "eax", "ecx", "edx", "esi", "edi", "memory", "cc");
}
static void pad_action(const char *name)
{
	((void (*)(int, const char *))(base + XR(0x9b40)))(0, name); // Cbuf_AddText
	cbuf_run_now();
}
#else
static void pad_action(const char *name)
{
	int index = ((int (*)(const char *))(base + 0xd1810))(name);
	if (!index) {
		static char logged[16][24];
		static int count;
		for (int i = 0; i < count; i++)
			if (!strcmp(logged[i], name))
				return;
		if (count < 16)
			snprintf(logged[count++], sizeof(logged[0]), "%s", name);
		log_printf("native: pad: no action '%s'", name);
		return;
	}
	((void (*)(int, int, int))(base + 0xce450))(0, index, 0x1ff);
}
#endif
static void pad_binding(const char *name, int down)
{
	char full[48];
	snprintf(full, sizeof(full), "%c%s", down ? '+' : '-', name);
	pad_action(full);
}

// the engine's key event (rva 0xd0f80: local client, key, down, time), for a menu: K_ENTER 13, K_ESCAPE 27, K_UPARROW 154 .. K_RIGHTARROW 157 (the key name table at rva 0x3bdb70)
#ifdef _WIN64
static void pad_key(int key, int down)
{
	((void (*)(int, int, int, unsigned))(base + 0xd0f80))(0, key, down, (unsigned)GetTickCount());
}
#else
static void (*key_event_original)(int, int, int, unsigned);
static void pad_key(int key, int down)
{
	key_event_original(0, key, down, (unsigned)GetTickCount());
	cbuf_run_now(); // (the key's binding went into the command buffer as text)
}
#endif

// a stick with a radial dead zone: the length past the dead zone is rescaled to 0..1 and raised to the response curve's exponent
static void pad_stick(float *x, float *y, float dead, float curve)
{
	float m = sqrtf(*x * *x + *y * *y);
	if (m <= dead) {
		*x = *y = 0;
		return;
	}
	float n = (m > 1.0f ? 1.0f : m);
	n = powf((n - dead) / (1.0f - dead), curve);
	*x = *x / m * n;
	*y = *y / m * n;
}

static int qa_noassist[2];
static int pad_read(int pl, xin_state_t *st)
{
	if (!xinput_get) {
		HMODULE m = LoadLibraryA("xinput1_4.dll");
		if (!m)
			m = LoadLibraryA("xinput1_3.dll");
		if (!m)
			m = LoadLibraryA("xinput9_1_0.dll");
		if (m)
			xinput_get = (DWORD(WINAPI *)(DWORD, xin_state_t *))GetProcAddress(m, "XInputGetState");
		if (!xinput_get) {
			static int logged;
			if (!logged++)
				log_printf("native: no XInput, no pads");
			return 0;
		}
	}
	memset(st, 0, sizeof(*st));
	int have = xinput_get((DWORD)pad_dev[pl], st) == 0;
	{ // QA: mw2coop\fakepad.txt "lx ly rx ry buttons lt rt" (stick values -32767..32767, buttons as the XInput mask) stands in for player 2's pad, fakepad0.txt for player 1's
		static DWORD checked[2];
		static int fake_have[2];
		static int f[2][7];
		DWORD now = GetTickCount();
		if (now - checked[pl] > 250) {
			checked[pl] = now;
			char path[MAX_PATH];
			lstrcpyA(path, settings_path);
			char *slash = strrchr(path, '\\');
			if (slash) {
				lstrcpyA(slash + 1, pl ? "fakepad.txt" : "fakepad0.txt");
				FILE *fp = fopen(path, "r");
				fake_have[pl] = 0;
				if (fp) {
					fake_have[pl] = fscanf(fp, "%d %d %d %d %d %d %d", &f[pl][0], &f[pl][1], &f[pl][2], &f[pl][3], &f[pl][4], &f[pl][5], &f[pl][6]) == 7;
					fclose(fp);
				}
			}
		}
		if (fake_have[pl]) {
			st->pad.lx = (SHORT)f[pl][0], st->pad.ly = (SHORT)f[pl][1], st->pad.rx = (SHORT)f[pl][2], st->pad.ry = (SHORT)f[pl][3];
			st->pad.buttons = (WORD)f[pl][4], st->pad.lt = (BYTE)f[pl][5], st->pad.rt = (BYTE)f[pl][6];
			have = 1;
		}
		// QA: the dvar mw2coop_qa_pad1 / mw2coop_qa_pad2, same format, stands in for the pad too: a level script presses at the moment it wants. An eighth
		// value 1 turns the aim assist off meanwhile (a script cannot write the option's dvar: the engine refuses script writes to code dvars)
		char name[24], v[96];
		int g[8] = {0};
		snprintf(name, sizeof(name), "mw2coop_qa_pad%d", pl + 1);
		qa_noassist[pl] = 0;
		if (game_dvar_raw(name, v, sizeof(v)) && sscanf(v, "%d %d %d %d %d %d %d %d", &g[0], &g[1], &g[2], &g[3], &g[4], &g[5], &g[6], &g[7]) >= 7) {
			st->pad.lx = (SHORT)g[0], st->pad.ly = (SHORT)g[1], st->pad.rx = (SHORT)g[2], st->pad.ry = (SHORT)g[3];
			st->pad.buttons = (WORD)g[4], st->pad.lt = (BYTE)g[5], st->pad.rt = (BYTE)g[6];
			qa_noassist[pl] = g[7] != 0;
			have = 1;
		}
	}
	return have;
}

// a menu has the keys (key catchers of local client 0, rva 0x533cb0, bit 0x10): the pad of either player moves through it (a held direction repeats after 400 ms, then every 120 ms)
static void pad_menu(int pl, const xin_state_t *st)
{
	pad_player_t *p = &pads[pl];
	WORD b = st->pad.buttons;
	float lx = st->pad.lx / 32767.0f, ly = st->pad.ly / 32767.0f;
	if (ly > 0.6f)
		b |= 0x0001;
	if (ly < -0.6f)
		b |= 0x0002;
	if (lx < -0.6f)
		b |= 0x0004;
	if (lx > 0.6f)
		b |= 0x0008;
	static const struct {
		WORD mask;
		int key;
	} keys[] = {{0x0001, 154}, {0x0002, 155}, {0x0004, 156}, {0x0008, 157}, {0x1000, 13}, {0x2000, 27}, {0x0010, 27}};
	DWORD now = GetTickCount();
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		int was = (p->menu_held & keys[i].mask) != 0, is = (b & keys[i].mask) != 0;
		int send = is && !was;
		if (is && was && i < 4 && (int)(now - p->menu_repeat) >= 0)
			send = 1;
		if (!send)
			continue;
		pad_key(keys[i].key, 1);
		pad_key(keys[i].key, 0);
		if (i < 4)
			p->menu_repeat = now + (was ? 120 : 400);
	}
	p->menu_held = b;
}

// The client's DObj of a body (the table of rva 0x1fece0) is built from the server's (rva 0x1fee30: the one the scripts' Attach / DetachAll change) when the entity
// goes through a snapshot transition (rva 0xb8000, through 0xb86f0). A player's own entity is in no snapshot of his own, so after the scripts re-attach a changed weapon
// the body of player 1 that context 1 draws kept the weapon it first had (QA scenario armed). The transition's own update, run by hand for the two players' entities
// whenever their client DObj and the server's do not carry the same models: 0xb86f0(server entity info 0x234520(n), 0x2968e0(server DObj)) ([native] bodysync=0 off).
static const uint8_t *dobj_model_names(const uint8_t *dobj, const char *out[8], int *count)
{
	*count = dobj ? dobj[DOBJ_NUMMODELS] : 0;
	const uint8_t *const *models = dobj ? *(const uint8_t *const **)(dobj + DOBJ_MODELS) : NULL;
	for (int i = 0; i < 8; i++)
		out[i] = NULL;
	for (int i = 0; models && i < *count && i < 8; i++)
		out[i] = models[i] && !IsBadReadPtr(models[i], 8) ? *(const char *const *)models[i] : NULL;
	return dobj;
}
#ifdef _WIN64
#define DOBJ_MAKE_DIRECT(info, sinfo) ((int)((int (*)(void *, void *))(base + 0xb86f0))(info, sinfo))
#else
static unsigned dobj_make_original(void *info, void *dobj_info); // (below) the 32-bit engine's make takes EDI and EBX
#define DOBJ_MAKE_DIRECT(info, sinfo) ((int)dobj_make_original(info, sinfo))
#endif
static void body_dobj_sync(void)
{
	static DWORD at;
	if (!native_active || ctx_current != 0 || !CFG_ONCE("bodysync", 1) || GetTickCount() - at < 200)
		return;
	at = GetTickCount();
	const uint8_t *(*client_dobj)(int) = (const uint8_t *(*)(int))(base + RVA_CLIENT_DOBJ);
	const uint8_t *(*server_dobj)(int) = (const uint8_t *(*)(int))(base + RVA_SERVER_DOBJ);
	for (int n = 0; n < 2; n++) {
		const uint8_t *cd = client_dobj(n), *sd = server_dobj(n);
		if (!cd || !sd)
			continue;
		const char *cn[8], *sn[8];
		int cc, sc;
		dobj_model_names(cd, cn, &cc);
		dobj_model_names(sd, sn, &sc);
		int same = cc == sc;
		for (int i = 0; same && i < cc && i < 8; i++)
			same = cn[i] && sn[i] && !strcmp(cn[i], sn[i]);
		if (same)
			continue;
		void *info = ((void *(*)(int))(base + RVA_ENT_INFO))(n);
		void *sinfo = ((void *(*)(const uint8_t *))(base + RVA_SDOBJ_INFO))(sd);
		int r = DOBJ_MAKE_DIRECT(info, sinfo);
		if (r && CFG_ONCE("bodysyncfree", 1)) { // what the transition does with a 1: the client DObj is dropped and the entity marked for the cleanup that follows
			((void (*)(unsigned))(base + RVA_DOBJ_CLRBIT))((unsigned)n);
			((uint32_t *)(base + RVA_ENT_FLAGS))[n >> 5] |= 0x80000000u >> (n & 31);
		}
		if (CFG_ONCE("bodysynclog", 0))
			log_printf("native: body DObj sync entity %d: client %d models (last %s), server %d models (last %s) -> %d", n, cc, cc && cn[cc - 1] ? cn[cc - 1] : "-", sc,
				   sc && sn[sc - 1] ? sn[sc - 1] : "-", r);
	}
}

// The snapshot transition (rva 0xb8000) queues a transfer of the server DObj's animation state into the client DObj of each entity (rva 0x29a120(server DObj info, client
// DObj, time), a queue of 0x28 bytes records processed later in the frame). Both contexts process every snapshot and the client DObjs are shared, so every entity's animation
// was queued once per context: [native] xfercount=1 logs the calls per second and context, [native] xferskip=1 drops context 1's queueing for every entity but player 1
// (his client DObj only goes through context 1's snapshots).
static void (*xfer_original)(void *, void *, unsigned);
static const uint8_t xfer_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30};
static void xfer_hook(void *src, void *dest, unsigned t)
{
	static LONG calls[2], skipped;
	static double sum[2];
	static DWORD at;
	if ((ctx_current == 0 || ctx_current == 1) && native_active) {
		InterlockedIncrement(&calls[ctx_current]);
		float tf;
		memcpy(&tf, &t, sizeof(tf));
		sum[ctx_current] += tf;
		if (CFG_ONCE("xfercount", 0) && GetTickCount() - at >= 1000) {
			at = GetTickCount();
			log_printf("native: DObj transfers in the last second: ctx0 %ld (time arg total %.3f) ctx1 %ld (%.3f) (skipped %ld)", calls[0], sum[0], calls[1], sum[1], skipped);
			calls[0] = calls[1] = 0;
			sum[0] = sum[1] = 0;
		}
		if (ctx_current == 1 && CFG_ONCE("xferskip", 0) && dest != (void *)((const uint8_t *(*)(int))(base + 0x1fece0))(0)) {
			skipped++;
			return;
		}
	}
	xfer_original(src, dest, t);
}

// The feet of an actor are adjusted to the ground by rva 0x89780 (state per entity in the cgame block at rva 0x440770, 0x48 bytes each, so each context has its own
// smoothed state) applied to the shared client DObj: both contexts adjust the same feet each frame with their own state. [native] ikskip=1: context 1 leaves them alone.
static void (*ik_original)(void *, void *);
static void ik_hook(void *ent, void *dobj)
{
	if (ctx_current == 1 && native_active && CFG_ONCE("ikskip", 0))
		return;
	ik_original(ent, dobj);
}

// The snapshot transition (rva 0xb8000, 0xb8300) makes an entity's client DObj with rva 0xb86f0(server entity info, server DObj info); for an actor it takes one of 112 pre-made
// DObjs from a pool (rva 0x12c6d0: in-use flags at rva 0xf50ec0, the DObjs at 0xf50b40) and gives none when all are in use: such an actor is not drawn at all (Price on Contingency,
// a different one in each run). Both contexts process every snapshot and the client DObjs are shared, so every actor made one twice, and the one the table lost stayed in use
// for ever. [native] dobjonce=1 (default): context 1 makes none for an entity but player 1 (his own entity is in no snapshot of context 0).
#ifdef _WIN64
static unsigned (*dobj_make_original)(void *, void *);
#else
static void *dobj_make_target;
static unsigned dobj_make_original(void *info, void *dobj_info)
{
	unsigned r;
	__asm__ volatile("call *%1" : "=a"(r) : "r"(dobj_make_target), "D"(info), "b"(dobj_info) : "ecx", "edx", "memory", "cc");
	return r;
}
#endif
static const uint8_t dobj_make_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xd2};
static unsigned dobj_make_hook(void *info, void *dobj_info)
{
	if (native_active && ctx_current == 1 && info && CFG_ONCE("dobjonce", 1) && *(const uint16_t *)((const uint8_t *)info + 0x7e) != 0)
		return 0;
	int ent = info ? *(const uint16_t *)((const uint8_t *)info + 0x7e) : -1;
	const uint8_t *(*client_dobj)(int) = (const uint8_t *(*)(int))(base + RVA_CLIENT_DOBJ);
	const uint8_t *before = ent >= 0 && ent < 2048 ? client_dobj(ent) : NULL;
	unsigned r = dobj_make_original(info, dobj_info);
	if (CFG_ONCE("dobjmakelog", 0) && ent >= 64) {
		static int logged;
		const uint8_t *after = client_dobj(ent);
		if (logged < 400 || ent == 1399) {
			logged++;
			log_printf("native: client DObj make: entity %d ctx %d type %d -> %u, client DObj %p -> %p, server DObj info %p", ent, ctx_current, *(const uint8_t *)info, r, (const void *)before,
				   (const void *)after, dobj_info);
		}
	}
	return r;
}

// ---- aim assist for a pad player, the console game's: this PC build left its code out but keeps its data. The cgame fills a list of the targets on the screen
// each frame (aaGlob[0], rva 0x3cf180, by rva 0x6a570 from the view at 0xbd2c5; one per context: it is in the swapped ranges), and the mouse move (0xcfcf0) computes
// the regions of the aim_* dvars into it (0x6a350, before its auto melee 0x685c0). On that list, like the console: the turn slows down over a target (aim_slowdown_*),
// the view stays on a target while the player moves (aim_lockon_*), and aiming down the sight turns onto the nearest target in the auto aim region (aim_autoaim_*).
// The weapon's ranges: BG_GetWeaponDef (rva 0x85850) +0x42c aim assist range, +0x430 the same aiming down the sight, +0x428 auto aim range.
#define AA_GLOB RVA_AIM_GLOB
typedef struct {
	int ent;
	float mins[2], maxs[2]; // its box on the screen, -1..1 around the cross hair
	float pos[3];           // where to aim (a tag of it)
	float vel[3];
	float dist2;  // squared distance from the eye
	float xhair2; // squared distance from the cross hair on the screen
} aa_target_t;        // 0x34 bytes, from +0x13c, the count at +0xe3c
typedef struct {
	int lock_ent, snap_ent, ads_was;
	float snap_t;
	float lock_yaw, lock_pitch; // the direction to the lock-on target at the last frame
} aa_state_t;
static aa_state_t aa_state[2] = {{-1, -1, 0, 0, 0, 0}, {-1, -1, 0, 0, 0, 0}};

#ifndef _WIN64
// the 38 aim_* dvar pointers (rva of the globals holding them, in the order of the 64-bit table 0x3cf050 + 8 * n; docs/32bit-native-map/aimrumble.md 1.1): 32-bit places them all over
const uint32_t aim_dvar_rvas32[38] = {0x33ba84, 0x33ce70, 0x33c934, 0x33ceb4, 0x33c920, 0x33ce74, 0x33c930, 0x33ba8c, 0x33ce88, 0x33ba88, 0x33ce6c, 0x33ceac, 0x33c93c,
				      0x33ce9c, 0x33ce60, 0x33ce68, 0x33ce94, 0x33ceb0, 0x33ce8c, 0x33ce90, 0x33ce7c, 0x33ce98, 0x33c914, 0x33cea8, 0x33cea0, 0x33c92c,
				      0x33c910, 0x33c938, 0x33c928, 0x33ce78, 0x33c918, 0x33ce84, 0x33ba80, 0x33ce64, 0x33c924, 0x33ce80, 0x33c91c, 0x33cea4};
#endif
static float aa_dvar(uint32_t rva) // the aim_* dvar pointers: rva 0x3cf050 .. 0x3cf178 (registered by 0x69a60); 32 bits: the same index in aim_dvar_rvas32
{
#ifndef _WIN64
	rva = XR(aim_dvar_rvas32[(rva - 0x3cf050) / 8]);
#endif
	const dvar_t *d = *(const dvar_t **)(base + rva);
	return !d ? 0.0f : d->type == DVAR_TYPE_BOOL ? (float)d->current.enabled : d->current.value;
}

static void aa_angles(const float eye[3], const float pos[3], float *pitch, float *yaw)
{
	float d[3] = {pos[0] - eye[0], pos[1] - eye[1], pos[2] - eye[2]};
	*yaw = atan2f(d[1], d[0]) * 57.2957795f;
	*pitch = -atan2f(d[2], sqrtf(d[0] * d[0] + d[1] * d[1])) * 57.2957795f; // down is positive
}

static float aa_delta(float to, float from)
{
	return fmodf(to - from + 540.0f, 360.0f) - 180.0f;
}

// the target nearest the cross hair whose screen box meets the region (half width / height around the cross hair, the screen being -1..1) within range
static const aa_target_t *aa_pick(const uint8_t *aa, float hw, float hh, float range)
{
	int n = *(const int *)(aa + 0xe3c);
	const aa_target_t *t = (const aa_target_t *)(aa + 0x13c), *best = NULL;
	for (int i = 0; i < n && i < 64; i++) {
		if (t[i].dist2 > range * range || hw < t[i].mins[0] || t[i].maxs[0] < -hw || hh < t[i].mins[1] || t[i].maxs[1] < -hh)
			continue;
		if (!best || t[i].xhair2 < best->xhair2)
			best = &t[i];
	}
	return best;
}

static const aa_target_t *aa_find(const uint8_t *aa, int ent)
{
	int n = *(const int *)(aa + 0xe3c);
	const aa_target_t *t = (const aa_target_t *)(aa + 0x13c);
	for (int i = 0; i < n && i < 64; i++)
		if (t[i].ent == ent)
			return &t[i];
	return NULL;
}

// yaw, pitch: this frame's turn from the stick, in the accumulators' senses (yaw left, pitch down positive); move, look: the sticks' deflections (0..1); ads: aiming
static void aim_assist(int pl, float dt, float *yaw, float *pitch, float move, float look, int ads)
{
	aa_state_t *s = &aa_state[pl];
	const uint8_t *aa = base + AA_GLOB;
	int ads_was = s->ads_was;
	s->ads_was = ads;
	int weapon = *(const int *)(aa + 0x24);
	const uint8_t *wd = aa[0xb0] && weapon > 0 ? WEAPON_DEF(weapon) : NULL;
	if (!wd) {
		s->lock_ent = s->snap_ent = -1;
		return;
	}
	const float ads_lerp = *(const float *)(aa + 0x128);
	const float range = (*(const float *)(wd + WD_AIM_RANGE) * (1.0f - ads_lerp) + *(const float *)(wd + WD_AIM_RANGE_ADS) * ads_lerp) * aa_dvar(0x3cf050);
	float auto_range = *(const float *)(wd + WD_AUTO_RANGE) * aa_dvar(0x3cf058);
	if (auto_range <= 0) // this PC build's weapons have no auto aim range: their aim assist range then
		auto_range = range;
	const float *eye = (const float *)(aa + 0xd8), *view = (const float *)(aa + 0xf0);
	const float *region = (const float *)(aa + 0xb8); // half width and height: slowdown, auto aim, auto melee, lock-on
	const float stick_yaw = *yaw, stick_pitch = *pitch;
	const aa_target_t *slow = aa_dvar(0x3cf0b0) != 0 ? aa_pick(aa, region[0], region[1], range) : NULL;
	if (slow) { // the turn slows down over a target
		*yaw *= aa_dvar(0x3cf0e0) * (1.0f - ads_lerp) + aa_dvar(0x3cf0e8) * ads_lerp;
		*pitch *= aa_dvar(0x3cf0d0) * (1.0f - ads_lerp) + aa_dvar(0x3cf0d8) * ads_lerp;
	}
	// lock-on: while a stick is pushed, the view follows (aim_lockon_strength of) the angle by which the direction to the target turned since the last frame, as the
	// player or the target moved; a jump (a teleport) is not followed
	const float deflection = aa_dvar(0x3cf158);
	const aa_target_t *lock = aa_dvar(0x3cf148) != 0 && (move > deflection || look > deflection) ? aa_pick(aa, region[6], region[7], range) : NULL;
	float lock_yaw = 0, lock_pitch = 0;
	if (lock) {
		float p, y, k = aa_dvar(0x3cf160);
		aa_angles(eye, lock->pos, &p, &y);
		if (lock->ent == s->lock_ent) {
			lock_yaw = aa_delta(y, s->lock_yaw) * k, lock_pitch = aa_delta(p, s->lock_pitch) * k;
			if (fabsf(lock_yaw) > 5.0f || fabsf(lock_pitch) > 5.0f)
				lock_yaw = lock_pitch = 0;
			*yaw += lock_yaw, *pitch += lock_pitch;
		}
		s->lock_yaw = y, s->lock_pitch = p;
	}
	s->lock_ent = lock ? lock->ent : -1;
	// aiming down the sight turns onto the target nearest the cross hair in the auto aim region, at aim_autoaim_lerp degrees a second, for half a second at most
	if (ads && !ads_was && aa_dvar(0x3cf0f0) != 0) {
		const aa_target_t *t = aa_pick(aa, region[2], region[3], auto_range);
		s->snap_ent = t ? t->ent : -1;
		s->snap_t = 0;
	}
	float snap_yaw = 0, snap_pitch = 0;
	const aa_target_t *snap = ads && s->snap_ent >= 0 ? aa_find(aa, s->snap_ent) : NULL;
	if (snap && s->snap_t < 0.5f) {
		float tp, ty, step = aa_dvar(0x3cf100) * dt;
		aa_angles(eye, snap->pos, &tp, &ty);
		snap_yaw = aa_delta(ty, view[1] + *yaw), snap_pitch = aa_delta(tp, view[0] + *pitch);
		snap_yaw = snap_yaw > step ? step : snap_yaw < -step ? -step : snap_yaw;
		snap_pitch = snap_pitch > step ? step : snap_pitch < -step ? -step : snap_pitch;
		*yaw += snap_yaw, *pitch += snap_pitch;
		s->snap_t += dt;
	} else
		s->snap_ent = -1;
	if (CFG_ONCE("aalog", 0)) {
		static int tuned;
		if (!tuned++)
			log_printf("native: aim dvars: slowdown %g yaw %g/%g pitch %g/%g, lock-on %g strength %g deflection %g, auto aim %g lerp %g, range scales %g %g",
				   aa_dvar(0x3cf0b0), aa_dvar(0x3cf0e0), aa_dvar(0x3cf0e8), aa_dvar(0x3cf0d0), aa_dvar(0x3cf0d8), aa_dvar(0x3cf148), aa_dvar(0x3cf160),
				   aa_dvar(0x3cf158), aa_dvar(0x3cf0f0), aa_dvar(0x3cf100), aa_dvar(0x3cf050), aa_dvar(0x3cf058));
		static DWORD last[2];
		DWORD now = GetTickCount();
		if (now - last[pl] >= 250 || (snap && s->snap_t <= dt)) {
			last[pl] = now;
			int n = *(const int *)(aa + 0xe3c);
			const aa_target_t *closest = aa_pick(aa, 1.0f, 1.0f, 1e9f);
			log_printf("native: aim %d: weapon %d ranges %.0f/%.0f ads %.2f, %d targets (nearest %d at %.0f, xhair %.3f), regions %.3f %.3f / %.3f %.3f / %.3f %.3f, "
				   "stick %.2f %.2f -> slow %d, lock %d (%.3f %.3f), snap %d (%.2f %.2f)",
				   pl, weapon, range, auto_range, ads_lerp, n, closest ? closest->ent : -1, closest ? sqrtf(closest->dist2) : 0, closest ? sqrtf(closest->xhair2) : 0, region[0],
				   region[1], region[2], region[3], region[6], region[7], stick_yaw, stick_pitch, slow ? slow->ent : -1, lock ? lock->ent : -1, lock_yaw,
				   lock_pitch, snap ? snap->ent : -1, snap_yaw, snap_pitch);
		}
	}
}

// one player's pad, his context live; play=0 reads it for the menus only (his context is not drawn: one picture)
static void pad_frame_player(int pl, int play)
{
	pad_player_t *p = &pads[pl];
	if (pad_dev[pl] < 0)
		return;
	xin_state_t st;
	static LARGE_INTEGER freq;
	LARGE_INTEGER now;
	if (!freq.QuadPart)
		QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);
	float dt = p->last_count ? (float)(now.QuadPart - p->last_count) / (float)freq.QuadPart : 0.016f;
	p->last_count = now.QuadPart;
	if (dt > 0.1f)
		dt = 0.1f;
	if (!pad_read(pl, &st)) { // no pad: no input
		p->forward = p->right = 0;
		p->active = 0;
		return;
	}
	p->active = 1;
	if (*(unsigned *)(base + RVA_KEY_CATCHERS) & 0x10) { // a menu: player 1 works it; player 2 only pauses the game (Start in play), he does not work the menus
		if (pl == 0)
			pad_menu(pl, &st);
		p->forward = p->right = 0;
		rumble_frame(pl, pad_dev[pl], 0); // paused: still
		return;
	}
	p->menu_held = st.pad.buttons; // (a button that closed a menu does not act in the game too)
	if (!play)
		return;
	padopts_t o; // the player's options (Options > Controls > Gamepad: Alpha / Bravo)
	padopts_get(pl, &o);
	rumble_frame(pl, pad_dev[pl], o.rumble);
	const float move_dead = CFG_ONCE("padmovedead", 18) / 100.0f, look_dead = CFG_ONCE("paddead", 22) / 100.0f;
	const float curve = CFG_ONCE("padcurve", 160) / 100.0f, trig = CFG_ONCE("padtrigger", 30) * 255 / 100.0f;
	float lx = st.pad.lx / 32767.0f, ly = st.pad.ly / 32767.0f, rx = st.pad.rx / 32767.0f, ry = st.pad.ry / 32767.0f;
	if (o.southpaw) { // the left stick looks, the right one moves
		float x = lx, y = ly;
		lx = rx, ly = ry, rx = x, ry = y;
	}
	pad_stick(&lx, &ly, move_dead, 1.0f);
	pad_stick(&rx, &ry, look_dead, curve);
	p->forward = (int)(ly * 127.0f);
	p->right = (int)(lx * 127.0f);
	const int rt = st.pad.rt > trig, lt = st.pad.lt > trig;
	const float scale = (lt ? CFG_ONCE("padads", 42) / 100.0f : 1.0f) * o.sens / 3.0f; // sensitivity 3: the speeds of padyaw / padpitch
	float yaw = -rx * (float)CFG_ONCE("padyaw", 180) * scale * dt;                       // left is positive
	float pitch = ((CFG_ONCE("padinvert", 0) != 0) != o.invert ? 1.0f : -1.0f) * ry * (float)CFG_ONCE("padpitch", 130) * scale * dt; // down is positive
	if (o.assist && !qa_noassist[pl])
		aim_assist(pl, dt, &yaw, &pitch, sqrtf(lx * lx + ly * ly), sqrtf(rx * rx + ry * ry), lt);
	else
		aa_state[pl].lock_ent = aa_state[pl].snap_ent = -1, aa_state[pl].ads_was = lt;
	float *acc = (float *)(base + RVA_ACC_PITCH); // the live context's
	acc[1] += yaw;
	acc[0] += pitch;
	if (acc[0] > 85.0f)
		acc[0] = 85.0f;
	if (acc[0] < -85.0f)
		acc[0] = -85.0f;
	static const struct {
		WORD mask;
		const char *hold;  // a held button (+/-)
		const char *press; // an action on the press
	} map[] = {
	    {0x1000, "gostand", NULL},       // A
	    {0x2000, "stance", NULL},        // B (held: prone)
	    {0x4000, "usereload", NULL},     // X (held: use)
	    {0x8000, NULL, "weapnext"},      // Y
	    {0x0100, "smoke", NULL},         // left bumper
	    {0x0200, "frag", NULL},          // right bumper
	    {0x0040, "breath_sprint", NULL}, // left stick
	    {0x0080, "melee", NULL},         // right stick
	    {0x0020, "scores", NULL},        // Back: the objectives
	    {0x0010, NULL, "togglemenu"},    // Start: pause
	    {0x0001, "actionslot 1", NULL},  // d-pad up
	    {0x0002, "actionslot 2", NULL},  // d-pad down
	    {0x0004, "actionslot 3", NULL},  // d-pad left
	    {0x0008, "actionslot 4", NULL},  // d-pad right
	};
	WORD buttons = st.pad.buttons;
	if (o.tactical && ((buttons & 0x2000) != 0) != ((buttons & 0x0080) != 0)) // the tactical layout: B and the right stick click change places
		buttons ^= 0x2000 | 0x0080;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		int was = (p->held & map[i].mask) != 0, is = (buttons & map[i].mask) != 0;
		if (was == is)
			continue;
		if (map[i].hold)
			pad_binding(map[i].hold, is);
		else if (map[i].press && is)
			pad_action(map[i].press);
	}
	p->held = buttons;
	if (rt != p->rt)
		pad_binding("attack", rt);
	if (lt != p->lt)
		pad_binding("speed_throw", lt);
	p->rt = rt;
	p->lt = lt;
}

// Mode 9: the engine is told the screen is a half (width x height/2) while the client frame records its views and its 2D: its viewport, the 2D placement and the text are made for the
// half (nothing is squeezed afterwards), drawn into the top rows of the frame; the compositor copies those rows into the half of the picture. Video globals (rva): 0x4113724 height,
// 0x411372c and 0x4358e84 the window height again, RVA_VIDEO_ASPECT aspect, RVA_PIXEL_ASPECT pixel aspect (= height * aspect / width), the cgame's own aspect RVA_CG_ASPECT.
static float video_saved_f[3];
static int video_saved_i[3];
static int video_pushed;
// Mode 10: the engine's 2D screen placements (rva 0x5e0b80, three of 0x70 bytes: scaleVirtualToReal, scaleVirtualToFull, scaleRealToVirtual, viewport position and size, the viewable and the
// adjustable rectangles; the console's split screen sets the per-client one to the half it draws into) are given the half: a 1920 x height/2 viewport with the scale of that height, so the
// client lays its 2D out for a half and the text keeps its shape (the virtual canvas becomes wider, as on a 16:9 screen).
#ifdef _WIN64
#define PLACE_RVA 0x5e0b80
#define PLACE_SIZE 0x70
#define PLACE_PTR(n) ((float *)(base + PLACE_RVA + (n) * PLACE_SIZE))
#else
// 32 bits: the three placements are not a regular array (docs/32bit-native-map/render.md): VA 0x937500 (the view's), 0x937578, 0x9375e8
static const uint32_t place_rvas32[3] = {0x537500, 0x537578, 0x5375e8};
#define PLACE_PTR(n) ((float *)(base + XR(place_rvas32[n])))
#endif
static float place_saved[3][28];
static int place_pushed;
static void place_push(void)
{
	if (!comp_w || !comp_h)
		return;
	float w = (float)comp_w, h = (float)(comp_h / 2), s = h / 480.0f, vw = w / s;
	for (int n = 0; n < 3; n++) {
		float *p = PLACE_PTR(n);
		if (!place_pushed) {
			if (p[8] != w || p[9] != (float)comp_h) {
				log_printf("native: screen placement %d is not the one expected (viewport %g x %g)", n, p[8], p[9]);
				return;
			}
		}
	}
	for (int n = 0; n < 3; n++) {
		float *p = PLACE_PTR(n);
		if (!place_pushed)
			memcpy(place_saved[n], p, sizeof(place_saved[n]));
		int safe = place_saved[n][10] > 0; // the two with a safe-area margin keep it (7.5 %)
		float m = safe ? 0.075f : 0.0f;
		p[0] = p[1] = s;
		p[2] = w / 640.0f, p[3] = s;
		p[4] = p[5] = 1.0f / s;
		p[6] = p[7] = 0;
		p[8] = w, p[9] = h;
		p[10] = m * vw, p[11] = m * 480.0f;
		p[12] = vw - m * vw, p[13] = 480.0f - m * 480.0f;
		p[14] = m * w, p[15] = m * h;
		p[16] = w - m * w, p[17] = h - m * h;
		p[18] = 0, p[19] = 0, p[20] = vw, p[21] = 480.0f;
		p[22] = 0, p[23] = 0, p[24] = w, p[25] = h;
	}
	static int logged; // (once: hudelem_hook pops and pushes them around each title line, every frame)
	if (!place_pushed && !logged++)
		log_printf("native: the 2D placements are made for a %gx%g half (scale %g)", w, h, s);
	place_pushed = 1;
}
// The HUD laid out as in the console's split screen. The Xbox 360 game draws a half's HUD with split screen versions of its HUD menus (ui/hud_splitscreen.txt:
// compassv2_ss, weaponinfo_ss, ...): the ammo, the grenades and the compass sit further in from the corner, the hints nearer the cross hair, the low ammo warning
// bigger, right of the cross hair. The PC game ships that menu list empty and its code never reads the split screen dvars, so while the picture is split the
// items of the PC's own HUD menus (the cgame's menu list, which both contexts draw) are moved the same way, and put back when it is one picture again ([native]
// hudss=0 off). The HUD is drawn from an item's rectClient (added to its menu's place), so both rects are moved. A menu is left as it is when its first item is not
// where the full screen HUD has it (another build of the menus). The PC's d-pad (the action slot keys, bottom centre) has no console counterpart and stays.
#ifdef _WIN64
#define CG_DC_RVA 0x51e330   // the cgame's UI context of local client 0 (ui/hud.txt is added to it at rva 0xa6b7f), menus[] at +0x38, their count at +0x1438
#define ITEM_TEXT_SCALE 0xe0 // itemDef_t: type, dataType, alignment, font, textAlignMode, textalignx, textaligny, textscale
#else
#define CG_DC_RVA 0x354d28 // VA 0x754d28 (+ 0x1684 per local client; the list is added at VA 0x5658a4)
#define ITEM_TEXT_SCALE 0xd4
#endif
#define ITEM_RECT_W 0x10
#define ITEM_RECT_CLIENT (ITEM_RECT + 0x14) // rectClient: x, y, w, h, horizontal and vertical alignment (+0x10, +0x11)
#ifdef _WIN64
#define ITEM_OWNER_DRAW 0x40 // windowDef_t: name, rect, rectClient, group, style, border, ownerDraw, ownerDrawFlags, borderSize, staticFlags, dynamicFlags (+0x50)
#else
#define ITEM_OWNER_DRAW 0x38
#endif
typedef struct {
	const char *menu;
	int item;	  // -1: every item of the menu without a line of its own
	float x0, y0;	  // where the full screen HUD has it (the first item when item is -1): the check that these are the menus the moves are for
	float dx, dy, w;  // the move; w > 0: the new width
	int align_v;	  // >= 0: the new vertical alignment
	float scale;	  // > 0: the new text scale
} hud_move_t;
static const hud_move_t hud_moves[] = {
    {"weaponinfo", 0, -347.333f, -74.6667f, -19.333f, 1.333f, 0, -1, 0}, // the bar under the weapon's name moves less than the rest
    {"weaponinfo", -1, -347.333f, -74.6667f, -50, 2, 0, -1, 0},		  // ammo, clip pictures, grenades
    {"compassv2", -1, -86, -74.6667f, -20, 0, 0, -1, 0},
    {"stance", -1, -84, -76, -20, 0, 0, -1, 0}, // (the PC draws the stance inside the compass, not above the ammo as the console: it moves with the compass)
    {"reload", 0, -50, -20, 110, -12, 200, -1, 0.5833f}, // "Low ammo" / "Reload": right of the cross hair, bigger
    {"cursorhints", 0, 0, 70, 0, -42, 0, -1, 0},		      // "Press [Use] to ...": nearer the cross hair
    {"mantlehint", 0, 0, 126, 0, -46, 0, -1, 0},
    // (the console puts the scope's "hold breath" hint and the invalid command hint at the top of the half, with an alignment the PC places at the top left:
    // they stay where the PC has them, above the cross hair, which fits a half)
};
#define HUD_MOVES (sizeof(hud_moves) / sizeof(hud_moves[0]))
typedef struct {
	uint8_t *item;
	float was[3], now[3];	// rect: x, y, w
	float cwas[3], cnow[3]; // rectClient (the one the HUD is drawn from: the menu's place plus this): x, y, w
	uint8_t was_av, now_av;
	float was_scale, now_scale;
} hud_slot_t;
static hud_slot_t hud_slots[64];
static int hud_nslots, hud_applied;
static uint8_t *hud_menu(const char *name, int *count)
{
#ifdef _WIN64
	uint32_t r = CG_DC_RVA;
#else
	uint32_t r = XR(0x354d28); // CG_DC_RVA, written out for tools/gen_addr159.py (iw4x-sp: 0 when the 159 build has no match)
#endif
	if (!r)
		return NULL;
	uint8_t *dc = base + r;
#ifdef _WIN64
	uint8_t **menus = (uint8_t **)(dc + 0x38);
	int n = *(int *)(dc + 0x1438);
#else
	uint8_t **menus = (uint8_t **)(dc + 0x38);
	int n = *(int *)(dc + 0xa38);
#endif
	if (n <= 0 || n > 640)
		return NULL;
	for (int i = 0; i < n; i++) {
		const char *mn = menus[i] ? *(const char **)menus[i] : NULL;
		if (mn && !strcmp(mn, name)) {
			*count = *(int *)(menus[i] + MENU_ITEM_COUNT);
			return menus[i];
		}
	}
	return NULL;
}
static int near_f(float a, float b)
{
	return fabsf(a - b) < 0.05f;
}
static void hud_log_menus(void) // QA ([native] hudlog=1): every menu of the cgame's list with its items' rects, alignments and text scales
{
#ifdef _WIN64
	uint32_t r = CG_DC_RVA;
	int off_count = 0x1438;
#else
	uint32_t r = XR(0x354d28);
	int off_count = 0xa38;
#endif
	if (!r)
		return;
	uint8_t *dc = base + r;
	uint8_t **menus = (uint8_t **)(dc + 0x38);
	int n = *(int *)(dc + off_count);
	log_printf("native: hudlog: %d menus in the cgame's list", n);
	for (int m = 0; m < n && m < 640; m++) {
		if (!menus[m])
			continue;
		int ic = *(int *)(menus[m] + MENU_ITEM_COUNT);
		float *mr = (float *)(menus[m] + ITEM_RECT);
		log_printf("native: hudlog: %s rect %g %g %g %g align %d %d, %d items", *(const char **)menus[m], mr[0], mr[1], mr[2], mr[3], menus[m][ITEM_RECT_ALIGN_H],
			   menus[m][ITEM_RECT_ALIGN_V], ic);
		uint8_t **items = *(uint8_t ***)(menus[m] + MENU_ITEMS);
		for (int i = 0; i < ic && i < 40; i++) {
			if (!items[i])
				continue;
			float *rc = (float *)(items[i] + ITEM_RECT), *cl = (float *)(items[i] + ITEM_RECT_CLIENT);
			const char *nm = *(const char **)items[i];
			log_printf("native: hudlog:   [%d] %s rect %g %g %g %g align %d %d client %g %g type %d ownerdraw %d scale %g", i, nm ? nm : "-", rc[0], rc[1], rc[2], rc[3],
				   items[i][ITEM_RECT_ALIGN_H], items[i][ITEM_RECT_ALIGN_V], cl[0], cl[1], *(int *)(items[i] + ITEM_TYPE), *(int *)(items[i] + ITEM_OWNER_DRAW),
				   *(float *)(items[i] + ITEM_TEXT_SCALE));
		}
	}
}
static void hud_apply(void)
{
	static int logged_menus;
	if (CFG_ONCE("hudlog", 0) && !logged_menus++)
		hud_log_menus();
	hud_nslots = 0;
	int moved_menus = 0;
	for (size_t m = 0; m < HUD_MOVES; m++) {
		if (m && !strcmp(hud_moves[m].menu, hud_moves[m - 1].menu))
			continue; // each menu once, with all its lines
		int count = 0;
		uint8_t *menu = hud_menu(hud_moves[m].menu, &count);
		if (!menu || count <= 0 || count > 32) {
			log_printf("native: hudss: no HUD menu %s", hud_moves[m].menu);
			continue;
		}
		uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
		const hud_move_t *all = NULL;
		int ok = 1;
		for (size_t k = m; k < HUD_MOVES && !strcmp(hud_moves[k].menu, hud_moves[m].menu); k++) {
			const hud_move_t *mv = &hud_moves[k];
			int i = mv->item < 0 ? 0 : mv->item;
			if (mv->item < 0)
				all = mv;
			float *rc = i < count && items[i] ? (float *)(items[i] + ITEM_RECT) : NULL;
			if (!rc || !near_f(rc[0], mv->x0) || !near_f(rc[1], mv->y0)) {
				log_printf("native: hudss: %s[%d] is at %g %g, not %g %g: the menu is left as it is", mv->menu, i, rc ? rc[0] : 0, rc ? rc[1] : 0, mv->x0, mv->y0);
				ok = 0;
			}
		}
		if (!ok)
			continue;
		for (int i = 0; i < count && hud_nslots < (int)(sizeof(hud_slots) / sizeof(hud_slots[0])); i++) {
			const hud_move_t *mv = all;
			for (size_t k = m; k < HUD_MOVES && !strcmp(hud_moves[k].menu, hud_moves[m].menu); k++)
				if (hud_moves[k].item == i)
					mv = &hud_moves[k];
			if (!mv || !items[i])
				continue;
			hud_slot_t *s = &hud_slots[hud_nslots++];
			float *rc = (float *)(items[i] + ITEM_RECT);
			s->item = items[i];
			s->was[0] = rc[0], s->was[1] = rc[1], s->was[2] = rc[2];
			s->was_av = items[i][ITEM_RECT_ALIGN_V];
			s->was_scale = *(float *)(items[i] + ITEM_TEXT_SCALE);
			s->now[0] = rc[0] + mv->dx, s->now[1] = rc[1] + mv->dy, s->now[2] = mv->w > 0 ? mv->w : rc[2];
			float *cl = (float *)(items[i] + ITEM_RECT_CLIENT);
			s->cwas[0] = cl[0], s->cwas[1] = cl[1], s->cwas[2] = cl[2];
			s->cnow[0] = cl[0] + mv->dx, s->cnow[1] = cl[1] + mv->dy, s->cnow[2] = mv->w > 0 ? mv->w : cl[2];
			s->now_av = mv->align_v >= 0 ? (uint8_t)mv->align_v : s->was_av;
			s->now_scale = mv->scale > 0 ? mv->scale : s->was_scale;
		}
		moved_menus++;
	}
	hud_applied = 1;
	static int logged;
	if (!logged++)
		log_printf("native: hudss: the HUD is laid out as in the console's split screen (%d items of %d menus)", hud_nslots, moved_menus);
}
// every frame while split: the layout is (re)written where an item still has the full screen values (the engine lays a menu out again when it opens it)
static void hud_write(void)
{
	for (int i = 0; i < hud_nslots; i++) {
		hud_slot_t *s = &hud_slots[i];
		float *rc = (float *)(s->item + ITEM_RECT), *cl = (float *)(s->item + ITEM_RECT_CLIENT);
		if (near_f(cl[0], s->cwas[0]) && near_f(cl[1], s->cwas[1])) {
			cl[0] = s->cnow[0], cl[1] = s->cnow[1], cl[2] = s->cnow[2];
			s->item[ITEM_RECT_CLIENT + 0x11] = s->now_av;
			*(float *)(s->item + ITEM_TEXT_SCALE) = s->now_scale;
		}
		if (near_f(rc[0], s->was[0]) && near_f(rc[1], s->was[1])) {
			rc[0] = s->now[0], rc[1] = s->now[1], rc[2] = s->now[2];
			s->item[ITEM_RECT_ALIGN_V] = s->now_av;
		}
	}
}
static void hud_restore(void)
{
	for (int i = 0; i < hud_nslots; i++) {
		hud_slot_t *s = &hud_slots[i];
		float *rc = (float *)(s->item + ITEM_RECT), *cl = (float *)(s->item + ITEM_RECT_CLIENT);
		if (near_f(cl[0], s->cnow[0]) && near_f(cl[1], s->cnow[1])) {
			cl[0] = s->cwas[0], cl[1] = s->cwas[1], cl[2] = s->cwas[2];
			s->item[ITEM_RECT_CLIENT + 0x11] = s->was_av;
			*(float *)(s->item + ITEM_TEXT_SCALE) = s->was_scale;
		}
		if (near_f(rc[0], s->now[0]) && near_f(rc[1], s->now[1])) {
			rc[0] = s->was[0], rc[1] = s->was[1], rc[2] = s->was[2];
			s->item[ITEM_RECT_ALIGN_V] = s->was_av;
		}
	}
	hud_applied = 0;
}
static void hud_layout_tick(int split)
{
	if (split && mode == 10 && CFG_ONCE("hudss", 1)) {
		if (!hud_applied)
			hud_apply();
		hud_write();
	} else if (hud_applied)
		hud_restore();
}

static void place_pop(void)
{
	if (!place_pushed)
		return;
	for (int n = 0; n < 3; n++)
		memcpy(PLACE_PTR(n), place_saved[n], sizeof(place_saved[n]));
	place_pushed = 0;
}

static void video_push(void)
{
	if (mode == 10) {
		place_push();
		return;
	}
	if (mode != 9 || !comp_w || !comp_h)
		return;
	int *h1 = (int *)(base + 0x4113724), *h2 = (int *)(base + 0x411372c), *h3 = (int *)(base + 0x4358e84);
	float *a = (float *)(base + RVA_VIDEO_ASPECT), *pa = (float *)(base + RVA_PIXEL_ASPECT), *ca = (float *)(base + RVA_CG_ASPECT);
	int half = (int)(comp_h / 2);
	if (!video_pushed) { // the first time: what the engine had, to give back when the picture is not split any more
		video_saved_i[0] = *h1, video_saved_i[1] = *h2, video_saved_i[2] = *h3;
		video_saved_f[0] = *a, video_saved_f[1] = *pa, video_saved_f[2] = *ca;
		video_pushed = 1;
		log_printf("native: the engine's screen is %ux%d while the picture is split (was %d)", comp_w, half, video_saved_i[0]);
	}
	// kept as long as the picture is split, every frame (the render thread reads them later than the client frame ends, and a video restart writes them again)
	*h1 = *h2 = *h3 = half;
	*a = (float)comp_w / (float)half;
	*pa = 1.0f;
	*ca = *a;
	if (ctx_current == 0 && ctx_store[1][0]) // context 1's cgame block holds its own copy of the cgame's aspect
		*(float *)(ctx_store[1][0] + (RVA_CG_ASPECT - RANGE_RVA(0))) = *a;
}
static void video_pop(void)
{
	place_pop();
	if (!video_pushed)
		return;
	*(int *)(base + 0x4113724) = video_saved_i[0];
	*(int *)(base + 0x411372c) = video_saved_i[1];
	*(int *)(base + 0x4358e84) = video_saved_i[2];
	*(float *)(base + RVA_VIDEO_ASPECT) = video_saved_f[0];
	*(float *)(base + RVA_PIXEL_ASPECT) = video_saved_f[1];
	*(float *)(base + RVA_CG_ASPECT) = video_saved_f[2];
	if (ctx_store[1][0])
		*(float *)(ctx_store[1][0] + (RVA_CG_ASPECT - RANGE_RVA(0))) = video_saved_f[2];
	video_pushed = 0;
}

// Mode 5: the engine's whole client frame (rva 0xd5b00, called once per engine frame from the main loop at rva 0x1fc331) runs for context 0 on even frames and for context 1 on odd ones, so
// everything the client does in a frame (time, snapshot use, usercmds, the cgame draw, the 2D) happens in the right context. One scene per frame, as the renderer needs.
static void (*client_frame_original)(void);
static unsigned make_cmd_calls[3]; // QA (padlog): calls of the usercmd maker per context
static void client_frame_hook(void)
{
	static unsigned frame;
	{
		static int seen;
		static DWORD last;
		DWORD now = GetTickCount();
		if (seen < 4 || now - last >= 2000) {
			log_printf("native: client frame #%d (active %d, ctx %d, ready %d, split %d, cg draws %u) cls %d | sv clients %d %d | hooks: snap %u ps %u step %u ucmd %u smsg %u scmd %u", seen, native_active, ctx_current, ctx1_ready, split_on, cg_draw_count, *(int *)(base + RVA_CLS_STATE), *(int *)(base + RVA_SV_CLIENTS), *(int *)(base + RVA_SV_CLIENTS + SV_CLIENT_STRIDE), hook_counts[0], hook_counts[1], hook_counts[2], hook_counts[3], hook_counts[4], hook_counts[5]);
			last = now;
		}
		seen++;
	}
	if (!native_active) {
		client_frame_original();
		return;
	}
	frame_bottom = 0;
	stall_tid = GetCurrentThreadId(), stall_frame_t = perf_now(); // (the QA stall watch)
	static LONGLONG perf_prev, perf_start;
	static int perf_on = -1;
	if (perf_on < 0)
		perf_on = cfg("perflog", 0);
	if (perf_on) {
		LONGLONG now = perf_now();
		if (perf_prev) {
			LONGLONG g = now - perf_prev;
			perf_gap_sum += g;
			perf_gap_n++;
			if (perf_freq.QuadPart)
				perf_gap_hist[perf_bucket((double)g * 1000.0 / (double)perf_freq.QuadPart)]++;
			if (g > perf_gap_max)
				perf_gap_max = g;
			static LONGLONG c0, c1, cp, wt, st; // the counters at the frame before
			if (perf_freq.QuadPart && g * 1000 > 50 * perf_freq.QuadPart) { // a hitch: what the last frame spent in the mod's parts (the rest is the engine's)
				double ms = 1000.0 / (double)perf_freq.QuadPart;
				perf_gap_long++;
				log_printf("native: perf hitch: frame %.1f ms; since the frame before: ctx0 %.1f ms, ctx1 %.1f ms, swap copies %.1f ms, worker waits %.1f ms, server steps %.1f ms",
					   (double)g * ms, (double)(perf_ctx[0] - c0) * ms, (double)(perf_ctx[1] - c1) * ms, (double)(perf_copy - cp) * ms, (double)(perf_wait - wt) * ms,
					   (double)(perf_step - st) * ms);
			}
			c0 = perf_ctx[0], c1 = perf_ctx[1], cp = perf_copy, wt = perf_wait, st = perf_step;
		}
		perf_prev = perf_start = now;
		perf_report();
	}
	{ // the scripts' switch: the dvar mw2coop_split_view is "1" once the intro is over
		uint8_t *d = (uint8_t *)game_find_dvar("mw2coop_split_view");
		const char *v = d ? *(const char **)(d + 0x10) : NULL;
		int on = v && v[0] == '1';
		if (*(unsigned *)(base + RVA_KEY_CATCHERS) & 0x11) // key catchers of client 0 (rva 0x533cb0, set by 0xd1c10): a menu (pause, objectives, end of game) or the console has the keys: one picture
			on = 0;
		if (on != split_on)
			log_printf("native: split view %s", on ? "on" : "off");
		if (on && !split_on)
			split_tick = GetTickCount();
		split_on = on;
		hud_layout_tick(on); // the console's split screen HUD layout while split, the full screen one otherwise
	}
	{ // QA: [native] qa_ads=<ms>: player 0 aims down the sights that long after the picture was split (and [native] qa_ads_off=<ms>: lets go), so that a frame series can be taken in the scope
		static int ads_at = -2, ads_off = -2, ads_state;
		if (ads_at == -2) {
			ads_at = cfg("qa_ads", -1);
			ads_off = cfg("qa_ads_off", -1);
		}
		if (split_on && ctx_current == 0 && ads_at >= 0) {
			DWORD since = GetTickCount() - split_tick;
			if (!ads_state && since >= (DWORD)ads_at) {
				pad_binding("speed_throw", 1);
				ads_state = 1;
				log_printf("native: qa: aiming down the sights");
			} else if (ads_state == 1 && ads_off >= 0 && since >= (DWORD)ads_off) {
				pad_binding("speed_throw", 0);
				ads_state = 2;
				log_printf("native: qa: sights released");
			}
		}
	}
	static int nopredict = -1;
	if (nopredict < 0)
		nopredict = cfg("nopredict", 0);
	uint8_t *np_dvar = *(uint8_t **)(base + RVA_NOPREDICT_DVAR); // cg_nopredict: context 1 shows the server's playerstate as it is (its own prediction fought the server)
	{ // [native] fov=<degrees>: the cg_fov (rva 0x51e1a8 holds the dvar; the angle across a 4:3 picture, 65 by default) while the picture is split: each half is a band of the full
	  // view (mode 6), so the field is widened to make up for the half height
		static int fov = -1;
		static float fov_saved = -1;
		if (fov < 0)
			fov = cfg("fov", (mode >= 8) ? 49 : 85); // 85 = the console split screen: cg_fovScale 0.75 (maps/_load.gsc) on a half of 16:9 is about 100 x 38 degrees; a band of half the height cut from a view with cg_fov 85 is the same
		uint8_t *d = *(uint8_t **)(base + RVA_FOV_DVAR);
		if (fov > 0 && d) {
			float *v = (float *)(d + 0x10);
			if (split_on) {
				if (fov_saved < 0)
					fov_saved = *v;
				*v = (float)fov;
			} else if (fov_saved >= 0) {
				*v = fov_saved;
				fov_saved = -1;
			}
		}
	}
	static int nogun = -1;
	if (nogun < 0)
		nogun = cfg("nogun", 0);
	uint8_t *gun_dvar = *(uint8_t **)(base + RVA_DRAWGUN_DVAR); // cg_drawGun
	if (mode >= 5 && ctx1_ready && split_on && (frame++ & 1)) {
		frame_bottom = 1;
		if (nogun && gun_dvar)
			*(int *)(gun_dvar + 0x10) = 0; // test: context 1 without the view model
		if (nopredict && np_dvar)
			*(int *)(np_dvar + 0x10) = 1;
		ctx_swap(1);
		{ // QA: [native] qa_ads1=<ms>: player 1 (the bottom half) aims down the sights that long after the split
			static int ads1_at = -2, ads1_state;
			if (ads1_at == -2)
				ads1_at = cfg("qa_ads1", -1);
			if (ads1_at >= 0 && !ads1_state && GetTickCount() - split_tick >= (DWORD)ads1_at) {
				pad_binding("speed_throw", 1);
				qa_ctx1_buttons = 1;
				ads1_state = 1;
				log_printf("native: qa: player 1 aims down the sights");
			}
		}
		pad_frame_player(1, 1);
		if (CFG_ONCE("padlog", 0)) {
			static unsigned frames1;
			static DWORD at1;
			frames1++;
			if (GetTickCount() - at1 >= 1000) {
				at1 = GetTickCount();
				log_printf("native: padlog ctx1 frames %u | make_cmd calls ctx0 %u ctx1 %u other %u | pad p2: dev %d active %d forward %d right %d", frames1, make_cmd_calls[0], make_cmd_calls[1], make_cmd_calls[2], pad_dev[1], pads[1].active,
					   pads[1].forward, pads[1].right);
			}
		}
		if (split_on)
			video_push();
		client_frame_original();
		native_unswap_entities();
		ctx_swap(0);
		if (perf_on) {
			perf_ctx[1] += perf_now() - perf_start;
			perf_ctx_n[1]++;
		}
		return;
	}
	if (nopredict && np_dvar)
		*(int *)(np_dvar + 0x10) = 0;
	if (nogun && gun_dvar)
		*(int *)(gun_dvar + 0x10) = 1;
	pad_frame_player(0, 1);
	{
		LONGLONG t0 = perf_now();
		body_dobj_sync();
		perf_sync += perf_now() - t0;
	}
	if (native_active && CFG_ONCE("clocklog", 0)) { // QA: the game's clock (QA scenario clock) against the wall clock: milliseconds of game time per wall second
		static DWORD wall0;
		static int game0, n;
		char v[16];
		DWORD wall = GetTickCount();
		if ((!wall0 || wall - wall0 >= 1000) && game_dvar_raw("mw2coop_qa_t", v, sizeof(v))) {
			int game = atoi(v);
			if (wall0 && n < 60)
				log_printf("native: clock: game time +%d ms in %lu ms of wall time", game - game0, (unsigned long)(wall - wall0)), n++;
			wall0 = wall, game0 = game;
		}
	}
	if (native_active) { // the menu tick does not run in a level: the pause menu's Options > Controls gets the gamepad pages from here (a level load may reload the menus)
		static DWORD opts_at;
		if (GetTickCount() - opts_at >= 1000) {
			opts_at = GetTickCount();
			padopts_tick();
		}
	}
	if (!(mode >= 5 && ctx1_ready && split_on))
		pad_frame_player(1, 0); // one picture: player 2's pad still works the menus
	if (split_on && ctx1_ready)
		video_push();
	else if (!split_on)
		video_pop();
	client_frame_original();
	if (perf_on) {
		perf_ctx[0] += perf_now() - perf_start;
		perf_ctx_n[0]++;
	}
}

// Diagnostics: the alpha of the co-op "waiting for the other player" text (rva 0x22eb70, called from the cgame's 2D at 0x94039).
static float (*wait_alpha_original)(int);
static float wait_alpha_hook(int lc)
{
	float a = wait_alpha_original(lc);
	static int logged;
	if ((a != 0.0f || !logged) && logged < 5) {
		logged++;
#ifdef _WIN64
		log_printf("native: waiting alpha for lc %d = %f (coop byte %d, cls %d)", lc, a, *(base + 0x52fb5c), *(int *)(base + 0x533cb8));
#else
		log_printf("native: waiting alpha for lc %d = %f", lc, (double)a);
#endif
	}
	// Both players are on this PC: nobody waits for the other (the message showed while level loading, until context 1's client state caught up, 2026-10-29: owner). [native] waitmsg=1 shows it again.
	if (native_active && !cfg("waitmsg", 0))
		return 0.0f;
	return a;
}

// Context 1 has no mouse (its player has no device yet): the engine's mouse and joystick reads (rva 0xcf4b0, called at 0xcfd23 and 0xd0061) return no movement for it, so the
// real cursor only turns player 0.
#ifdef _WIN64
static void (*mouse_read_original)(intptr_t, float *, float *);
#else
// 32 bits: the mouse read has the mouse state in ESI and two stack arguments (the caller cleans them): a stub below passes plain cdecl arguments to the hook,
// and the original is called through a wrapper that sets ESI (docs/32bit-native-map/clientctx.md)
static void *mouse_read_target, *mouse_read2_target;
static void mouse_read_original(intptr_t state, float *x, float *y);
static void mouse_read2_original(intptr_t state, float *x, float *y);
#endif
static void mouse_read_hook(intptr_t state, float *x, float *y)
{
	if (ctx_current != (native_active ? kb_player() : 0)) { // not the mouse's player (left unread: it stays for his context)
		*x = 0;
		*y = 0;
		return;
	}
	mouse_read_original(state, x, y);
}
#ifdef _WIN64
static void (*mouse_read2_original)(intptr_t, float *, float *);
#endif
static void mouse_read2_hook(intptr_t state, float *x, float *y)
{
	if (ctx_current != (native_active ? kb_player() : 0)) { // not the mouse's player (left unread: it stays for his context)
		*x = 0;
		*y = 0;
		return;
	}
	mouse_read2_original(state, x, y);
}

// The keyboard's player 2: the event loop's key events (rva 0xd0f80, called at 0x1fbc1d) run with context 1 live, so the keys press his kbuttons (in the swapped input block).
// Player 2 does not work the menus (owner, 2026-10-28: he only pauses the game): while a menu is open (key catchers 0x10) his key presses go nowhere; the console (0x1) takes them.
#ifdef _WIN64
static void (*key_event_original)(int, int, int, unsigned);
#endif
static void key_event_hook(int lc, int key, int down, unsigned time)
{
	unsigned catchers = *(unsigned *)(base + RVA_KEY_CATCHERS);
	if (native_active && kb_player() == 1 && (catchers & 0x10) && !(catchers & 0x1) && down)
		return;
	if (native_active && kb_player() == 1 && ctx1_ready && ctx_current == 0 && !(catchers & 0x11)) {
		ctx_swap(1);
		key_event_original(lc, key, down, time);
#ifndef _WIN64
		cbuf_run_now(); // the binding's text runs now, in context 1 (the command buffer would otherwise run it in whichever context is live later)
#endif
		ctx_swap(0);
		return;
	}
	key_event_original(lc, key, down, time);
}

// The keyboard and the pad reach context 1 too (the keys are read by the same input code): its usercmd, made by 0xd03a0 (called at 0xcf8b1), loses its buttons and movement,
// so that the player who has no device stands still and his prediction agrees with the server.
#ifdef _WIN64
static uint8_t *(*make_cmd_original)(uint8_t *, int);
#else
// 32 bits: the usercmd maker takes its output buffer in EAX and the local client in EBX and returns the buffer in EAX
static void *make_cmd_target;
static uint8_t *make_cmd_original(uint8_t *out, int lc);
#endif
static volatile int qa_c0_fwd, qa_c0_fire; // QA (console mw2coop_qa_move <forward 0|1> <fire 0|1>): player 0's movement and trigger set straight in his usercmds
static uint8_t *make_cmd_hook(uint8_t *out, int a)
{
	make_cmd_calls[ctx_current >= 0 && ctx_current < 2 ? ctx_current : 2]++;
	uint8_t *cmd = make_cmd_original(out, a);
	if (ctx_current == 0 && cmd && (qa_c0_fwd || qa_c0_fire)) {
		if (qa_c0_fwd)
			cmd[0x1a] = 127;
		if (qa_c0_fire)
			*(int *)(cmd + 4) |= 1;
	}
	if (CFG_ONCE("padlog", 0) && cmd && native_active) { // QA: once a second, per context, what the usercmd maker saw: the context, its local client argument, the pad state of the player
		static DWORD at[3];
		int k = ctx_current >= 0 && ctx_current < 3 ? ctx_current : 2;
		if (GetTickCount() - at[k] >= 1000) {
			at[k] = GetTickCount();
			const pad_player_t *q = &pads[ctx_current == 1 ? 1 : 0];
			log_printf("native: padlog make_cmd ctx %d lc %d | pad_dev %d active %d forward %d right %d kb_player %d qa_ctx1_buttons %d | cmd fwd %d right %d", ctx_current, a, pad_dev[ctx_current == 1 ? 1 : 0], q->active, q->forward, q->right, kb_player(),
				   (int)qa_ctx1_buttons, (int)(int8_t)cmd[0x1a], (int)(int8_t)cmd[0x1b]);
		}
	}
	if ((ctx_current == 0 || ctx_current == 1) && cmd && native_active) {
		int pl = ctx_current;
		pad_player_t *p = &pads[pl];
		if (pl == 1 && qa_ctx1_buttons) { // QA ([native] qa_ads1): the buttons the QA pressed for player 2 reach the server although he has no pad
			cmd[0x1a] = cmd[0x1b] = cmd[0x1c] = cmd[0x1d] = 0;
		} else if (pad_dev[pl] >= 0 && p->active && (p->forward || p->right || kb_player() != pl)) {
			cmd[0x1a] = (uint8_t)(int8_t)p->forward; // usercmd_s: forwardmove +0x1a, rightmove +0x1b, upmove +0x1c, downmove +0x1d
			cmd[0x1b] = (uint8_t)(int8_t)p->right;
		} else if (pl == 1 && kb_player() != 1 && !(pad_dev[1] >= 0 && p->active)) { // player 2 with no device: still
			*(int *)(cmd + 4) = 0;
			cmd[0x1a] = cmd[0x1b] = cmd[0x1c] = cmd[0x1d] = 0;
		}
	}
	return cmd;
}

// The simulation step and the hand-over of its results (rva 0x2343a0) run from inside the client frame (called at 0xb8d50): in context 1's frame they would write context 0's data
// into the live (context 1's) client block. They run with context 0 live; the hand-over hooks above put each client's data where its context keeps it.
static void (*server_step_original)(void);
static void aim_targets_update(void);
#ifndef _WIN64
static void *aim_for_client_fn; // set at install: VA 0x55b040, EAX = gentity, [esp+4] = client
#endif
static void server_step_hook(void)
{
	hook_counts[2]++;
	LONGLONG t0 = perf_now();
	if (ctx_current == 1) {
		ctx_swap(0);
		server_step_original();
		if (native_active) {
			LONGLONG t1 = perf_now();
			aim_targets_update();
			perf_aim += perf_now() - t1;
		}
		ctx_swap(1);
		perf_step += perf_now() - t0;
		return;
	}
	server_step_original();
	if (native_active) {
		LONGLONG t1 = perf_now();
		aim_targets_update();
		perf_aim += perf_now() - t1;
	}
	perf_step += perf_now() - t0;
}

// Player 1 (local client 1) has no input device yet: every simulation step he gets the usercmd of player 0 with the buttons and the movement cleared and the view turned by
// [native] p1yaw (a 16-bit angle, 0x8000 = half a turn), through the engine's own SV_ClientThink analogue (rva 0x22d000, client_t[n] + 0x1630 holds the last cmd).
static int p1_yaw;
static void usercmd_step_inner(int lc)
{
	if (lc == 0) {
		usercmd_step_original(0);
		return;
	}
	if (!CFG_ONCE("p1cmd", 0))
		return;
	{
		static unsigned seen;
		if (++seen % 10 == 0 && seen < 3000) {
			const uint8_t *c0 = base + RVA_SV_CLIENTS + 0x1630, *c1 = base + RVA_SV_CLIENTS + SV_CLIENT_STRIDE + 0x1630;
			log_printf("native: step %u ctx %d: client 0 cmd t %d yaw %d pitch %d | client 1 cmd t %d yaw %d pitch %d | ctx1 acc yaw %.1f pitch %.1f", seen, ctx_current, *(int *)c0, *(int *)(c0 + 12),
				   *(int *)(c0 + 8), *(int *)c1, *(int *)(c1 + 12), *(int *)(c1 + 8), ctx_store[1][4] ? *(float *)(ctx_store[1][4] + ACC_IN_CLS + 4) : -1.0f,
				   ctx_store[1][4] ? *(float *)(ctx_store[1][4] + ACC_IN_CLS) : -1.0f);
		}
	}
	if (ctx1_ready && ctx_current == 0 && ctx_store[1][5]) {
		// context 1's own client frame makes its usercmds in its cl block (ring of 128 x 0x40 at +0, cmdNumber at +0x2000, the number sent at +0x2004): player 1 gets them
		// through the engine's SV_ClientThink analogue, as player 0 gets his through rva 0xd02a0
		uint8_t *cl = ctx_store[1][5];
		int number = *(int *)(cl + CL_CMDNUM);
		int *sent = (int *)(cl + CL_SENT);
		if (number - *sent > 32)
			*sent = number - 32;
		for (int n = *sent + 1; n <= number; n++)
			((void (*)(int, void *))(base + RVA_SV_CLIENT_THINK))(1, cl + (n & CL_RING_MASK) * 0x40);
		*sent = number;
		return;
	}
	static int logged;
	uint8_t cmd[0x40];
	memcpy(cmd, base + RVA_SV_CLIENTS + 0x1630, sizeof(cmd)); // player 0's last cmd
	if (!*(int *)cmd) // not set yet
		return;
	static unsigned steps;
	if (++steps % 400 == 0 && steps < 6000) {
		char hex[0x40 * 3 + 1];
		for (int i = 0; i < 0x40; i++)
			snprintf(hex + i * 3, 4, "%02x ", cmd[i]);
		log_printf("native: player 0's cmd: %s", hex);
	}
	*(int *)(cmd + 4) = 0; // no buttons
	cmd[0x1a] = cmd[0x1b] = cmd[0x1c] = cmd[0x1d] = 0; // no movement
	if (!logged) {
		p1_yaw = cfg("p1yaw", 0);
		log_printf("native: player 1 cmd from player 0's: serverTime %d, angles %d %d %d, p1yaw %x", *(int *)cmd, *(int *)(cmd + 8), *(int *)(cmd + 12), *(int *)(cmd + 16), p1_yaw);
		logged = 1;
	}
	*(int *)(cmd + 8) = 0; // a fixed view (pitch 0, yaw p1yaw, roll 0): player 0's mouse must not turn player 1
	*(int *)(cmd + 12) = p1_yaw;
	*(int *)(cmd + 16) = 0;
	((void (*)(int, void *))(base + RVA_SV_CLIENT_THINK))(1, cmd);
}

// These server-side hand-overs can be reached while context 1 is the live one (they run from inside the client frame): they always run with context 0 live.
static void usercmd_step_hook(int lc)
{
	hook_counts[3]++;
	int was = ctx_current;
	if (was)
		ctx_swap(0);
	usercmd_step_inner(lc);
	if (was)
		ctx_swap(was);
}

// Diagnostics ([native] entdraw=1, skipent=<n>): the entities the cgame adds to the scene (rva 0x9c960), logged for one frame of context 1; skipent drops one of them.
static const uint8_t add_entity_bytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20, 0x57, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00};
static void (*add_entity_original)(int);
static int skip_entity = 1; // context 1 is player 1: his own entity is not drawn (the cgame hides "my" entity by the local client number, which is 0 in context 1)
static int log_entities;
static int swap_entities = 1;
static int entities_swapped;
void native_unswap_entities(void)
{
	if (!entities_swapped)
		return;
	uint8_t *e0 = base + RVA_CENT, *e1 = e0 + CENT_SIZE, t[CENT_SIZE];
	memcpy(t, e0, sizeof(t));
	memcpy(e0, e1, sizeof(t));
	memcpy(e1, t, sizeof(t));
	entities_swapped = 0;
}
// Lag fix ([native] lagfix=1, default 1): the other local player's body is drawn from the cgame's entity, which comes through the server's snapshot and its
// interpolation (measured ~125 ms behind the player's own view). Both players are on this PC, so his body is turned to his own view yaw of this very moment:
// the yaw accumulators (block +0x60) of both contexts are kept for a moment, the fixed offset between a context's accumulator and its entity's yaw is tracked by
// pairing the entity's (late) yaw with the accumulator of 125 ms before, and the entity's yaw fields (floats 10, 42 and 73: current state, next state, lerp) are
// replaced for the draw and put back after it.
// Context 1's own entity table holds player 2's data in both slots 0 and 1 (swaplog), so player 1's body was never drawn in the bottom half and player 2's own body
// was (from inside, late behind the camera). Context 0's entity 0 is player 1's valid data: it is copied here at the start of context 0's draw and put into slot 1
// for context 1's draw ([native] p0copy=0 turns it off).
static uint8_t p0_ent[CENT_SIZE];
static int p0_ent_ok;
#define LAG_HIST 128
static struct { double t; float acc[2]; } lag_hist[LAG_HIST];
static unsigned lag_hist_n;
static float lag_offset[2];
static int lag_offset_set[2];
static double lag_now(void)
{
	LARGE_INTEGER f, c;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart / (double)f.QuadPart;
}
static float lag_acc_of(int k)
{
	if (ctx_current == k)
		return *(float *)(base + RVA_ACC_PITCH + 4);
	return ctx_store[k][4] ? *(float *)(ctx_store[k][4] + ACC_IN_CLS + 4) : 0.0f;
}
static float lag_wrap(float d)
{
	while (d > 180.0f)
		d -= 360.0f;
	while (d < -180.0f)
		d += 360.0f;
	return d;
}
static float lag_acc_at(int k, double t)
{
	for (unsigned i = 0; i < LAG_HIST - 1 && i < lag_hist_n; i++) {
		unsigned a = (lag_hist_n - 1 - i) % LAG_HIST, b = (lag_hist_n - 2 - i) % LAG_HIST;
		if (lag_hist_n < 2 + i)
			break;
		if (lag_hist[b].t <= t && t <= lag_hist[a].t) {
			double span = lag_hist[a].t - lag_hist[b].t;
			double w = span > 0 ? (t - lag_hist[b].t) / span : 1.0;
			return lag_hist[b].acc[k] + (float)w * (lag_hist[a].acc[k] - lag_hist[b].acc[k]);
		}
	}
	return lag_hist[(lag_hist_n - 1) % LAG_HIST].acc[k];
}
static void lag_record(void)
{
	if (lag_hist_n && fabs(lag_hist[(lag_hist_n - 1) % LAG_HIST].t - lag_now()) < 0.0005)
		return;
	unsigned at = lag_hist_n % LAG_HIST;
	lag_hist[at].t = lag_now();
	lag_hist[at].acc[0] = lag_acc_of(0);
	lag_hist[at].acc[1] = lag_acc_of(1);
	lag_hist_n++;
}
// the entity of the OTHER player (index 1 in both contexts: context 1's list has the two traded); k is the player whose body it is.
// Yaw: replaced by the owner's own view yaw of this moment (offset tracked). Position: the interpolated origin (floats 63..65, and its copies 6..8 and 32..34)
// is pushed forward by its velocity over the measured delay (a window of the last ~0.2 s), the Unlagged / CPMA way of drawing a remote player.
#define LAG_POS_N 16
static struct { double t; float p[3]; } lag_pos[2][LAG_POS_N];
static unsigned lag_pos_n[2];
#define LAG_SAVED 12
static void lag_fix_yaw(int n, int k, float saved[LAG_SAVED], int *applied)
{
	*applied = 0;
	static int on = -1, pos_on = -1;
	if (on < 0) {
		on = cfg("lagfix", 1);
		pos_on = cfg("lagpos", 0); // not verified yet (players did not walk in the tests)
	}
	// only while a split screen session runs: the hooks stay in place afterwards and must not touch a normal game's entities (review 2026-10-28)
	if (!on || !native_active || n != (ctx_current == 0 ? 1 : 0) || lag_hist_n < 8)
		return;
	float *e = (float *)(base + RVA_CENT + (size_t)n * CENT_SIZE);
	const int yidx[3] = {CF_YAW0, CF_YAW1, CF_YAW2}, pidx[3] = {CF_ORG0, CF_ORG1, CF_ORG2};
	double now = lag_now();
	float seen = e[CF_YAW2];
	float measured = lag_wrap(seen - lag_acc_at(k, now - 0.125));
	if (!lag_offset_set[k]) {
		lag_offset[k] = measured;
		lag_offset_set[k] = 1;
	} else {
		lag_offset[k] += 0.05f * lag_wrap(measured - lag_offset[k]);
	}
	// the yaw the player himself looks along: his own playerState's view yaw (+0x10c; pitch +0x108, roll +0x110) in the other context's client block. The entity
	// state of a player keeps the yaw it spawned with here, so the body faced another way than its camera (Vlad's report); the old fix (view accumulator +
	// a tracked offset to that frozen yaw) kept the error ([native] psyaw=0 goes back to it)
	float fresh = lag_wrap(lag_acc_of(k) + lag_offset[k]);
	if (CFG_ONCE("psyaw", 1)) {
		const uint8_t *ps = ctx_current == k ? base + RVA_CL_PS : (ctx_store[k][5] ? ctx_store[k][5] + CL_BLOCK_PS : NULL);
		if (ps)
			fresh = *(const float *)(ps + 0x10c);
	}
	fresh = lag_wrap(fresh + (float)CFG_ONCE("lagtest", 0)); // lagtest: a QA turn to see that the draw uses these fields
	float ex[3] = {0, 0, 0};
	if (pos_on) {
		unsigned at = lag_pos_n[k] % LAG_POS_N;
		float *lp = &e[63];
		// a jump (teleport, level change) starts the window again
		if (lag_pos_n[k] && (fabsf(lp[0] - lag_pos[k][(lag_pos_n[k] - 1) % LAG_POS_N].p[0]) > 150.0f || fabsf(lp[1] - lag_pos[k][(lag_pos_n[k] - 1) % LAG_POS_N].p[1]) > 150.0f))
			lag_pos_n[k] = at = 0;
		lag_pos[k][at].t = now;
		lag_pos[k][at].p[0] = lp[0];
		lag_pos[k][at].p[1] = lp[1];
		lag_pos[k][at].p[2] = lp[2];
		lag_pos_n[k]++;
		unsigned have = lag_pos_n[k] < LAG_POS_N ? lag_pos_n[k] : LAG_POS_N, old = 0;
		for (unsigned i = 1; i < have; i++) { // the oldest sample not older than 0.25 s
			unsigned idx = (lag_pos_n[k] - 1 - i) % LAG_POS_N;
			if (now - lag_pos[k][idx].t <= 0.25)
				old = i;
		}
		if (old > 0) {
			unsigned oi = (lag_pos_n[k] - 1 - old) % LAG_POS_N;
			double dt = now - lag_pos[k][oi].t;
			if (dt > 0.03) {
				float len = 0;
				for (int i = 0; i < 3; i++) {
					ex[i] = (float)((lp[i] - lag_pos[k][oi].p[i]) / dt * 0.125);
					len += ex[i] * ex[i];
				}
				len = sqrtf(len);
				if (len > 70.0f)
					for (int i = 0; i < 3; i++)
						ex[i] *= 70.0f / len;
			}
		}
	}
	if (CFG_ONCE("laglog", 0)) {
		static unsigned fx;
		if (++fx % 4 == 0 && fx < 3000)
			log_printf("native: lagfix player %d seen %.1f %.1f extra %.1f %.1f yaw seen %.1f fresh %.1f", k, e[63], e[64], ex[0], ex[1], seen, fresh);
	}
	for (int i = 0; i < 3; i++) {
		saved[i] = e[yidx[i]];
		e[yidx[i]] = fresh;
		for (int c = 0; c < 3; c++) {
			saved[3 + i * 3 + c] = e[pidx[i] + c];
			e[pidx[i] + c] += ex[c];
		}
	}
	*applied = 1;
}
// The bodies are drawn by the cgame's second per-entity pass (rva 0xb0680) and posed between the two passes: a yaw put back after the first pass was never seen
// on a body, which kept its spawn yaw while its camera turned (owner 2026-10-28: "the bottom camera looks left while the body looks straight"). The yaw now stays
// for the whole frame; the next snapshot and the next frame's lerp write the fields again anyway ([native] yawkeep=0 puts it back as before).
static void lag_unfix_yaw(int n, float saved[LAG_SAVED], int applied)
{
	if (!applied)
		return;
	static int keep = -1;
	if (keep < 0)
		keep = cfg("yawkeep", 1);
	float *e = (float *)(base + RVA_CENT + (size_t)n * CENT_SIZE);
	const int yidx[3] = {CF_YAW0, CF_YAW1, CF_YAW2}, pidx[3] = {CF_ORG0, CF_ORG1, CF_ORG2};
	for (int i = 0; i < 3; i++) {
		if (!keep)
			e[yidx[i]] = saved[i];
		for (int c = 0; c < 3; c++)
			e[pidx[i] + c] = saved[3 + i * 3 + c];
	}
}

// "Mine" for the cgame is rva 0xa5ec0 (localClientNum -> the entity/slot number of that local client): with the coop connection table filled (flag byte at rva
// 0x52fb5c) it is the slot of the table (rva 0x540ba0, stride 0x2650, int type at +0x10 == 2 for a local client, its local client number as a ushort at +0x18),
// otherwise the number itself. Context 1 is local client 0 as far as the engine can tell, so it answered "player 0": the cgame's second per-entity pass (rva 0xb0680,
// one call per frame) then drew player 2's OWN entity from the inside (the head at the camera, late behind it; found with Hide() on the server entity) and player 1 counted
// as "mine" and was never drawn. The replacement below is the original function with one change: in context 1 local client 0 is player 2, entity 1 ([native] ownfix=0 off).
#ifdef _WIN64
static const uint8_t slot_bytes[] = {0x80, 0x3d, 0x95, 0x9c, 0x48, 0x00, 0x00, 0x44, 0x8b, 0xc1, 0x75, 0x03, 0x8b, 0xc1, 0xc3}; // cmp [flag],0; mov r8d,ecx; jne +3; mov eax,ecx; ret
static unsigned (*slot_original)(unsigned);
#endif
static unsigned local_slot_hook(unsigned lc)
{
	static int on = -1;
	if (on < 0)
		on = cfg("ownfix", 1);
	if (on && ctx_current == 1 && lc == 0)
		return 1;
	if (!*(const uint8_t *)(base + RVA_COOP_BYTE))
		return lc;
	const uint8_t *q = base + RVA_COOP_SLOTS + 8;
	for (unsigned i = 0;;) {
		if (*(const int *)(q - 8) == 2 && *(const uint16_t *)q == lc)
			return i;
		i++;
		q += SLOT_STRIDE;
		if (SLOT_END_PAST(q))
			return 0;
	}
}

// Mode 10: the game message windows (rva 0x52f348, outside the swapped ranges: one set, local client 0's) are drawn by both contexts, so their texts came in both halves and
// the layer comparison left a ghost of them. The HUD's draw of a game message window (rva 0xcb4f0: local client, window, x, y, ...) draws window 2 (console channel
// "subtitle") in the bottom half only, window 0 ("gamenotify obituary", top left) in the top half only ([native] subonce=0 off).
static const uint8_t msgwin_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x70};
static void (*msgwin_original)(int, int, int, int, int, int, void *, float, const float *, int, int, int); // x and y come in integer registers (r8d, r9d)
static void msgwin_hook(int lc, int window, int x, int y, int horz, int vert, void *font, float scale, const float *color, int style, int align, int how)
{
	static int on = -1;
	if (on < 0)
		on = cfg("subonce", 1);
	if (on && mode == 10 && native_active && split_on && ((window == 2 && ctx_current == 0) || (window == 0 && ctx_current == 1)))
		return;
	msgwin_original(lc, window, x, y, horz, vert, font, scale, color, style, align, how);
}

// The names over the other players (rva 0x93ac0, the loop at 0x93d90 over clients 0 and 1): a client is skipped when it is the drawing local client ("CMP EBP,EAX" at 0x93db3).
// Context 1 is not player 2 to that test, so the bottom half had no name over player 1. The 17 bytes from 0x93daa go to a stub that does the same test, but in context 1 the
// client skipped is player 2 (itself): it names player 1 ([native] namefix=0 off).
static const uint8_t name_self_bytes[] = {0x8b, 0xc7, 0x84, 0xc9, 0x74, 0x03, 0x0f, 0xb7, 0x06, 0x3b, 0xe8, 0x0f, 0x84, 0x21, 0x01, 0x00, 0x00};
#ifdef _WIN64
static void name_self_install(void)
{
	uint8_t *site = base + 0x93daa;
	if (memcmp(site, name_self_bytes, sizeof(name_self_bytes))) {
		log_printf("native: name loop bytes differ");
		return;
	}
	uint8_t code[64];
	size_t n = 0;
	static const uint8_t head[] = {0x8b, 0xc7, 0x84, 0xc9, 0x74, 0x03, 0x0f, 0xb7, 0x06, 0x52, 0x48, 0xba}; // mov eax,edi; test cl,cl; jz +3; movzx eax,word [rsi]; push rdx; mov rdx,imm64
	memcpy(code + n, head, sizeof(head));
	n += sizeof(head);
	uint64_t at = (uint64_t)(uintptr_t)&ctx_current;
	memcpy(code + n, &at, 8);
	n += 8;
	// cmp dword [rdx],1; pop rdx; jne +5; cmp eax,1 (context 1: the client to skip is player 2); jmp +2; cmp ebp,eax (otherwise the drawing local client); jz +14 (the second jump)
	static const uint8_t mid[] = {0x83, 0x3a, 0x01, 0x5a, 0x75, 0x05, 0x83, 0xf8, 0x01, 0xeb, 0x02, 0x3b, 0xe8, 0x74, 0x0e};
	memcpy(code + n, mid, sizeof(mid));
	n += sizeof(mid);
	hook_jump_bytes(code + n, base + 0x93dbb); // not this client: the name is drawn
	n += 14;
	hook_jump_bytes(code + n, base + 0x93edc); // this client: the next one
	n += 14;
	uint8_t *stub = hook_code(code, n);
	if (!stub) {
		log_printf("native: no page for the name loop stub");
		return;
	}
	jhook_jump(site, sizeof(name_self_bytes), stub, 0x90);
}
#else
// 32 bits (docs/32bit-native-map/inputui.md): the loop is its own function (rva 0x15ea00) and the drawing local client is on the stack at [esp+0x3c]; 19 bytes at rva 0x15ea9e
// (test dl,dl; mov eax,ebp; jz +3; movzx eax,[ecx]; cmp [esp+0x3c],eax; jz next) are replaced by the same test with context 1 skipping client 1 instead (the stub is jumped to, so esp is the loop's)
static void name_self_install(void)
{
	static const uint8_t expected[19] = {0x84, 0xd2, 0x8b, 0xc5, 0x74, 0x03, 0x0f, 0xb7, 0x01, 0x39, 0x44, 0x24, 0x3c, 0x0f, 0x84, 0xe4, 0x00, 0x00, 0x00};
	uint8_t *site = base + XR(0x15ea9e);
	if (memcmp(site, expected, sizeof(expected))) {
		log_printf("native: the name loop's self test is not the expected code");
		return;
	}
	uint8_t code[64];
	size_t n = 0;
	static const uint8_t head[] = {0x84, 0xd2, 0x8b, 0xc5, 0x74, 0x03, 0x0f, 0xb7, 0x01, 0x83, 0x3d}; // ... cmp dword [ctx_current], 1
	memcpy(code + n, head, sizeof(head));
	n += sizeof(head);
	uint32_t at = (uint32_t)(uintptr_t)&ctx_current;
	memcpy(code + n, &at, 4);
	n += 4;
	static const uint8_t mid[] = {0x01, 0x75, 0x05, 0x83, 0xf8, 0x01, 0xeb, 0x04, 0x39, 0x44, 0x24, 0x3c}; // jne +5; cmp eax,1; jmp +4; cmp [esp+0x3c],eax
	memcpy(code + n, mid, sizeof(mid));
	n += sizeof(mid);
	size_t jz_at = n; // jz next (rel32), then jmp to the name drawing
	code[n++] = 0x0f, code[n++] = 0x84, n += 4;
	size_t jmp_at = n;
	code[n++] = 0xe9, n += 4;
	uint8_t *stub = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!stub) {
		log_printf("native: no page for the name loop stub");
		return;
	}
	memcpy(stub, code, n);
	int32_t rel = (int32_t)((uintptr_t)(site + (0x15eb95 - 0x15ea9e)) - (uintptr_t)(stub + jz_at + 6));
	memcpy(stub + jz_at + 2, &rel, 4);
	rel = (int32_t)((uintptr_t)(site + (0x15eab1 - 0x15ea9e)) - (uintptr_t)(stub + jmp_at + 5));
	memcpy(stub + jmp_at + 1, &rel, 4);
	DWORD old;
	VirtualProtect(stub, 0x1000, PAGE_EXECUTE_READ, &old);
	FlushInstructionCache(GetCurrentProcess(), stub, n);
	jhook_jump(site, sizeof(expected), stub, 0x90);
}
#endif

// The mission's title lines (maps\_introscreen patch: sort 1.0078125 instead of 1) are drawn once, over the whole picture and as big as in single player: the HUD element draw
// (rva 0xa3b80: local client, element; the sort at +0x88) leaves them out in context 1 and draws them in context 0 with the full-screen 2D placement, into the rows of its layer
// below its half, which overlay_titles lays over the bottom half. So the split can come while the title is still on screen without cutting it in two ([native] titles=0 off).
static const uint8_t hudelem_bytes[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x68, 0xfc, 0xff, 0xff, 0x48, 0x81, 0xec, 0x98, 0x04, 0x00, 0x00};
static void (*hudelem_original)(int, void *);
static void hudelem_hook(int lc, void *e)
{
	if (mode == 10 && native_active && split_on && *(const float *)((const uint8_t *)e + 0x88) == 1.0078125f) {
		if (ctx_current == 1)
			return;
		int pushed = place_pushed;
		if (pushed)
			place_pop();
		hudelem_original(lc, e);
		if (pushed)
			place_push();
		title_tick = GetTickCount();
		return;
	}
	hudelem_original(lc, e);
}

// The name the loop shows comes from rva 0xd3110, which outside the engine's co-op gives the profile's name for every client: player 2 got player 1's name. The call at 0x93e2f
// gets player 2's from [native] p2name, by default the profile's name with " (2)" after it, the way the console names a second player on one profile.
static const char *(*name_get_original)(int);
static const char *name_get_hook(int client)
{
	static char p2[64], v[48];
	static unsigned gen, logged;
	const char *own = name_get_original(client);
	if (native_active && client >= 0 && client < 2 && !(logged & (1u << (client * 2 + ctx_current)))) { // once per client and context: whose name each half draws
		logged |= 1u << (client * 2 + ctx_current);
		log_printf("native: name over client %d drawn in context %d", client, ctx_current);
	}
	if (client != 1 || !native_active)
		return own;
	if (ctx_current == 1) // the loop takes client 1 for a remote one when the local player count it reads is 1 (it never reaches the test above): no name over player 2 in his own half
		return "";
	if (gen != cfg_gen) { // read once per session
		GetPrivateProfileStringA("native", "p2name", "", v, sizeof(v), settings_path);
		gen = cfg_gen;
	}
	if (v[0])
		snprintf(p2, sizeof(p2), "%s", v);
	else
		snprintf(p2, sizeof(p2), "%.40s (2)", own ? own : "");
	return p2;
}

// The cgame's second per-entity pass (rva 0xb0680) draws every entity that is not "mine". In context 1, with ownfix, the other player is entity 0, whose slot holds a copy
// of player 2's own data there (it is filled from the player state): for the call it gets player 1's entity as context 0 holds it.
// Player 1's entity data for context 1's slot 0: origin and angles of the pose (+0x18..0x30) and the entity states (+0x60..0x170, and +0x178 on) from context 0,
// while the slot keeps its own pose bookkeeping (local client number, lighting and ragdoll handles: +0..0x18, +0x30..0x60) and its own animation tree pointer
// (+0x170): a whole copy took context 0's "mine" bookkeeping along and the body was drawn as a stretched mesh ([native] p0full=1 copies everything as before).
// The tail of a centity (from the valid flag on: interpolate, currentValid, the previous event sequence, miscTime, the lighting origin) is the cgame's bookkeeping between frames. The
// copy of player 1's entity is put into context 1's slot 0 for the draw and the slot is put back afterwards, so what the draw wrote there (the events it has handled) was lost and the
// next frame played the last event again, for ever: the muzzle flash of a player who had fired stayed on his gun in the other half. The tail the draw left is kept and used by the
// next copy ([native] p0tail=0 off).
#define P0_TAIL CENT_VALID
#define P0_TAIL_LEN (CENT_SIZE - CENT_VALID)
static uint8_t p0_tail[P0_TAIL_LEN];
static int p0_tail_ok;
static void p0_tail_take(const uint8_t *slot)
{
	if (CFG_ONCE("p0tail", 1)) {
		memcpy(p0_tail, slot + P0_TAIL, P0_TAIL_LEN);
		p0_tail_ok = 1;
	}
}
static void p0_put(uint8_t *slot)
{
	// [native] p0keep: bit 0 keeps the slot's own +0..0x18 (pose header), bit 1 its +0x30..0x60 (rest of the pose), bit 2 its +0x170..0x178 (animation tree
	// pointer); bit 3 clears the per-client hide bits (0x30) of the copied eFlags (+0xec): context 0's own entity has them, the second pass needs them clear.
	static int keep = -1;
	if (keep < 0)
		keep = cfg("p0keep", 8 | 4 | 1);
	uint8_t saved[CENT_SIZE];
	memcpy(saved, slot, sizeof(saved));
	memcpy(slot, p0_ent, sizeof(p0_ent));
	if (p0_tail_ok && CFG_ONCE("p0tail", 1))
		memcpy(slot + P0_TAIL, p0_tail, P0_TAIL_LEN);
	if (keep & 1)
		memcpy(slot, saved, P0_HDR);
	if (keep & 2)
		memcpy(slot + P0_REST, saved + P0_REST, P0_REST_LEN);
	if (keep & 4)
		memcpy(slot + P0_TREE, saved + P0_TREE, 8);
	if (keep & 8)
		*(uint32_t *)(slot + CENT_FLAGS) &= ~0x30u;
	// player 1's yaw as he looks now (his playerState in context 0's block), in the pose and in both entity states: the copy is put back into the slot several
	// times a frame (own_write_hook, the passes), and each time it carried the yaw the entity states froze at ([native] yawkeep=0 off)
	static int yaw_on = -1;
	if (yaw_on < 0)
		yaw_on = cfg("yawkeep", 1) && cfg("psyaw", 1);
	if (yaw_on && ctx_store[0][5]) {
		float y = *(const float *)(ctx_store[0][5] + CL_BLOCK_PS + 0x10c);
		float *f = (float *)slot;
		f[CF_YAW0] = f[CF_YAW1] = f[CF_YAW2] = y;
	}
}

#ifdef _WIN64
static void (*pass2_original)(unsigned);
#else
// 32 bits: the second pass (rva 0x202d0) lies in the engine's checked code, so it is replaced as a whole from its call in the cgame draw: the two player entities that are not "mine"
// are drawn by the entity function 0x569ba0 (centity in ESI, local client on the stack), then 0x438f80(lc, centity) when the dword at 0x85e8cc is set. Its "mine" test is local_slot_hook.
static unsigned local_slot_hook(unsigned lc);
static void pass2_original(unsigned lc)
{
	for (int i = 0; i < 2; i++) {
		uint8_t *e = base + RVA_CENT + (size_t)i * CENT_SIZE;
		if (local_slot_hook(lc) == (unsigned)i || !(*(uint32_t *)(e + CENT_VALID) & 1))
			continue;
		uint8_t *esi = e;
		__asm__ volatile("pushl %1\n\tcall *%2\n\taddl $4, %%esp" : "+S"(esi) : "r"(lc), "r"(base + XR(0x169ba0)) : "eax", "ecx", "edx", "memory", "cc");
		if (*(int *)(base + XR(0x45e8cc)))
			((void (*)(unsigned, void *))(base + XR(0x38f80)))(lc, e);
	}
}
#endif
static const uint8_t pass2_bytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x68, 0x18, 0x56, 0x57, 0x41, 0x57, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00};
static void pass2_hook(unsigned lc)
{
	{ // [native] timelog2=1: each context's cgame time (rva 0x422434) at its entity pass, the two side by side every 120 frames and whenever they are more than 50 ms apart
		static int tl = -1;
		static int last_t[2];
		static unsigned n, logged;
		if (tl < 0)
			tl = cfg("timelog2", 0);
		if (tl && (ctx_current == 0 || ctx_current == 1)) {
			int t = *(const int *)(base + 0x422434);
			int was = last_t[ctx_current];
			last_t[ctx_current] = t;
			if (ctx_current == 1 && last_t[0] && abs(t - last_t[0]) > 50 && logged < 200)
				logged++, log_printf("native: entity pass time ctx1 %d ctx0 %d (diff %d)", t, last_t[0], t - last_t[0]);
			if (was && t < was && logged < 200)
				logged++, log_printf("native: entity pass time of ctx %d went back %d -> %d", ctx_current, was, t);
			if (++n % 120 == 0)
				log_printf("native: entity pass times ctx0 %d ctx1 %d", last_t[0], last_t[1]);
		}
	}
	static int on = -1;
	if (on < 0)
		on = cfg("p0copy", 1) && cfg("ownfix", 1);
	if (on && ctx_current == 1 && p0_ent_ok) {
		uint8_t saved[CENT_SIZE];
		memcpy(saved, base + RVA_CENT, sizeof(saved));
		p0_put(base + RVA_CENT);
		pass2_original(lc);
		p0_tail_take(base + RVA_CENT);
		memcpy(base + RVA_CENT, saved, sizeof(saved));
		return;
	}
	pass2_original(lc);
}

// Diagnostics ([native] dobjlog=1): the scene's "add a DObj" function (rva 0x2bb0: DObj in rcx, origin pointer in rdx, entity number as a ushort in r8, flags in r9) logged
// for context 1's frames, to see which entity a stray model comes from and where it is placed.
static void *(*dobj_add_original)(void *, void *, unsigned, unsigned, void *, void *, void *, void *);
static const uint8_t dobj_add_bytes[] = {0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0xf3, 0x0f, 0x10, 0x8c, 0x24, 0x98, 0x00, 0x00, 0x00};
static void *dobj_add_hook(void *a, void *b, unsigned c, unsigned d, void *e, void *f, void *g, void *h)
{
	static int on = -1;
	if (on < 0)
		on = cfg("dobjlog", 0);
	if (a && CFG_ONCE("dobjmodels", 0) && ((ctx_current == 1 && (c & 0xffff) == 0) || (ctx_current == 0 && (c & 0xffff) == 1)) && !IsBadReadPtr(a, 0xb0)) { // QA: the models of a body's DObj (names), twice a second
		static DWORD at[2];
		if (GetTickCount() - at[ctx_current] >= 500) {
			at[ctx_current] = GetTickCount();
			const uint8_t *dobj = (const uint8_t *)a;
			int n = dobj[0xd];
			const uint8_t *const *models = *(const uint8_t *const **)(dobj + 0xa8);
			char line[600];
			int len = snprintf(line, sizeof(line), "native: dobjmodels ctx %d ent %u dobj %p models %d:", ctx_current, c & 0xffff, a, n);
			for (int i = 0; models && i < n && i < 8 && len < (int)sizeof(line) - 80; i++) {
				const char *name = models[i] && !IsBadReadPtr(models[i], 8) && !IsBadReadPtr(*(const char *const *)models[i], 4) ? *(const char *const *)models[i] : "?";
				len += snprintf(line + len, sizeof(line) - len, " %s", name);
			}
			log_printf("%s", line);
			for (int h = 0; h < 2; h++) { // the engine's own DObj of entity h, and the slot's client number the draw looked it up by
				const uint8_t *pool = ((const uint8_t *(*)(int))(base + 0x1fece0))(h);
				int nm = pool ? pool[0xd] : -1;
				const uint8_t *const *mm = pool ? *(const uint8_t *const **)(pool + 0xa8) : NULL;
				char names[300] = "";
				for (int i = 0; mm && i < nm && i < 6 && strlen(names) < 200; i++) {
					const char *nn = mm[i] && !IsBadReadPtr(mm[i], 8) && !IsBadReadPtr(*(const char *const *)mm[i], 4) ? *(const char *const *)mm[i] : "?";
					snprintf(names + strlen(names), sizeof(names) - strlen(names), " %s", nn);
				}
				log_printf("native: dobjmodels   pool[%d] %p models %d:%s | slot0 clientnum %u slot1 clientnum %u", h, (const void *)pool, nm, names,
					   (unsigned)*(const uint16_t *)(base + RVA_CENT + 0x15e), (unsigned)*(const uint16_t *)(base + RVA_CENT + CENT_SIZE + 0x15e));
			}
		}
	}
	if (CFG_ONCE("speedlog", 0) && e && !IsBadReadPtr(e, 12) && (ctx_current == 0 || ctx_current == 1)) { // QA (scenario speed): the drawn path length of the ally the script follows, per context, per second
		static int ent = -1;
		static float last[2][3], path[2];
		static int have[2];
		static DWORD at;
		const float *o = (const float *)e;
		if ((int)(c & 0xffff) == ent) {
			if (have[ctx_current]) {
				float dx = o[0] - last[ctx_current][0], dy = o[1] - last[ctx_current][1], dz = o[2] - last[ctx_current][2], dd = sqrtf(dx * dx + dy * dy + dz * dz);
				if (dd < 100.0f)
					path[ctx_current] += dd;
			}
			memcpy(last[ctx_current], o, sizeof(last[0]));
			have[ctx_current] = 1;
		}
		if (GetTickCount() - at >= 1000) {
			char v[64] = "";
			int n = 0, script_len = 0;
			float rate = 1;
			if (game_dvar_raw("mw2coop_qa_s", v, sizeof(v)))
				sscanf(v, "%d %d %f", &n, &script_len, &rate);
			if (at && ent >= 0)
			{
				const uint8_t *cd = ((const uint8_t *(*)(int))(base + 0x1fece0))(ent), *sd = ((const uint8_t *(*)(int))(base + 0x1fee30))(ent);
				int pool = 0;
				for (int k = 0; k < 0x70; k++)
					pool += base[0xf50ec0 + k] != 0;
				log_printf("native: speed entity %d: drawn path ctx0 %.0f ctx1 %.0f units per second, script %d units per second (playback rate %.2f), client DObj %p (%d models), server DObj %p (%d models), actor DObj pool %d of 112 in use, flags150 %08x",
					   ent, path[0], path[1], script_len, rate, (const void *)cd, cd ? cd[0xd] : -1, (const void *)sd, sd ? sd[0xd] : -1, pool,
					   ent >= 0 && ent < 2048 ? *(const unsigned *)(base + 0xd9e070 + (size_t)ent * 0x2a0 + 0x150) : 0u);
			}
			ent = n;
			path[0] = path[1] = 0;
			have[0] = have[1] = 0;
			at = GetTickCount();
		}
	}
	if (a && CFG_ONCE("treelog", 0) && (c & 0xffff) >= 2 && (c & 0xffff) < 2048 && ctx_current == 0 && !IsBadReadPtr(a, 0xb0)) { // QA: the animation tree nodes of Price, ten times a second (split or plain game)
		const uint8_t *dobj = (const uint8_t *)a;
		int nm = dobj[0xd];
		const uint8_t *const *models = *(const uint8_t *const **)(dobj + 0xa8);
		const char *name = nm > 0 && models && models[0] && !IsBadReadPtr(models[0], 8) && !IsBadReadPtr(*(const char *const *)models[0], 4) ? *(const char *const *)models[0] : "";
		static DWORD at;
		static int ent = -1;
		static float first[2048][3];
		static uint8_t seen[2048];
		if (ent < 0 && strncmp(name, "body_", 5) == 0 && e && !IsBadReadPtr(e, 12)) { // the first character that walks away from where it was first drawn: Price or a squad mate in the first seconds
			const float *o = (const float *)e;
			unsigned k = c & 0xffff;
			if (!seen[k])
				seen[k] = 1, memcpy(first[k], o, sizeof(first[k]));
			else if ((o[0] - first[k][0]) * (o[0] - first[k][0]) + (o[1] - first[k][1]) * (o[1] - first[k][1]) > 150.0f * 150.0f) {
				ent = (int)k;
				log_printf("native: treelog follows entity %d (%s)", ent, name);
			}
		}
		if (ent == (int)(c & 0xffff) && GetTickCount() - at >= 50 && e && !IsBadReadPtr(e, 12)) {
			at = GetTickCount();
			const uint8_t *tree = *(const uint8_t *const *)dobj;
			int idx = tree && !IsBadReadPtr(tree, 16) ? *(const short *)(tree + 8) : 0;
			char line[1500];
			const float *org = (const float *)e;
			int len = snprintf(line, sizeof(line), "native: tree t=%lu ent %d idx %d pos %.1f %.1f:", (unsigned long)GetTickCount(), ent, idx, e ? org[0] : 0.0f, e ? org[1] : 0.0f);
			for (int k = 0; idx > 0 && k < 5 && len < (int)sizeof(line) - 200; k++) {
				const uint8_t *node = base + 0x1bc0a30 + (size_t)idx * 0x40;
				const float *f = (const float *)node;
				len += snprintf(line + len, sizeof(line) - len, " [n%d", idx);
				for (int j = 0; j < 16; j++) {
					float v = f[j];
					if (v != 0 && fabsf(v) > 1e-6f && fabsf(v) < 1e6f)
						len += snprintf(line + len, sizeof(line) - len, " %x:%.3f", j * 4, v);
					else if (((const uint32_t *)node)[j])
						len += snprintf(line + len, sizeof(line) - len, " %x:#%x", j * 4, ((const uint32_t *)node)[j]);
				}
				len += snprintf(line + len, sizeof(line) - len, "]");
				idx = *(const uint16_t *)(node + 0xc); // the first child
			}
			log_printf("%s", line);
		}
	}
	if (ctx_current == 0 && (c & 0xffff) == 1)
		p0cnt_flags0 = d;
	if (CFG_ONCE("p0cnt", 0) >= 2 && ((ctx_current == 0 && (c & 0xffff) == 1) || (ctx_current == 1 && (c & 0xffff) == 0))) {
		int k = ctx_current;
		memcpy(p0dump_slot[k], base + RVA_CENT + (size_t)(c & 0xffff) * CENT_SIZE, sizeof(p0dump_slot[k]));
		if (a && !IsBadReadPtr(a, sizeof(p0dump_dobj[k]))) {
			memcpy(p0dump_dobj[k], a, sizeof(p0dump_dobj[k]));
			p0dump_have[k] = 1;
		}
	}
	if (ctx_current == 1 && (c & 0xffff) == 0) {
		p0cnt_adds++;
		p0cnt_flags1 = d;
		if (CFG_ONCE("p0cnt", 0) && a && !IsBadReadPtr(a, 0x100)) {
			int k = 0;
			while (k < 6 && p0ptr[k] && p0ptr[k] != a)
				k++;
			if (k < 6) {
				p0ptr[k] = a;
				p0ptr_cnt[k]++;
				p0ptr_b8[k] = *(const uint32_t *)((const uint8_t *)a + 0xb8);
				p0ptr_bc[k] = *(const uint32_t *)((const uint8_t *)a + 0xbc);
			}
		}
		const float *dp = (const float *)e, *pp = (const float *)b;
		for (int i = 0; i < 3; i++) {
			p0cnt_draw_org[i] = dp ? dp[i] : 0;
			p0cnt_draw_org_b[i] = pp ? pp[6 + i] : 0; // the pose's origin sits at +0x18 of what rdx points to (see jumplog)
		}
	}
	if (on && ctx_current == 1) {
		static unsigned seen, frame;
		if (c == 0 || ++seen % 1 == 0) {
			if (seen < 5000 && seen > 800) {
				const float *o = (const float *)b;
				{ const float *p = (const float *)e; static const float zero[CENT_SIZE / 4]; const float *eo = (c & 0xffff) < 64 ? (const float *)(base + RVA_CENT + (size_t)(c & 0xffff) * CENT_SIZE) : zero; const char *mname = "?";
				if (a && !IsBadReadPtr(a, 0xb0)) { const uint8_t *dj = (const uint8_t *)a; const uint8_t *const *mm = *(const uint8_t *const **)(dj + DOBJ_MODELS); if (dj[DOBJ_NUMMODELS] && mm && !IsBadReadPtr(mm, 4) && mm[0] && !IsBadReadPtr(mm[0], 8) && *(const char *const *)mm[0] && !IsBadReadPtr(*(const char *const *)mm[0], 4)) mname = *(const char *const *)mm[0]; }
				log_printf("native: dobj add ctx1 ent %u flags %08x draw %.1f %.1f %.1f | model %s", c & 0xffff, d, p ? p[0] : 0, p ? p[1] : 0, p ? p[2] : 0, mname); (void)o; }
			}
		}
		(void)frame;
	}
	{ // [native] jumplog=1: an entity whose drawn origin (the pose at +0x18 of rdx) moves more than 16 units from one add to the next in the same context is logged (the far
	  // models' jumps in the bottom half: in the data the cgame hands over, or later in the renderer)
		static int jl = -1;
		static float last[2][1024][3];
		static uint8_t have[2][1024];
		static unsigned logged;
		if (jl < 0)
			jl = cfg("jumplog", 0);
		unsigned k = c & 0xffff;
		if (jl && (ctx_current == 0 || ctx_current == 1) && k >= 2 && k < 1024 && b) {
			const float *o = (const float *)((const uint8_t *)b + 0x18);
			float *l = last[ctx_current][k];
			if (have[ctx_current][k]) {
				float dx = o[0] - l[0], dy = o[1] - l[1], dz = o[2] - l[2], d = dx * dx + dy * dy + dz * dz;
				if (d > 256.0f && logged < 400) {
					logged++;
					log_printf("native: jump ctx %d ent %u by %.1f: %.1f %.1f %.1f -> %.1f %.1f %.1f", ctx_current, k, sqrtf(d), l[0], l[1], l[2], o[0], o[1], o[2]);
				}
			}
			l[0] = o[0], l[1] = o[1], l[2] = o[2];
			have[ctx_current][k] = 1;
		}
	}
	{ // [native] dobjcount=1: counts only (no per-call lines, they change the timing): context 1's adds of entities 0 and 1, by where they are drawn
		static int cnt_on = -1;
		static unsigned at_p0[2], at_p2[2], other[2], calls;
		if (cnt_on < 0)
			cnt_on = cfg("dobjcount", 0);
		if (cnt_on && ctx_current == 1 && (c & 0xffff) < 2 && e) {
			const float *pp = (const float *)e;
			unsigned k = c & 0xffff;
			float d0 = (pp[0] + 2515.2f) * (pp[0] + 2515.2f) + (pp[1] - 5190.2f) * (pp[1] - 5190.2f);
			float d2 = (pp[0] + 2588.0f) * (pp[0] + 2588.0f) + (pp[1] - 5212.0f) * (pp[1] - 5212.0f);
			if (d0 < 400.0f)
				at_p0[k]++;
			else if (d2 < 400.0f)
				at_p2[k]++;
			else
				other[k]++;
			if (++calls % 300 == 0)
				log_printf("native: dobjcount ctx1: ent0 at p1 %u at p2 %u other %u | ent1 at p1 %u at p2 %u other %u", at_p0[0], at_p2[0], other[0], at_p0[1], at_p2[1], other[1]);
		}
	}
	{ // Player 1's body in context 1 is added twice: by the cgame's add-by-type function (rva 0x9ea20, its "general" branch: context 1's slot 0 holds a copy of context 0's
	  // own player) and by the pass that draws the other co-op player (rva 0xb0680, as context 0 draws player 2). With both in the scene the body was not drawn in the
	  // bottom half, only the name over it; with the second one alone it is (2026-10-28, frames of Cliffhanger tarmac). The first is dropped ([native] p0general=1 keeps it).
		static int keep_general = -1;
		if (keep_general < 0)
			keep_general = cfg("p0general", 0);
		if (ctx_current == 1 && (c & 0xffff) == 0 && !keep_general) {
			const uint8_t *ret = (const uint8_t *)__builtin_return_address(0);
			if (P0GENERAL_RET(ret))
				return 0;
		}
	}
	return dobj_add_original(a, b, c, d, e, f, g, h);
}

// The local player's own origin and angles are written into cg_entities by rva 0xb28b0(lc) (after the snapshot's entity states), into the slot that the lc -> slot rule
// gives (inline at 0xb28d2, the same rule as 0xa5ec0: lc itself without the coop table). Context 1 runs with lc 0, so player 2's position went into slot 0 every frame and
// player 1's body was drawn in player 2's camera (dobjlog). In context 1 the write is moved to slot 1 and slot 0 keeps what it had ([native] ownpos=0 off).
static void (*own_write_original)(unsigned);
static const uint8_t own_write_bytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18};
static void own_write_hook(unsigned lc)
{
	static int on = -1;
	if (on < 0)
		on = cfg("ownpos", 1);
	if ((ctx_current == 0 || ctx_current == 1) && CFG_ONCE("entdump", 0)) { // QA: the cg_entities slots 0 and 1 of each context: the dwords that changed since the last look (which fields carry a body's weapon)
		static DWORD at[2];
		static uint32_t before[2][2][CENT_SIZE / 4];
		static int have[2];
		if (split_tick && GetTickCount() - split_tick >= (DWORD)CFG_ONCE("entdump", 0) * 1000 && GetTickCount() - at[ctx_current] >= 500) {
			at[ctx_current] = GetTickCount();
			for (int k = 0; k < 2; k++) {
				const uint32_t *d = (const uint32_t *)(base + RVA_CENT + (size_t)k * CENT_SIZE);
				char line[1400];
				int n = snprintf(line, sizeof(line), "native: entdump ctx %d ent %d:", ctx_current, k);
				for (int i = 0; have[ctx_current] && i < CENT_SIZE / 4 && n < (int)sizeof(line) - 24; i++)
					if (d[i] != before[ctx_current][k][i] && i != 0 && i != 6 && i != 7 && i != 8 && i != 0x20 && i != 0x21 && i != 0x22)
						n += snprintf(line + n, sizeof(line) - n, " %x:%x>%x", i * 4, before[ctx_current][k][i], d[i]);
				if (have[ctx_current] && n > 30)
					log_printf("%s", line);
				memcpy(before[ctx_current][k], d, sizeof(before[0][0]));
			}
			have[ctx_current] = 1;
		}
	}
	if (ctx_current == 0 && CFG_ONCE("psdiff", 0)) { // QA: the dwords of player 1's playerState (context 0's block) that changed since the last look (a weapon switch)
		static uint32_t before[0xc00];
		static int have;
		static DWORD at;
		if (GetTickCount() - at >= 500) {
			at = GetTickCount();
			const uint32_t *now = (const uint32_t *)(base + RVA_CL_PS); // context 0 is live: its playerState is in the live block
			char line[1200];
			int n = snprintf(line, sizeof(line), "native: psdiff:");
			for (int i = 0; have && i < 0xc00 && n < (int)sizeof(line) - 24; i++)
				if (now[i] != before[i])
					n += snprintf(line + n, sizeof(line) - n, " %x:%x>%x", i * 4, before[i], now[i]);
			if (have && n > 20)
				log_printf("%s", line);
			memcpy(before, now, sizeof(before));
			have = 1;
		}
	}
	if (ctx_current == 1 && CFG_ONCE("pslog", 0)) {
		static unsigned seen;
		if (++seen % 60 == 1 && seen < 4000) {
			const uint8_t *nps = *(const uint8_t **)(base + 0x40cad8), *pps = *(const uint8_t **)(base + 0x40cad0);
			const float *f0 = (const float *)(base + RVA_CENT), *f1 = (const float *)(base + RVA_CENT + CENT_SIZE);
			{
				const float *q1 = (const float *)(base + RVA_CL_PS), *q0 = ctx_store[0][5] ? (const float *)(ctx_store[0][5] + CL_BLOCK_PS) : NULL;
				log_printf("native: ps1(live) +0x100..: %d %.1f %.1f %.1f %.1f %.1f %.1f | ps0(store) %d %.1f %.1f %.1f %.1f %.1f %.1f | acc1 %.1f", *(const int *)(q1 + 64), q1[65], q1[66], q1[67], q1[68], q1[69], q1[70],
					   q0 ? *(const int *)(q0 + 64) : -9, q0 ? q0[65] : 0, q0 ? q0[66] : 0, q0 ? q0[67] : 0, q0 ? q0[68] : 0, q0 ? q0[69] : 0, q0 ? q0[70] : 0, *(const float *)(base + 0x533d18));
			}
			log_printf("native: ctx1 b28b0 lc %u: nextPs clientNum %d prevPs clientNum %d | slot0 %.1f %.1f valid %x | slot1 %.1f %.1f valid %x", lc, nps ? *(const int *)(nps + 0x100) : -9,
				   pps ? *(const int *)(pps + 0x100) : -9, f0[6], f0[7], *(const uint32_t *)((const uint8_t *)f0 + CENT_VALID), f1[6], f1[7], *(const uint32_t *)((const uint8_t *)f1 + CENT_VALID));
		}
	}
	if (on && ctx_current == 1 && lc == 0) {
		uint8_t *s0 = base + RVA_CENT;
#ifdef _WIN64
		uint8_t *s1 = s0 + CENT_SIZE;
		uint8_t keep0[0x18];
		memcpy(keep0, s0 + 0x18, sizeof(keep0));
		own_write_original(lc);
		memcpy(s1 + 0x18, s0 + 0x18, sizeof(keep0));
		memcpy(s0 + 0x18, keep0, sizeof(keep0));
#else
		own_write_original(lc); // (the 32-bit function asks local_slot_hook for its slot: it writes slot 1 itself)
#endif
		// player 1's entity for the whole of context 1's frame, not only for the draws: the skeleton and controllers (rva 0xb0860 between the two passes) are
		// computed from this slot too, and with player 2's leftover position in it the body was posed in player 2's camera, depending on timing ([native] p0frame=0 off)
		static int whole = -1;
		if (whole < 0)
			whole = cfg("p0frame", 1);
		if (whole && p0_ent_ok && CFG_ONCE("p0copy", 1))
			p0_put(s0);
		return;
	}
	own_write_original(lc);
#ifndef _WIN64
	// 32 bits: context 0's entity pass never hands entity 0 (the local player) to add_entity_hook, so player 1's entity is taken here, right after the cgame has written his
	// origin and angles into slot 0 (the 64-bit capture in add_entity_hook runs for n == 0)
	if (ctx_current == 0 && lc == 0) {
		memcpy(p0_ent, base + RVA_CENT, sizeof(p0_ent));
		p0_ent_ok = 1;
	}
#endif
}

static void add_entity_hook(int n)
{
	static unsigned frames;
	static int logging;
	if (ctx_current == 0) { // diagnostics: [native] skip0=<n> drops entity n in context 0's draw (is the bottom half's head a leftover of the top half's scene?)
		static int skip0 = -2;
		if (skip0 == -2)
			skip0 = cfg("skip0", -1);
		if (n == skip0)
			return;
	}

	// Lag measurement ([native] laglog=1): while context 0 draws, the floats of player 1's entity as the cgame holds them, next to context 1's own view yaw
	// (its accumulator), both stamped by the log's own clock: the field that follows the yaw and its delay behind the accumulator show where the time goes.
	if (ctx_current == 0 && n == 1 && CFG_ONCE("laglog", 0)) {
		static unsigned lag_frames;
		if (++lag_frames % 2 == 0 && lag_frames < 6000 && ctx_store[1][4]) {
			const float *f = (const float *)(base + RVA_CENT + (size_t)n * CENT_SIZE);
			char line[1400];
			int len = snprintf(line, sizeof(line), "native: lag acc %.2f f:", *(float *)(ctx_store[1][4] + ACC_IN_CLS + 4));
			for (int i = 0; i < CENT_SIZE / 4 && len < (int)sizeof(line) - 12; i++)
				len += snprintf(line + len, sizeof(line) - len, " %.1f", f[i]);
			log_printf("%s", line);
		}
	}
	if (ctx_current == 1) {
		if (swap_entities && n == 0 && !entities_swapped) {
			// the cgame hides the entity numbered like the local client (0) as "mine"; context 1 is local client 0 but player 1: his entity and player 0's trade places for the draw
			uint8_t *e0 = base + RVA_CENT, *e1 = e0 + CENT_SIZE, t[CENT_SIZE];
			memcpy(t, e0, sizeof(t));
			memcpy(e0, e1, sizeof(t));
			memcpy(e1, t, sizeof(t));
			entities_swapped = 1;
		}
		if (log_entities && n == 0 && logging == 0 && ++frames == 700)
			logging = 1;
		if (logging == 1) {
			const uint8_t *e = base + RVA_CENT + (size_t)n * CENT_SIZE;
			log_printf("native: ctx1 adds entity %d type %d flags %08x pos %.1f %.1f %.1f yaw %.1f", n, e[CENT_TYPE], *(unsigned *)(e + CENT_FLAGS), ((const float *)e)[63], ((const float *)e)[64], ((const float *)e)[65], ((const float *)e)[73]);
		}
		if (CFG_ONCE("swaplog", 0) && n <= 1) {
			static unsigned sl;
			const float *q0 = (const float *)(base + RVA_CENT), *q1 = (const float *)(base + RVA_CENT + CENT_SIZE);
			if (++sl % 40 == 0 && sl < 1600)
				log_printf("native: swaplog ctx1 n %d swapped %d slot0 pos %.1f %.1f type %d | slot1 pos %.1f %.1f type %d", n, entities_swapped, q0[63], q0[64], ((const uint8_t *)q0)[CENT_TYPE], q1[63], q1[64], ((const uint8_t *)q1)[CENT_TYPE]);
		}
		{ // diagnostics: [native] skiptypes=<mask> drops, in context 1's draw, every entity whose type (byte 0xe0) has its bit set (bisect for the garbled mesh)
			static int skiptypes = -1;
			if (skiptypes < 0)
				skiptypes = cfg("skiptypes", 0);
			unsigned type = ((const uint8_t *)(base + RVA_CENT + (size_t)n * CENT_SIZE))[CENT_TYPE];
			if (type < 31 && (skiptypes & (1 << type)))
				return;
		}
		if (n == skip_entity)
			return;
		// the per-client "hide from" bits of an entity (flags & 0x3f0, bit 4 + local client) are made for local client numbers: context 1 is local client 0 but is client 1
		// ([native] swapbits=0 turns it off)
		static int swapbits = -1;
		if (swapbits < 0)
			swapbits = cfg("swapbits", 1);
		unsigned *flags = (unsigned *)(base + RVA_CENT + (size_t)n * CENT_SIZE + CENT_FLAGS);
		unsigned saved = *flags;
		if (swapbits) {
			unsigned b0 = saved & 0x10, b1 = saved & 0x20;
			*flags = (saved & ~0x30u) | (b0 ? 0x20 : 0) | (b1 ? 0x10 : 0);
		}
		{
			float sv[LAG_SAVED];
			int ap;
			uint8_t slot1_saved[CENT_SIZE];
			int p0copy = n == 0 && p0_ent_ok && CFG_ONCE("p0copy", 1);
			if (p0copy) {
				memcpy(slot1_saved, base + RVA_CENT, sizeof(slot1_saved));
				p0_put(base + RVA_CENT);
			}
			if (n == 0 && CFG_ONCE("p0log", 0)) { // player 1's body in context 1's draw: the slot as it was drawn (copy of context 0's entity 0)
				static unsigned pl;
				const uint8_t *e = base + RVA_CENT;
				if (++pl % 10 == 0 && pl < 30000)
					log_printf("native: p0log ctx1 draws entity0: ok %d copied %d pos %.1f %.1f %.1f yaw %.1f type %d flags %08x valid %08x | own p2 pos %.1f %.1f", p0_ent_ok, p0copy, (double)((const float *)e)[CF_ORG2], (double)((const float *)e)[CF_ORG2 + 1], (double)((const float *)e)[CF_ORG2 + 2],
						   (double)((const float *)e)[CF_YAW2], e[CENT_TYPE], *(const unsigned *)(e + CENT_FLAGS), *(const unsigned *)(e + CENT_VALID), (double)*(const float *)(base + 0x42245c), (double)*(const float *)(base + 0x422460));
			}
			if (n == 0)
				lag_record();
			lag_fix_yaw(n, 0, sv, &ap); // player 0's body in context 1's picture (his entity sits at index 1 after the swap)
			add_entity_original(n);
			lag_unfix_yaw(n, sv, ap);
			if (p0copy)
				p0_tail_take(base + RVA_CENT);
				memcpy(base + RVA_CENT, slot1_saved, sizeof(slot1_saved));
		}
		*flags = saved;
		return;
	}
	{
		float sv[LAG_SAVED];
		int ap;
		if (n == 0) {
			memcpy(p0_ent, base + RVA_CENT, sizeof(p0_ent));
			p0_ent_ok = 1;
			lag_record();
			if (CFG_ONCE("p0log", 0)) { // what context 0's draw hands over as player 1's entity
				static unsigned cl0;
				if (++cl0 % 10 == 0 && cl0 < 30000)
					log_printf("native: p0log ctx0 captured entity0: pos %.1f %.1f %.1f yaw %.1f type %d flags %08x valid %08x", (double)((const float *)p0_ent)[CF_ORG2], (double)((const float *)p0_ent)[CF_ORG2 + 1], (double)((const float *)p0_ent)[CF_ORG2 + 2],
						   (double)((const float *)p0_ent)[CF_YAW2], p0_ent[CENT_TYPE], *(const unsigned *)(p0_ent + CENT_FLAGS), *(const unsigned *)(p0_ent + CENT_VALID));
			}
		}
		lag_fix_yaw(n, 1, sv, &ap); // player 1's body in context 0's picture
		add_entity_original(n);
		lag_unfix_yaw(n, sv, ap);
	}
}

// Diagnostics / fix for the 2D texts that both halves draw (the objective text, subtitles, "who stands in front" hints are global): [native] skiphud=<mask> drops, in context 1's
// frames, the HUD parts called at the end of the cgame draw: bit 0 rva 0xc6070, bit 1 rva 0xc14e0, bit 2 rva 0x94d70, bit 3 rva 0xc5b70.
static void (*hud_c6070_original)(unsigned);
static void (*hud_c14e0_original)(unsigned);
static void (*hud_94d70_original)(unsigned);
static void (*hud_c5b70_original)(unsigned);
static void hud_c6070_hook(unsigned lc) { if (!(ctx_current == skip_ctx && (skip_hud_mask & 1))) hud_c6070_original(lc); }
static void hud_c14e0_hook(unsigned lc) { if (!(ctx_current == skip_ctx && (skip_hud_mask & 2))) hud_c14e0_original(lc); }
static void hud_94d70_hook(unsigned lc) { if (!(ctx_current == skip_ctx && (skip_hud_mask & 4))) hud_94d70_original(lc); }
static void hud_c5b70_hook(unsigned lc) { if (!(ctx_current == skip_ctx && (skip_hud_mask & 8))) hud_c5b70_original(lc); }
// more candidates: the calls between and after them in the cgame draw (rva of the call instruction, the callee)
typedef void (*hud_fn_t)(void *, void *, void *, void *, void *, void *);
static hud_fn_t hud_x_original[8]; // called with the six argument registers/stack slots as they come (the callees take up to four register arguments and one on the stack)
#define HUD_X(n, bit) static void hud_x##n##_hook(void *a, void *b, void *c, void *d, void *e, void *f) { if (!(ctx_current == skip_ctx && (skip_hud_mask & (bit)))) hud_x_original[n](a, b, c, d, e, f); }
HUD_X(0, 0x10)  // 0xbd03b be710
HUD_X(1, 0x20)  // 0xbd0fb 11f0c0 (4 arguments)
HUD_X(2, 0x40)  // 0xbd102 1214e0
HUD_X(3, 0x80)  // 0xbd109 fd010
#ifdef _WIN64
// Diagnostics ([native] sndcount=1, with sndlog=1 in the report): how often each context starts a world sound (rva 0x23a9b0); a stub that counts and jumps on
extern char snd_count_stub[];
void snd_note(const char **alias); // (below: counts per alias name)
__asm__(".text\n.globl snd_count_stub\nsnd_count_stub:\n"
	"\tpush %rax\n"
	"\tpush %rcx\n"
	"\tpush %rdx\n"
	"\tpush %r8\n"
	"\tpush %r9\n"
	"\tpush %r10\n"
	"\tpush %r11\n"
	"\tsub $0x60, %rsp\n"
	"\tmovdqu %xmm0, 0x20(%rsp)\n"
	"\tmovdqu %xmm1, 0x30(%rsp)\n"
	"\tmovdqu %xmm2, 0x40(%rsp)\n"
	"\tmovdqu %xmm3, 0x50(%rsp)\n"
	"\tcall snd_note\n"
	"\tmovdqu 0x20(%rsp), %xmm0\n"
	"\tmovdqu 0x30(%rsp), %xmm1\n"
	"\tmovdqu 0x40(%rsp), %xmm2\n"
	"\tmovdqu 0x50(%rsp), %xmm3\n"
	"\tadd $0x60, %rsp\n"
	"\tpop %r11\n"
	"\tpop %r10\n"
	"\tpop %r9\n"
	"\tpop %r8\n"
	"\tpop %rdx\n"
	"\tpop %rcx\n"
	"\tpop %rax\n"
	"\tjmp *snd_count_orig(%rip)\n"
	);
// the aliases' start counts of the last second, per context ([native] sndcount=1: totals, 2: names too)
void snd_note(const char **alias)
{
	int c = ctx_current & 1;
	InterlockedIncrement(&snd_counts[c]);
	const char *name = alias ? *alias : NULL;
	if (!name || cfg("sndcount", 0) < 2)
		return;
	for (int i = 0; i < 512; i++) {
		if (snd_names[i].name == name) {
			snd_names[i].n[c]++;
			return;
		}
		if (!snd_names[i].name) {
			snd_names[i].name = name;
			snd_names[i].n[c] = 1;
			return;
		}
	}
}
#endif
// 0xbd130 is the cgame's SND_SetListener(local client, ...) (rva 0x23db90): the engine keeps two listeners (rva 0x1a28f48, 0x3c bytes each; every sound is heard by the nearer one, like on the console's split screen).
// Context 1 runs as local client 0, so both contexts set listener 0 and the hearing jumped between the players every frame (constant judder of the sound); context 1 sets listener 1.
static void hud_x4_hook(void *a, void *b, void *c, void *d, void *e, void *f)
{
	if (ctx_current == skip_ctx && (skip_hud_mask & 0x100))
		return;
	if (native_active && ctx_current == 1 && CFG_ONCE("snd2", 1))
		a = (void *)(uintptr_t)1;
	hud_x_original[4](a, b, c, d, e, f);
}
#ifndef _WIN64
// The same for the 32-bit build: the call at rva 0x66a20 in the cgame draw (SND_SetListener at rva 0x47b70, cdecl: local client first)
static void (*snd_listener_original)(int, int, void *, void *, void *);
static void snd_listener_hook(int lc, int a, void *origin, void *axis, void *velocity)
{
	static int calls;
	if (native_active && ctx_current == 1 && CFG_ONCE("snd2", 1))
		lc = 1;
	if (calls < 400 && CFG_ONCE("sndlog", 0) && (calls++ % 50) == 0)
		log_printf("native: sound listener call %d: local client %d, context %d, origin %.0f %.0f %.0f", calls, lc, ctx_current, (double)((float *)origin)[0], (double)((float *)origin)[1], (double)((float *)origin)[2]);
	snd_listener_original(lc, a, origin, axis, velocity);
}
#endif
HUD_X(5, 0x200) // 0xbd1a4 11a0b0
HUD_X(6, 0x400) // 0xbd206 c8600

// Diagnostics ([native] entcleanlog=1): the cgame's cleanup of removed entities (rva 0xb9320; the flags are the bit set at rva 0x52c300) logs which entities it frees and for which context
static const uint8_t ent_clean_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
static void (*ent_clean_original)(void);
static void ent_clean_hook(void)
{
	static int logged;
	if (logged < 60 && CFG_ONCE("entcleanlog", 0)) {
		uint32_t *f = (uint32_t *)(base + RVA_ENT_FLAGS);
		char line[400];
		int n = snprintf(line, sizeof(line), "native: entity cleanup in context %d, flags:", ctx_current);
		int any = 0;
		for (int i = 0; i < 64 && n < (int)sizeof(line) - 24; i++)
			if (f[i]) {
				n += snprintf(line + n, sizeof(line) - n, " [%d]=%08x", i, f[i]);
				any = 1;
			}
		if (any) {
			logged++;
			log_printf("%s", line);
		}
	}
	if (ctx_current == 1) { // both contexts process every snapshot: the cleanup frees the engine's shared per-entity objects (the DObjs), so only context 0 may do it (a second free of the same entity crashed in the pool free).
		// The entities flagged for it are NOT forgotten (as they were: the flags were cleared here): a client DObj made by a transition is only the pending one until the cleanup of its flag
		// makes it the current one (rva 0x1fec30), and a flag wiped before context 0's cleanup ran left the entity without a client DObj for good: not drawn (Price on Contingency, in some runs).
		// Context 0's next cleanup takes them. [native] cleanwipe=1 clears them as before.
		if (CFG_ONCE("cleanwipe", 0)) {
			uint32_t *f = (uint32_t *)(base + RVA_ENT_FLAGS);
			memset(f, 0, 64 * sizeof(uint32_t));
		}
		return;
	}
#ifdef _WIN64
	// The cleanup also carries the effects attached to an entity over to its new DObj (a weapon change makes a new one) or kills them: rva 0x115110 saves the old DObj's models and tags
	// before the old one is freed, rva 0x1151d0 re-attaches (or kills) afterwards, in the live effects system, which is context 0's. Context 1 has a system of its own (fxsplit) that
	// never got this: the muzzle flash of a body whose weapon was changed stayed on it for ever in the other half. The same two calls are made for context 1's system.
	if (native_active && ctx1_ready && ctx_store[1][NRANGES - 1] && CFG_ONCE("fxsplit", 1) && CFG_ONCE("fxentfix", 1)) {
		struct {
			uint8_t buf[528];
			int ent, flag;
			unsigned short key;
		} saved[64];
		int count = 0;
		const uint32_t *flags = (const uint32_t *)(base + RVA_ENT_FLAGS);
		const uint8_t *(*client_dobj)(int) = (const uint8_t *(*)(int))(base + RVA_CLIENT_DOBJ);
		for (int w = 0; w < 64 && count < 64; w++)
			for (int bit = 0; bit < 32 && count < 64; bit++)
				if (flags[w] & (0x80000000u >> bit)) {
					int n = w * 32 + bit;
					const uint8_t *slot = base + RVA_CENT + (size_t)n * CENT_SIZE;
					saved[count].ent = n;
					saved[count].flag = *(const int *)(slot + 0x16c) == 0xffffff;
					saved[count].key = *(const unsigned short *)(slot + 0x164);
					((void (*)(void *, const uint8_t *, uint8_t, uint16_t))(base + 0x115110))(saved[count].buf, client_dobj(n), (uint8_t)saved[count].flag, saved[count].key);
					count++;
				}
		ent_clean_original();
		if (count) {
			ctx_swap(1);
			for (int i = 0; i < count; i++)
				((void (*)(void *, int, int, const uint8_t *, char, short))(base + 0x1151d0))(saved[i].buf, 0, saved[i].ent, client_dobj(saved[i].ent), (char)saved[i].flag, (short)saved[i].key);
			ctx_swap(0);
		}
		return;
	}
#endif
	ent_clean_original();
}

// The first-person weapon's animation blend (rva 0xc6ee0, called at 0xc7499 from the HUD's weapon part): its tree sits behind a pointer of the cgame block (rva 0x440730 + 0x20 * client) that is
// null while a context has no view model yet (right after a weapon change in context 1: a read at null + 8, the crash of the first test of a script's SwitchToWeapon); the blend waits a frame then.
static void (*viewmodel_blend_original)(float, void *, int);
static void viewmodel_blend_hook(float a, void *tree, int which)
{
	if (native_active && (!tree || !*(void **)tree))
		return;
	viewmodel_blend_original(a, tree, which);
}

// The first-person weapon's setup (rva 0xc6480, called at 0xc41b1, 0xc58ed, 0xc594a, 0xc5dfb, 0xc6033) makes its DObjs under the handles 0x800 + 2 * local client + hand, a pool both contexts
// would share (context 1 runs as local client 0): a weapon change in one freed what the other still pointed to (the crash at the first change of a weapon). Context 1's calls are made for
// local client 1 (handles 0x802 and 0x803: the table has room for four local clients), and its copy of the cgame's pointers to the view model is cleared when it starts (ctx_after_draw).
static void (*viewmodel_setup_original)(int, void *, unsigned, void *, char, char);
static void viewmodel_setup_hook(int lc, void *a, unsigned weapon, void *b, char c, char d)
{
	viewmodel_setup_original(native_active && ctx_current == 1 ? 1 : lc, a, weapon, b, c, d);
}

// The sound listener (rva 0x6a570, called at 0xbd2c5 near the end of the cgame draw) takes the camera's position and axes of local client 0: both contexts would set it, the hearing would jump between
// the players every frame. Only context 0's player is heard.
// The call at rva 0xbd2c5 is the aim assist's screen target update (rva 0x6a570: eye, view origin, view angles), not the sound listener it was once taken for: it runs
// in both contexts, each into its own copy of aaGlob (a swapped range). Its target list (rva 0x6b230, called at 0x6a711) is the server client's of local client 0 in
// both: context 1 gets client 1's.
static void (*screen_targets_original)(int, void *, void *, void *, unsigned, float);
static void screen_targets_hook(int lc, void *eye, void *origin, void *angles, unsigned a, float b)
{
	screen_targets_original(lc, eye, origin, angles, a, b);
}
static void (*target_list_original)(int, void **, int *);
static void target_list_hook(int lc, void **list, int *count)
{
	target_list_original(native_active && ctx_current == 1 ? 1 : lc, list, count);
}

// The server side of the aim targets: the PC build no longer runs AimTarget_ProcessEntity (rva 0x6b5a0) for each entity, so the lists the cgame takes its screen
// targets from (aimTargetGlob, rva 0x3d0530, 0x210c bytes per client: the list being built at +0 with its count at +0xb00, the one read at +0x1608 with its count
// at +0x2108, 0x2c bytes a target) stay empty. After each simulation step: each client's list cleared, every entity that ProcessEntity would take checked for
// both clients (an enemy the client can see goes into his list, rva 0x6b630 -> 0x6a9a0; ProcessEntity's own loop asks rva 0x6bae0 for the number of clients,
// which reads the client code's copy of the local player count, 1), and the lists published. g_entities: rva 0xd9e070, 0x2a0 bytes each: in use +0xac / +0xb0,
// type +0 (13 an actor: health +0x164, sentient +0x118 with its team at +8), eFlags +0xc, +0x8c, the clients' "seen" bits 0x80000 << client at +0x150;
// level.num_entities at rva 0xd96e80.
#ifndef _WIN64
static void aim_for_client32(int c, uint8_t *ent)
{
	__asm__ volatile("pushl %1\n\tcall *%2\n\taddl $4, %%esp" : "+a"(ent) : "r"(c), "m"(aim_for_client_fn) : "ecx", "edx", "memory", "cc");
}
#define AIM_FOR_CLIENT32(c, ent) aim_for_client32(c, ent)
#endif
static void aim_targets_update(void)
{
	if (!CFG_ONCE("aimtargets", 1))
		return;
	uint8_t *glob = base + RVA_AIM_TARGET_GLOB;
	int count = *(int *)(base + RVA_LEVEL_NUM_ENT);
	if (count <= 0 || count > 2048)
		count = 2048;
	for (int c = 0; c < 2; c++)
		*(int *)(glob + c * 0x210c + 0xb00) = 0;
	// Only living actors of another team than player 1's; and the check's "seen" bits are kept here, not in the entity: this PC build is not known to leave those
	// bits of +0x150 alone (with them written for every entity, Price was not drawn on Contingency, owner 2026-10-28), so the entity gets ours for the call and its
	// own back after it
	static uint32_t seen[2048];
	const uint8_t *own = *(uint8_t **)(base + RVA_GENTITIES + GENT_SENTIENT); // player 1's sentient: his team at +8 (32 bits +4)
	const int team = own ? *(const int *)(own + SENT_TEAM) : -1;
#ifdef _WIN64
	void (*for_client)(int, uint8_t *) = (void (*)(int, uint8_t *))(base + 0x6b630);
#else
	// 32 bits: the per-client check takes the gentity in EAX and the client on the stack
#define for_client(c, ent) AIM_FOR_CLIENT32(c, ent)
#endif
	for (int i = 0; i < count; i++) {
		uint8_t *ent = base + RVA_GENTITIES + (size_t)i * GENT_SIZE;
		const uint8_t *sentient = *(uint8_t **)(ent + GENT_SENTIENT);
		if (CFG_ONCE("aalog", 0) && ent[0xac] && ent[0xb0] && ent[0] == 13 && sentient && i < 2048) { // QA: the teams of the actors seen, once each
			static unsigned char logged[8];
			int tm = *(const int *)(sentient + SENT_TEAM) & 7;
			if (!logged[tm]) {
				logged[tm] = 1;
				log_printf("native: aim targets: an actor of team %d (entity %d), player 1's team %d", tm, i, team);
			}
		}
		// only the player's enemies (the axis team, 1): an ally in the list ran the check on him, and Price was not drawn (owner, Contingency, 2026-10-29)
		if (!ent[0xac] || !ent[0xb0] || ent[0] != 13 || *(int *)(ent + GENT_HEALTH) <= 0 || !sentient || *(const int *)(sentient + SENT_TEAM) != 1) {
			seen[i] = 0;
			continue;
		}
		uint32_t *flags = (uint32_t *)(ent + GENT_SEEN), keep = *flags;
		*flags = (keep & ~0x180000u) | seen[i];
		for (int c = 0; c < 2; c++)
			for_client(c, ent);
		seen[i] = *flags & 0x180000u;
		*flags = keep;
	}
	for (int c = 0; c < 2; c++) {
		uint8_t *list = glob + c * 0x210c;
		int n = *(int *)(list + 0xb00);
		if (n < 0 || n > 64)
			n = 0;
		memcpy(list + 0x1608, list, (size_t)n * 0x2c);
		*(int *)(list + 0x2108) = n;
	}
	static DWORD logged;
	if (CFG_ONCE("aalog", 0) && GetTickCount() - logged >= 1000) {
		logged = GetTickCount();
		log_printf("native: aim targets: %d entities, client 0 %d, client 1 %d", count, *(int *)(glob + 0x2108), *(int *)(glob + 0x210c + 0x2108));
	}
}

#ifndef _WIN64
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

// The engine checks its own code: seven hash checks (functions 0x4e30f0, 0x4ca740, 0x4f8710, 0x43ffb0 ... run at the renderer init and level loads) over VA 0x401000-0x408000, 0x418000-0x428000 and
// 0x430000-0x440000 and over 0x6aa000-0x6aa378 (data), and the game quits with exit code 0x8000dead when one differs, so nothing in those ranges can be patched (the 10 client reads of the player count
// that lie there made the game quit at devmap). The four server reads in them (0x4014e2, 0x4015e9, 0x418d20, 0x418d6b) are left alone. The 64-bit design (the client's reads
// point at a copy that stays 1) is turned around: the client reads stay on the engine's own count (1) and the other reads (the server, the game module: these 54 sites, docs/32bit-native-map/clientctx.md)
// point at a variable that holds the session's number of players.
static const struct {
	uint32_t va;
	uint8_t length, disp;
} server_n_sites32[] = {
	{0x408889, 6, 2}, {0x40a579, 6, 2}, {0x40c714, 6, 2}, {0x42aed0, 6, 2}, {0x42af2f, 6, 2}, {0x42d116, 6, 2}, {0x42d15a, 6, 2}, {0x46ce9f, 6, 2}, {0x46cecc, 6, 2}, {0x47840d, 6, 2}, {0x482ea4, 6, 2}, {0x49d008, 6, 2}, {0x49d0e3, 6, 2}, {0x49d3e4, 6, 2}, {0x4a9754, 6, 2}, {0x4a97ca, 6, 2}, {0x4ae390, 7, 2}, {0x4ae6b0, 6, 2}, {0x4d3f7c, 6, 2}, {0x4d6774, 6, 2}, {0x4e7c89, 6, 2}, {0x4e8366, 6, 2}, {0x4e83b7, 6, 2}, {0x501e99, 6, 2}, {0x59a476, 6, 2}, {0x59d86e, 6, 2}, {0x59d939, 6, 2}, {0x5c5ca8, 6, 2}, {0x5c6ba4, 6, 2}, {0x5c74e8, 6, 2}, {0x5d7b19, 6, 2}, {0x5e2870, 6, 2}, {0x659f3f, 7, 2}, {0x659fc8, 6, 2}, {0x65a01d, 6, 2}, {0x65a096, 6, 2}, {0x65a0fa, 6, 2}, {0x65a4d7, 6, 2}, {0x65a898, 6, 2}, {0x65ab2e, 6, 2}, {0x65acfd, 5, 1}, {0x65af35, 6, 2}, {0x65b16f, 6, 2}, {0x65b463, 6, 2}, {0x65b48d, 6, 2}, {0x65b7a9, 6, 2}, {0x65c140, 6, 2}, {0x65c167, 6, 2}, {0x65ca9b, 6, 2}, {0x65cb65, 6, 2}
};
static int32_t n_server_copy; // what the server and the game module read instead of the number of local players: the engine's own count, N once a split level has started
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
// QA ([native] countwatch=1): every write to the engine's local player count, found with a hardware data breakpoint on all threads (the log says which instruction wrote it)
static volatile int32_t count_seen;
static LONG CALLBACK count_veh(EXCEPTION_POINTERS *ep)
{
	if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
		return EXCEPTION_CONTINUE_SEARCH;
	CONTEXT *c = ep->ContextRecord;
	if (!(c->Dr6 & 1))
		return EXCEPTION_CONTINUE_SEARCH;
	c->Dr6 = 0;
	int32_t v = *(volatile int32_t *)(base + RVA_LOCAL_PLAYERS);
	log_printf("native: write to the local player count by eip %p (rva 0x%x), value now %d, copy %d", (void *)(uintptr_t)c->Eip, (unsigned)((uintptr_t)c->Eip - (uintptr_t)base), (int)v, (int)n_server_copy);
	return EXCEPTION_CONTINUE_EXECUTION;
}
// QA ([native] exitwatch=1, 32 bits): who ends the process: the return addresses on the stack when ExitProcess is called
static void (WINAPI *exit_process_original)(UINT);
static void WINAPI exit_process_hook(UINT code)
{
	uintptr_t *sp = (uintptr_t *)__builtin_frame_address(0);
	char line[1200];
	int n = snprintf(line, sizeof(line), "native: ExitProcess(%#x) from:", (unsigned)code);
	for (int i = 0; i < 300 && n < (int)sizeof(line) - 60; i++) {
		uintptr_t v;
		if (IsBadReadPtr(sp + i, 4))
			break;
		v = sp[i];
		MEMORY_BASIC_INFORMATION mbi;
		if (v > 0x10000 && VirtualQuery((void *)v, &mbi, sizeof(mbi)) && mbi.Type == MEM_IMAGE && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
			char name[MAX_PATH] = "?";
			GetModuleFileNameA((HMODULE)mbi.AllocationBase, name, sizeof(name));
			const char *slash = strrchr(name, 0x5c);
			n += snprintf(line + n, sizeof(line) - n, " %s+%x", slash ? slash + 1 : name, (unsigned)(v - (uintptr_t)mbi.AllocationBase));
		}
	}
	log_printf("%s", line);
	exit_process_original(code);
}
static void (*die_original)(void);
static void die_hook(void)
{
	uintptr_t *sp = (uintptr_t *)__builtin_frame_address(0);
	char line[1500];
	int n = snprintf(line, sizeof(line), "native: the engine's fatal exit (rva 0x103690) called from:");
	for (int i = 0; i < 400 && n < (int)sizeof(line) - 40; i++) {
		if (IsBadReadPtr(sp + i, 4))
			break;
		uintptr_t v = sp[i];
		if (v >= (uintptr_t)base + 0x1000 && v < (uintptr_t)base + 0x2a0000)
			n += snprintf(line + n, sizeof(line) - n, " %x", (unsigned)(v - (uintptr_t)base));
	}
	log_printf("%s", line);
	die_original();
}
static DWORD WINAPI count_watch(LPVOID unused)
{
	(void)unused;
	AddVectoredExceptionHandler(1, count_veh);
	for (int round = 0; round < 40; round++) {
		log_printf("native: count watch: engine %d, copy %d at %p", (int)*(volatile int32_t *)(base + RVA_LOCAL_PLAYERS), (int)*(volatile int32_t *)&n_server_copy, (void *)&n_server_copy);
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		THREADENTRY32 te;
		te.dwSize = sizeof(te);
		for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
			if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId())
				continue;
			HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
			if (!th)
				continue;
			if (SuspendThread(th) != (DWORD)-1) {
				CONTEXT ctx;
				memset(&ctx, 0, sizeof(ctx));
				ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				if (GetThreadContext(th, &ctx) && ctx.Dr0 != (DWORD)(uintptr_t)(base + RVA_LOCAL_PLAYERS)) {
					ctx.Dr0 = (DWORD)(uintptr_t)(base + RVA_LOCAL_PLAYERS);
					ctx.Dr7 = (ctx.Dr7 & ~0xf0003u) | 1u | (1u << 16) | (3u << 18); // Dr0 enabled, write, 4 bytes
					SetThreadContext(th, &ctx);
				}
				ResumeThread(th);
			}
			CloseHandle(th);
		}
		CloseHandle(snap);
		Sleep(100);
	}
	return 0;
}
static void client_n_install(void)
{
	if (cfg("exitwatch", 0)) {
		uint8_t *fn = (uint8_t *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "ExitProcess");
		static const uint8_t prolog[5] = {0x55, 0x8b, 0xec, 0x6a, 0xff};
		if (!hook_function(fn, prolog, sizeof(prolog), (void *)exit_process_hook, (void **)&exit_process_original))
			log_printf("native: ExitProcess starts with %02x %02x %02x %02x %02x", fn[0], fn[1], fn[2], fn[3], fn[4]);
	}
	if (cfg("exitwatch", 0)) {
		static const uint8_t die_bytes[6] = {0x81, 0xec, 0x00, 0x08, 0x00, 0x00};
		if (!hook_function(base + XR(0x103690), die_bytes, sizeof(die_bytes), (void *)die_hook, (void **)&die_original))
			log_printf("native: the fatal exit function's bytes differ");
	}
	if (cfg("countwatch", 0))
		CreateThread(NULL, 0, count_watch, NULL, 0, NULL);
	int done = 0;
	int lo = cfg("n32lo", 0), hi = cfg("n32hi", 999); // QA bisect
	n_server_copy = *(int32_t *)(base + RVA_LOCAL_PLAYERS); // what the engine has now (see players_apply)
	for (size_t i = 0; i < sizeof(server_n_sites32) / sizeof(server_n_sites32[0]); i++) {
		if ((int)i < lo || (int)i > hi)
			continue;
		uint8_t *site = (uint8_t *)(uintptr_t)XV(server_n_sites32[i].va) - 0x400000 + (uintptr_t)base;
		uint32_t at;
		memcpy(&at, site + server_n_sites32[i].disp, 4);
		if (at != (uint32_t)(uintptr_t)(base + RVA_LOCAL_PLAYERS))
			continue; // not the instruction the build has
		absolute_patch(site, server_n_sites32[i].disp, &n_server_copy);
		done++;
	}
	log_printf("native: %d of %d server reads of the local player count point at a copy (%d now)", done, (int)(sizeof(server_n_sites32) / sizeof(server_n_sites32[0])), (int)n_server_copy);
}
static void list_players_install(void)
{
	uint8_t *site = base + XR(0x25cc10); // mov ebp, [count] in the entity list of the snapshot: the local players' own entities stay out of it
	uint32_t at;
	memcpy(&at, site + 2, 4);
	if (site[0] != 0x8b || site[1] != 0x2d || at != (uint32_t)(uintptr_t)(base + RVA_LOCAL_PLAYERS)) {
		log_printf("native: entity list patch: unexpected code");
		return;
	}
	absolute_patch(site, 2, &n_zero_copy);
}

void *n32_vm_blend_target;
void n32_scene2d_hook(void *vp);
void n32_final2d_hook(void *vp);
void n32_scene2d_hook(void *vp)
{
	scene2d_hook(vp);
}
void n32_final2d_hook(void *vp)
{
	final2d_hook(vp);
}
unsigned n32_dobj_make_hook(void *info, void *dobj_info);
unsigned n32_dobj_make_hook(void *info, void *dobj_info)
{
	return dobj_make_hook(info, dobj_info);
}
// the entity make (EDI = entity info, EBX = DObj info, result in EAX) and the first-person weapon's blend (the DObj in ESI, two stack arguments the caller cleans)
__asm__(".text\n"
	".globl _n32_dobj_make_stub\n"
	"_n32_dobj_make_stub:\n"
	"	pushl %ebx\n"
	"	pushl %edi\n"
	"	call _n32_dobj_make_hook\n"
	"	addl $8, %esp\n"
	"	ret\n"
	".globl _n32_vm_blend_stub\n"
	"_n32_vm_blend_stub:\n"
	"	cmpl $0, _native_active\n"
	"	je 1f\n"
	"	testl %esi, %esi\n"
	"	je 2f\n"
	"	cmpl $0, (%esi)\n"
	"	je 2f\n"
	"1:	jmp *_n32_vm_blend_target\n"
	"2:	ret\n"
	".globl _n32_scene2d_stub\n"
	"_n32_scene2d_stub:\n"
	"	pushl %esi\n"
	"	call _n32_scene2d_hook\n"
	"	addl $4, %esp\n"
	"	ret\n"
	".globl _n32_final2d_stub\n"
	"_n32_final2d_stub:\n"
	"	pushl %esi\n"
	"	call _n32_final2d_hook\n"
	"	addl $4, %esp\n"
	"	ret\n");
extern char n32_dobj_make_stub[], n32_vm_blend_stub[], n32_scene2d_stub[], n32_final2d_stub[];

static int hook_calls32(const uint32_t *sites, size_t n, void *hook, void **original, const char *what)
{
	void *dummy = NULL;
	for (size_t i = 0; i < n; i++)
		if (!jhook_call(base + XR(sites[i]), hook, original ? original : &dummy)) {
			log_printf("native: no call at rva 0x%x (%s)", XR(sites[i]), what);
			return 0;
		}
	return 1;
}

// the entity side (docs/32bit-native-map/entities.md): none of these places may lie in the engine's checked code (docs/32bit-plan.md)
static int hooks_apply32_entities(void)
{
	static const uint32_t ent_clean_sites[] = {0xc1c24, 0x59d71, 0x16e9b1, 0x9cb6a}; // (the fifth caller, 0x432872, is in checked code: left alone)
	static const uint32_t slot_sites[] = {0x40e0e, 0x667ff, 0x904ab, 0xa7e45, 0xba4e9}; // callers of the "mine" rule 0x4215a0 outside the checked code (pass2's own call is replaced with pass2)
	static const uint32_t make_sites[] = {0xa5a28, 0xa5ae7, 0xc1d35, 0xc1e73};
	static const uint32_t vm_setup_sites[] = {0xb2e52, 0xacea0, 0xb72a, 0xaf6c1, 0xaf719};
	static const uint32_t vm_blend_sites[] = {0x1723ec};
	static const uint32_t pass2_sites[] = {0x6685b};
	static const uint8_t add_entity_bytes32[] = {0x83, 0xec, 0x18, 0x53, 0x55};
	static const uint8_t own_write_bytes32[] = {0x83, 0xec, 0x10, 0x55, 0x8b, 0x6c, 0x24, 0x18};
	static const uint8_t dobj_add_bytes32[] = {0xd9, 0xee, 0x83, 0xec, 0x14};
	if (!hook_calls32(ent_clean_sites, sizeof(ent_clean_sites) / sizeof(ent_clean_sites[0]), ent_clean_hook, (void **)&ent_clean_original, "entity cleanup") ||
	    !hook_calls32(slot_sites, sizeof(slot_sites) / sizeof(slot_sites[0]), local_slot_hook, NULL, "local slot") ||
	    !hook_calls32(make_sites, sizeof(make_sites) / sizeof(make_sites[0]), n32_dobj_make_stub, &dobj_make_target, "DObj make") ||
	    !hook_calls32(vm_setup_sites, sizeof(vm_setup_sites) / sizeof(vm_setup_sites[0]), viewmodel_setup_hook, (void **)&viewmodel_setup_original, "view model setup") ||
	    !hook_calls32(vm_blend_sites, 1, n32_vm_blend_stub, &n32_vm_blend_target, "view model blend") ||
	    !hook_calls32(pass2_sites, 1, pass2_hook, NULL, "second entity pass"))
		return 0;
	if (!jhook_function(base + XR(0x6df50), add_entity_bytes32, sizeof(add_entity_bytes32), add_entity_hook, (void **)&add_entity_original) ||
	    !jhook_function(base + XR(0x904a0), own_write_bytes32, sizeof(own_write_bytes32), own_write_hook, (void **)&own_write_original) ||
	    !jhook_function(base + XR(0x103c00), dobj_add_bytes32, sizeof(dobj_add_bytes32), dobj_add_hook, (void **)&dobj_add_original)) {
		log_printf("native: entity function bytes differ");
		return 0;
	}
	log_printf("native: 32-bit entity hooks on");
	return 1;
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
		if (!jhook_call(base + XR(sites[i].site), sites[i].hook, sites[i].original)) {
			log_printf("native: no call at rva 0x%x, context hook %zu off", XR(sites[i].site), i);
			return 0;
		}
	if (cfg("entities", 1) && !hooks_apply32_entities())
		return 0;
	{ // the 2D texts of the HUD (docs/32bit-native-map/inputui.md): message windows, the title lines' element draw, the names over the other player
		static const uint8_t key_event_bytes32[] = {0x8b, 0x4c, 0x24, 0x0c, 0x53};
		uint8_t msgwin_bytes32[] = {0xa1, 0xc0, 0xa7, 0x86, 0x00}; // mov eax, [0x86a7c0]
#ifdef MW2COOP_IW4X
		{
			uint32_t at = XV(0x86a7c0);
			memcpy(msgwin_bytes32 + 1, &at, 4);
		}
#endif
		if (!jhook_function(base + XR(0xd2350), key_event_bytes32, sizeof(key_event_bytes32), key_event_hook, (void **)&key_event_original))
			log_printf("native: key event bytes differ");
		uint8_t hudelem_bytes32[] = {0xa1, 0x50, 0x86, 0x75, 0x00}; // mov eax, [0x758650]
#ifdef MW2COOP_IW4X
		{
			uint32_t at = XV(0x758650);
			memcpy(hudelem_bytes32 + 1, &at, 4);
		}
#endif
		if (!jhook_function(base + XR(0x713b0), msgwin_bytes32, sizeof(msgwin_bytes32), msgwin_hook, (void **)&msgwin_original))
			log_printf("native: game message window draw bytes differ");
		if (cfg("titles", 1) && !jhook_function(base + XR(0x164b40), hudelem_bytes32, sizeof(hudelem_bytes32), hudelem_hook, (void **)&hudelem_original))
			log_printf("native: HUD element draw bytes differ");
		if (cfg("namefix", 1)) {
			static const uint32_t name_site[] = {0x15eaf9};
			name_self_install();
			if (!hook_calls32(name_site, 1, name_get_hook, (void **)&name_get_original, "name lookup"))
				log_printf("native: no call at the name loop's name lookup");
		}
	}
	aim_for_client_fn = base + XR(0x15b040);
	{ // the aim assist's screen targets of each context's cgame and the server's target list of the client it is (docs/32bit-native-map/aimrumble.md 1.4, 1.5)
		static const uint32_t screen_site[] = {0x66adc}, list_site[] = {0x88ba8};
		static const uint32_t snd_listener_site[] = {0x66a20};
		if (!hook_calls32(screen_site, 1, screen_targets_hook, (void **)&screen_targets_original, "screen targets") ||
		    !hook_calls32(list_site, 1, target_list_hook, (void **)&target_list_original, "target list"))
			return 0;
		if (!hook_calls32(snd_listener_site, 1, snd_listener_hook, (void **)&snd_listener_original, "sound listener"))
			log_printf("native: the sound listener stays shared");
	}
	if (cfg("experiment", 0) >= 8) { // the console's way (mode 8 / 10): the 2D of the scene goes into layers of its own
		static const uint32_t scene2d_site[] = {0x1536fc}, final2d_site[] = {0x1537ae}, wait_site[] = {RVA_WAIT_ALPHA_SITE};
		if (!hook_calls32(scene2d_site, 1, n32_scene2d_stub, &scene2d_target, "scene 2D pass") || !hook_calls32(final2d_site, 1, n32_final2d_stub, &final2d_target, "frame 2D list") ||
		    !hook_calls32(wait_site, 1, wait_alpha_hook, (void **)&wait_alpha_original, "wait alpha"))
			return 0;
		log_printf("native: 32-bit 2D layer hooks on");
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

#ifdef _WIN64
static void contexts_install(void)
{
	static const struct {
		uint32_t site;
		void *hook;
		void **original;
	} sites[] = {
	    {0x2336c7, cg_init_all_hook, (void **)&cg_init_all_original},
	    {0x2343b8, snapshot_take_hook, (void **)&snapshot_take_original},
	    {0x2346ca, ps_publish_hook, (void **)&ps_publish_original},
	    {0x234a02, server_cmds_hook, (void **)&server_cmds_original},
	    {0x15ce84, usercmd_step_hook, (void **)&usercmd_step_original},
	    {0x234925, snapshot_msg_hook, (void **)&snapshot_msg_original},
	    {0x1fc331, client_frame_hook, (void **)&client_frame_original},
	    {0xbd093, hud_c6070_hook, (void **)&hud_c6070_original},
	    {0xbd09a, hud_c14e0_hook, (void **)&hud_c14e0_original},
	    {0xbd0a1, hud_94d70_hook, (void **)&hud_94d70_original},
	    {0xbd034, hud_c5b70_hook, (void **)&hud_c5b70_original},
	    {0xc7499, viewmodel_blend_hook, (void **)&viewmodel_blend_original},
	    {0xc41b1, viewmodel_setup_hook, (void **)&viewmodel_setup_original},
	    {0xc58ed, viewmodel_setup_hook, (void **)&viewmodel_setup_original},
	    {0xc594a, viewmodel_setup_hook, (void **)&viewmodel_setup_original},
	    {0xc5dfb, viewmodel_setup_hook, (void **)&viewmodel_setup_original},
	    {0xc6033, viewmodel_setup_hook, (void **)&viewmodel_setup_original},
	    {0xbd2c5, screen_targets_hook, (void **)&screen_targets_original},
	    {0x6a711, target_list_hook, (void **)&target_list_original},
	    {0xbd03b, hud_x0_hook, (void **)&hud_x_original[0]},
	    {0xbd0fb, hud_x1_hook, (void **)&hud_x_original[1]},
	    {0xbd102, hud_x2_hook, (void **)&hud_x_original[2]},
	    {0xbd109, hud_x3_hook, (void **)&hud_x_original[3]},
	    {0xbd130, hud_x4_hook, (void **)&hud_x_original[4]},
	    {0xbd1a4, hud_x5_hook, (void **)&hud_x_original[5]},
	    {0xbd206, hud_x6_hook, (void **)&hud_x_original[6]},
	    {0x94039, wait_alpha_hook, (void **)&wait_alpha_original},
	    {0xcfd23, mouse_read_hook, (void **)&mouse_read_original},
	    {0xcf8b1, make_cmd_hook, (void **)&make_cmd_original},
	    {0xb8d50, server_step_hook, (void **)&server_step_original},
	    {0xd0061, mouse_read2_hook, (void **)&mouse_read2_original},
	};
	int enabled = cfg("sites", 0x7fffffff);
	for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); i++)
		if ((enabled & (1 << i)) && !jhook_call(base + XR(sites[i].site), sites[i].hook, sites[i].original))
			log_printf("native: no call at rva 0x%x, context hook %zu off", XR(sites[i].site), i);
	swap_entities = cfg("swapent", 0); // the old workaround for context 1's "mine" (superseded by ownfix, local_slot_hook)
	skip_entity = cfg("skipent", 1); // context 1's own entity (player 2) is never drawn by the first pass either
	log_entities = cfg("entdraw", 0);
	{
		if (cfg("sndcount", 0)) {
			static const uint8_t snd_play_bytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20};
			snd_ctx_ptr = &ctx_current;
			if (!jhook_function(base + 0x23a9b0, snd_play_bytes, sizeof(snd_play_bytes), snd_count_stub, &snd_count_orig))
				{
				const uint8_t *t = base + 0x23a9b0;
				log_printf("native: sound play bytes differ: %02x %02x %02x %02x %02x %02x %02x %02x", t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7]);
			}
		}
		if (!jhook_function(base + 0x9c960, add_entity_bytes, sizeof(add_entity_bytes), add_entity_hook, (void **)&add_entity_original))
			log_printf("native: add entity bytes differ");
		if (!jhook_function(base + 0xb0680, pass2_bytes, sizeof(pass2_bytes), pass2_hook, (void **)&pass2_original))
			log_printf("native: second entity pass bytes differ");
		if (!jhook_function(base + 0xb86f0, dobj_make_bytes, sizeof(dobj_make_bytes), dobj_make_hook, (void **)&dobj_make_original))
			log_printf("native: client DObj make bytes differ");
		if (!jhook_call(base + 0x896d3, ik_hook, (void **)&ik_original))
			log_printf("native: no call at the foot adjustment");
		if (!jhook_function(base + 0x29a120, xfer_bytes, sizeof(xfer_bytes), xfer_hook, (void **)&xfer_original))
			log_printf("native: DObj transfer bytes differ");
		if (!jhook_function(base + 0x2bb0, dobj_add_bytes, sizeof(dobj_add_bytes), dobj_add_hook, (void **)&dobj_add_original))
			log_printf("native: dobj add bytes differ");
		if (!jhook_function(base + 0xb28b0, own_write_bytes, sizeof(own_write_bytes), own_write_hook, (void **)&own_write_original))
			log_printf("native: own entity write bytes differ");
		if (!jhook_function(base + 0xa5ec0, slot_bytes, sizeof(slot_bytes), local_slot_hook, (void **)&slot_original))
			log_printf("native: local slot function bytes differ");
		if (!jhook_function(base + 0xcb4f0, msgwin_bytes, sizeof(msgwin_bytes), msgwin_hook, (void **)&msgwin_original))
			log_printf("native: game message window draw bytes differ");
		if (!jhook_call(base + 0x1fbc1d, key_event_hook, (void **)&key_event_original))
			log_printf("native: no call at the event loop's key event");
		if (cfg("titles", 1) && !jhook_function(base + 0xa3b80, hudelem_bytes, sizeof(hudelem_bytes), hudelem_hook, (void **)&hudelem_original))
			log_printf("native: HUD element draw bytes differ");
		if (cfg("namefix", 1)) {
			name_self_install();
			if (!jhook_call(base + 0x93e2f, name_get_hook, (void **)&name_get_original))
				log_printf("native: no call at the name loop's name lookup");
		}
	}
	p1pad = cfg("p1pad", -1);
	pad_dev[0] = cfg("p0pad", -1);
	rumble_set_log(cfg("rumblelog", 0));
	skip_hud_mask = cfg("skiphud", 0);
	skip_ctx = cfg("skipctx", 1);
	if (cfg("entclean", 1) &&
	    !jhook_function(base + 0xb9320, ent_clean_bytes, sizeof(ent_clean_bytes), ent_clean_hook, (void **)&ent_clean_original))
		log_printf("native: entity cleanup bytes differ");
	ctx_on = 1;
	log_printf("native: client contexts on (call sites redirected)");
}
#endif

// The scripts decide "co-op" from the dvar `coop` (rva 0x1905308, a bool whose script-side writes are refused); the engine's own co-op byte (rva 0x52fb5c) stays 0 so that the engine takes its
// local-players branch. [native] coopdvar=1 keeps the dvar at 1 (written from a thread, every 20 ms) so that the level scripts, the mod's among them, run their two-player code.
static volatile LONG coopdvar_on; // the thread below writes only while a split session runs
static DWORD WINAPI coop_dvar_thread(LPVOID unused)
{
	(void)unused;
	for (;;) {
		uint8_t *dvar = *(uint8_t **)(base + RVA_COOP_DVAR);
		if (dvar && coopdvar_on)
			*(int *)(dvar + 0x10) = 1;
		Sleep(20);
	}
	return 0;
}

static int hooks_installed;
static int hooks_ok;

// What makes the game a two local players game: put back by native_deactivate() (journal group 1)
static int players_apply(int n)
{
	uint8_t *at = base + RVA_LOCAL_PLAYERS_STORE;
	if (at[0] != 0xc7 || at[1] != 0x05 || at[6] != 1 || at[7] || at[8] || at[9]) {
		log_printf("native: unexpected code at the local players store, players=%d ignored", n);
		return 0;
	}
	DWORD old;
	jgroup = 1;
#ifdef _WIN64
	j_save(at + 6, 1);
	if (!VirtualProtect(at + 6, 1, PAGE_EXECUTE_READWRITE, &old)) {
		log_printf("native: VirtualProtect failed (%lu)", GetLastError());
		jgroup = 0;
		return 0;
	}
	at[6] = (uint8_t)n;
	VirtualProtect(at + 6, 1, old, &old);
#else
	(void)old;
	// 32 bits: the engine's own count stays what it is (1: the client code, which cannot be patched, reads it); the store in G_InitGame gets code after it that sets the variable
	// the server's reads were pointed at (client_n_install) to n. The store itself (mov dword [count], 1) is copied into the code as it is.
	{
		uint32_t copy_at = (uint32_t)(uintptr_t)&n_server_copy;
		uint32_t nn = (uint32_t)n;
		uint8_t cave[25] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xc7, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0xe9, 0, 0, 0, 0};
		memcpy(cave, at, 10);
		memcpy(cave + 12, &copy_at, 4);
		memcpy(cave + 16, &nn, 4);
		uint8_t *stub = hook_code(cave, sizeof(cave));
		if (!stub) {
			jgroup = 0;
			return 0;
		}
		// the jump back is relative to where the page is: build it in place
		DWORD o2;
		VirtualProtect(stub, sizeof(cave), PAGE_EXECUTE_READWRITE, &o2);
		int32_t rel = (int32_t)((uintptr_t)(at + 10) - (uintptr_t)(stub + 25));
		memcpy(stub + 21, &rel, 4);
		VirtualProtect(stub, sizeof(cave), PAGE_EXECUTE_READ, &o2);
		FlushInstructionCache(GetCurrentProcess(), stub, sizeof(cave));
		if (!(cfg("n32skip", 0) & 1))
			jhook_jump(at, 10, stub, 0x90);
	}
#endif
	log_printf("native: the number of local players is %d (patched G_InitGame)", n);
#ifdef _WIN64
	jhook_jump(base + RVA_SV_ADD_COMMAND, 14, sv_add_command, 0xcc);
#else
	if (!(cfg("n32skip", 0) & 2))
		jhook_jump(base + RVA_SV_ADD_COMMAND, 5, sv_add_command, 0xcc);
#endif
#ifndef _WIN64
	if (!(cfg("n32skip", 0) & 4))
#endif
	client_n_install();
	if (cfg("listplayers", 0))
		list_players_install();
	jgroup = 0;
	log_printf("native: server commands to client 1 are dropped (no client behind it yet)");
	return 1;
}

// The hooks of the client side: installed once, they pass everything through while native_active is 0 (journal group 0)
#ifndef _WIN64
static int hooks_apply32_contexts(void);
static DWORD WINAPI coop_dvar_thread(LPVOID unused);
#endif
#ifndef _WIN64
// The 32-bit port, step 1 (docs/32bit-native-map/): mode 2 only, the alternate frames and the compositor with the one player in both halves: [native] experiment=2 in settings.ini.
static int hooks_apply32(void)
{
	if (hooks_installed)
		return hooks_ok;
	hooks_installed = 1;
	jgroup = 0;
	uint8_t *site = base + RVA_BEGIN_FRAME_CALL;
	if (!jhook_call(site, (void *)begin_frame_hook, (void **)&begin_frame_original) ||
	    !jhook_function(base + RVA_CG_DRAW, cg_draw_bytes, sizeof(cg_draw_bytes), cg_draw_hook, (void **)&cg_draw_original)) {
		log_printf("native: unexpected code at the cgame draw or its begin-frame call, the 32-bit split screen is off");
		return 0;
	}
	if (cfg("experiment", 0) >= 5 && cfg("ctx", 1) && !hooks_apply32_contexts())
		return 0;
	if (cfg("experiment", 0) >= 5)
		CreateThread(NULL, 0, coop_dvar_thread, NULL, 0, NULL);
	hooks_ok = 1;
	return 1;
}
#endif

static int hooks_apply(void)
{
#ifndef _WIN64
	return hooks_apply32();
}
#else
	if (hooks_installed)
		return hooks_ok;
	hooks_installed = 1;
	jgroup = 0;
	if (cfg("ctx", 1))
		contexts_install();
	if (cfg("logcbuf", 0) && !jhook_function(base + 0x1ee890, cbuf_bytes, sizeof(cbuf_bytes), cbuf_hook, (void **)&cbuf_original))
		log_printf("native: Cbuf_AddText bytes differ");
	if (!jhook_function(base + RVA_BEGIN_FRAME, begin_frame_bytes, sizeof(begin_frame_bytes), begin_frame_hook, (void **)&begin_frame_original) ||
	    !jhook_function(base + RVA_CG_DRAW, cg_draw_bytes, sizeof(cg_draw_bytes), cg_draw_hook, (void **)&cg_draw_original)) {
		log_printf("native: unexpected code at the cgame draw or begin frame, the split screen is off");
		return 0;
	}
	if (*(base + 0x5c5bf) != 0xe8 || !jhook_call(base + 0x5c5bf, (void *)scene2d_hook, (void **)&scene2d_original)) {
		log_printf("native: unexpected code at the scene's 2D pass, mode 10 is off");
		return 0;
	}
	if (*(base + 0x4545b) != 0xe8 || !jhook_call(base + 0x4545b, (void *)final2d_hook, (void **)&final2d_original)) {
		log_printf("native: unexpected code at the frame's 2D command list, mode 10 is off");
		return 0;
	}
	CreateThread(NULL, 0, coop_dvar_thread, NULL, 0, NULL);
	hooks_ok = 1;
	return 1;
}
#endif

// the client state of a session: context 1 starts again with every level
static void session_reset(void)
{
	if (ctx_current)
		ctx_swap(0);
	ctx1_ready = 0;
	cg_init_done = 0;
	draws_after_init = 0;
	split_on = 0;
	frame_bottom = 0;
	place_pop();
	video_pop();
}

static int activate(int players)
{
	if (native_active)
		return 1;
	if (players < 2 || players > 4)
		return 0;
	cfg_gen++;
	mode = cfg("experiment", 10);
#ifndef _WIN64
	if (mode != 5 && mode != 10)
		mode = 5; // 32 bits: modes 5 and 10 (the others are experiments of the 64-bit port's history)
#endif
	if (!hooks_apply() || !players_apply(players)) {
		mode = 0;
		return 0;
	}
	session_reset();
	ctx_on = 1;
	InterlockedExchange(&coopdvar_on, cfg("coopdvar", 1));
	native_active = 1;
	log_printf("native: split screen session on (%d local players, mode %d%s)", players, mode, from_menu ? ", from the menu" : "");
	return 1;
}

// Console commands for the QA bots and for the lobby: mw2coop_native_start [pad], mw2coop_native_stop, mw2coop_native_launch <map> [pad] [start] (session on, then the map)
static void native_start_f(void)
{
	const char *a = game_cmd_argv(1);
	native_split_start(a[0] ? atoi(a) : -1);
}
static void native_stop_f(void)
{
	native_split_stop();
}
static void native_launch_f(void)
{
	char map[64];
	lstrcpynA(map, game_cmd_argv(1), sizeof(map));
	const char *a = game_cmd_argv(2);
	if (!map[0] || strpbrk(map, " \t;\"\n"))
		return;
	if (!native_split_start(a[0] ? atoi(a) : -1))
		return;
	char text[200], start[64];
	lstrcpynA(start, game_cmd_argv(3), sizeof(start));
	if (strpbrk(start, " \t;\"\n"))
		start[0] = 0;
	snprintf(text, sizeof(text), "set start \"%s\"\nset coop_mapName %s\nmap %s\n", start, map, map);
	game_cbuf_addtext(text);
}
// QA: mw2coop_press <binding> <1|0> presses or releases a game button ("speed_throw", "forward", "attack") for player 0 through the engine's key handler, as the pad code does for player 1
// (console "+forward" does not move a player without a key number)
static void native_press_f(void)
{
	char name[40];
	lstrcpynA(name, game_cmd_argv(1), sizeof(name));
	log_printf("native: press %s %s (context %d live)", name, game_cmd_argv(2), ctx_current);
	if (!name[0] || ctx_current != 0)
		return;
	pad_binding(name, game_cmd_argv(2)[0] != '0');
}
static void native_qa_move_f(void)
{
	qa_c0_fwd = game_cmd_argv(1)[0] == '1';
	qa_c0_fire = game_cmd_argv(2)[0] == '1';
}
static cmd_function_t native_commands[] = {
	{NULL, "mw2coop_qa_move", NULL, NULL, native_qa_move_f CMD_NODE_FLAG},
	{NULL, "mw2coop_press", NULL, NULL, native_press_f CMD_NODE_FLAG},
	{NULL, "mw2coop_native_start", NULL, NULL, native_start_f CMD_NODE_FLAG},
	{NULL, "mw2coop_native_stop", NULL, NULL, native_stop_f CMD_NODE_FLAG},
	{NULL, "mw2coop_native_launch", NULL, NULL, native_launch_f CMD_NODE_FLAG},
};

// For padglyphs.c: the player whose half's texts are being made (his context live), if he plays on a pad; *tactical: his layout
static int text_pad_player(int *tactical)
{
	if (!native_active || ctx_current < 0 || ctx_current > 1 || pad_dev[ctx_current] < 0)
		return -1;
	padopts_t o;
	padopts_get(ctx_current, &o);
	*tactical = o.tactical;
	return ctx_current;
}

void native_install(void)
{
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\settings.ini");
	lstrcpyA(settings_path, path);
	base = (uint8_t *)GetModuleHandleA(NULL);
#ifndef _WIN64
	// the 32-bit port is switched on piece by piece: experiment=2 (alternate frames, compositor), then players=2 with experiment=5 (two client contexts)
	if (cfg("players", 0) < 2 && cfg("experiment", 0) == 2) {
		mode = 2;
		hooks_apply32();
		log_printf("native: 32-bit experiment 2 %s", hooks_ok ? "on" : "failed");
		return;
	}
#endif
	for (size_t i = 0; i < sizeof(native_commands) / sizeof(native_commands[0]); i++)
		game_add_command(&native_commands[i]);
	padglyphs_install(text_pad_player);
#ifdef _WIN64
	if (cfg("treelog", 0) && cfg("players", 0) < 2 && !jhook_function(base + 0x2bb0, dobj_add_bytes, sizeof(dobj_add_bytes), dobj_add_hook, (void **)&dobj_add_original))
		log_printf("native: dobj add bytes differ (treelog in the plain game)");
#endif
	if (cfg("smoothlog", 0) && cfg("players", 0) < 2 && cfg("experiment", 0) < 2) { // plain game, for comparison: only the hooks, no session
		mode = 0;
		hooks_apply();
		log_printf("native: smoothlog in the plain game");
		return;
	}
	// the QA runs: settings.ini [native] players=2 starts the session at once (the menu's button does it later, native_split_start)
	int players = cfg("players", 0);
	if (players >= 2)
		activate(players);
}

const char *native_crash_state(void)
{
	static char text[96];
	snprintf(text, sizeof(text), "session %d, context %d live, context 1 ready %d, split %d, frame_bottom %d", native_active, ctx_current, ctx1_ready, split_on, frame_bottom);
	return text;
}

int native_split_available(void)
{
	return base != NULL;
}

// One key press for the split screen lobby's pad (menus.c mw2coop_menukey): the engine's key event, down and up
void native_menu_key(int key)
{
	unsigned t = (unsigned)GetTickCount();
#ifdef _WIN64
	((void (*)(int, int, int, unsigned))(base + 0xd0f80))(0, key, 1, t);
	((void (*)(int, int, int, unsigned))(base + 0xd0f80))(0, key, 0, t);
#else
	if (!key_event_original)
		return;
	key_event_original(0, key, 1, t);
	key_event_original(0, key, 0, t);
	cbuf_run_now();
#endif
}

int native_split_active(void)
{
	return native_active;
}

// The Start of a split screen lobby: the game becomes a two local players game; the caller launches the mission. dev0 / dev1: the players' devices (an XInput pad 0-3,
// -1 the keyboard and mouse)
int native_split_start2(int dev0, int dev1)
{
	if (native_active)
		return 1;
	from_menu = 1;
	menu_pad0 = dev0;
	menu_pad = dev1;
	pad_dev[0] = dev0;
	p1pad = dev1;
	int ok = activate(2);
	if (ok && !cfg("waitmsg", 0)) { // both players are on this PC: nothing to wait for, not even on the loading screen (set now: an asset lookup does not go while a level loads)
		localize_override("COOP_WAITINGFORPLAYER", " ");
		localize_override("SPECIAL_OPS_WAITING_OTHER_PLAYER", " ");
	}
	return ok;
}
int native_split_start(int pad)
{
	return native_split_start2(-1, pad);
}

// The session ends (back in the menus): the patches come out and the game is what it was
void native_split_stop(void)
{
	if (!native_active)
		return;
	localize_override("COOP_WAITINGFORPLAYER", NULL);
	localize_override("SPECIAL_OPS_WAITING_OTHER_PLAYER", NULL);
	hud_layout_tick(0); // the HUD menus get the full screen layout back
	native_active = 0;
	mode = 0;
	ctx_on = 0;
	InterlockedExchange(&coopdvar_on, 0);
	Sleep(60); // the forcing thread has written its last value
	{
		uint8_t *dvar = *(uint8_t **)(base + RVA_COOP_DVAR); // the scripts' co-op dvar goes back to 0 (the game's own co-op sets it itself)
		if (dvar)
			*(int *)(dvar + 0x10) = 0;
	}
	session_reset();
	journal_restore(1);
	rumble_reset();
	log_printf("native: split screen session over, the game's own player count is back");
}
