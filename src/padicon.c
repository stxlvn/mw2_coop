// A gamepad picture for the split screen lobby's player cards. The game has the keyboard and mouse one only ("pc_keyboard_mouse";
// no gamepad material in its zones, probed by name), so a gamepad is drawn here, in the same style (a black body with a white outline),
// into a texture made on the game's own device, and given to a copy of the keyboard's material: Material -> textureTable (+0x60,
// MaterialTextureDef of 16 bytes, GfxImage * at +8) -> GfxImage, whose first field is the IDirect3DTexture9 * (as fonts.c found).
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "padicon.h"

#ifdef _WIN64
#define MATERIAL_TABLE 0x60
#else
#define MATERIAL_TABLE 0x54 // 32 bits (fonts.c): Material.textureTable
#endif

static float sd_circle(float x, float y, float cx, float cy, float r)
{
	return sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
}
static float sd_rrect(float x, float y, float cx, float cy, float hw, float hh, float r)
{
	float qx = fabsf(x - cx) - hw + r, qy = fabsf(y - cy) - hh + r;
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
	float in = qx > qy ? qx : qy;
	return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}
static float sd_capsule(float x, float y, float ax, float ay, float bx, float by, float r)
{
	float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
	float h = (px * dx + py * dy) / (dx * dx + dy * dy);
	h = h < 0 ? 0 : h > 1 ? 1 : h;
	px -= dx * h, py -= dy * h;
	return sqrtf(px * px + py * py) - r;
}
// a union with rounded joins (k: how far the rounding reaches)
static float smooth_min(float a, float b, float k)
{
	float h = 0.5f + 0.5f * (b - a) / k;
	h = h < 0 ? 0 : h > 1 ? 1 : h;
	return b + (a - b) * h - k * h * (1 - h);
}

// The gamepad's layout, in units where its body is about 1 high: laid out like an Xbox 360 pad seen from above (left stick and face
// buttons up, d-pad and right stick down)
#define PAD_W 1.56f // the layout's width
#define PAD_TOP 0.11f // its top and bottom, outline and shadow included
#define PAD_BOTTOM 0.99f

static float pad_body(float x, float y)
{
	const float c = PAD_W / 2;
	float d = sd_rrect(x, y, c, 0.39f, 0.52f, 0.25f, 0.2f);
	d = smooth_min(d, sd_capsule(x, y, c - 0.42f, 0.42f, c - 0.55f, 0.78f, 0.17f), 0.08f);
	return smooth_min(d, sd_capsule(x, y, c + 0.42f, 0.42f, c + 0.55f, 0.78f, 0.17f), 0.08f);
}

// a stick: a light ring with a grey cap
static int stick(float x, float y, float cx, float cy, float *grey)
{
	float d = sd_circle(x, y, cx, cy, 0.095f);
	if (d >= 0)
		return 0;
	*grey = d > -0.024f ? 0.9f : sd_circle(x, y, cx, cy, 0.045f) < 0 ? 0.62f : 0.1f;
	return 1;
}

// One sample: x, y in the layout's units; out: grey (0..1) and opacity. Drawn like the game's keyboard and mouse picture: a black body
// with a white outline, light grey details, and a soft dark edge and shadow that keep it readable on the light menu background.
static void sample(float x, float y, float *grey, float *alpha)
{
	const float c = PAD_W / 2, line = 0.024f, edge = 0.016f;
	float body = pad_body(x, y);
	*grey = 0, *alpha = 0;
	if (body >= 0) {
		if (body < edge || pad_body(x - 0.02f, y - 0.03f) < 0) // the dark edge and the shadow below and right
			*alpha = body < edge ? 0.45f : 0.25f;
		return;
	}
	*alpha = 1;
	if (body > -line) {
		*grey = 0.93f; // the outline
		return;
	}
	*grey = 0.1f;
	if (stick(x, y, c - 0.34f, 0.34f, grey) || stick(x, y, c + 0.17f, 0.5f, grey))
		return;
	// the d-pad
	float px = c - 0.17f, py = 0.5f;
	if ((fabsf(x - px) < 0.024f && fabsf(y - py) < 0.07f) || (fabsf(y - py) < 0.024f && fabsf(x - px) < 0.07f)) {
		*grey = 0.75f;
		return;
	}
	// the four face buttons
	float bx = c + 0.34f, by = 0.34f, o = 0.075f;
	float buttons[4][2] = {{bx, by - o}, {bx, by + o}, {bx - o, by}, {bx + o, by}};
	for (int i = 0; i < 4; i++)
		if (sd_circle(x, y, buttons[i][0], buttons[i][1], 0.036f) < 0) {
			*grey = 0.75f;
			return;
		}
	// the guide button (a ring) and Back and Start beside it
	float g = sd_circle(x, y, c, 0.25f, 0.05f);
	if (g < 0 && g > -0.018f)
		*grey = 0.75f;
	else if (sd_circle(x, y, c - 0.13f, 0.36f, 0.022f) < 0 || sd_circle(x, y, c + 0.13f, 0.36f, 0.022f) < 0)
		*grey = 0.6f;
}

// The keyboard and mouse picture for a card whose own picture the game does not have (the 32-bit game's lobby shows an empty box there): a keyboard with its keys and a mouse
#define KB_W 1.7f
static float kb_body(float x, float y)
{
	float d = sd_rrect(x, y, 0.58f, 0.46f, 0.54f, 0.26f, 0.05f);
	float m = sd_rrect(x, y, 1.42f, 0.46f, 0.13f, 0.22f, 0.12f);
	return d < m ? d : m;
}
static void sample_kb(float x, float y, float *grey, float *alpha)
{
	const float line = 0.024f, edge = 0.016f;
	float body = kb_body(x, y);
	*grey = 0, *alpha = 0;
	if (body >= 0) {
		if (body < edge || kb_body(x - 0.02f, y - 0.03f) < 0)
			*alpha = body < edge ? 0.45f : 0.25f;
		return;
	}
	*alpha = 1;
	if (body > -line) {
		*grey = 0.93f;
		return;
	}
	*grey = 0.1f;
	if (x < 1.2f) { // the keys: 4 rows of 11 in the keyboard
		float kx = (x - 0.1f) / 0.9f * 11.0f, ky = (y - 0.27f) / 0.38f * 4.0f;
		if (kx >= 0 && kx < 11 && ky >= 0 && ky < 4) {
			float fx = kx - floorf(kx), fy = ky - floorf(ky);
			if (fx > 0.14f && fx < 0.86f && fy > 0.2f && fy < 0.8f)
				*grey = 0.75f;
		}
	} else { // the mouse: a line between its buttons and the buttons' split
		if ((fabsf(y - 0.37f) < 0.012f && fabsf(x - 1.42f) < 0.12f) || (fabsf(x - 1.42f) < 0.012f && y < 0.37f && y > 0.27f))
			*grey = 0.75f;
	}
}

typedef void (*sample_fn)(float, float, float *, float *);
static uint8_t *pad_material, *kb_material;

static void *build_material(void *keyboard_material, sample_fn fn, float width, float top, float bottom, const char *name, uint8_t **cache)
{
	if (*cache || !keyboard_material)
		return *cache;
	uint8_t *kb = (uint8_t *)keyboard_material;
	uint8_t *table = *(uint8_t **)(kb + MATERIAL_TABLE);
	uint8_t *image = table ? *(uint8_t **)(table + 8) : NULL;
	IDirect3DTexture9 *old = image ? *(IDirect3DTexture9 **)image : NULL;
	if (!old)
		return NULL; // the renderer has not made the keyboard's texture yet
	IDirect3DDevice9 *dev = NULL;
	D3DSURFACE_DESC desc;
	if (FAILED(IDirect3DTexture9_GetDevice(old, &dev)) || FAILED(IDirect3DTexture9_GetLevelDesc(old, 0, &desc)))
		return NULL;
	UINT w = desc.Width < 128 ? 128 : desc.Width, h = desc.Height < 128 ? 128 : desc.Height; // (the 32-bit game's placeholder of the card is 16 x 16: too small for a picture)
	IDirect3DTexture9 *tex = NULL;
	HRESULT hr = IDirect3DDevice9_CreateTexture(dev, w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
	IDirect3DDevice9_Release(dev);
	if (FAILED(hr)) {
		log_printf("padicon: cannot make the gamepad texture (%ux%u)", w, h);
		return NULL;
	}
	D3DLOCKED_RECT r;
	if (FAILED(IDirect3DTexture9_LockRect(tex, 0, &r, NULL, 0))) {
		IDirect3DTexture9_Release(tex);
		return NULL;
	}
	// the picture is square; the keyboard and mouse in theirs span about 0.9 of its width and, vertically, 0.36 to 0.88 (measured on a
	// 1080p screenshot of the lobby): the gamepad gets 0.84 of the width and the same middle, 0.62
	const float scale = width / 0.84f;
	for (UINT py = 0; py < h; py++) {
		uint32_t *row = (uint32_t *)((uint8_t *)r.pBits + py * r.Pitch);
		for (UINT px = 0; px < w; px++) {
			float g = 0, a = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++) {
					float grey, alpha;
					float u = ((float)px + (sx + 0.5f) / 4) / (float)w, v = ((float)py + (sy + 0.5f) / 4) / (float)h;
					fn((u - 0.5f) * scale + width / 2, (v - 0.62f) * scale + (top + bottom) / 2, &grey, &alpha);
					g += grey * alpha, a += alpha;
				}
			g = a > 0 ? g / a : 0;
			a /= 16;
			uint32_t gv = (uint32_t)(g * 255.0f + 0.5f), av = (uint32_t)(a * 255.0f + 0.5f);
			row[px] = av << 24 | gv << 16 | gv << 8 | gv;
		}
	}
	IDirect3DTexture9_UnlockRect(tex, 0);
	// the copies: the material with its own texture table and image (they are read a little over their ends, as menu copies are)
	static uint8_t mats[2][0x100], tabs[2][0x40], imgs[2][0x80];
	int slot = cache == &kb_material;
	uint8_t *mat = mats[slot], *tab = tabs[slot], *img = imgs[slot];
	memcpy(mat, kb, 0x100);
	memcpy(tab, table, 0x40);
	memcpy(img, image, 0x80);
	*(IDirect3DTexture9 **)img = tex;
	for (int i = 0; i < 4; i++)
		if (*(uint8_t **)(tab + i * 16 + 8) == image)
			*(uint8_t **)(tab + i * 16 + 8) = img;
	*(uint8_t **)(mat + MATERIAL_TABLE) = tab;
	*(const char **)mat = name;
	*cache = mat;
	log_printf("padicon: %s picture made (%ux%u)", name, w, h);
	return *cache;
}

void *padicon_material(void *keyboard_material)
{
	return build_material(keyboard_material, sample, PAD_W, PAD_TOP, PAD_BOTTOM, "mw2coop_gamepad", &pad_material);
}

void *padicon_keyboard_material(void *template_material)
{
	return build_material(template_material, sample_kb, KB_W, 0.2f, 0.72f, "mw2coop_keyboard", &kb_material);
}
