// Pictures of a pad's buttons in the game's texts, as the console game shows them: a hint's "[{+usereload}]" names the key bound to the
// command (rva 0xd0e40: local client, command, two names of 0x80 chars; called at 0x2513ce by the text localizer and at 0x1913fa by the
// scripts' GetKeyBinding). For a player on a pad (his context is live while his half's texts are made) it gives a marker of the pad's
// button for that command in his layout instead: ESC and a letter, two bytes that pass through every string copy. Right before a text
// is drawn (the render thread's text command, rva 0x44b20, calls the text draw 0x433b0 at 0x44c12 with the text in rcx), a marker
// becomes the engine's own inline picture: '^', 1, width, height and the Material * (8 bytes: zeros in it, so it never goes through a
// string copy), the brackets around it dropped. The text draw scales width and height (bytes) as ((n - 16) * font height + 16) / 32.
// The pictures are drawn here into textures of copies of the "white" material (its texture table +0x60, 16-byte entries with the
// GfxImage * at +8; the GfxImage's first field the IDirect3DTexture9 *, width and height u16 at +0x18).
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "padglyphs.h"

#ifdef _WIN64
#define RVA_DB_FIND 0x0ef5d0 // void *DB_FindXAssetHeader(int type, const char *name)
#define ASSET_MATERIAL 5
#define MATERIAL_TABLE 0x60
#define ICON 64
#define ICON_SIZE 54 // the text draw's size byte: (54 - 16) / 32 of the font's height ~ 1.2

enum { B_A, B_B, B_X, B_Y, B_LB, B_RB, B_LT, B_RT, B_LS, B_RS, B_BACK, B_START, B_UP, B_DOWN, B_LEFT, B_RIGHT, BUTTONS };
static const char *const fallback[BUTTONS] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back", "Start", "Up", "Down", "Left", "Right"};

// the game's commands and the pad buttons they are on (the layout of native.c's pad_frame_player; tactical: B and the right stick change places)
static int button_of(const char *cmd, int tactical)
{
	static const struct {
		const char *cmd;
		int button;
	} map[] = {
	    {"gostand", B_A},      {"raisestance", B_A},   {"stance", B_B},       {"movedown", B_B},          {"togglecrouch", B_B},
	    {"toggleprone", B_B},  {"prone", B_B},         {"lowerstance", B_B},  {"usereload", B_X},         {"activate", B_X},
	    {"reload", B_X},       {"weapnext", B_Y},      {"weapprev", B_Y},     {"smoke", B_LB},            {"frag", B_RB},
	    {"breath_sprint", B_LS}, {"sprint", B_LS},     {"holdbreath", B_LS},  {"melee", B_RS},            {"melee_breath", B_RS},
	    {"scores", B_BACK},    {"togglemenu", B_START}, {"attack", B_RT},     {"speed_throw", B_LT},      {"toggleads_throw", B_LT},
	    {"speed", B_LT},       {"actionslot 1", B_UP}, {"actionslot 2", B_DOWN}, {"actionslot 3", B_LEFT}, {"actionslot 4", B_RIGHT},
	};
	if (*cmd == '+' || *cmd == '-')
		cmd++;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (!_stricmp(cmd, map[i].cmd)) {
			int b = map[i].button;
			if (tactical && b == B_B)
				return B_RS;
			if (tactical && b == B_RS)
				return B_B;
			return b;
		}
	return -1;
}

static uint8_t *base;
static int (*pad_player_of)(int *);
static void *materials[BUTTONS];

// ---- the pictures (64 x 64, drawn with distance fields, 4 x 4 samples a pixel; the letters by GDI)
static float sd_rrect(float x, float y, float cx, float cy, float hw, float hh, float r)
{
	float qx = fabsf(x - cx) - hw + r, qy = fabsf(y - cy) - hh + r;
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
	return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

// the shape of a button: its distance (negative inside) and the fill colour; *lit: a d-pad arm drawn light
static float shape(int b, float x, float y, uint32_t *fill, int *lit)
{
	static const uint32_t face[4] = {0x2f9e36, 0xd2392c, 0x2a6fd6, 0xe6b512}; // A green, B red, X blue, Y yellow
	*lit = 0;
	*fill = 0x2b2b2b;
	if (b <= B_Y) {
		*fill = face[b];
		return sqrtf((x - 32) * (x - 32) + (y - 32) * (y - 32)) - 29;
	}
	if (b == B_LB || b == B_RB)
		return sd_rrect(x, y, 32, 32, 30, 15, 7);
	if (b == B_LT || b == B_RT)
		return sd_rrect(x, y, 32, 33, 21, 28, 11);
	if (b == B_LS || b == B_RS)
		return sqrtf((x - 32) * (x - 32) + (y - 32) * (y - 32)) - 28;
	if (b == B_BACK || b == B_START)
		return sd_rrect(x, y, 32, 32, 29, 17, 17);
	// the d-pad: a cross, the pressed arm lit
	float v = sd_rrect(x, y, 32, 32, 10, 29, 3), h = sd_rrect(x, y, 32, 32, 29, 10, 3);
	float d = v < h ? v : h;
	if (d < 0)
		*lit = (b == B_UP && y < 22) || (b == B_DOWN && y > 42) || (b == B_LEFT && x < 22) || (b == B_RIGHT && x > 42);
	return d;
}

// the label's coverage, rendered by GDI (antialiased, bold)
static void label_mask(const wchar_t *text, int px, float mask[ICON * ICON])
{
	memset(mask, 0, sizeof(float) * ICON * ICON);
	HDC dc = CreateCompatibleDC(NULL);
	BITMAPINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader), bi.bmiHeader.biWidth = ICON, bi.bmiHeader.biHeight = -ICON, bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
	void *bits = NULL;
	HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!dc || !bmp || !bits) {
		if (bmp)
			DeleteObject(bmp);
		if (dc)
			DeleteDC(dc);
		return;
	}
	HGDIOBJ old_bmp = SelectObject(dc, bmp);
	memset(bits, 0, ICON * ICON * 4);
	HFONT font = CreateFontW(-px, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Arial");
	HGDIOBJ old_font = SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	RECT r = {0, 0, ICON, ICON + 1};
	DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	GdiFlush();
	for (int i = 0; i < ICON * ICON; i++)
		mask[i] = ((const uint8_t *)bits)[i * 4 + 1] / 255.0f;
	SelectObject(dc, old_font);
	SelectObject(dc, old_bmp);
	DeleteObject(font);
	DeleteObject(bmp);
	DeleteDC(dc);
}

static void draw_icon(int b, uint8_t *bits, int pitch)
{
	static const wchar_t *const labels[BUTTONS] = {L"A", L"B", L"X", L"Y", L"LB", L"RB", L"LT", L"RT", L"LS", L"RS", L"\x25c0", L"\x25b6", L"", L"", L"", L""};
	static const int sizes[BUTTONS] = {40, 40, 40, 40, 22, 22, 22, 22, 22, 22, 22, 22, 0, 0, 0, 0};
	static float mask[ICON * ICON];
	label_mask(labels[b], sizes[b] ? sizes[b] : 10, mask);
	for (int py = 0; py < ICON; py++) {
		uint32_t *row = (uint32_t *)(bits + py * pitch);
		for (int px = 0; px < ICON; px++) {
			float r = 0, g = 0, bl = 0, a = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++) {
					uint32_t fill;
					int lit;
					float d = shape(b, px + (sx + 0.5f) / 4, py + (sy + 0.5f) / 4, &fill, &lit);
					if (d > 1.5f)
						continue;
					uint32_t c = d > -1.5f ? 0xdedede : lit ? 0xf2f2f2 : fill; // a light rim, then the fill
					if (d > 0) // a dark edge outside the rim keeps it apart from a light background
						c = 0x101010;
					r += (c >> 16 & 255) / 255.0f, g += (c >> 8 & 255) / 255.0f, bl += (c & 255) / 255.0f, a += 1;
				}
			if (a > 0)
				r /= a, g /= a, bl /= a;
			a /= 16;
			float m = sizes[b] ? mask[py * ICON + px] : 0; // the label in white over it
			r += (1 - r) * m, g += (1 - g) * m, bl += (1 - bl) * m;
			uint32_t A = (uint32_t)(a * 255 + 0.5f), R = (uint32_t)(r * 255 + 0.5f), G = (uint32_t)(g * 255 + 0.5f), B = (uint32_t)(bl * 255 + 0.5f);
			row[px] = A << 24 | R << 16 | G << 8 | B;
		}
	}
}

// the materials, made once the "white" material's texture is there (main thread)
static void make_materials(void)
{
	static int failed;
	if (materials[0] || failed > 3)
		return;
	uint8_t *white = ((uint8_t * (*)(int, const char *))(base + RVA_DB_FIND))(ASSET_MATERIAL, "white");
	uint8_t *table = white ? *(uint8_t **)(white + MATERIAL_TABLE) : NULL;
	uint8_t *image = table ? *(uint8_t **)(table + 8) : NULL;
	IDirect3DTexture9 *tex0 = image ? *(IDirect3DTexture9 **)image : NULL;
	IDirect3DDevice9 *dev = NULL;
	if (!tex0 || FAILED(IDirect3DTexture9_GetDevice(tex0, &dev))) {
		failed++;
		return;
	}
	static char names[BUTTONS][24];
	for (int b = 0; b < BUTTONS; b++) {
		IDirect3DTexture9 *tex = NULL;
		D3DLOCKED_RECT lr;
		if (FAILED(IDirect3DDevice9_CreateTexture(dev, ICON, ICON, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL)) ||
		    FAILED(IDirect3DTexture9_LockRect(tex, 0, &lr, NULL, 0))) {
			if (tex)
				IDirect3DTexture9_Release(tex);
			continue;
		}
		draw_icon(b, (uint8_t *)lr.pBits, lr.Pitch);
		IDirect3DTexture9_UnlockRect(tex, 0);
		uint8_t *mat = HeapAlloc(GetProcessHeap(), 0, 0x100), *tab = HeapAlloc(GetProcessHeap(), 0, 0x40), *img = HeapAlloc(GetProcessHeap(), 0, 0x80);
		memcpy(mat, white, 0x100); // (the copies read a little over the ends, as the menus' copies do)
		memcpy(tab, table, 0x40);
		memcpy(img, image, 0x80);
		*(IDirect3DTexture9 **)img = tex;
		*(uint16_t *)(img + 0x18) = ICON, *(uint16_t *)(img + 0x1a) = ICON;
		for (int i = 0; i < 4; i++)
			if (*(uint8_t **)(tab + i * 16 + 8) == image)
				*(uint8_t **)(tab + i * 16 + 8) = img;
		*(uint8_t **)(mat + MATERIAL_TABLE) = tab;
		lstrcpynA(names[b], "mw2coop_btn_", sizeof(names[b]));
		lstrcpynA(names[b] + 12, fallback[b], sizeof(names[b]) - 12);
		*(const char **)mat = names[b];
		materials[b] = mat;
	}
	IDirect3DDevice9_Release(dev);
	log_printf("padglyphs: %d button pictures made", materials[0] ? BUTTONS : 0);
}

static unsigned (*binding_original[2])(int64_t, const char *, char *);
static unsigned binding(int which, int64_t lc, const char *cmd, char *out)
{
	int tactical = 0;
	int pl = pad_player_of && cmd ? pad_player_of(&tactical) : -1;
	int b = pl >= 0 ? button_of(cmd, tactical) : -1;
	if (b < 0)
		return binding_original[which](lc, cmd, out);
	make_materials();
	out[0] = 0x1b, out[1] = (char)('a' + b), out[2] = 0;
	out[0x80] = 0;
	return 1;
}
static unsigned binding0(int64_t lc, const char *cmd, char *out)
{
	return binding(0, lc, cmd, out);
}
static unsigned binding1(int64_t lc, const char *cmd, char *out)
{
	return binding(1, lc, cmd, out);
}

// render thread: a text about to be drawn, its markers made pictures (the brackets around one dropped)
__attribute__((used)) const char *padglyphs_text(const char *text)
{
	static char buf[2][2048];
	static int at;
	if (!text || !strchr(text, 0x1b))
		return text;
	char *out = buf[at ^= 1];
	size_t n = 0;
	for (const char *p = text; *p && n < sizeof(buf[0]) - 16;) {
		if (p[0] == 0x1b && p[1] >= 'a' && p[1] < 'a' + BUTTONS) {
			int b = p[1] - 'a';
			p += 2;
			if (n && out[n - 1] == '[' && *p == ']')
				n--, p++;
			if (n >= 2 && out[n - 2] == '^' && out[n - 1] >= '0' && out[n - 1] <= '9')
				n -= 2; // the key's highlight colour ("^3", the localizer's): it would tint the picture
			if (materials[b]) {
				out[n++] = '^', out[n++] = 1, out[n++] = ICON_SIZE, out[n++] = ICON_SIZE;
				memcpy(out + n, &materials[b], 8);
				n += 8;
			} else
				for (const char *s = fallback[b]; *s; s++)
					out[n++] = *s;
			continue;
		}
		out[n++] = *p++;
	}
	out[n] = 0;
	return out;
}

// the stub at the text command's call of the text draw: the text (rcx) through padglyphs_text, every other argument (rdx, r8, r9, xmm0-3, the stack) as it was
__attribute__((used)) static void *draw_original;
void padglyphs_draw_stub(void);
__asm__(".text\n"
	".globl padglyphs_draw_stub\n"
	"padglyphs_draw_stub:\n"
	"\tpush %rdx\n"
	"\tpush %r8\n"
	"\tpush %r9\n"
	"\tpush %rcx\n"
	"\tsub $0x68, %rsp\n"
	"\tmovdqu %xmm0, 0x20(%rsp)\n"
	"\tmovdqu %xmm1, 0x30(%rsp)\n"
	"\tmovdqu %xmm2, 0x40(%rsp)\n"
	"\tmovdqu %xmm3, 0x50(%rsp)\n"
	"\tcall padglyphs_text\n"
	"\tmovdqu 0x20(%rsp), %xmm0\n"
	"\tmovdqu 0x30(%rsp), %xmm1\n"
	"\tmovdqu 0x40(%rsp), %xmm2\n"
	"\tmovdqu 0x50(%rsp), %xmm3\n"
	"\tadd $0x68, %rsp\n"
	"\tpop %rcx\n"
	"\tpop %r9\n"
	"\tpop %r8\n"
	"\tpop %rdx\n"
	"\tmov %rax, %rcx\n"
	"\tjmp *draw_original(%rip)\n");

void padglyphs_install(int (*pad_player)(int *tactical))
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	pad_player_of = pad_player;
	// the draw first: without it a marker would be drawn as two letters
	int ok = hook_call(base + 0x44c12, (void *)padglyphs_draw_stub, &draw_original);
	if (ok) {
		ok &= hook_call(base + 0x2513ce, (void *)binding0, (void **)&binding_original[0]);
		ok &= hook_call(base + 0x1913fa, (void *)binding1, (void **)&binding_original[1]);
	}
	log_printf("padglyphs: %s", ok ? "pad buttons as pictures in texts" : "a call site is not as expected, some texts keep key names");
}
#else
void padglyphs_install(int (*pad_player)(int *tactical))
{
	(void)pad_player;
}
#endif
