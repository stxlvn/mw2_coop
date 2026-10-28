// Co-op in the game's own menus (no .bat files). This build has no menu text parser, so the
// zone's menus (ASSET_TYPE_MENU 26, as the UI has loaded them) are changed in memory:
//
// - Campaign main menu (main_text): a "TWO PLAYERS ONLINE" button under Mission Select, cloned
//   from Mission Select. It opens the game's own Spec Ops co-op lobby (coop_lobby: Steam friend
//   invites, ready state, Start) with a campaign mission: where the players stopped last
//   (mw2coop\progress.txt), else The Pit (trainer).
// - In that lobby "Change Op" opens the campaign mission list (levels) instead of the Spec Ops
//   list. The mission buttons end in the difficulty popup's "devmap <map>"; in the campaign lobby
//   the devmap command is caught and sets the lobby's mission instead.
//
// Layout of this build's structures (64-bit, found with memdump.c, see NOTES.md):
//   menuDef_t:  name +0, itemCount int +0xbc, onOpen +0xe0, onClose +0xf0, onESC +0xf8,
//               itemDef_t **items +0x168
//   itemDef_t (0x1e8 bytes): name +0, rect {x,y,w,h} +8, rectClient +0x1c, group +0x30,
//               dynamicFlags +0x50 (2 = has focus, 4 = visible), text +0xf0, parent menu +0x100,
//               action +0x128, onFocus +0x138, leaveFocus +0x140, floatExpressions +0x198, disabledExp +0x1a8
// (the 32-bit values are in menus_ui.h)
//   MenuEventHandlerSet {int count; MenuEventHandler **handlers}
//   MenuEventHandler {void *data; char type}  type 0: data is a menu script string
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "access.h"
#include "firewall.h"
#include "localize.h"
#include "instance.h"
#include "splitscreen.h"
#include "native.h"
#include "log.h"
#include "settings.h"
#include "upnp.h"
#include "menus.h"
#include "menus_ui.h"
#include "net.h"
#include "progress.h"

#ifdef _WIN64
#define RVA_UI_CONTEXT 0x1a3ab00 // uiContext_t (Menus_OpenByName's first argument, "openmenu" @0x24ca10)
#define UI_MENUS 0x38		 // menuDef_t *menus[]
#define UI_MENU_COUNT 0x1438	 // int
#define UI_MAX_MENUS 640	 // (0x1438 - 0x38) / 8

typedef struct {
	void *data;
	char type;
	char pad[7];
} MenuEventHandler;

typedef struct {
	int count;
	int pad;
	MenuEventHandler **handlers;
} MenuEventHandlerSet;
#else
// 32-bit: the uiContext_t at 0x191cef0 ("openmenu", 0x61ed60, passes it to Menus_OpenByName 0x465010); menus[640] at +0x38 and
// the count after them at +0xa38; handlers have 4-byte pointers (no padding)
#ifdef MW2COOP_IW4X
#define RVA_UI_CONTEXT 0x1520470 // the 1.0.159 exe of iw4x-sp (uiInfoArray)
#else
#define RVA_UI_CONTEXT 0x151cef0
#endif
#define UI_MENUS 0x38
#define UI_MENU_COUNT 0xa38
#define UI_MAX_MENUS 640

typedef struct {
	void *data;
	char type;
	char pad[3];
} MenuEventHandler;

typedef struct {
	int count;
	MenuEventHandler **handlers;
} MenuEventHandlerSet;
#endif

static uint8_t *base;
#define FN_SLOT_USED(lobby, i) ((char (*)(void *, int))(base + RVA_LOBBY_SLOT_USED))(lobby, i)

// The menus the UI has loaded (the list Menus_FindByName, rva 0x259d30, searches): no asset
// database call, which blocks while a zone loads (a control-thread lookup during a level load
// hung both games after the briefing).
uint8_t *ui_find_menu(const char *name)
{
	uint8_t *ui = base + RVA_UI_CONTEXT;
	int count = *(int *)(ui + UI_MENU_COUNT);
	uint8_t **menus = (uint8_t **)(ui + UI_MENUS);
	for (int i = 0; i < count && i < UI_MAX_MENUS; i++) {
		const char *n = menus[i] ? *(const char **)menus[i] : NULL;
		if (n && lstrcmpiA(n, name) == 0)
			return menus[i];
	}
	return NULL;
}

int ui_item_index(uint8_t *menu, const char *name)
{
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		const char *n = *(const char **)items[i];
		if (n && strcmp(n, name) == 0)
			return i;
	}
	return -1;
}

// QA: logs what a menu item runs ("!menuaction <menu> <item>" in commands.txt): its action
// handlers, a script for type 0, else the handler type.
void menus_log_action(const char *menu_name, const char *item_name)
{
	uint8_t *menu = ui_find_menu(menu_name);
	int at = menu ? ui_item_index(menu, item_name) : -1;
	if (at < 0) {
		log_printf("menus: no item '%s' in menu '%s'", item_name, menu_name);
		return;
	}
	uint8_t *item = (*(uint8_t ***)(menu + MENU_ITEMS))[at];
	MenuEventHandlerSet *set = *(MenuEventHandlerSet **)(item + ITEM_ACTION);
	log_printf("menus: %s/%s action: %d handler(s)", menu_name, item_name, set ? set->count : 0);
	for (int i = 0; set && i < set->count && i < 16; i++) {
		MenuEventHandler *h = set->handlers[i];
		if (h && h->type == 0 && h->data)
			log_printf("menus:   script: %.400s", (const char *)h->data);
		else if (h)
			log_printf("menus:   handler type %d", h->type);
	}
}

// QA: logs the names of the menus the UI has loaded ("!menus").
void menus_log_names(void)
{
	uint8_t *ui = base + RVA_UI_CONTEXT;
	int count = *(int *)(ui + UI_MENU_COUNT);
	uint8_t **menus = (uint8_t **)(ui + UI_MENUS);
	char line[600] = "";
	for (int i = 0; i < count && i < UI_MAX_MENUS; i++) {
		const char *n = menus[i] ? *(const char **)menus[i] : NULL;
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s ", n ? n : "-");
		if (strlen(line) > 480 || i == count - 1) {
			log_printf("menus: loaded: %s", line);
			line[0] = 0;
		}
	}
}

// QA: logs a menu's items ("!menuitems <menu>"): name, text, rect and its alignment.
void menus_log_items(const char *menu_name)
{
	uint8_t *menu = ui_find_menu(menu_name);
	if (!menu) {
		log_printf("menus: no menu '%s'", menu_name);
		return;
	}
	static const int events[] = {MENU_ON_OPEN, MENU_ON_CLOSE};
	for (int e = 0; e < 2; e++) {
		MenuEventHandlerSet *set = *(MenuEventHandlerSet **)(menu + events[e]);
		for (int i = 0; set && i < set->count && i < 16; i++) {
			MenuEventHandler *h = set->handlers[i];
			if (h && h->type == 0 && h->data)
				log_printf("menus: %s %s script: %.400s", menu_name, e ? "onClose" : "onOpen", (const char *)h->data);
			else if (h)
				log_printf("menus: %s %s handler type %d", menu_name, e ? "onClose" : "onOpen", h->type);
		}
	}
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		uint8_t *it = items[i];
		const char *name = *(const char **)it, *text = *(const char **)(it + ITEM_TEXT);
		float *r = (float *)(it + ITEM_RECT);
		log_printf("menus: %s[%d] %s '%s' x %g y %g w %g h %g align %d/%d", menu_name, i, name ? name : "-",
			   text ? text : "", r[0], r[1], r[2], r[3], it[ITEM_RECT_ALIGN_H], it[ITEM_RECT_ALIGN_V]);
		// what can lock or hide it, and what it runs (the campaign's mission buttons)
		MenuEventHandlerSet *act = *(MenuEventHandlerSet **)(it + ITEM_ACTION);
		const char *script = act && act->count > 0 && act->handlers[0] && act->handlers[0]->type == 0
					     ? (const char *)act->handlers[0]->data
					     : NULL;
		void *fexp = *(void **)(it + ITEM_FLOAT_EXPS);
		if (fexp || *(void **)(it + ITEM_DISABLED_EXP) || script)
			log_printf("menus:   floatExps %p disabledExp %p flags %x action %.200s", fexp,
				   *(void **)(it + ITEM_DISABLED_EXP), *(uint32_t *)(it + ITEM_DYNAMIC_FLAGS), script ? script : "-");
	}
}

// A handler set running one menu script, e.g. "\"open\" \"levels\" ;" (the parsed form the
// zone stores: quoted tokens, commands ended by ';').
void *ui_script_set(const char *script)
{
	MenuEventHandlerSet *set = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*set));
	MenuEventHandler **list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
	MenuEventHandler *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
	h->data = (void *)script;
	h->type = 0;
	list[0] = h;
	set->count = 1;
	set->handlers = list;
	return set;
}

uint8_t *ui_clone_item(uint8_t *from, uint8_t *parent_menu)
{
	uint8_t *it = HeapAlloc(GetProcessHeap(), 0, ITEM_SIZE);
	memcpy(it, from, ITEM_SIZE);
	*(uint8_t **)(it + ITEM_PARENT) = parent_menu;
	*(uint32_t *)(it + ITEM_DYNAMIC_FLAGS) &= ~WINDOW_HASFOCUS;
	return it;
}

uint8_t *ui_clone_menu(uint8_t *from, const char *name)
{
	uint8_t *menu = HeapAlloc(GetProcessHeap(), 0, MENU_SIZE);
	memcpy(menu, from, MENU_SIZE);
	*(const char **)menu = name;
	int count = *(int *)(from + MENU_ITEM_COUNT);
	uint8_t **src = *(uint8_t ***)(from + MENU_ITEMS);
	uint8_t **items = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*items));
	for (int i = 0; i < count; i++)
		items[i] = ui_clone_item(src[i], menu);
	*(uint8_t ***)(menu + MENU_ITEMS) = items;
	return menu;
}

int ui_register_menu(uint8_t *menu)
{
	uint8_t *ui = base + RVA_UI_CONTEXT;
	int count = *(int *)(ui + UI_MENU_COUNT);
	if (count >= UI_MAX_MENUS)
		return 0;
	((uint8_t **)(ui + UI_MENUS))[count] = menu;
	*(int *)(ui + UI_MENU_COUNT) = count + 1;
	return 1;
}

// ---------------------------------------------------------------- campaign main menu button

static uint8_t **main_items; // our items array for main_text, NULL until patched

static const char online_action[] = "\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_host_menu\" ; ";

// the split screen button: the same host popup, and the lobby it opens brings the second player (this PC's second process, instance.c)
static const char split_action[] = "\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_split_menu\" ; ";

static const char join_action[] =
	"\"play\" \"mouse_click\" ; "
	"\"exec\" \"xrequirelivesignin\" ; "
	"\"exec\" \"mw2coop_find\" ; ";

// Buttons added under Mission Select, in this order
static const struct {
	const char *name, *text, *action;
} main_buttons[] = {
	{"mw2coop_online", "@MENU_SP_TWO_PLAYER_ONLINE_CAPS", online_action},
	{"mw2coop_find", "@MW2COOP_JOIN", join_action},
	{"mw2coop_splitscreen", "@MW2COOP_SPLITSCREEN", split_action},
};
#define MAIN_BUTTONS (int)(sizeof(main_buttons) / sizeof(main_buttons[0]))

static void patch_main_menu(void)
{
	uint8_t *menu = ui_find_menu("main_text");
	if (!menu)
		return;
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	if (items == main_items)
		return;
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	int at = ui_item_index(menu, "levelselect");
	if (at < 0 || ui_item_index(menu, main_buttons[0].name) >= 0) {
		log_printf("menus: main_text has no levelselect button, no co-op buttons added");
		main_items = items;
		return;
	}
	uint8_t *src = items[at];
	float y0 = *(float *)(src + ITEM_RECT_Y) + BUTTON_HEIGHT;

	uint8_t **list = HeapAlloc(GetProcessHeap(), 0, (count + MAIN_BUTTONS) * sizeof(*list));
	memcpy(list, items, (at + 1) * sizeof(*list));
	for (int b = 0; b < MAIN_BUTTONS; b++) {
		uint8_t *item = HeapAlloc(GetProcessHeap(), 0, ITEM_SIZE);
		memcpy(item, src, ITEM_SIZE);
		float y = y0 + b * BUTTON_HEIGHT;
		*(const char **)item = main_buttons[b].name;
		*(const char **)(item + ITEM_TEXT) = main_buttons[b].text;
		*(MenuEventHandlerSet **)(item + ITEM_ACTION) = ui_script_set(main_buttons[b].action);
		*(uint32_t *)(item + ITEM_DYNAMIC_FLAGS) &= ~WINDOW_HASFOCUS;
		*(float *)(item + ITEM_RECT_Y) = y;
		*(float *)(item + ITEM_RECT_CLIENT_Y) = y;
		list[at + 1 + b] = item;
	}
	memcpy(list + at + 1 + MAIN_BUTTONS, items + at + 1, (count - at - 1) * sizeof(*list));
	// everything below the new buttons moves down
	for (int i = at + 1 + MAIN_BUTTONS; i < count + MAIN_BUTTONS; i++) {
		if (*(float *)(list[i] + ITEM_RECT_Y) >= y0 - 0.5f) {
			*(float *)(list[i] + ITEM_RECT_Y) += MAIN_BUTTONS * BUTTON_HEIGHT;
			*(float *)(list[i] + ITEM_RECT_CLIENT_Y) += MAIN_BUTTONS * BUTTON_HEIGHT;
		}
	}
	// the old items stay valid, so the UI may read the new array before the new count
	main_items = list;
	*(uint8_t ***)(menu + MENU_ITEMS) = list;
	*(int *)(menu + MENU_ITEM_COUNT) = count + MAIN_BUTTONS;
	log_printf("menus: main_text: co-op buttons added under Mission Select (%d items)", count + MAIN_BUTTONS);
}

// ---------------------------------------------------------------- joining a game on the network

// mw2coop_find: the campaign menu's join button. Asks every network the PC is on (LAN, Radmin
// VPN and the like: each adapter's broadcast address, net.c) for campaign lobbies for 2 s, then
// lists the hosts that answered in a popup ("mw2coop_lobbies", built from the difficulty popup);
// the player picks one (mw2coop_join_found <n>; a locked lobby asks for its password, access.c)
// or enters an address. A host answers while it holds a campaign lobby with a free slot that
// is open to the network: "mw2coop_here <host xuid> <p|-> <map> <host name>" (p: password; a
// host on several networks answers on each, the XUID keeps it to one row).
#define FIND_TAG "mw2coop_find"
#define HERE_TAG "mw2coop_here "
#define FIND_MS 2000
#define LIST_ROWS 4
#define MAX_LOBBIES (LIST_ROWS - 1) // the last row enters an address

typedef struct {
	uint8_t ip[4];
	uint16_t port;
	uint64_t xuid;
	int locked;
	char map[32];
	char name[48];
} Lobby;

static void show_row(uint8_t *menu, const char *row, int visible);
static CRITICAL_SECTION lobby_lock; // the packet thread adds, the main thread reads
static Lobby lobbies[MAX_LOBBIES];
static int lobby_count;
static volatile LONG finding;
static volatile DWORD find_started, find_sent, join_text_until;
static int find_target; // asking one address (typed in) instead of every network
static uint8_t target_ip[4];
static uint16_t target_port;

static void find_f(void)
{
	if (finding)
		return;
	EnterCriticalSection(&lobby_lock);
	lobby_count = 0;
	LeaveCriticalSection(&lobby_lock);
	find_started = GetTickCount();
	find_sent = 0;
	find_target = 0;
	InterlockedExchange(&finding, 1);
	localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_SEARCHING");
	log_printf("menus: looking for campaign lobbies on the network");
}

// "Name - Mission" in the game's language (Windows-1251)
static void lobby_row(const Lobby *l, char *out, int size)
{
	char key[64] = "MENU_SP_SP_";
	int n = (int)strlen(key);
	for (const char *m = l->map; *m && n < (int)sizeof(key) - 1; m++)
		key[n++] = (char)((*m >= 'a' && *m <= 'z') ? *m - 32 : *m);
	key[n] = 0;
	const char *mission = localize_text(key);
	const char *lock = l->locked ? localize_text("MW2COOP_LOCKED") : NULL;
	snprintf(out, size, "%s - %s%s%s", l->name[0] ? l->name : "?", mission ? mission : l->map, lock ? " " : "",
		 lock ? lock : "");
}

void menus_find_at(const uint8_t ip[4], uint16_t port)
{
	find_f();
	memcpy(target_ip, ip, 4);
	target_port = port;
	find_target = 1;
}

static void find_tick(void)
{
	DWORD now = GetTickCount();
	if (finding) {
		if (now - find_started < FIND_MS) {
			if (!find_sent || now - find_sent >= 700) {
				find_sent = now;
				if (find_target)
					net_send_mod_to(target_ip, target_port, FIND_TAG);
				else
					net_broadcast_mod(FIND_TAG, settings_port());
			}
			return;
		}
		InterlockedExchange(&finding, 0);
		if (find_target) {
			// the address typed in: join it whether or not it answered (an answer tells if it is locked)
			EnterCriticalSection(&lobby_lock);
			int locked = lobby_count ? lobbies[0].locked : 0;
			LeaveCriticalSection(&lobby_lock);
			localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_IDLE");
			access_join(target_ip, target_port, locked);
			return;
		}
		EnterCriticalSection(&lobby_lock);
		int count = lobby_count;
		for (int i = 0; i < MAX_LOBBIES; i++) {
			char alias[32], row[128] = "";
			snprintf(alias, sizeof(alias), "MW2COOP_LOBBY_%d", i + 1);
			if (i < count)
				lobby_row(&lobbies[i], row, sizeof(row));
			localize_set_text(alias, row);
		}
		LeaveCriticalSection(&lobby_lock);
		log_printf("menus: %d campaign lobb%s answered", count, count == 1 ? "y" : "ies");
		// rows without a lobby are hidden, so the focus starts on the first lobby or the address row
		uint8_t *list = ui_find_menu("mw2coop_lobbies");
		for (int i = 0; i < MAX_LOBBIES; i++) {
			char row[24];
			snprintf(row, sizeof(row), "mw2coop_lobby_%d", i + 1);
			show_row(list, row, i < count);
		}
		// the list opens even when nothing answered: its last row enters an address
		localize_select("MW2COOP_JOIN", count ? "MW2COOP_JOIN_IDLE" : "MW2COOP_JOIN_NONE");
		if (!count)
			join_text_until = now + 4000;
		game_cbuf_addtext("openmenu mw2coop_lobbies\n");
		return;
	}
	if (join_text_until && (int)(now - join_text_until) > 0) {
		join_text_until = 0;
		localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_IDLE");
	}
}

// mw2coop_join_found <n>: a row of the list (1-based)
static void join_found_f(void)
{
	int n = atoi(game_cmd_argv(1)) - 1;
	Lobby l;
	EnterCriticalSection(&lobby_lock);
	int ok = n >= 0 && n < lobby_count;
	if (ok)
		l = lobbies[n];
	LeaveCriticalSection(&lobby_lock);
	if (!ok)
		return;
	if (!l.locked) {
		localize_select("MW2COOP_JOIN", "MW2COOP_JOIN_CONNECTING");
		join_text_until = GetTickCount() + 6000;
	}
	access_join(l.ip, l.port, l.locked);
}

// The list popup: a copy of the difficulty popup (title, four buttons, the description lines)
// with our texts and actions. It is added to the UI's menu list, which a UI zone reload rebuilds.

// A popup with a title and four rows, copied from the difficulty popup (its rows are the four
// difficulty buttons; the descriptions and the checkpoint warning are blanked).
static const char *const popup_rows[LIST_ROWS] = {"newgame_easy", "newgame_regular", "newgame_hardened", "newgame_veteran"};

static uint8_t *rows_popup(const char *name, const char *title, const char *const row_names[LIST_ROWS],
			   const char *const row_texts[LIST_ROWS], const char *const row_actions[LIST_ROWS])
{
	uint8_t *src = ui_find_menu("popmenu_difficulty");
	int first = src ? ui_item_index(src, popup_rows[0]) : -1;
	if (first < 0)
		return NULL;
	uint8_t *menu = ui_clone_menu(src, name);
	*(void **)(menu + MENU_ON_OPEN) = ui_script_set("\"focusfirst\" ; ");
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		uint8_t *it = items[i];
		const char *text = *(const char **)(it + ITEM_TEXT);
		if (text && strcmp(text, "@MENU_SELECT_DIFFICULTY") == 0)
			*(const char **)(it + ITEM_TEXT) = title;
		else if (i > first - 6 && i < first) {
			*(const char **)(it + ITEM_TEXT) = "";
			*(void **)(it + ITEM_TEXT_EXP) = NULL;
		}
	}
	for (int r = 0; r < LIST_ROWS; r++) {
		int i = ui_item_index(src, popup_rows[r]);
		if (i < 0)
			continue;
		uint8_t *it = items[i];
		*(const char **)it = row_names[r];
		*(const char **)(it + ITEM_TEXT) = row_texts[r];
		*(void **)(it + ITEM_ACTION) = ui_script_set(row_actions[r]);
		*(void **)(it + ITEM_ON_FOCUS) =
			ui_script_set("\"play\" \"mouse_over\" ; \"setItemColor\" \"self\" \"backcolor\" 0 0 0 1 ; ");
		*(void **)(it + ITEM_LEAVE_FOCUS) =
			ui_script_set("\"setItemColor\" \"self\" \"backcolor\" 0 0 0 \"0.0\" ; ");
	}
	return ui_register_menu(menu) ? menu : NULL;
}

// show or hide a popup row (WINDOW_VISIBLE in dynamicFlags; a hidden row cannot have the focus)
static void show_row(uint8_t *menu, const char *row, int visible)
{
	int at = menu ? ui_item_index(menu, row) : -1;
	if (at < 0)
		return;
	uint32_t *flags = (uint32_t *)((*(uint8_t ***)(menu + MENU_ITEMS))[at] + ITEM_DYNAMIC_FLAGS);
	if (visible)
		*flags |= WINDOW_VISIBLE;
	else
		*flags &= ~(WINDOW_VISIBLE | WINDOW_HASFOCUS);
}

static void add_lobbies_menu(void)
{
	if (ui_find_menu("mw2coop_lobbies"))
		return;
	// rows 1..3: the lobbies found; row 4: enter an address
	static const char *const names[LIST_ROWS] = {"mw2coop_lobby_1", "mw2coop_lobby_2", "mw2coop_lobby_3", "mw2coop_lobby_4"};
	static const char *const texts[LIST_ROWS] = {"@MW2COOP_LOBBY_1", "@MW2COOP_LOBBY_2", "@MW2COOP_LOBBY_3", "@MW2COOP_BY_ADDRESS"};
	static const char *const actions[LIST_ROWS] = {
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_join_found 1\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_join_found 2\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_join_found 3\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_join_address\" ; "};
	if (rows_popup("mw2coop_lobbies", "@MW2COOP_LOBBIES", names, texts, actions))
		log_printf("menus: list of games on the network added");
}

// ---------------------------------------------------------------- hosting: continue, new, pick

// The campaign menu's "two players online" opens a popup: continue where the players stopped
// (progress.txt; the row is hidden without progress), a new campaign (the game's difficulty
// popup, then The Pit), or a mission from the campaign list (then its difficulty popup). The
// pick ends in the game's own "devmap <map>", which the devmap hook turns into the lobby.
static volatile LONG picking; // a new campaign or a mission is being picked for the lobby
static DWORD picking_seen;    // last tick one of the picking menus was open

static void add_host_menu(void)
{
	if (ui_find_menu("mw2coop_host"))
		return;
	static const char *const names[LIST_ROWS] = {"mw2coop_continue", "mw2coop_new", "mw2coop_pick", "mw2coop_host_4"};
	static const char *const texts[LIST_ROWS] = {"@MW2COOP_CONTINUE", "@MW2COOP_NEW_CAMPAIGN", "@MENU_MISSION_SELECT_CAPS", "@MW2COOP_PICK_SAVE"};
	static const char *const actions[LIST_ROWS] = {
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_host_go\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_host_new\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_host_pick\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_saves_menu\" ; "};
	uint8_t *menu = rows_popup("mw2coop_host", "@MENU_SP_TWO_PLAYER_ONLINE", names, texts, actions);
	if (menu) {
		show_row(menu, "mw2coop_host_4", 0);
		log_printf("menus: host popup added");
	}
}

static volatile LONG split_pending; // the lobby about to be hosted is a split screen one
static volatile LONG split_mode;    // the lobby being hosted is a split screen one (the second player is on this PC, his process starts at Start)

// mw2coop_host_menu: the campaign menu's button (split: the split screen button's, same popup)
static void host_menu_open(int split)
{
	InterlockedExchange(&split_pending, split);
	uint8_t *menu = ui_find_menu("mw2coop_host");
	char map[64], start[64];
	int have = progress_last(map, sizeof(map), start, sizeof(start));
	if (have) {
		char key[80] = "MENU_SP_SP_", text[160];
		int n = (int)strlen(key);
		for (const char *m = map; *m && n < (int)sizeof(key) - 1; m++)
			key[n++] = (char)((*m >= 'a' && *m <= 'z') ? *m - 32 : *m);
		key[n] = 0;
		const char *prefix = localize_text("MW2COOP_CONTINUE_PREFIX"), *mission = localize_text(key);
		snprintf(text, sizeof(text), "%s%s", prefix ? prefix : "", mission ? mission : map);
		localize_set_text("MW2COOP_CONTINUE", text);
	}
	show_row(menu, "mw2coop_continue", have);
	show_row(menu, "mw2coop_host_4", have);
	game_cbuf_addtext("openmenu mw2coop_host\n");
}

static void host_menu_f(void)
{
	host_menu_open(0);
}

static int save_picked; // the host picked a save in the menu (the lobby stays on it)
static void host_lobby(const char *map);

// The host's saves (progress.c): the newest four, "Mission - members".
static void add_saves_menu(void)
{
	if (ui_find_menu("mw2coop_saves"))
		return;
	static const char *const names[LIST_ROWS] = {"mw2coop_save_1", "mw2coop_save_2", "mw2coop_save_3", "mw2coop_save_4"};
	static const char *const texts[LIST_ROWS] = {"@MW2COOP_SAVE_1", "@MW2COOP_SAVE_2", "@MW2COOP_SAVE_3", "@MW2COOP_SAVE_4"};
	static const char *const actions[LIST_ROWS] = {
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_save_pick 1\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_save_pick 2\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_save_pick 3\" ; ",
		"\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_save_pick 4\" ; "};
	if (rows_popup("mw2coop_saves", "@MW2COOP_SAVES", names, texts, actions))
		log_printf("menus: saves popup added");
}

// mw2coop_saves_menu: the host popup's "pick a save"
static void saves_menu_f(void)
{
	static const char *const rows[LIST_ROWS] = {"mw2coop_save_1", "mw2coop_save_2", "mw2coop_save_3", "mw2coop_save_4"};
	static const char *const keys[LIST_ROWS] = {"MW2COOP_SAVE_1", "MW2COOP_SAVE_2", "MW2COOP_SAVE_3", "MW2COOP_SAVE_4"};
	uint8_t *menu = ui_find_menu("mw2coop_saves");
	for (int r = 0; r < LIST_ROWS; r++) {
		char map[64], name[96], key[80] = "MENU_SP_SP_", text[192];
		int have = progress_save_info(r, map, sizeof(map), name, sizeof(name));
		if (have) {
			int n = (int)strlen(key);
			for (const char *m = map; *m && n < (int)sizeof(key) - 1; m++)
				key[n++] = (char)((*m >= 'a' && *m <= 'z') ? *m - 32 : *m);
			key[n] = 0;
			const char *mission = localize_text(key);
			const char *nobody = localize_text("MW2COOP_SAVE_NO_PARTNER");
			snprintf(text, sizeof(text), "%s - %s", mission ? mission : map, name[0] ? name : (nobody ? nobody : "?"));
			localize_set_text(keys[r], text);
		}
		show_row(menu, rows[r], have);
	}
	game_cbuf_addtext("openmenu mw2coop_saves\n");
}

// mw2coop_save_pick <1..4>: host a lobby on that save
static void save_pick_f(void)
{
	const char *arg = game_cmd_argv(1);
	if (arg[0] < '1' || arg[0] > '0' + LIST_ROWS || arg[1])
		return;
	progress_choose(arg[0] - '1');
	save_picked = 1;
	host_lobby(NULL);
}

// Host a lobby: what the Spec Ops "two players online" button runs, then mw2coop_online [map].
static void host_lobby(const char *map)
{
	char cmd[512];
	snprintf(cmd, sizeof(cmd),
		 "set arcademode 0\nset arcademode_full 0\nset solo_play 0\nset ui_skip_level_select 1\n"
		 "set so_ophud_player0 0\nset so_ophud_player1 0\nxrequirelivesignin\n"
		 "stopListening\nset arcademode_lives 2\nset arcademode_combined_score 0\nset mblur_enable 0\n"
		 "mw2coop_online %s\nlisten\n",
		 map ? map : "");
	game_cbuf_addtext(cmd);
	if (InterlockedExchange(&split_pending, 0)) { // a split screen lobby: the second player presses a button on his device first
		InterlockedExchange(&split_mode, 1);
		instance_assign_begin();
	} else {
		InterlockedExchange(&split_mode, 0);
		instance_assign_end();
	}
}

// the Split screen button: the game goes from full screen to a borderless window of the screen's size once (the only change the player may see: it is
// the moment he chose the mode), then the host popup opens; from here on nothing changes on the screen until the mission is on
static DWORD WINAPI split_prepare_thread(LPVOID unused)
{
	(void)unused;
	splitscreen_prepare_windowed();
	game_cbuf_addtext("mw2coop_split_menu_open\n");
	return 0;
}

static void split_menu_f(void)
{
	CreateThread(NULL, 0, split_prepare_thread, NULL, 0, NULL);
}

static void split_menu_open_f(void)
{
	host_menu_open(1);
}

static void host_go_f(void)
{
	progress_choose(-1);
	save_picked = 0;
	host_lobby(NULL);
}

static void host_new_f(void)
{
	InterlockedExchange(&picking, 1);
	picking_seen = GetTickCount();
	game_cbuf_addtext("set ui_load_level trainer\nopenmenu popmenu_difficulty\n");
}

static void host_pick_f(void)
{
	InterlockedExchange(&picking, 1);
	picking_seen = GetTickCount();
	game_cbuf_addtext("setdvar arcademode 0\nopenmenu levels\n");
}

// Picking ends when its menus are all closed without a devmap (Esc): menuDef dynamicFlags
// (+0x50) has WINDOW_VISIBLE while a menu is open.
static void picking_tick(void)
{
	if (!picking)
		return;
	static const char *const menus[] = {"levels", "popmenu_act1", "popmenu_act2", "popmenu_act3", "popmenu_difficulty"};
	for (size_t i = 0; i < sizeof(menus) / sizeof(menus[0]); i++) {
		uint8_t *m = ui_find_menu(menus[i]);
		if (m && (*(uint32_t *)(m + ITEM_DYNAMIC_FLAGS) & WINDOW_VISIBLE))
			picking_seen = GetTickCount();
	}
	if (GetTickCount() - picking_seen > 1500) {
		InterlockedExchange(&picking, 0);
		log_printf("menus: mission pick for the lobby cancelled");
	}
}

// Unlock all missions for the co-op host's pick (issue #3): the game's mission list gates on the dvar mis_cheat
// (the cheat that opens every mission; the profile is untouched). It is 1 only while one of the list's menus is
// open in a co-op pick or lobby, and put back as it was afterwards, so the player's own Mission Select and saves
// keep their rules.
static int cheat_on;
static char cheat_old[16];

static void unlock_missions_tick(void);

// ---------------------------------------------------------------- the lobby with a campaign mission

// Both PCs: the party is up, no level runs, and the lobby's mission is not a Spec Ops one
// (the client learns coop_mapName from the host's coopAccept).
static int campaign_lobby(void)
{
	if (!base[RVA_PARTY_ACTIVE])
		return 0;
	char v[64] = "";
	game_dvar_raw("sv_running", v, sizeof(v));
	if (v[0] == '1')
		return 0;
	game_dvar_raw("coop_mapName", v, sizeof(v));
	return v[0] && strncmp(v, "so_", 3) != 0;
}

// Items changed while the lobby holds a campaign mission; the native values are kept to switch
// back for Spec Ops.
typedef struct {
	const char *item;   // name, "name+1" for the unnamed item right after it, or "@TEXT" for the item showing it
	int offset;
	const char *script; // replacement action ...
	const void *value;  // ... or replacement pointer (NULL clears a statement)
	uint8_t *seen;	    // the item the native value was read from (zone reloads bring new ones)
	void *native;
	void *campaign;
} LobbySwap;

static LobbySwap lobby_swaps[] = {
	// the title: "SPECIAL OPS : Two players online" -> "CAMPAIGN : ..."
	{"@MENU_SPECIAL_OPS", ITEM_TEXT, NULL, "@MW2COOP_CAMPAIGN", NULL, NULL, NULL},
	// Change Op: the campaign missions
	{"selectmap", ITEM_ACTION, "\"play\" \"mouse_click\" ; \"open\" \"levels\" ; ", NULL, NULL, NULL, NULL},
	// Start: the native button needs coopready(), which counts the Steam lobby's members; a
	// partner who joined by address has none. mw2coop_start checks the party and the ready state.
	{"startmatch", ITEM_ACTION, "\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_start\" ; ", NULL, NULL, NULL, NULL},
	{"startmatch", ITEM_FLOAT_EXPS, NULL, NULL, NULL, NULL, NULL}, // disabled when ( !gamehost() || !coopready() )
	{"startmatch", ITEM_DISABLED_EXP, NULL, NULL, NULL, NULL, NULL},
	{"startmatch", ITEM_TEXT, NULL, "@MW2COOP_START", NULL, NULL, NULL},
	// Ready: the Spec Ops difficulty line, shown (greyed) for missions without Spec Ops difficulty
	{"dont_selectdiff", ITEM_ACTION, "\"play\" \"mouse_click\" ; \"exec\" \"mw2coop_ready\" ; ", NULL, NULL, NULL, NULL},
	{"dont_selectdiff", ITEM_FLOAT_EXPS, NULL, NULL, NULL, NULL, NULL}, // disabled when the op has no difficulty
	{"dont_selectdiff", ITEM_TEXT, NULL, "@MW2COOP_READY", NULL, NULL, NULL},
	// its focus hint read "not available for this op"
	{"dont_selectdiff", ITEM_ON_FOCUS, "\"play\" \"mouse_over\" ; \"setItemColor\" \"self\" \"backcolor\" 0 0 0 1 ; ", NULL, NULL, NULL, NULL},
	// the greyed "Select difficulty" text drawn on the same line
	{"dont_selectdiff+1", ITEM_TEXT, NULL, "", NULL, NULL, NULL},
};

static int lobby_campaign_set;
static void patch_lobby_mission(uint8_t *menu, int campaign);

// the item showing <text> natively (or our text, once swapped: the swap remembers its item)
static int item_by_text(uint8_t *menu, const char *text, const LobbySwap *w)
{
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	for (int i = 0; i < count; i++) {
		const char *t = *(const char **)(items[i] + ITEM_TEXT);
		if (items[i] == w->seen || (t && strcmp(t, text) == 0))
			return i;
	}
	return -1;
}

static void patch_lobby(int campaign)
{
	uint8_t *menu = ui_find_menu("coop_lobby");
	if (!menu)
		return;
	int changed = 0;
	for (size_t k = 0; k < sizeof(lobby_swaps) / sizeof(lobby_swaps[0]); k++) {
		LobbySwap *w = &lobby_swaps[k];
		char name[64];
		lstrcpynA(name, w->item, sizeof(name));
		char *plus = strchr(name, '+');
		if (plus)
			*plus = 0;
		int i = name[0] == '@' ? item_by_text(menu, name, w) : ui_item_index(menu, name);
		if (i < 0)
			continue;
		if (plus && ++i >= *(int *)(menu + MENU_ITEM_COUNT))
			continue;
		uint8_t *item = (*(uint8_t ***)(menu + MENU_ITEMS))[i];
		void **field = (void **)(item + w->offset);
		if (item != w->seen) {
			w->seen = item;
			w->native = *field;
		}
		if (!w->campaign)
			w->campaign = w->script ? ui_script_set(w->script) : (void *)w->value;
		void *want = campaign ? w->campaign : w->native;
		if (*field != want) {
			*field = want;
			changed = 1;
		}
	}
	patch_lobby_mission(menu, campaign);
	if (changed || campaign != lobby_campaign_set)
		log_printf("menus: coop_lobby set for %s", campaign ? "a campaign mission" : "Spec Ops");
	lobby_campaign_set = campaign;
}

// The lobby's mission picture and name: Spec Ops ops get them from sp/specOpsTable.csv (the
// picture's materialExp, the name's textExp); a campaign mission has no row there and showed a
// checkerboard. With a campaign mission the expressions are switched off and the item shows the
// mission list's own: material "levelshot_mw2_<map>", text "@MENU_SP_SP_<MAP>".
#define ASSET_TYPE_MATERIAL 5
#define ITEM_BACKGROUND ITEM_BACKGROUND_PTR
#define ITEM_MATERIAL_EXP ITEM_MATERIAL_EXP_PTR
static uint8_t *pic_item, *name_item;
static void *pic_native_material, *pic_native_exp, *name_native_exp;
static const char *name_native_text;
static char shown_map[64];

// splitscreen.c: after a video restart the lobby's mission picture is a check pattern until it is set again
void menus_video_restarted(void)
{
	shown_map[0] = 0;
}

static void find_mission_items(uint8_t *menu)
{
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	uint8_t *pic = NULL, *name = NULL;
	for (int i = 0; i < count; i++) {
		float *r = (float *)(items[i] + ITEM_RECT);
		if (r[2] == 216 && r[3] == 120 && *(void **)(items[i] + ITEM_MATERIAL_EXP))
			pic = items[i];
	}
	for (int i = 0; pic && i < count; i++) {
		float *r = (float *)(items[i] + ITEM_RECT), *pr = (float *)(pic + ITEM_RECT);
		if (r[2] == 216 && r[3] == 20 && r[1] == pr[1] - 20 && *(void **)(items[i] + ITEM_TEXT_EXP))
			name = items[i];
	}
	if (!pic || !name)
		return;
	if (pic != pic_item) {
		pic_item = pic;
		pic_native_material = *(void **)(pic + ITEM_BACKGROUND);
		pic_native_exp = *(void **)(pic + ITEM_MATERIAL_EXP);
	}
	if (name != name_item) {
		name_item = name;
		name_native_exp = *(void **)(name + ITEM_TEXT_EXP);
		name_native_text = *(const char **)(name + ITEM_TEXT);
	}
}

static void patch_lobby_mission(uint8_t *menu, int campaign)
{
	find_mission_items(menu);
	if (!pic_item || !name_item)
		return;
	if (!campaign) {
		*(void **)(pic_item + ITEM_MATERIAL_EXP) = pic_native_exp;
		*(void **)(pic_item + ITEM_BACKGROUND) = pic_native_material;
		*(void **)(name_item + ITEM_TEXT_EXP) = name_native_exp;
		*(const char **)(name_item + ITEM_TEXT) = name_native_text;
		shown_map[0] = 0;
		return;
	}
	char map[64] = "";
	game_dvar_raw("coop_mapName", map, sizeof(map));
	if (!map[0] || strcmp(map, shown_map) == 0)
		return;
	lstrcpynA(shown_map, map, sizeof(shown_map));
	// only the missions of the campaign list have a picture: asking the asset database for one
	// that does not exist ("levelshot_mw2_ending") hung the game. The knife fight map (ending)
	// shows Endgame's.
	static const char *const listed[] = {"trainer", "roadkill", "cliffhanger", "airport", "favela", "invasion",
		"favela_escape", "arcadia", "oilrig", "gulag", "dcburning", "contingency", "dcemp", "dc_whitehouse",
		"estate", "boneyard", "af_caves", "af_chase"};
	const char *shown = strcmp(map, "ending") == 0 ? "af_chase" : map;
	int in_list = 0;
	for (size_t i = 0; i < sizeof(listed) / sizeof(listed[0]); i++)
		in_list |= strcmp(shown, listed[i]) == 0;
	if (!in_list) {
		log_printf("menus: no picture for '%s'", map);
		return;
	}
	static char material[96], text[96];
	snprintf(material, sizeof(material), "levelshot_mw2_%s", shown);
	int n = snprintf(text, sizeof(text), "@MENU_SP_SP_");
	for (const char *m = shown; *m && n < (int)sizeof(text) - 1; m++)
		text[n++] = (char)((*m >= 'a' && *m <= 'z') ? *m - 32 : *m);
	text[n] = 0;
	typedef void *(*find_t)(int, const char *);
	*(void **)(pic_item + ITEM_MATERIAL_EXP) = NULL;
	*(void **)(pic_item + ITEM_BACKGROUND) = ((find_t)(base + RVA_DB_FIND_HEADER))(ASSET_TYPE_MATERIAL, material);
	*(void **)(name_item + ITEM_TEXT_EXP) = NULL;
	*(const char **)(name_item + ITEM_TEXT) = text;
	log_printf("menus: lobby shows %s (%s)", material, text);
}

// Ready state: each PC sends its own to the partner every second while the lobby is open
// ("\xff\xff\xff\xffmw2coop_ready 0|1 <player id> <Steam ID>", net.c); a partner not heard from
// for 3 s is not there. The player id (settings.ini) and the Steam ID tell the host who the
// partner is: continuing saved progress with someone else asks first (mw2coop_partner popup).
#define READY_TAG "mw2coop_ready "
static volatile LONG my_ready, partner_ready;
static volatile DWORD partner_heard, ready_sent;
static char partner_id[33];
static uint64_t partner_xuid;
static int continuing;		// the host's lobby opened on its saved progress
static volatile LONG partner_new;	// a partner id arrived: look up the pair's save (tick)
static volatile LONG partner_confirmed;	// the host answered both partner popups
static int add_member;		// ...and said yes to adding the partner to the save's members

static int partner_present(void)
{
	return partner_heard && GetTickCount() - partner_heard < 3000;
}

static int party_full(void)
{
	uint8_t *lobby = *(uint8_t **)(base + RVA_PARTY_LOBBY);
	if (!lobby)
		return 0;
	int members = 0;
	for (int i = 0; i < 2; i++)
		members += FN_SLOT_USED(lobby, i) != 0;
	return members == 2;
}

static void send_ready(void)
{
	char text[96];
	snprintf(text, sizeof(text), READY_TAG "%ld %s %016llx", my_ready, settings_player_id(),
		 (unsigned long long)*(uint64_t *)(base + RVA_XUID_TABLE));
	net_send_mod(text);
	ready_sent = GetTickCount();
}

// net.c: a lobby query from the network, or a host's answer to ours. Returns 1 if it was one.
int menus_on_lobby_packet(const char *buf, int len, const uint8_t *from_ip, uint16_t from_port, char *reply,
			  int reply_size, int *reply_len)
{
	*reply_len = 0;
	static const char find[] = "\xff\xff\xff\xff" FIND_TAG;
	static const char here[] = "\xff\xff\xff\xff" HERE_TAG;
	if (len == (int)sizeof(find) - 1 && memcmp(buf, find, len) == 0) {
		// answer only while hosting a campaign lobby that still has room
		char lock = access_lobby_flag();
		if (base[RVA_PARTY_IS_HOST] && lobby_campaign_set && !party_full() && lock) {
			char map[64] = "", name[33] = "";
			game_dvar_raw("coop_mapName", map, sizeof(map));
			// the host's name as its party has it (slot 0, rva 0x5dd960, set by the party join 0xd7220)
			lstrcpynA(name, (const char *)base + RVA_PARTY_MEMBER_NAMES, sizeof(name));
			uint64_t xuid = *(uint64_t *)(base + RVA_XUID_TABLE);
			*reply_len = snprintf(reply, reply_size, "%s%016llx %c %s %s", here, (unsigned long long)xuid, lock, map, name);
			log_printf("menus: answered a lobby query from %u.%u.%u.%u:%u", from_ip[0], from_ip[1], from_ip[2],
				   from_ip[3], from_port);
		}
		return 1;
	}
	if (len >= (int)sizeof(here) - 1 && memcmp(buf, here, sizeof(here) - 1) == 0) {
		if (!finding)
			return 1;
		// "<xuid> <p|-> <map> <host name>"
		char text[128];
		int n = len - ((int)sizeof(here) - 1);
		n = n < (int)sizeof(text) - 1 ? n : (int)sizeof(text) - 1;
		memcpy(text, buf + sizeof(here) - 1, n);
		text[n] = 0;
		char *flag = strchr(text, ' ');
		if (!flag || flag[1] == 0 || flag[2] != ' ')
			return 1;
		*flag++ = 0;
		char *map = flag + 2;
		char *name = strchr(map, ' ');
		if (name)
			*name++ = 0;
		uint64_t xuid = strtoull(text, NULL, 16);
		EnterCriticalSection(&lobby_lock);
		int known = 0;
		for (int i = 0; i < lobby_count; i++)
			known |= lobbies[i].xuid == xuid;
		if (!known && lobby_count < MAX_LOBBIES) {
			Lobby *l = &lobbies[lobby_count++];
			memcpy(l->ip, from_ip, 4);
			l->port = from_port;
			l->xuid = xuid;
			l->locked = *flag == 'p';
			lstrcpynA(l->map, map, sizeof(l->map));
			lstrcpynA(l->name, name ? name : "", sizeof(l->name));
			log_printf("menus: lobby at %u.%u.%u.%u:%u: %s, host '%s'%s", from_ip[0], from_ip[1], from_ip[2],
				   from_ip[3], from_port, l->map, l->name, l->locked ? ", password" : "");
		}
		LeaveCriticalSection(&lobby_lock);
		return 1;
	}
	return 0;
}

int menus_on_packet(const char *buf, int len)
{
	static const char tag[] = "\xff\xff\xff\xff" READY_TAG;
	int tl = (int)sizeof(tag) - 1;
	if (len < tl + 1 || memcmp(buf, tag, tl) != 0)
		return 0;
	LONG ready = buf[tl] == '1';
	char rest[96] = "", id[33] = "";
	int n = len - tl - 1 < (int)sizeof(rest) - 1 ? len - tl - 1 : (int)sizeof(rest) - 1;
	if (n > 0)
		memcpy(rest, buf + tl + 1, n);
	unsigned long long xuid = 0;
	if (sscanf(rest, " %32s %llx", id, &xuid) >= 1 && strcmp(id, partner_id) != 0) {
		lstrcpynA(partner_id, id, sizeof(partner_id));
		partner_xuid = xuid;
		log_printf("menus: partner id %s, Steam ID %016llx", partner_id, xuid);
		InterlockedExchange(&partner_new, 1);
	}
	if (InterlockedExchange(&partner_ready, ready) != ready || !partner_present())
		log_printf("menus: partner %s", ready ? "ready" : "not ready");
	partner_heard = GetTickCount();
	return 1;
}

// the native split screen lobby: both players pressed a button on their devices
static int split_devices_chosen(void)
{
	return instance_device(0) != DEVICE_NONE && instance_device(1) != DEVICE_NONE;
}

static void update_texts(void)
{
	int host = base[RVA_PARTY_IS_HOST];
	// a split screen lobby has no Ready button: the second player is on this PC, his device is all that is needed, his process comes with Start
	if (split_mode)
		localize_set_text("MW2COOP_READY", "");
	else
		localize_select("MW2COOP_READY", my_ready ? "MW2COOP_READY_UNSET" : "MW2COOP_READY_SET");
	show_row(ui_find_menu("coop_lobby"), "dont_selectdiff", !split_mode);
	const char *start;
	if (split_mode && native_split_available())
		start = split_devices_chosen() ? "MW2COOP_START_GO" : "MW2COOP_START_NEED_DEVICES";
	else if (split_mode)
		start = instance_pad() < 0 ? "MW2COOP_START_NEED_PARTNER" : "MW2COOP_START_GO";
	else if (!partner_present() || (host && !party_full()))
		start = "MW2COOP_START_NEED_PARTNER";
	else if (my_ready && partner_ready)
		start = host ? "MW2COOP_START_GO" : "MW2COOP_START_HOST";
	else if (my_ready)
		start = "MW2COOP_START_WAIT_PARTNER";
	else if (partner_ready)
		start = "MW2COOP_START_WAIT_YOU";
	else
		start = "MW2COOP_START_WAIT_BOTH";
	localize_select("MW2COOP_START", start);
}

// mw2coop_ready: the lobby's ready button (both PCs)
static void ready_f(void)
{
	if (split_mode)
		return; // no Ready button in a split screen lobby
	InterlockedExchange(&my_ready, !my_ready);
	log_printf("menus: %s", my_ready ? "ready" : "not ready");
	send_ready();
	update_texts();
}

static void partner_questions(void);
static void start_launch(void);
static volatile LONG split_starting; // a split screen Start is on its way (the screen change, the second process, its join)

// instance.c: the second player's device or Ready changed
void menus_split_changed(void)
{
	update_texts();
}

// A split screen Start: the second process was started, it joins this lobby by itself and marks itself ready; then the launch goes on as usual
static DWORD WINAPI split_wait_thread(LPVOID unused)
{
	(void)unused;
	for (int i = 0; i < 2400 && !(party_full() && partner_present() && partner_ready); i++)
		Sleep(50); // two minutes: the second process boots, joins and marks itself ready
	if (!(party_full() && partner_present() && partner_ready)) {
		log_printf("menus: Start: the second process did not join the lobby");
		return 0;
	}
	log_printf("menus: Start: the second process is in the lobby");
	start_launch();
	return 0;
}

// A split screen Start, first part: the screen is made ready (a window as big as the screen: a full screen game would close when the second process takes
// the focus), then the second process is started
static DWORD WINAPI split_start_thread(LPVOID unused)
{
	(void)unused;
	splitscreen_cover(1); // black over the screen: the second process starts and the video mode changes behind it
	if (instance_start(instance_pad()))
		split_wait_thread(NULL);
	InterlockedExchange(&split_starting, 0);
	return 0;
}

// The native split screen's Start (src/native.c): the second player is on this PC, in this process. The lobby's party is let go, the game becomes a two local players game and the
// mission the lobby shows starts as a normal level.
static volatile LONG native_started; // tick count of the launch, for tick_f's check that the session ends
static void native_launch(void)
{
	if (native_split_active()) // a second press of Start while the mission loads
		return;
	if (!split_devices_chosen()) {
		log_printf("menus: Start: the players have not both pressed a button on their devices yet");
		return;
	}
	char map[64] = "";
	game_dvar_raw("coop_mapName", map, sizeof(map));
	if (!map[0] || strpbrk(map, " \t;\"\n"))
		lstrcpyA(map, "trainer");
	// the second player is a member of the save like a partner who joins it
	progress_begin(continuing, "split-screen", 0x53504c4954ULL, "Player 2", 1);
	int dev0 = instance_device(0), dev1 = instance_device(1);
	if (!native_split_start2(dev0, dev1)) {
		log_printf("menus: Start: the split screen could not be started");
		return;
	}
	log_printf("menus: Start: native split screen, mission '%s', player 1 on %d, player 2 on %d (-1 keyboard)", map, dev0, dev1);
	instance_assign_end();
	InterlockedExchange(&split_mode, 0);
	((void (*)(void))(base + RVA_PARTY_RESET))();
	char cmd[160];
	snprintf(cmd, sizeof(cmd), "stopListening\nset mw2coop_split_view 0\nmap %s\n", map);
	game_cbuf_addtext(cmd);
	InterlockedExchange(&native_started, (LONG)GetTickCount() | 1);
}

// tick_f: the session ends once the game is back in the menus (or when the level never started)
static void native_session_tick(void)
{
	static int level_seen;
	if (!native_split_active() || !native_started) { // (a session the settings file started is not ours to end)
		level_seen = 0;
		return;
	}
	char v[16] = "";
	game_dvar_raw("sv_running", v, sizeof(v));
	int running = v[0] == '1';
	if (running)
		level_seen = 1;
	if ((level_seen && !running) || (!level_seen && GetTickCount() - (DWORD)native_started > 30000)) {
		level_seen = 0;
		InterlockedExchange(&native_started, 0);
		native_split_stop();
	}
}

// mw2coop_start: the lobby's Start in campaign mode. The party frame launches once both slots
// are taken and the bandwidth test is done.
static void start_f(void)
{
	if (split_mode && native_split_available()) { // the second player is on this PC: no party, the host check does not apply
		native_launch();
		return;
	}
	if (!base[RVA_PARTY_IS_HOST]) {
		log_printf("menus: Start: only the host starts");
		return;
	}
	if (!access_can_start())
		return; // password mode without a password: the popup asks for it
	if (split_mode) {
		if (split_starting)
			return; // it is on its way: the threads go on
		if (instance_pad() < 0) {
			log_printf("menus: Start: the second player has not pressed a button on his device yet");
			return;
		}
		log_printf("menus: Start: starting the second game process");
		InterlockedExchange(&split_starting, 1);
		CreateThread(NULL, 0, split_start_thread, NULL, 0, NULL);
		return;
	}
	if (!party_full() || !partner_present()) {
		log_printf("menus: Start: waiting for the second player");
		return;
	}
	if (!my_ready || !partner_ready) {
		log_printf("menus: Start: not everyone is ready (host %ld, partner %ld)", my_ready, partner_ready);
		return;
	}
	start_launch();
}

// the launch itself, once everyone is in and ready (a normal Start, or the split screen one when the second process has joined)
static void start_launch(void)
{
	if (continuing && !partner_confirmed && progress_partner_is_new(partner_id, partner_xuid)) {
		log_printf("menus: Start: the partner is not a member of this save, asking");
		partner_questions();
		game_cbuf_addtext("openmenu mw2coop_partner\n");
		return;
	}
	progress_begin(continuing, partner_id, partner_xuid, (const char *)base + RVA_PARTY_MEMBER_NAMES + 0x38, // slot 1
		       add_member);
	log_printf("menus: Start: coopLaunch");
	if (!splitscreen_before_launch())
		game_cbuf_addtext("coopLaunch\n");
}

// The popups' questions with the names in them, in the game's language.
static void partner_questions(void)
{
	char map[64] = "", members[96] = "", key[80] = "MENU_SP_SP_", text[256];
	const char *partner = (const char *)base + RVA_PARTY_MEMBER_NAMES + 0x38; // slot 1
	progress_chosen_info(map, sizeof(map), members, sizeof(members));
	int n = (int)strlen(key);
	for (const char *m = map; *m && n < (int)sizeof(key) - 1; m++)
		key[n++] = (char)((*m >= 'a' && *m <= 'z') ? *m - 32 : *m);
	key[n] = 0;
	const char *mission = localize_text(key), *fmt;
	if ((fmt = localize_text("MW2COOP_PARTNER_FMT"))) {
		snprintf(text, sizeof(text), fmt, mission ? mission : map, members, partner);
		localize_set_text("MW2COOP_PARTNER_Q", text);
	}
	if ((fmt = localize_text("MW2COOP_MEMBER_FMT"))) {
		snprintf(text, sizeof(text), fmt, partner);
		localize_set_text("MW2COOP_MEMBER_Q", text);
	}
	if ((fmt = localize_text("MW2COOP_NEWGAME_FMT"))) {
		snprintf(text, sizeof(text), fmt, partner);
		localize_set_text("MW2COOP_NEWGAME_Q", text);
	}
}

// mw2coop_partner_yes: the first popup's Yes (continue with this partner): ask about the members
static void partner_yes_f(void)
{
	game_cbuf_addtext("openmenu mw2coop_member\n");
}

// mw2coop_member_yes: the second popup's Yes, the partner joins this save's members
static void member_yes_f(void)
{
	add_member = 1;
	log_printf("menus: the partner joins this save's members");
	InterlockedExchange(&partner_confirmed, 1);
	start_f();
}

// mw2coop_member_no: the second popup's No, ask about a new game
static void member_no_f(void)
{
	game_cbuf_addtext("openmenu mw2coop_newgame\n");
}

// mw2coop_new_game: the third popup's Yes, a new game from the start (The Pit, a new save); the
// old save stays as it was
static void new_game_f(void)
{
	add_member = 0;
	continuing = 0;
	game_cbuf_addtext("set coop_mapName trainer\nset start \"\"\n"); // before start_f's coopLaunch
	log_printf("menus: a new game from the start");
	InterlockedExchange(&partner_confirmed, 1);
	start_f();
}

// A yes/no question (a copy of the quit popup, as firewall.c does).
static void yes_no_popup(const char *name, const char *title, const char *question, const char *yes_action,
			 const char *no_action)
{
	if (ui_find_menu(name))
		return;
	// From the difficulty popup (rows_popup), not the quit popup: the difficulty popup is opened
	// from the lobby in Spec Ops. Its own text items stay hidden whatever they are given, so the
	// question (up to three lines) is a copy of the title item one line under it; rows: Yes, No,
	// lowered under the question.
	static const char *const names[LIST_ROWS] = {"yes", "no", "mw2coop_row_3", "mw2coop_row_4"};
	static const char *const texts[LIST_ROWS] = {"@MENU_YES", "@MENU_NO", "", ""};
	const char *const actions[LIST_ROWS] = {yes_action, no_action, "", ""};
	uint8_t *menu = rows_popup(name, title, names, texts, actions);
	if (!menu)
		return;
	show_row(menu, "mw2coop_row_3", 0);
	show_row(menu, "mw2coop_row_4", 0);
	int count = *(int *)(menu + MENU_ITEM_COUNT);
	uint8_t **items = *(uint8_t ***)(menu + MENU_ITEMS);
	int at = -1;
	for (int i = 0; i < count; i++)
		if (*(const char **)(items[i] + ITEM_TEXT) == title)
			at = i;
	if (at < 0)
		return;
	uint8_t *text = ui_clone_item(items[at], menu); // by a difference: the two rects hold different origins
	*(const char **)(text + ITEM_TEXT) = question;
	*(float *)(text + ITEM_RECT_Y) += 26;
	*(float *)(text + ITEM_RECT_CLIENT_Y) += 26;
	uint8_t **more = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(*more));
	memcpy(more, items, count * sizeof(*more));
	more[count] = text;
	*(uint8_t ***)(menu + MENU_ITEMS) = more;
	*(int *)(menu + MENU_ITEM_COUNT) = count + 1;
	int rows[2] = {ui_item_index(menu, "yes"), ui_item_index(menu, "no")};
	for (int i = 0; i < 2; i++) {
		*(float *)(more[rows[i]] + ITEM_RECT_Y) += 72;
		*(float *)(more[rows[i]] + ITEM_RECT_CLIENT_Y) += 72;
	}
	log_printf("menus: popup %s added", name);
}

// Start with a partner who is not a member of the save: continue with him? Then: add him to this
// playthrough's members? No to that: start a new game from the very beginning? No anywhere closes
// the popup and nothing starts.
static void add_partner_popups(void)
{
	yes_no_popup("mw2coop_partner", "@MW2COOP_PARTNER_TITLE", "@MW2COOP_PARTNER_Q",
		     "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_partner_yes\" ; ",
		     "\"play\" \"mouse_click\" ; \"close\" \"self\" ; ");
	yes_no_popup("mw2coop_member", "@MW2COOP_MEMBER_TITLE", "@MW2COOP_MEMBER_Q",
		     "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_member_yes\" ; ",
		     "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_member_no\" ; ");
	yes_no_popup("mw2coop_newgame", "@MW2COOP_NEWGAME_TITLE", "@MW2COOP_NEWGAME_Q",
		     "\"play\" \"mouse_click\" ; \"close\" \"self\" ; \"exec\" \"mw2coop_new_game\" ; ",
		     "\"play\" \"mouse_click\" ; \"close\" \"self\" ; ");
}

// mw2coop_online: run by the campaign menu's co-op button before "listen" opens the lobby
static void online_f(void)
{
	char map[64] = "trainer", start[64] = "";
	const char *chosen = game_cmd_argv(1);
	continuing = 0;
	add_member = 0;
	InterlockedExchange(&partner_confirmed, 0);
	if (chosen[0] && !strpbrk(chosen, " \t;\"\n")) {
		lstrcpynA(map, chosen, sizeof(map));
		log_printf("menus: campaign lobby with '%s' from the start", map);
	} else if (progress_chosen(map, sizeof(map), start, sizeof(start))) {
		log_printf("menus: campaign lobby, continuing at '%s' start '%s'", map, start);
		continuing = 1;
	}
	else
		log_printf("menus: campaign lobby, no progress yet: '%s'", map);
	char cmd[320], spot[64] = "";
	if (continuing)
		progress_chosen_spot(spot, sizeof(spot));
	snprintf(cmd, sizeof(cmd), "set coop_mapName %s\nset start \"%s\"\nset mw2coop_cp_start \"%s\"\nset mw2coop_cp_pos \"%s\"\n", map,
		 start, spot[0] ? start : "", spot);
	game_cbuf_addtext(cmd);
}

// devmap in the host's lobby: run by the campaign mission list (The Pit directly, the other
// missions from the difficulty popup); it sets the lobby's mission instead of starting solo.
static void (*devmap_native)(void);

// The list shows "cheats enabled" while mis_cheat is 1: the item's text is an empty string during the co-op pick.
static void hide_cheat_text(int hide)
{
	static const char *const menus[] = {"levels", "popmenu_act1", "popmenu_act2", "popmenu_act3"};
	static const char *saved[4];
	static const char empty[] = "";
	for (size_t i = 0; i < sizeof(menus) / sizeof(menus[0]); i++) {
		uint8_t *m = ui_find_menu(menus[i]);
		int at = m ? ui_item_index(m, "cheat_indicator") : -1;
		if (at < 0)
			continue;
		const char **text = (const char **)((*(uint8_t ***)(m + MENU_ITEMS))[at] + ITEM_TEXT);
		if (hide && *text != empty) {
			saved[i] = *text;
			*text = empty;
		} else if (!hide && *text == empty) {
			*text = saved[i];
		}
	}
}

static void unlock_missions_tick(void)
{
	int want = 0;
	if (picking || campaign_lobby()) {
		static const char *const menus[] = {"levels", "popmenu_act1", "popmenu_act2", "popmenu_act3"};
		for (size_t i = 0; i < sizeof(menus) / sizeof(menus[0]); i++) {
			uint8_t *m = ui_find_menu(menus[i]);
			if (m && (*(uint32_t *)(m + ITEM_DYNAMIC_FLAGS) & WINDOW_VISIBLE))
				want = 1;
		}
	}
	if (cheat_on)
		hide_cheat_text(want);
	if (want && !cheat_on) {
		char buf[32];
		game_dvar_string("mis_cheat", buf, sizeof(buf));
		snprintf(cheat_old, sizeof(cheat_old), "%s",
			 (strcmp(buf, "<missing>") == 0 || strcmp(buf, "false") == 0) ? "0" : (strcmp(buf, "true") == 0 ? "1" : buf));
		game_cbuf_addtext("set mis_cheat 1\n");
		cheat_on = 1;
		log_printf("menus: mission list open for the lobby: mis_cheat 1 (was %s), all missions selectable", cheat_old);
	} else if (!want && cheat_on) {
		char cmd[48];
		snprintf(cmd, sizeof(cmd), "set mis_cheat %s\n", cheat_old);
		game_cbuf_addtext(cmd);
		cheat_on = 0;
		log_printf("menus: mission list closed: mis_cheat back to %s", cheat_old);
	}
}

static void devmap_f(void)
{
	char map[64];
	lstrcpynA(map, game_sv_cmd_argv(1), sizeof(map));
	if (picking && map[0] && !strpbrk(map, " \t;\"\n")) {
		InterlockedExchange(&picking, 0);
		game_cbuf_addtext("closemenu popmenu_act1\nclosemenu popmenu_act2\nclosemenu popmenu_act3\nclosemenu levels\n");
		log_printf("menus: hosting '%s' (picked from the list)", map);
		host_lobby(map);
		return;
	}
	if (!campaign_lobby() || !base[RVA_PARTY_IS_HOST] || !map[0] || strpbrk(map, " \t;\"\n")) {
		devmap_native();
		return;
	}
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		 "set coop_mapName %s\nset start \"\"\n"
		 "closemenu popmenu_act1\nclosemenu popmenu_act2\nclosemenu popmenu_act3\nclosemenu levels\n",
		 map);
	game_cbuf_addtext(cmd);
	log_printf("menus: campaign lobby mission -> '%s'", map);
}

static void tick_f(void);

static cmd_function_t commands[] = {
	{NULL, "mw2coop_online", NULL, NULL, online_f CMD_NODE_FLAG},
	{NULL, "mw2coop_host_menu", NULL, NULL, host_menu_f CMD_NODE_FLAG},
	{NULL, "mw2coop_split_menu", NULL, NULL, split_menu_f CMD_NODE_FLAG},
	{NULL, "mw2coop_split_menu_open", NULL, NULL, split_menu_open_f CMD_NODE_FLAG},
	{NULL, "mw2coop_host_go", NULL, NULL, host_go_f CMD_NODE_FLAG},
	{NULL, "mw2coop_host_new", NULL, NULL, host_new_f CMD_NODE_FLAG},
	{NULL, "mw2coop_host_pick", NULL, NULL, host_pick_f CMD_NODE_FLAG},
	{NULL, "mw2coop_start", NULL, NULL, start_f CMD_NODE_FLAG},
	{NULL, "mw2coop_ready", NULL, NULL, ready_f CMD_NODE_FLAG},
	{NULL, "mw2coop_partner_yes", NULL, NULL, partner_yes_f CMD_NODE_FLAG},
	{NULL, "mw2coop_member_yes", NULL, NULL, member_yes_f CMD_NODE_FLAG},
	{NULL, "mw2coop_member_no", NULL, NULL, member_no_f CMD_NODE_FLAG},
	{NULL, "mw2coop_new_game", NULL, NULL, new_game_f CMD_NODE_FLAG},
	{NULL, "mw2coop_saves_menu", NULL, NULL, saves_menu_f CMD_NODE_FLAG},
	{NULL, "mw2coop_save_pick", NULL, NULL, save_pick_f CMD_NODE_FLAG},
	{NULL, "mw2coop_menus_tick", NULL, NULL, tick_f CMD_NODE_FLAG},
	{NULL, "mw2coop_find", NULL, NULL, find_f CMD_NODE_FLAG},
	{NULL, "mw2coop_join_found", NULL, NULL, join_found_f CMD_NODE_FLAG},
};

// The menu work runs on the game's main thread, between frames: the control thread only queues
// "mw2coop_menus_tick" (every 250 ms, never while a level runs or loads). The UI zone may be
// reloaded (back from a level), which brings fresh menus without our changes.
static volatile LONG tick_queued;
static volatile DWORD tick_queued_at;

void menus_poll(void)
{
	if (!base)
		return;
	char v[8] = "", coop[8] = "";
	game_dvar_raw("sv_running", v, sizeof(v));
	game_dvar_raw("coop", coop, sizeof(coop));
	// hosting: the host's party is up (lobby), or a co-op level runs on the host
	upnp_want(base[RVA_PARTY_IS_HOST] && (base[RVA_PARTY_ACTIVE] || (v[0] == '1' && coop[0] == '1')));
	if (v[0] == '1')
		return;
	// the queue is cleared by a map load; a tick queued before it never runs
	if (tick_queued && GetTickCount() - tick_queued_at < 3000)
		return;
	tick_queued_at = GetTickCount();
	InterlockedExchange(&tick_queued, 1);
	game_cbuf_addtext("mw2coop_menus_tick\n");
}

static void tick_f(void)
{
	InterlockedExchange(&tick_queued, 0);
	if (!base)
		return;
	native_session_tick();
	patch_main_menu();
	add_lobbies_menu();
	add_host_menu();
	add_saves_menu();
	add_partner_popups();
	picking_tick();
	unlock_missions_tick();
	find_tick();
	int campaign = campaign_lobby();
	patch_lobby(campaign);
	access_tick(campaign, base[RVA_PARTY_IS_HOST]);
	firewall_tick(campaign && base[RVA_PARTY_IS_HOST] && !split_mode); // the second player is on this PC: nothing to let in
	if (!campaign) {
		InterlockedExchange(&my_ready, 0);
		InterlockedExchange(&partner_ready, 0);
		partner_heard = 0;
		partner_id[0] = 0;
		partner_xuid = 0;
		return;
	}
	if (GetTickCount() - ready_sent >= 1000)
		send_ready();
	// the host continuing ("Continue"): once the partner is known, the lobby moves to his newest save
	if (InterlockedExchange(&partner_new, 0) && base[RVA_PARTY_IS_HOST] && continuing && !save_picked) {
		char map[64], start[64];
		if (progress_find_with(partner_id, partner_xuid, map, sizeof(map), start, sizeof(start))) {
			char cmd[320], spot[64] = "";
			progress_chosen_spot(spot, sizeof(spot));
			snprintf(cmd, sizeof(cmd), "set coop_mapName %s\nset start \"%s\"\nset mw2coop_cp_start \"%s\"\nset mw2coop_cp_pos \"%s\"\n",
				 map, start, spot[0] ? start : "", spot);
			game_cbuf_addtext(cmd);
			log_printf("menus: continuing this partner's save: '%s' start '%s'", map, start);
		} else
			log_printf("menus: no save with this partner yet");
	}
	update_texts();
	// a split screen lobby: until the second player has pressed a button on his device, the lobby's own hint line (bottom right, where the game says
	// "double-click a friend's name for voice chat") tells him what to do
	static int hint_shown;
	if (split_mode && native_split_available()) { // each player presses a button on his device: the line says who plays on what, and who is still to press
		char line[256] = "", dev[64];
		for (int pl = 0; pl < 2; pl++) {
			int d = instance_device(pl);
			const char *who = localize_text(pl ? "MW2COOP_SPLIT_P2" : "MW2COOP_SPLIT_P1");
			const char *what;
			if (d == DEVICE_NONE)
				what = localize_text("MW2COOP_SPLIT_PRESS");
			else if (d == DEVICE_KEYBOARD)
				what = localize_text("MW2COOP_SPLIT_KEYBOARD");
			else {
				const char *f = localize_text("MW2COOP_SPLIT_PAD");
				snprintf(dev, sizeof(dev), f ? f : "pad %d", d + 1);
				what = dev;
			}
			size_t n = strlen(line);
			snprintf(line + n, sizeof(line) - n, "%s%s%s", pl ? "    " : "", who ? who : "", what ? what : "");
			if (d == DEVICE_NONE)
				break; // player 2 is asked once player 1 has his device
		}
		localize_override("MENU_SP_STEAM_CHAT_HINT", line);
		hint_shown = 1;
	} else if (split_mode && instance_pad() < 0) {
		const char *hint = localize_text("MW2COOP_SPLIT_HINT");
		if (hint) {
			localize_override("MENU_SP_STEAM_CHAT_HINT", hint);
			hint_shown = 1;
		}
	} else if (hint_shown) {
		localize_override("MENU_SP_STEAM_CHAT_HINT", NULL);
		hint_shown = 0;
	}
}

void menus_install(void)
{
	InitializeCriticalSection(&lobby_lock);
	base = (uint8_t *)GetModuleHandleA(NULL);
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		game_add_command(&commands[i]);
	// devmap is a server command: the console node's function is empty and the command goes on
	// to this node (Cmd_AddServerCommand in SV_AddOperatorCommands, rva 0x22bef0)
	cmd_function_t *sv_devmap = (cmd_function_t *)(base + RVA_SV_CMD_DEVMAP);
	if (sv_devmap->name && strcmp(sv_devmap->name, "devmap") == 0 && sv_devmap->function) {
		devmap_native = sv_devmap->function;
		sv_devmap->function = devmap_f;
	}
	log_printf("menus: installed (devmap %s)", devmap_native ? "hooked" : "NOT FOUND");
	if (!devmap_native)
		base = NULL;
}
