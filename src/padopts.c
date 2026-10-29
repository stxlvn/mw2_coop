// The split screen players' gamepad options, one set per player (Alpha = player 1, Bravo = player 2): look sensitivity, inverted look,
// aim assist, swapped sticks, the tactical layout and vibration, the console game's controller options. They are archived dvars
// (mw2coop_pad1_sens ... mw2coop_pad2_rumble), saved in players\config.cfg like the game's own options, and shown on two pages under
// Options > Controls ("Gamepad: Alpha", "Gamepad: Bravo") made from the game's Look page (pc_options_look): its slider and its yes / no
// rows, bound to these dvars, so the game draws and changes them as it does its own options (a pad works them with the d-pad and A).
// The console's aim assist dvars (aim_*, registered by rva 0x69a60 with the cheat flag) lose that flag, so they can be tuned too.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "menus_ui.h"
#include "padopts.h"

enum { OPT_SENS, OPT_INVERT, OPT_ASSIST, OPT_SOUTHPAW, OPT_TACTICAL, OPT_RUMBLE, OPTS };
static const struct {
	const char *key, *label, *description;
	float def;
} opts[OPTS] = {
    {"sens", "@MW2COOP_PAD_SENS", "Split screen: the gamepad's look sensitivity (1-10)", 3.0f},
    {"invert", "@MW2COOP_PAD_INVERT", "Split screen: the gamepad's right stick looks inverted", 0},
    {"assist", "@MW2COOP_PAD_ASSIST", "Split screen: aim assist for the gamepad", 1},
    {"southpaw", "@MW2COOP_PAD_SOUTHPAW", "Split screen: the gamepad's sticks swapped", 0},
    {"tactical", "@MW2COOP_PAD_TACTICAL", "Split screen: crouch on the right stick, melee on B", 0},
    {"rumble", "@MW2COOP_PAD_RUMBLE", "Split screen: the gamepad vibrates", 1},
};

#ifdef _WIN64
#define RVA_DVAR_REGISTER_BOOL 0x26c930  // dvar_t *(const char *name, bool value, unsigned flags, const char *description)
#define RVA_DVAR_REGISTER_FLOAT 0x26cb90 // dvar_t *(const char *name, float value, float min, float max, unsigned flags, const char *description)
#define RVA_AIM_DVARS 0x3cf050		 // the aim_* dvars: aim_aimAssistRangeScale (0x3cf050) ... aim_scale_view_axis (0x3cf178)
#define RVA_AIM_DVARS_END 0x3cf180
#else
#define RVA_DVAR_REGISTER_BOOL 0x866b0   // VA 0x4866b0, cdecl, the value is a byte at [esp+8] (docs/32bit-native-map/inputui.md)
#define RVA_DVAR_REGISTER_FLOAT 0x9f700  // VA 0x49f700, cdecl
extern const uint32_t aim_dvar_rvas32[38]; // native.c: where each aim_* dvar_t * lives in 32 bits (scattered, not one block)
#endif
#define DVAR_ARCHIVE 1
#define DVAR_CHEAT 4
#define ITEM_TYPE_SLIDER 10
#define ITEM_TYPE_YESNO 11

static dvar_t *dvars[2][OPTS];
static char names[2][OPTS][32];

static void register_dvars(void)
{
	if (dvars[0][0])
		return;
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	dvar_t *(*reg_bool)(const char *, int, unsigned, const char *) = (void *)(base + RVA_DVAR_REGISTER_BOOL);
	dvar_t *(*reg_float)(const char *, float, float, float, unsigned, const char *) = (void *)(base + RVA_DVAR_REGISTER_FLOAT);
	for (int pl = 0; pl < 2; pl++)
		for (int o = 0; o < OPTS; o++) {
			snprintf(names[pl][o], sizeof(names[pl][o]), "mw2coop_pad%d_%s", pl + 1, opts[o].key);
			dvars[pl][o] = o == OPT_SENS ? reg_float(names[pl][o], opts[o].def, 1.0f, 10.0f, DVAR_ARCHIVE, opts[o].description)
						     : reg_bool(names[pl][o], opts[o].def != 0, DVAR_ARCHIVE, opts[o].description);
		}
	log_printf("padopts: gamepad options registered (sensitivity %.1f / %.1f)", dvars[0][OPT_SENS]->current.value, dvars[1][OPT_SENS]->current.value);
}

// the aim_* dvars are registered with the cgame (a level's start), after the main menu: checked on every tick
static void unlock_aim_dvars(void)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	int unlocked = 0;
#ifdef _WIN64
	for (uint32_t rva = RVA_AIM_DVARS; rva < RVA_AIM_DVARS_END; rva += 8) {
		dvar_t *d = *(dvar_t **)(base + rva);
#else
	for (int i = 0; i < 38; i++) {
		dvar_t *d = *(dvar_t **)(base + aim_dvar_rvas32[i]);
#endif
		if (d && (d->flags & DVAR_CHEAT)) {
			d->flags &= ~DVAR_CHEAT;
			unlocked++;
		}
	}
	if (unlocked)
		log_printf("padopts: %d aim assist dvars unlocked", unlocked);
}

void padopts_get(int player, padopts_t *out)
{
	float v[OPTS];
	for (int o = 0; o < OPTS; o++) {
		dvar_t *d = player >= 0 && player < 2 ? dvars[player][o] : NULL;
		v[o] = !d ? opts[o].def : o == OPT_SENS ? d->current.value : (float)d->current.enabled;
	}
	out->sens = v[OPT_SENS];
	out->invert = v[OPT_INVERT] != 0;
	out->assist = v[OPT_ASSIST] != 0;
	out->southpaw = v[OPT_SOUTHPAW] != 0;
	out->tactical = v[OPT_TACTICAL] != 0;
	out->rumble = v[OPT_RUMBLE] != 0;
}

static uint8_t *item_by_text(uint8_t *menu, const char *text)
{
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		const char *t = *(const char **)(items[i] + ITEM_TEXT);
		if (t && !strcmp(t, text))
			return items[i];
	}
	return NULL;
}

static uint8_t *item_by_type(uint8_t *menu, int type)
{
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++)
		if (*(int *)(items[i] + ITEM_TYPE) == type)
			return items[i];
	return NULL;
}

static void place(uint8_t *item, float y)
{
	*(float *)(item + ITEM_RECT_Y) = y;
	*(float *)(item + ITEM_RECT_CLIENT_Y) = y;
}

// A player's page: the Look page's background, title and Back items, then a slider row (sensitivity) and yes / no rows, the game's own
// items bound to this player's dvars. Rows 20 units apart from y 28 like the Look page, its separator line after the third row.
static void add_page(int pl)
{
	const char *name = pl ? "mw2coop_pad2" : "mw2coop_pad1";
	if (ui_find_menu(name))
		return;
	uint8_t *src = ui_find_menu("pc_options_look");
	if (!src)
		return;
	uint8_t *menu = ui_clone_menu(src, name);
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	uint8_t *title = item_by_text(menu, "@MENU_LOOK"), *slider = item_by_type(menu, ITEM_TYPE_SLIDER), *yesno = item_by_type(menu, ITEM_TYPE_YESNO);
	uint8_t *slider_label = item_by_text(menu, "@MENU_MOUSE_SENSITIVITY"), *label = item_by_text(menu, "@MENU_INVERT_MOUSE");
	if (!title || !slider || !yesno || !slider_label || !label) {
		log_printf("padopts: the Look page is not as expected (title %p slider %p yes/no %p labels %p %p), no gamepad pages", (void *)title, (void *)slider,
			   (void *)yesno, (void *)slider_label, (void *)label);
		return;
	}
	int title_at = 0;
	while (items[title_at] != title)
		title_at++;
	uint8_t **list = HeapAlloc(GetProcessHeap(), 0, (count + 2 * OPTS) * sizeof(*list));
	int n = 0;
	for (int i = 0; i <= title_at; i++) // the backgrounds and the title
		list[n++] = items[i];
	*(const char **)(title + ITEM_TEXT) = pl ? "@MW2COOP_PAD_MENU_2" : "@MW2COOP_PAD_MENU_1";
	const float slider_dy = *(float *)(slider + ITEM_RECT_Y) - *(float *)(slider_label + ITEM_RECT_Y);
	void *click = ui_script_set("\"play\" \"mouse_click\" ; ");
	for (int o = 0; o < OPTS; o++) {
		float y = 28.0f + 20.0f * o + (o >= 3 ? 20.0f : 0); // the separator's line between the third and the fourth row
		uint8_t *control = o == OPT_SENS ? slider : o == OPT_INVERT ? yesno : ui_clone_item(yesno, menu);
		uint8_t *text = o == OPT_SENS ? slider_label : o == OPT_INVERT ? label : ui_clone_item(label, menu);
		place(control, y + (o == OPT_SENS ? slider_dy : 0));
		place(text, y);
		*(const char **)(control + ITEM_DVAR) = names[pl][o];
		*(const char **)(text + ITEM_TEXT) = opts[o].label;
		if (o == OPT_SENS) { // the slider's range: editFieldDef {minVal, maxVal, defVal, range, ...}
			float *field = HeapAlloc(GetProcessHeap(), 0, 0x20);
			memcpy(field, *(void **)(control + ITEM_TYPE_DATA), 0x20);
			static int logged;
			if (!logged++)
				log_printf("padopts: the Look page's slider runs %g..%g (default %g, range %g)", field[0], field[1], field[2], field[3]);
			field[0] = 1.0f, field[1] = 10.0f, field[2] = opts[o].def, field[3] = 9.0f;
			*(void **)(control + ITEM_TYPE_DATA) = field;
		} else
			*(void **)(control + ITEM_ACTION) = click; // (the mouse pitch row also ran "uiScript update ui_mousePitch")
		list[n++] = control;
		list[n++] = text;
	}
	for (int i = title_at + 1; i < count; i++) { // the separator line (a 1 unit high rule) and the Back items
		uint8_t *it = items[i];
		const char *t = *(const char **)(it + ITEM_TEXT);
		if ((t && !strcmp(t, "@PLATFORM_BACK_CAPS")) || ((const float *)(it + ITEM_RECT))[3] == 1.0f)
			list[n++] = it;
	}
	*(uint8_t ***)(menu + MENU_ITEMS) = list;
	*(int *)(menu + MENU_ITEM_COUNT) = n;
	if (ui_register_menu(menu))
		log_printf("padopts: page %s added (%d items)", name, n);
}

// The two buttons under Look in the controls page's right column (pc_options_controls), the items below them moved two rows down.
// Each runs what Look runs with its page instead of pc_options_look.
static void patch_controls(void)
{
	uint8_t *menu = ui_find_menu("pc_options_controls");
	if (!menu || ui_item_index(menu, "mw2coop_pad1") >= 0)
		return;
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	int at = -1;
	for (int i = 0; i < count && at < 0; i++) {
		const char *t = *(const char **)(items[i] + ITEM_TEXT);
		if (t && !strcmp(t, "@MENU_LOOK") && ((const float *)(items[i] + ITEM_RECT))[0] > 100.0f)
			at = i;
	}
	if (at < 0) {
		static int logged;
		if (!logged++)
			log_printf("padopts: no Look button in pc_options_controls");
		return;
	}
	uint8_t *look = items[at];
	const float look_y = *(float *)(look + ITEM_RECT_Y), col_x = ((const float *)(look + ITEM_RECT))[0];
	uint8_t **list = HeapAlloc(GetProcessHeap(), 0, (count + 2) * sizeof(*list));
	memcpy(list, items, (at + 1) * sizeof(*list));
	for (int b = 0; b < 2; b++) {
		uint8_t *it = ui_clone_item(look, menu);
		*(const char **)it = b ? "mw2coop_pad2" : "mw2coop_pad1";
		*(const char **)(it + ITEM_TEXT) = b ? "@MW2COOP_PAD_MENU_2" : "@MW2COOP_PAD_MENU_1";
		void *action = ui_script_replace(*(void **)(look + ITEM_ACTION), "pc_options_look", b ? "mw2coop_pad2" : "mw2coop_pad1");
		*(void **)(it + ITEM_ACTION) =
			action ? action : ui_script_set(b ? "\"play\" \"mouse_click\" ; \"open\" \"mw2coop_pad2\" ; " : "\"play\" \"mouse_click\" ; \"open\" \"mw2coop_pad1\" ; ");
		place(it, look_y + BUTTON_HEIGHT * (b + 1));
		list[at + 1 + b] = it;
	}
	memcpy(list + at + 3, items + at + 1, (count - at - 1) * sizeof(*list));
	for (int i = at + 3; i < count + 2; i++) { // the right column below Look (Chat, the line, Restore defaults) moves down
		uint8_t *it = list[i];
		const float *r = (const float *)(it + ITEM_RECT);
		if (r[0] >= col_x - 20.0f && r[1] > look_y + 0.5f && r[1] < 400.0f && it[ITEM_RECT_ALIGN_V] == look[ITEM_RECT_ALIGN_V]) {
			uint8_t *moved = ui_clone_item(it, menu);
			place(moved, r[1] + 2 * BUTTON_HEIGHT);
			list[i] = moved;
		}
	}
	*(uint8_t ***)(menu + MENU_ITEMS) = list; // the old items stay valid, so the UI may read the new array before the new count
	*(int *)(menu + MENU_ITEM_COUNT) = count + 2;
	log_printf("padopts: gamepad buttons added under Look in pc_options_controls");
}

void padopts_tick(void)
{
	register_dvars();
	unlock_aim_dvars();
	add_page(0);
	add_page(1);
	patch_controls();
}
