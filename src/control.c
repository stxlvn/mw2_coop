// Remote control for QA: lines appended to <game dir>\mw2coop\commands.txt are run in order.
//   plain line        -> Cbuf_AddText(0, line)
//   !cmds             -> log every registered console command
//   !dvars a b c ...  -> log the current values of those dvars ("@rva:size" reads memory)
//   !watch a b c ...  -> log those dvars whenever one of them changes (checked every 250 ms)
//   !sleep <ms>       -> pause the queue
//   !xuid_add <n>     -> QA only: shift this copy's XUID (two copies on one Steam account)
//   !loc <NAME>       -> log a localized string's text and bytes
//   !asset <type> <name>, !mem <address> <size> -> annotated memory dumps (memdump.c)
//   !party            -> QA: log the party slots and timers
//   !netlog <n>       -> QA: log the next n co-op packets
//   !trace <rva>      -> QA: log calls of the builtin method whose table entry is at rva, with GSC stacks
//   mw2coop_bind +activate (a plain line) -> QA: press a button as its key would (see bind_f)
//   !menuaction <menu> <item> -> QA: log what a menu item runs
//   !menus            -> QA: log the names of the menus the UI has loaded
//   !menuitems <menu> -> QA: log a menu's open/close scripts and its items
//   !shot <name>      -> QA: save this machine's next frame as mw2coop\shots\<name>.bmp (shot.c)
//   !burst <prefix>   -> QA: 40 frames in a row of the bottom half, <prefix>-NN.bmp (shot.c, the native split screen); !burstfull: 30 whole frames
//   # comment
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "game.h"
#include "log.h"
#include "transplant.h"
#include "memdump.h"
#include "menus.h"
#include "net.h"
#include "party.h"
#include "progress.h"
#include "scrdebug.h"
#include "shot.h"

#define MAX_WATCH 24

static BOOL CALLBACK qa_find_window(HWND h, LPARAM data)
{
	DWORD pid;
	char cls[32];
	GetWindowThreadProcessId(h, &pid);
	if (pid == GetCurrentProcessId() && GetClassNameA(h, cls, sizeof(cls)) && lstrcmpA(cls, "IW4") == 0) {
		*(*(HWND **)data) = h;
		return FALSE;
	}
	return TRUE;
}

static char watch_names[MAX_WATCH][64];
static char watch_last[MAX_WATCH][256];
static int watch_count;

static void watch_set(char *args)
{
	watch_count = 0;
	for (char *tok = strtok(args, " \t"); tok && watch_count < MAX_WATCH; tok = strtok(NULL, " \t")) {
		lstrcpynA(watch_names[watch_count], tok, sizeof(watch_names[0]));
		watch_last[watch_count][0] = 0;
		watch_count++;
	}
}

// "@<rva hex>:<1|2|4|8>" reads game memory instead of a dvar;
// "*<rva hex>+<off hex>:<size>" reads the pointer at rva, then <size> bytes at pointer+off
static const char *watch_value(const char *name, char *buf, int size)
{
	if (name[0] == '*') {
		char *end;
		unsigned long long rva = strtoull(name + 1, &end, 16);
		unsigned long long off = *end == '+' ? strtoull(end + 1, &end, 16) : 0;
		int width = *end == ':' ? atoi(end + 1) : 4;
		snprintf(buf, size, "0x%llx", game_peek_ptr(rva, off, width));
		return buf;
	}
	if (name[0] != '@')
		return game_dvar_string(name, buf, size);
	char *end;
	unsigned long long rva = strtoull(name + 1, &end, 16);
	int width = *end == ':' ? atoi(end + 1) : 4;
	unsigned long long v = game_peek(rva, width);
	snprintf(buf, size, "0x%llx", v);
	return buf;
}

// Script channel (content/scripts/maps/_mw2coop.gsc): the level scripts run on both machines
// and set these dvars identically. mw2coop_state and mw2coop_qa_log are logged on both;
// requests in mw2coop_cmd ("<what> @<time>") are carried out by the host only.
static char last_state[256], last_qa[256], last_cmd[256], last_shot[96];

static void script_request(const char *cmd)
{
	int host = (int)game_peek(RVA_PARTY_IS_HOST, 1);
	log_printf("script: request '%s' (%s)", cmd, host ? "host, acting" : "client, host acts");
	if (strncmp(cmd, "nextmap ", 8) == 0) {
		char map[64] = "";
		sscanf(cmd + 8, "%63s", map);
		if (map[0] == '@') // last mission: no next one
			map[0] = 0;
		if (host)
			progress_save(map, "", "");
		// Both machines start the next mission themselves (see mw2coop_launch_map in party.c).
		// ChangeLevel() cannot: its exit handler (rva 0x170e60) skips gameMode "so", which a
		// co-op session uses.
		if (map[0]) {
			char launch[128];
			snprintf(launch, sizeof(launch), "set coop_mapName %s\nmw2coop_launch_map\n", map);
			game_cbuf_addtext(launch);
		}
		return;
	}
	if (!host)
		return;
	if (strncmp(cmd, "restart", 7) == 0) {
		game_cbuf_addtext("fast_restart\n"); // SV_MapRestart sends coopRestart to the client
	} else if (strncmp(cmd, "checkpoint ", 11) == 0) {
		char map[64] = "", start[64] = "", spot[64] = "";
		int n = sscanf(cmd + 11, "%63s %63s %63s", map, start, spot);
		if (n >= 2 && start[0] != '@')
			progress_save(map, start, n == 3 && spot[0] != '@' ? spot : "");
	}
}

// In a co-op session GSC getdvar only returns the value of dvars whose flags contain one of
// 0x20c4 (SV_GetDvarCoop @0x230fc0); for all others it returns the default ("") so both
// lockstep machines see the same thing. Dvars made with "set" or script setdvar only carry
// 0x100 (external). These are the ones mw2coop needs scripts to read: give them 0x80 as soon
// as they exist (they keep 0x100, so script setdvar still works). The harness creates them
// before the level starts, so both machines flag them before any script reads them.
static const char *const synced_dvars[] = {"start", "mw2coop_god", "mw2coop_skill", "mw2coop_ff", "mw2coop_cp_start", "mw2coop_cp_pos",
					   "mw2coop_qa", "mw2coop_qa_phase"};

static void sync_dvar_flags(void)
{
	static int done[sizeof(synced_dvars) / sizeof(synced_dvars[0])];
	for (size_t i = 0; i < sizeof(synced_dvars) / sizeof(synced_dvars[0]); i++) {
		dvar_t *d = game_find_dvar(synced_dvars[i]);
		if (!d || (d->flags & 0x20c4))
			continue;
		d->flags |= 0x80;
		if (!done[i]++)
			log_printf("control: dvar %s made readable by co-op scripts (flags now 0x%x)", synced_dvars[i], d->flags);
	}
}

// A film-like scene hides the partner's name over his head (maps\_mw2coop_knife: the knife
// fight). The level hides friendly names with g_friendlyNameDist 0, but a co-op player's name is
// drawn at coopNameFontSize, which scripts may not set (not a saved dvar). While a script holds
// mw2coop_hide_names "1" the size is 0 here; drawing only, both lockstep machines stay the same.
static void partner_name_poll(void)
{
	static float saved = -1;
	char v[16];
	int hide = game_dvar_raw("mw2coop_hide_names", v, sizeof(v)) && strcmp(v, "1") == 0;
	dvar_t *d = game_find_dvar("coopNameFontSize");
	if (!d || d->type != DVAR_TYPE_FLOAT)
		return;
	if (hide && saved < 0) {
		saved = d->current.value;
		d->current.value = 0;
		log_printf("control: partner name hidden (coopNameFontSize %.2f -> 0)", saved);
	} else if (!hide && saved >= 0) {
		d->current.value = saved;
		log_printf("control: partner name shown again (coopNameFontSize %.2f)", saved);
		saved = -1;
	}
}

// QA input: a bot presses a button the way a key bound to it does. For a key the game looks the
// binding's name up in its bind table (rva 0x3be770, the index is the name's place: +attack 1,
// -attack 2, ..., +activate 0x31, -activate 0x32) and runs that index for the local client
// (rva 0xce450): odd indices hold a kbutton down, even ones let it go, and the usercmd built from
// the kbuttons carries the button to the server like any input. "+activate" in the console is
// no command in this build (only +moveup, +speed, +throw and a few more are), so
//   mw2coop_bind +activate | -activate | +forward | ...
// runs the binding itself, on the main thread like any console command. The key number is one
// no keyboard sends, so letting a real key go cannot end the press.
#define QA_KEY           0x1ff

static void bind_f(void)
{
#ifdef _WIN64
#define RVA_BIND_INDEX   0x0d1810 // int (const char *name): the name's place in the bind table, 0 if none
#define RVA_EXEC_BINDING 0x0ce450 // void (int localClient, int index, int key)
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	const char *name = game_cmd_argv(1);
	int index = ((int (*)(const char *))(base + RVA_BIND_INDEX))(name);
	if (!index) {
		log_printf("control: no binding '%s'", name);
		return;
	}
	((void (*)(int, int, int))(base + RVA_EXEC_BINDING))(0, index, QA_KEY);
	log_printf("control: binding %s (%d)", name, index);
#else
	// The 32-bit game has the buttons as console commands ("+activate <key> [time]" / "-activate <key>", the kbutton
	// handlers 0x578340 / 0x5783e0 read the key number from the first argument): the same press with the QA key.
	char cmd[64];
	const char *name = game_cmd_argv(1);
	if (name[0] != '+' && name[0] != '-') {
		log_printf("control: no binding '%s'", name);
		return;
	}
	snprintf(cmd, sizeof(cmd), "%s %d\n", name, QA_KEY);
	game_cbuf_addtext(cmd);
	log_printf("control: binding %s", name);
#endif
}

static cmd_function_t bind_cmd = {NULL, "mw2coop_bind", NULL, NULL, bind_f CMD_NODE_FLAG};

// A QA scenario asks for a press with mw2coop_qa_press "<player 1|2> <button> <ms> <n>" (button
// without the +, n counts the presses). Both machines run the scenario; the one whose own player
// it is (the host plays player 1) holds the button down for ms (checked every 250 ms).
#define MAX_HELD 8
static char press_last[96];
static char held_name[MAX_HELD][32];
static DWORD held_until[MAX_HELD];

static void press_poll(void)
{
	char v[96], cmd[64];
	for (int i = 0; i < MAX_HELD; i++) {
		if (held_name[i][0] && (int)(GetTickCount() - held_until[i]) >= 0) {
			snprintf(cmd, sizeof(cmd), "mw2coop_bind -%s\n", held_name[i]);
			game_cbuf_addtext(cmd);
			held_name[i][0] = 0;
		}
	}
	if (!game_dvar_raw("mw2coop_qa_press", v, sizeof(v)) || !v[0] || strcmp(v, press_last) == 0)
		return;
	lstrcpynA(press_last, v, sizeof(press_last));
	int player, ms;
	char name[32];
	if (sscanf(v, "%d %31s %d", &player, name, &ms) != 3)
		return;
	if (player != (game_peek(RVA_PARTY_IS_HOST, 1) ? 1 : 2))
		return;
	int slot = 0;
	while (slot < MAX_HELD - 1 && held_name[slot][0] && strcmp(held_name[slot], name) != 0)
		slot++;
	lstrcpynA(held_name[slot], name, sizeof(held_name[slot]));
	held_until[slot] = GetTickCount() + ms;
	log_printf("control: qa press %s for %d ms", name, ms);
	snprintf(cmd, sizeof(cmd), "mw2coop_bind +%s\n", name);
	game_cbuf_addtext(cmd);
}

static void script_channel_poll(void)
{
	char v[256];
	sync_dvar_flags();
	partner_name_poll();
	transplant_poll();
	progress_poll();
	press_poll();
	// a scenario asks both machines for a frame (mw2coop_qa_shot "<name>"): each saves its own
	if (game_dvar_raw("mw2coop_qa_shot", v, sizeof(v)) && v[0] && strcmp(v, last_shot) != 0) {
		lstrcpynA(last_shot, v, sizeof(last_shot));
		shot_request(v);
	}
	if (game_dvar_raw("mw2coop_state", v, sizeof(v)) && strcmp(v, last_state) != 0) {
		log_printf("script: %s", v);
		lstrcpynA(last_state, v, sizeof(last_state));
	}
	if (game_dvar_raw("mw2coop_qa_log", v, sizeof(v)) && strcmp(v, last_qa) != 0) {
		log_printf("qa: %s", v);
		lstrcpynA(last_qa, v, sizeof(last_qa));
	}
	if (game_dvar_raw("mw2coop_cmd", v, sizeof(v)) && v[0] && strcmp(v, last_cmd) != 0) {
		lstrcpynA(last_cmd, v, sizeof(last_cmd));
		script_request(v);
	}
}

static void watch_poll(void)
{
	char now[256];
	script_channel_poll();
	for (int i = 0; i < watch_count; i++) {
		watch_value(watch_names[i], now, sizeof(now));
		if (strcmp(now, watch_last[i]) != 0) {
			log_printf("watch: %s = %s", watch_names[i], now);
			lstrcpynA(watch_last[i], now, sizeof(watch_last[i]));
		}
	}
}

static void run_line(char *line)
{
	size_t n = strlen(line);
	while (n && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' '))
		line[--n] = 0;
	if (!n || line[0] == '#')
		return;
	if (strncmp(line, "!sleep ", 7) == 0) {
		int ms = atoi(line + 7);
		log_printf("control: sleep %d ms", ms);
		for (int t = 0; t < ms; t += 250) {
			Sleep(250);
			watch_poll();
			menus_poll();
		}
		return;
	}
	if (strncmp(line, "!xuid_add ", 10) == 0) {
		// QA only: two test copies on one Steam account share an XUID, and the host ignores a
		// coopConnect from its own XUID. Shift this copy's XUID so they can pair up.
		game_xuid_add(strtoll(line + 10, NULL, 10));
		return;
	}
	if (strcmp(line, "!cmds") == 0) {
		game_log_commands();
		return;
	}
	if (strncmp(line, "!trace ", 7) == 0) {
		scrdebug_trace_method((uintptr_t)strtoull(line + 7, NULL, 16));
		return;
	}
	if (strncmp(line, "!dvars ", 7) == 0) {
		char val[256];
		for (char *tok = strtok(line + 7, " \t"); tok; tok = strtok(NULL, " \t"))
			log_printf("dvar: %s = %s", tok, watch_value(tok, val, sizeof(val)));
		return;
	}
	if (strncmp(line, "!loc ", 5) == 0) {
		// a localized string's text and bytes (ASSET_TYPE_LOCALIZE_ENTRY 27: {value, name})
		typedef void *(*find_t)(int, const char *);
		const char **entry = ((find_t)((uint8_t *)GetModuleHandleA(NULL) + RVA_DB_FIND_HEADER))(27, line + 5);
		const char *v = entry ? entry[0] : NULL;
		char hex[512] = "";
		for (int i = 0; v && v[i] && i < 80; i++)
			snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02x ", (unsigned char)v[i]);
		log_printf("loc: %s = '%s' [%s]", line + 5, v ? v : "(none)", hex);
		return;
	}
	if (strcmp(line, "!menus") == 0) {
		menus_log_names();
		return;
	}
	if (strncmp(line, "!menuinst ", 10) == 0) { // !menuinst <menu> <material>
		char nm[64], mt[64];
		if (sscanf(line + 10, "%63s %63s", nm, mt) == 2)
			menus_log_instances(nm, mt);
		return;
	}
	if (strncmp(line, "!menusearch ", 12) == 0) {
		menus_find_text(line + 12);
		return;
	}
	if (strncmp(line, "!menuitems ", 11) == 0) {
		menus_log_items(line + 11);
		return;
	}
	if (strncmp(line, "!menuaction ", 12) == 0) {
		char menu[64] = "", item[64] = "";
		if (sscanf(line + 12, "%63s %63s", menu, item) == 2)
			menus_log_action(menu, item);
		return;
	}
	if (strncmp(line, "!key ", 5) == 0) { // QA: a key press posted to the game window ("!key 1b" is Escape; virtual key code in hex)
		unsigned vk = 0;
		if (sscanf(line + 5, "%x", &vk) == 1) {
			HWND game = NULL;
			struct find_data {
				HWND *out;
			} fd = {&game};
			EnumWindows(qa_find_window, (LPARAM)&fd);
			if (game) {
				UINT scan = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
				PostMessageA(game, WM_KEYDOWN, vk, 1 | (scan << 16));
				Sleep(80);
				PostMessageA(game, WM_KEYUP, vk, 1 | (scan << 16) | (3u << 30));
				log_printf("control: key %02x posted to the game window", vk);
			}
		}
		return;
	}
	if (strncmp(line, "!shot ", 6) == 0) {
		shot_request(line + 6);
		return;
	}
	if (strncmp(line, "!burst ", 7) == 0) { // !burst <prefix>: 40 frames in a row of the bottom half of a 1920x1080 picture (the native split screen's QA)
		shot_burst_start(line + 7, 40, 0, 540, 1920, 540);
		return;
	}
	if (strncmp(line, "!burstfull ", 11) == 0) { // !burstfull <prefix>: 30 whole 1920x1080 frames in a row
		shot_burst_start(line + 11, 30, 0, 0, 1920, 1080);
		return;
	}
	if (strcmp(line, "!party") == 0) {
		party_log_state();
		return;
	}
	if (strncmp(line, "!netlog ", 8) == 0) {
		net_log_budget(atoi(line + 8));
		return;
	}
	if (memdump_command(line))
		return;
	if (strncmp(line, "!watch ", 7) == 0) {
		watch_set(line + 7);
		watch_poll();
		return;
	}
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "%s\n", line);
	log_printf("control: cbuf <- %s", line);
	game_cbuf_addtext(cmd);
}

// The command file (QA: tools/qa_pair.sh) runs console commands, so it is read only when the file is
// already there at start-up: a file that shows up in the game folder later is not run, and a normal
// session does not look for it four times a second.
static int commands_enabled;

static DWORD WINAPI control_thread(LPVOID path_arg)
{
	const char *path = (const char *)path_arg;
	LONGLONG offset = 0;
	char pending[4096];
	int plen = 0;
	for (;;) {
		Sleep(250);
		watch_poll();
		menus_poll();
		if (!commands_enabled)
			continue;
		HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
				       OPEN_EXISTING, 0, NULL);
		if (f == INVALID_HANDLE_VALUE)
			continue;
		LARGE_INTEGER size;
		GetFileSizeEx(f, &size);
		if (size.QuadPart < offset)
			offset = 0; // file was replaced
		LARGE_INTEGER pos = {.QuadPart = offset};
		SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
		char buf[1024];
		DWORD got;
		while (ReadFile(f, buf, sizeof(buf), &got, NULL) && got) {
			offset += got;
			for (DWORD i = 0; i < got; i++) {
				if (buf[i] == '\n' || plen == (int)sizeof(pending) - 1) {
					pending[plen] = 0;
					run_line(pending);
					plen = 0;
				} else {
					pending[plen++] = buf[i];
				}
			}
		}
		CloseHandle(f);
	}
	return 0;
}

// A dvar a level script sets itself (GSC setdvar) has the same value on both lockstep machines,
// so scripts may read it back. Without this, getdvar reads "" in co-op for any dvar the script
// made: roadkill.gsc sets newintro 1 and then reads 0, which turns the SP-only slam-zoom intro
// back on in co-op and unlinks player 1 from the opening scene. The builtin's table entry
// ({name, func, developer}, rva 0x3c1568) is pointed at a wrapper that adds 0x80 after the set.
static void (*setdvar_orig)(void);

static void setdvar_wrap(void)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	char name[64] = "";
	const char *s = ((const char *(*)(unsigned int))(base + RVA_SCR_GET_STRING))(0);
	if (s)
		lstrcpynA(name, s, sizeof(name));
	// the session recorder (maps\_mw2coop_rec.gsc): each line straight into the log, no dvar
	if (strcmp(name, "mw2coop_rec") == 0) {
		const char *line = ((const char *(*)(unsigned int))(base + RVA_SCR_GET_STRING))(1);
		log_printf("rec: %s", line ? line : "");
		return;
	}
	setdvar_orig();
	dvar_t *d = game_find_dvar(name);
	if (d && !(d->flags & 0x20c4))
		d->flags |= 0x80;
}

static void hook_script_setdvar(void)
{
	uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
	uint8_t *entry = base + RVA_FN_SETDVAR_ENTRY;
	if (lstrcmpA(*(const char **)entry, "setdvar") != 0) {
		log_printf("control: no setdvar entry at rva 0x%x, script dvars stay unreadable in co-op", RVA_FN_SETDVAR_ENTRY);
		return;
	}
	void (**func)(void) = (void (**)(void))(entry + sizeof(char *));
	setdvar_orig = *func;
	DWORD old;
	VirtualProtect(func, sizeof(*func), PAGE_READWRITE, &old);
	*func = setdvar_wrap;
	VirtualProtect(func, sizeof(*func), old, &old);
	log_printf("control: dvars set by scripts are readable by co-op scripts");
}

void control_start(void)
{
	static char path[MAX_PATH];
	hook_script_setdvar();
	game_add_command(&bind_cmd);
	DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
	while (n > 0 && path[n - 1] != '\\')
		n--;
	path[n] = 0;
	lstrcatA(path, "mw2coop\\commands.txt");
	commands_enabled = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
	if (commands_enabled)
		log_printf("control: reading %s (QA)", path);
	else
		log_printf("control: no %s at start-up, the command file is not read", path);
	CloseHandle(CreateThread(NULL, 0, control_thread, path, 0, NULL));
}
