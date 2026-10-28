// The second player's game process for a split screen session, started from the SAME install: nothing is copied. The
// install's own files are shared with it (hard links for the files of the game folder, junctions for its folders) in
// <game dir>\mw2coop\p2, where it also gets what the game writes: its own settings.ini, players folder, log and
// command file. It runs hidden, joins this process as the partner (the mod's own lobby join on this PC) and, when a
// level starts, both windows go into the shared window (splitscreen.c).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "instance.h"
#include "log.h"
#include "menus.h"
#include "splitscreen.h"

static HANDLE child = NULL;
static char pad_file[2 * MAX_PATH];

// ---------------------------------------------------------------- the lobby: the second player's device and Ready

static volatile LONG assigned_dev[2] = {DEVICE_NONE, DEVICE_NONE}; // the players' devices: DEVICE_NONE, DEVICE_KEYBOARD, or an XInput pad 0-3
static volatile LONG second_ready;
static volatile LONG assign_gen;

// The lobby's devices are polled here (XInput state as XInputGetState gives it)
typedef struct {
	DWORD packet;
	WORD buttons;
	BYTE lt, rt;
	SHORT lx, ly, rx, ry;
} assign_pad_t;
static DWORD(WINAPI *assign_xinput)(DWORD, assign_pad_t *);

// what a device did since the last look: 0 nothing, 1 a press (a button, a trigger, a stick pushed), 2 its cancel (a pad's Back, the keyboard's Backspace)
static int pad_event(int i)
{
	static WORD was[4];
	static int moved[4];
	if (!assign_xinput) {
		const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
		for (int k = 0; k < 3 && !assign_xinput; k++) {
			HMODULE h = LoadLibraryA(dlls[k]);
			if (h)
				assign_xinput = (DWORD(WINAPI *)(DWORD, assign_pad_t *))GetProcAddress(h, "XInputGetState");
		}
		if (!assign_xinput)
			return 0;
	}
	assign_pad_t st;
	memset(&st, 0, sizeof(st));
	if (assign_xinput((DWORD)i, &st) != ERROR_SUCCESS) {
		was[i] = 0, moved[i] = 0;
		return 0;
	}
	WORD b = st.buttons;
	int m = st.lt > 100 || st.rt > 100 || st.lx > 20000 || st.lx < -20000 || st.ly > 20000 || st.ly < -20000;
	WORD pressed = b & ~was[i];
	int pushed = m && !moved[i];
	was[i] = b, moved[i] = m;
	if (pressed & 0x0020) // Back
		return 2;
	return pressed || pushed ? 1 : 0;
}
static int keyboard_event(void)
{
	static BYTE was[256];
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	int event = 0;
	for (int vk = 8; vk < 256; vk++) {
		if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2)
			continue; // (the lobby is clicked with the mouse)
		BYTE down = (GetAsyncKeyState(vk) & 0x8000) != 0;
		if (down && !was[vk] && pid == GetCurrentProcessId())
			event = vk == VK_BACK ? 2 : event ? event : 1;
		was[vk] = down;
	}
	return event;
}

// The split screen lobby: each player presses a button on his device. The first device pressed is player 1's, the next other one player 2's; a pad's Back or the
// keyboard's Backspace takes the choice back (the slot is free for the next press). Runs as long as the lobby is open.
static DWORD WINAPI assign_thread(LPVOID gen_arg)
{
	LONG gen = (LONG)(LONG_PTR)gen_arg;
	keyboard_event(); // the keys held while the lobby opened are not a press
	for (int i = 0; i < 4; i++)
		pad_event(i);
	while (gen == assign_gen) {
		Sleep(30);
		int ev[5];
		for (int i = 0; i < 4; i++)
			ev[i] = pad_event(i);
		ev[4] = keyboard_event();
		for (int k = 0; k < 5 && gen == assign_gen; k++) {
			int dev = k < 4 ? k : DEVICE_KEYBOARD;
			int slot = assigned_dev[0] == dev ? 0 : assigned_dev[1] == dev ? 1 : -1;
			if (ev[k] == 2 && slot >= 0) {
				InterlockedExchange(&assigned_dev[slot], DEVICE_NONE);
				log_printf("instance: player %d gave his device back", slot + 1);
				menus_split_changed();
			} else if (ev[k] == 1 && slot < 0) {
				int free = assigned_dev[0] == DEVICE_NONE ? 0 : assigned_dev[1] == DEVICE_NONE ? 1 : -1;
				if (free < 0)
					continue;
				InterlockedExchange(&assigned_dev[free], dev);
				log_printf("instance: player %d plays on %s %d", free + 1, dev == DEVICE_KEYBOARD ? "the keyboard" : "pad", dev == DEVICE_KEYBOARD ? 0 : dev);
				menus_split_changed();
			}
		}
	}
	return 0;
}

// for menus.c: the lobby is worked by player 1; when he plays on a pad, his pad's buttons as menu keys (the lobby's own thread only reads presses)
int instance_menu_pad(void)
{
	return assigned_dev[0] >= 0 ? (int)assigned_dev[0] : -1;
}

void instance_assign_begin(void)
{
	LONG gen = InterlockedIncrement(&assign_gen);
	InterlockedExchange(&assigned_dev[0], DEVICE_NONE);
	InterlockedExchange(&assigned_dev[1], DEVICE_NONE);
	InterlockedExchange(&second_ready, 0);
	CreateThread(NULL, 0, assign_thread, (LPVOID)(LONG_PTR)gen, 0, NULL);
}

void instance_assign_end(void)
{
	InterlockedIncrement(&assign_gen);
	InterlockedExchange(&assigned_dev[0], DEVICE_NONE);
	InterlockedExchange(&assigned_dev[1], DEVICE_NONE);
	InterlockedExchange(&second_ready, 0);
}

int instance_device(int player)
{
	return (int)assigned_dev[player & 1];
}

// the two-process split screen: player 1 is the keyboard, player 2 the pad (the first pad either player pressed)
int instance_pad(void)
{
	for (int i = 0; i < 2; i++)
		if (assigned_dev[i] >= 0)
			return (int)assigned_dev[i];
	return -1;
}

int instance_second_ready(void)
{
	return (int)second_ready;
}

// a pad's number written for the second process (it reads <its folder>\mw2coop\pad.txt)
static void write_pad(int pad)
{
	FILE *f = fopen(pad_file, "w");
	if (f) {
		fprintf(f, "%d%c", pad, 10);
		fclose(f);
	}
	log_printf("instance: pad %d goes to the second process", pad);
}

// The player's game runs full screen as a rule; the second process is made as a window that is hidden, and a full screen device of its own would take
// the screen over while it starts: its copy of the settings file says windowed (the player's own file is not touched).
static char *replace_first(char *text, long *size, const char *from, const char *to)
{
	char *at = strstr(text, from);
	if (!at)
		return text;
	size_t before = (size_t)(at - text), flen = strlen(from), tlen = strlen(to);
	char *out = malloc((size_t)*size + tlen + 1);
	if (!out)
		return text;
	memcpy(out, text, before);
	memcpy(out + before, to, tlen);
	memcpy(out + before + tlen, at + flen, (size_t)*size - before - flen);
	*size = (long)((size_t)*size - flen + tlen);
	out[*size] = 0;
	free(text);
	return out;
}

static void patch_windowed(const char *path)
{
	size_t n = strlen(path);
	if (n < 4 || lstrcmpiA(path + n - 4, ".cfg") != 0)
		return;
	FILE *f = fopen(path, "rb");
	if (!f)
		return;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *text = size > 0 && size < (1 << 22) ? malloc((size_t)size + 1) : NULL;
	if (!text || fread(text, 1, (size_t)size, f) != (size_t)size) {
		free(text);
		fclose(f);
		return;
	}
	fclose(f);
	text[size] = 0;
	long before = size;
	text = replace_first(text, &size, "r_fullscreen \"1\"", "r_fullscreen \"0\"");
	text = replace_first(text, &size, "r_displayMode \"fullscreen\"", "r_displayMode \"windowed\"");
	if (size != before || memcmp(text, text, 0) != 0) {
		f = fopen(path, "wb");
		if (f) {
			fwrite(text, 1, (size_t)size, f);
			fclose(f);
		}
	}
	free(text);
}

static void parent_dir(const char *path, char *out)
{
	lstrcpynA(out, path, MAX_PATH);
	char *slash = strrchr(out, '\\');
	if (slash)
		slash[1] = 0;
}

static int run_hidden(char *cmdline)
{
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
		return 0;
	WaitForSingleObject(pi.hProcess, 15000);
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return code == 0;
}

static int is_dir(const char *path)
{
	DWORD a = GetFileAttributesA(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// a link to a folder: a junction (no administrator rights needed)
static int link_dir(const char *dst, const char *src)
{
	if (is_dir(dst))
		return 1; // from an earlier start (a junction or a real folder)
	char cmd[2 * MAX_PATH + 40];
	snprintf(cmd, sizeof(cmd), "cmd.exe /c mklink /J \"%s\" \"%s\"", dst, src);
	return run_hidden(cmd);
}

// a link to a file: a hard link (the same volume), else a copy (small files: the game's own dlls)
static int link_file(const char *dst, const char *src)
{
	DeleteFileA(dst);
	if (CreateHardLinkA(dst, src, NULL))
		return 1;
	return CopyFileA(src, dst, FALSE);
}

static void own_address(char *out, size_t n)
{
	snprintf(out, n, "127.0.0.1"); // mw2coop_join maps a loopback address to this PC's own (party.c)
}

int instance_start(int pad)
{
	if (child && WaitForSingleObject(child, 0) == WAIT_TIMEOUT) {
		log_printf("instance: the second process is running already");
		return 1;
	}
	char exe[MAX_PATH], game[MAX_PATH], p2[MAX_PATH], path[2 * MAX_PATH], src[2 * MAX_PATH];
	GetModuleFileNameA(NULL, exe, MAX_PATH);
	parent_dir(exe, game);
	const char *exe_name = strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe;
	snprintf(p2, sizeof(p2), "%smw2coop\\p2\\", game);
	char dir[MAX_PATH];
	snprintf(dir, sizeof(dir), "%smw2coop", game);
	CreateDirectoryA(dir, NULL);
	snprintf(dir, sizeof(dir), "%smw2coop\\p2", game);
	CreateDirectoryA(dir, NULL);

	// the game folder: files as hard links, folders as junctions; what the game writes is the second process's own
	WIN32_FIND_DATAA fd;
	snprintf(path, sizeof(path), "%s*", game);
	HANDLE h = FindFirstFileA(path, &fd);
	int failed = 0;
	if (h != INVALID_HANDLE_VALUE) {
		do {
			const char *n = fd.cFileName;
			if (!strcmp(n, ".") || !strcmp(n, "..") || !strcmp(n, "mw2coop") || !strcmp(n, "players") || !strcmp(n, "__iw4sp"))
				continue;
			snprintf(src, sizeof(src), "%s%s", game, n);
			snprintf(path, sizeof(path), "%s%s", p2, n);
			int ok = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? link_dir(path, src) : link_file(path, src);
			if (!ok) {
				log_printf("instance: cannot link %s", n);
				failed = 1;
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	if (failed)
		return 0;

	// players: the settings and bindings of this player's game (files only; saves are not shared)
	snprintf(path, sizeof(path), "%splayers", p2);
	CreateDirectoryA(path, NULL);
	snprintf(path, sizeof(path), "%splayers\\*", game);
	h = FindFirstFileA(path, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			snprintf(src, sizeof(src), "%splayers\\%s", game, fd.cFileName);
			snprintf(path, sizeof(path), "%splayers\\%s", p2, fd.cFileName);
			CopyFileA(src, path, FALSE);
			patch_windowed(path);
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}

	// the mod's folder: its files shared, its settings, log, progress and commands the second process's own
	snprintf(path, sizeof(path), "%smw2coop", p2);
	CreateDirectoryA(path, NULL);
	static const char *shared[] = {"scripts", "patches", "fonts"};
	for (int i = 0; i < 3; i++) {
		snprintf(src, sizeof(src), "%smw2coop\\%s", game, shared[i]);
		snprintf(path, sizeof(path), "%smw2coop\\%s", p2, shared[i]);
		if (is_dir(src) && !link_dir(path, src)) {
			log_printf("instance: cannot link mw2coop\\%s", shared[i]);
			return 0;
		}
	}
	snprintf(src, sizeof(src), "%smw2coop\\settings.ini", game);
	snprintf(path, sizeof(path), "%smw2coop\\settings.ini", p2);
	CopyFileA(src, path, FALSE); // a missing one is fine
	WritePrivateProfileStringA("splitscreen", "slot", "1", path);
	char padtext[16];
	snprintf(padtext, sizeof(padtext), "%d", pad);
	WritePrivateProfileStringA("splitscreen", "pad", padtext, path);
	snprintf(path, sizeof(path), "%smw2coop\\mw2coop.log", p2);
	DeleteFileA(path);
	// the game's lock file: one left by a killed copy makes the next start ask whether to run in safe mode
	snprintf(path, sizeof(path), "%s__iw4sp", p2);
	DeleteFileA(path);

	// what it does once it runs: the mod's own join of this PC as the partner (a second XUID, there is one Steam account)
	char addr[32];
	own_address(addr, sizeof(addr));
	snprintf(path, sizeof(path), "%smw2coop\\commands.txt", p2);
	FILE *f = fopen(path, "w");
	if (!f) {
		log_printf("instance: cannot write the command file");
		return 0;
	}
	fprintf(f, "# the second player of a split screen session\n");
#ifndef _WIN64
	fprintf(f, "set arcademode 0\nset solo_play 0\n"); // the 32-bit game's own settings for a lobby
#endif
	// the join itself comes from splitscreen.c once the picture has its size (a video restart in the middle of it breaks it)
	(void)addr;
	fprintf(f, "!sleep 12000\n!xuid_add 1\n!sleep 600000\n");
	fclose(f);

	// the game's own checks: started by this process, with its Steam environment
	SetEnvironmentVariableA("SteamAppId", "10180");
	SetEnvironmentVariableA("SteamGameId", "10180");
	char pid[16];
	snprintf(pid, sizeof(pid), "%lu", GetCurrentProcessId());
	SetEnvironmentVariableA("MW2COOP_PARENT_PID", pid); // the second process quits with this one
	splitscreen_host_activate();
	char cmd[2 * MAX_PATH];
	snprintf(path, sizeof(path), "%s%s", p2, exe_name);
	snprintf(cmd, sizeof(cmd), "\"%s\"", path);
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(path, cmd, NULL, NULL, FALSE, 0, NULL, p2, &si, &pi)) {
		log_printf("instance: cannot start %s (error %lu)", path, GetLastError());
		return 0;
	}
	CloseHandle(pi.hThread);
	child = pi.hProcess;
	log_printf("instance: the second process runs (pid %lu) from %s", pi.dwProcessId, p2);
	snprintf(pad_file, sizeof(pad_file), "%smw2coop\\pad.txt", p2);
	DeleteFileA(pad_file);
	if (pad >= 0)
		write_pad(pad);
	return 1;
}

int instance_running(void)
{
	return child && WaitForSingleObject(child, 0) == WAIT_TIMEOUT;
}

void instance_stop(void)
{
	if (child && WaitForSingleObject(child, 0) == WAIT_TIMEOUT) {
		TerminateProcess(child, 0);
		log_printf("instance: the second process was closed");
	}
}

static void split_f(void)
{
	const char *a = game_cmd_argv(1);
	instance_start(a[0] ? atoi(a) : -1);
}

// QA: what the lobby's Start does in a split screen session
static void splitlaunch_f(void)
{
	if (!splitscreen_before_launch())
		game_cbuf_addtext("coopLaunch\n");
}

// QA: what the second player's button presses do in the lobby
static void split_pad_f(void)
{
	const char *a = game_cmd_argv(1);
	// QA: mw2coop_split_pad <pad> [<player 1's: -1 keyboard>]: the second player's pad (and the first player's device)
	InterlockedExchange(&assigned_dev[1], a[0] ? atoi(a) : 0);
	const char *b = game_cmd_argv(2);
	InterlockedExchange(&assigned_dev[0], b[0] ? atoi(b) : DEVICE_KEYBOARD);
	menus_split_changed();
}

static void split_ready_f(void)
{
	InterlockedExchange(&second_ready, !second_ready);
	menus_split_changed();
}

static cmd_function_t split_cmd = {NULL, "mw2coop_split", NULL, NULL, split_f CMD_NODE_FLAG};
static cmd_function_t splitpad_cmd = {NULL, "mw2coop_split_pad", NULL, NULL, split_pad_f CMD_NODE_FLAG};
static cmd_function_t splitready_cmd = {NULL, "mw2coop_split_ready", NULL, NULL, split_ready_f CMD_NODE_FLAG};
static cmd_function_t splitlaunch_cmd = {NULL, "mw2coop_splitlaunch", NULL, NULL, splitlaunch_f CMD_NODE_FLAG};

void instance_install(void)
{
	game_add_command(&split_cmd);
	game_add_command(&splitlaunch_cmd);
	game_add_command(&splitpad_cmd);
	game_add_command(&splitready_cmd);
}
