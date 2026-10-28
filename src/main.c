// mw2coop: campaign co-op for Modern Warfare 2 (2009), 64-bit Steam build.
// Milestone 2: run console commands from a file and watch dvars (QA remote control).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "access.h"
#include "control.h"
#include "crash.h"
#include "firewall.h"
#include "splitscreen.h"
#include "instance.h"
#include "native.h"
#include "game.h"
#include "localize.h"
#include "log.h"
#include "menus.h"
#include "mirror.h"
#include "rumble.h"
#include "net.h"
#include "party.h"
#include "progress.h"
#include "scrdebug.h"
#include "settings.h"
#include "shot.h"
#include "fov.h"
#include "steamnet.h"
#include "transplant.h"
#include "upnp.h"
#include "zones.h"
#include "scripts.h"

int proxy_init(void);

static BOOL CALLBACK find_game_window(HWND h, LPARAM out)
{
	DWORD pid;
	char cls[32];
	GetWindowThreadProcessId(h, &pid);
	if (pid == GetCurrentProcessId() && GetClassNameA(h, cls, sizeof(cls)) && lstrcmpA(cls, "IW4") == 0) {
		*(HWND *)out = h;
		return FALSE;
	}
	return TRUE;
}

static DWORD WINAPI watcher(LPVOID unused)
{
	(void)unused;
	DWORD start = GetTickCount();
	HWND game = NULL;
	while (!game && GetTickCount() - start < 120000) {
		Sleep(100);
		EnumWindows(find_game_window, (LPARAM)&game);
	}
	if (!game) {
		log_printf("main: no game window after 120 s");
		return 0;
	}
	log_printf("main: game window %p after %lu ms", (void *)game, GetTickCount() - start);
	if (game_init()) {
		crash_install();
		scripts_install();
		mirror_install();
		rumble_install();
		transplant_install();
		zones_install();
		localize_install();
		scrdebug_install();
		net_install();
		party_install();
		progress_install();
		menus_install();
		access_install();
		upnp_install();
		steamnet_install();
		firewall_install();
		shot_install();
		fov_install();
		splitscreen_install();
		instance_install();
		native_install();
		control_start();
	}
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_DETACH && reserved) {
		// the process is ending: log where in the game it was asked to (an exit before the game
		// window shows leaves no other trace; the game's own console log is not open yet)
		void *frames[48];
		USHORT n = RtlCaptureStackBackTrace(0, 48, frames, NULL);
		uint8_t *exe = (uint8_t *)GetModuleHandleA(NULL);
		char line[512] = "";
		for (USHORT i = 0; i < n; i++) {
			HMODULE mod = NULL;
			char name[MAX_PATH] = "?";
			if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					       (LPCSTR)frames[i], &mod)) {
				GetModuleFileNameA(mod, name, MAX_PATH);
			}
			const char *file = strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
			snprintf(line + strlen(line), sizeof(line) - strlen(line), " %s+0x%llx", (uint8_t *)mod == exe ? "exe" : file,
				 (unsigned long long)((uint8_t *)frames[i] - (uint8_t *)mod));
		}
		log_printf("main: process exit, stack:%s", line);
		return TRUE;
	}
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(self);
	log_open();
	settings_load(); // before the game registers net_port

	char exe[MAX_PATH];
	GetModuleFileNameA(NULL, exe, MAX_PATH);
	HMODULE base = GetModuleHandleA(NULL);
	SYSTEMTIME day;
	GetLocalTime(&day);
	log_printf("main: %04d-%02d-%02d, attached to %s, image base %p, pid %lu", day.wYear, day.wMonth, day.wDay, exe,
		   (void *)base, GetCurrentProcessId());

	if (!proxy_init()) {
		log_printf("main: could not load the real winmm.dll");
		return FALSE;
	}
	CloseHandle(CreateThread(NULL, 0, watcher, NULL, 0, NULL));
	return TRUE;
}
