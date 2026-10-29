// mw2coop for the 32-bit build of Modern Warfare 2 (the build before the 2026 move to 64 bits). The first
// slice: the winmm.dll proxy loads into iw4sp.exe, logs what game it is in and whether it knows that build.
// The hooks of the 64-bit DLL come over one at a time, each with the addresses of this build (see
// docs/32bit-plan.md); so far the script loader (scripts.c) and the QA command file.
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "scripts.h"
#include "zones.h"
#include "control.h"
#include "d3dcap.h"
#include "crash.h"
#include "fov.h"
#include "access.h"
#include "firewall.h"
#include "splitscreen.h"
#include "instance.h"
#include "localize.h"
#include "menus.h"
#include "net.h"
#include "progress.h"
#include "scrdebug.h"
#include "steamnet.h"
#include "transplant.h"
#include "mirror.h"
#include "shot.h"
#include "party.h"
#include "settings.h"
#include "upnp.h"

int proxy_init(void);

// The official exe is wrapped by Steam's DRM and Steam rewrites that wrapper for each account at the first start
// (the file in a test copy grew from 3,497,984 to 3,503,672 bytes and its hash changed), so the file's hash
// says nothing about the build. The code is the same once the wrapper has unpacked it: the build is known by the
// SHA-256 of the exe's executable sections in memory, taken when the game window exists.
static const struct {
	const char *code_sha256; // upper case
	const char *name;
} builds[] = {
	{"FC9E4843FF9F0E2E17EAABBE254B96FAF3EE7A2750D143E65597AD33481F52D7", "official Steam build, 32-bit, linked 2010-02-10 (depots 10181, 10182)"},
};

static void hex_of(const uint8_t digest[32], char out[65])
{
	for (int i = 0; i < 32; i++)
		snprintf(out + 2 * i, 3, "%02X", digest[i]);
	out[64] = 0;
}

// SHA-256 of every executable section of the running exe, as it is in memory: 64 hex characters.
static int sha256_code(char out[65], unsigned *bytes)
{
	const uint8_t *base = (const uint8_t *)GetModuleHandleW(NULL);
	const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	int ok = 0;
	*bytes = 0;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
		ok = 1;
		for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
			if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
				continue;
			if (BCryptHashData(hash, (PUCHAR)(base + sec[i].VirtualAddress), sec[i].Misc.VirtualSize, 0) != 0)
				ok = 0;
			*bytes += sec[i].Misc.VirtualSize;
		}
		uint8_t digest[32];
		if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
			hex_of(digest, out);
		else
			ok = 0;
	}
	if (hash)
		BCryptDestroyHash(hash);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	return ok;
}

#ifdef MW2COOP_IW4X
// The iw4x-sp client (docs: games/modern-warfare-2/NOTES.md, "iw4x-sp support") maps the 1.0.159 iw4sp.exe into its own process and patches
// the code in memory, so the code hash changes with the client's build. The build is known by the SHA-256 of the iw4sp.exe file next to it.
static int sha256_file(const char *path, char out[65])
{
	HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return 0;
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	int ok = 0;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 && BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) == 0) {
		static uint8_t buf[1 << 16];
		DWORD got;
		ok = 1;
		while (ReadFile(f, buf, sizeof(buf), &got, NULL) && got)
			if (BCryptHashData(hash, buf, got, 0) != 0)
				ok = 0;
		uint8_t digest[32];
		if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
			hex_of(digest, out);
		else
			ok = 0;
	}
	if (hash)
		BCryptDestroyHash(hash);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	CloseHandle(f);
	return ok;
}

static const char iw4x_exe_sha256[] = "9480909B18CF5A4EC483C989AAFCE929D2172C5F9448F8C23B723683D1A43565"; // SP_ALTER_HASH of iw4x-sp
static int iw4x_known(void)
{
	char dir[MAX_PATH], hex[65] = "";
	DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != '\\')
		n--;
	dir[n] = 0;
	lstrcatA(dir, "iw4sp.exe");
	if (!sha256_file(dir, hex)) {
		log_printf("main32: cannot hash iw4sp.exe next to the client");
		return 0;
	}
	log_printf("main32: iw4sp.exe file SHA-256 %s", hex);
	return strcmp(hex, iw4x_exe_sha256) == 0;
}
#endif

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

// For reverse engineering: with the file <game dir>\mw2coop\dump_image present, the exe's image as it is in memory
// (the wrapper has unpacked the code) is written to <game dir>\mw2coop\image32.bin, laid out from the image base
// (pages that cannot be read are zeros), and the flag file is deleted. Load it in Ghidra as a raw binary at the
// image base (docs/32bit-plan.md).
static void dump_image(void)
{
	char dir[MAX_PATH], flag[MAX_PATH], out[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, dir, MAX_PATH);
	while (n > 0 && dir[n - 1] != '\\')
		n--;
	dir[n] = 0;
	snprintf(flag, sizeof(flag), "%smw2coop\\dump_image", dir);
	snprintf(out, sizeof(out), "%smw2coop\\image32.bin", dir);
	if (GetFileAttributesA(flag) == INVALID_FILE_ATTRIBUTES)
		return;
	DeleteFileA(flag);
	const uint8_t *base = (const uint8_t *)GetModuleHandleW(NULL);
	const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	DWORD size = nt->OptionalHeader.SizeOfImage;
	HANDLE f = CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) {
		log_printf("main32: cannot write %s", out);
		return;
	}
	static uint8_t zeros[4096];
	DWORD readable = 0, wrote;
	for (DWORD at = 0; at < size; at += 4096) {
		MEMORY_BASIC_INFORMATION mbi;
		const uint8_t *page = base + at;
		if (VirtualQuery(page, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
			WriteFile(f, page, 4096, &wrote, NULL);
			readable += 4096;
		} else {
			WriteFile(f, zeros, 4096, &wrote, NULL);
		}
	}
	CloseHandle(f);
	log_printf("main32: image of %lu bytes written to %s (%lu bytes readable)", (unsigned long)size, out, (unsigned long)readable);
}

static DWORD WINAPI identify(LPVOID unused)
{
	(void)unused;
	DWORD start = GetTickCount();
	HWND game = NULL;
	while (!game && GetTickCount() - start < 120000) {
		Sleep(100);
		EnumWindows(find_game_window, (LPARAM)&game);
	}
	if (!game) {
		log_printf("main32: no game window after 120 s");
		return 0;
	}
	log_printf("main32: game window %p after %lu ms", (void *)game, GetTickCount() - start);
	Sleep(3000); // the wrapper has unpacked the code by now; a few seconds more for the game's own start
	dump_image();
	char hex[65] = "";
	unsigned bytes = 0;
	if (!sha256_code(hex, &bytes)) {
		log_printf("main32: could not hash the code of iw4sp.exe");
		return 0;
	}
	log_printf("main32: image base %p, code sections %u bytes, SHA-256 %s", (void *)GetModuleHandleW(NULL), bytes, hex);
#ifdef MW2COOP_IW4X
	for (size_t i = 0; i < 1; i++) {
		if (iw4x_known()) {
			log_printf("main32: known build: 1.0.159 iw4sp.exe of the iw4x-sp client");
#else
	for (size_t i = 0; i < sizeof(builds) / sizeof(builds[0]); i++) {
		if (strcmp(hex, builds[i].code_sha256) == 0) {
			log_printf("main32: known build: %s", builds[i].name);
#endif
			game_init();
			crash_install();
			log_printf("main32: settings port %u", (unsigned)settings_port());
			scripts_install();
			mirror_install();
			transplant_install();
			localize_install();
			zones_install();
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
			control_start();
			return 0;
		}
	}
	log_printf("main32: unknown build: nothing is patched");
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved)
{
	(void)reserved;
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(self);
	log_open();
	settings_load(); // before the game registers net_port
	SYSTEMTIME day;
	GetLocalTime(&day);
	log_printf("main32: %04d-%02d-%02d, 32-bit proxy attached, pid %lu", day.wYear, day.wMonth, day.wDay,
		   GetCurrentProcessId());
	d3dcap_install(); // the exe's imports are loaded and its code has not run: the game has not created its device yet
	if (!proxy_init()) {
		log_printf("main32: could not load the real winmm.dll");
		return FALSE;
	}
	CloseHandle(CreateThread(NULL, 0, identify, NULL, 0, NULL));
	return TRUE;
}
