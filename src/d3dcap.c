// The game's Direct3D 9 device without knowing where the game stores it. DllMain runs while the exe's imports are loaded (d3d9.dll is one of
// them, docs/32bit-plan.md), before the game's code runs: the first bytes of d3d9.dll's Direct3DCreate9 get a jump to our function, which
// calls the real one and replaces the CreateDevice entry of the IDirect3D9 object's vtable with ours; ours calls the real CreateDevice and keeps
// the device it returns. A hook installed later (the Steam overlay's) finds our entries where the originals were and calls them.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <string.h>

#include "d3dcap.h"
#include "hook.h"
#include "log.h"

#define VT_CREATE_DEVICE 16 // IDirect3D9::CreateDevice

typedef IDirect3D9 *(WINAPI *create9_t)(UINT);
typedef HRESULT(STDMETHODCALLTYPE *create_device_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);

static create9_t create9_orig;
static create_device_t create_device_orig;
static void *volatile device;

void *d3dcap_device(void)
{
	return device;
}

static HRESULT STDMETHODCALLTYPE create_device_hook(IDirect3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS *pp,
						     IDirect3DDevice9 **out)
{
	HRESULT hr = create_device_orig(d3d, adapter, type, window, flags, pp, out);
	if (SUCCEEDED(hr) && out && *out) {
		device = *out;
		log_printf("d3dcap: CreateDevice(adapter %u, type %d, window %p, flags %lx) -> device %p, vtable %p", adapter, (int)type, (void *)window,
			   (unsigned long)flags, (void *)*out, *(void **)*out);
	} else {
		log_printf("d3dcap: CreateDevice failed, hr %lx", (unsigned long)hr);
	}
	return hr;
}

static IDirect3D9 *WINAPI create9_hook(UINT version)
{
	IDirect3D9 *d3d = create9_orig(version);
	if (d3d && !create_device_orig) {
		void **vt = *(void ***)d3d;
		DWORD old;
		if (VirtualProtect(&vt[VT_CREATE_DEVICE], sizeof(void *), PAGE_READWRITE, &old)) {
			create_device_orig = (create_device_t)vt[VT_CREATE_DEVICE];
			vt[VT_CREATE_DEVICE] = (void *)create_device_hook;
			VirtualProtect(&vt[VT_CREATE_DEVICE], sizeof(void *), old, &old);
			log_printf("d3dcap: Direct3DCreate9(%u) -> %p, CreateDevice hooked", version, (void *)d3d);
		}
	}
	return d3d;
}

static void hook_create9(HMODULE d3d9);

// A client that maps the game itself (iw4x-sp) loads d3d9.dll after our DllMain: watch for it, a millisecond at a time, until the game has its device
static DWORD WINAPI wait_for_d3d9(LPVOID unused)
{
	(void)unused;
	for (int i = 0; i < 120000 && !create9_orig; i++) {
		HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
		if (d3d9) {
			hook_create9(d3d9);
			return 0;
		}
		Sleep(1);
	}
	log_printf("d3dcap: d3d9.dll never loaded");
	return 0;
}

void d3dcap_install(void)
{
	HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
	if (!d3d9) {
		log_printf("d3dcap: d3d9.dll is not loaded yet, waiting for it");
		CloseHandle(CreateThread(NULL, 0, wait_for_d3d9, NULL, 0, NULL));
		return;
	}
	hook_create9(d3d9);
}

static void hook_create9(HMODULE d3d9)
{
	uint8_t *fn = (uint8_t *)GetProcAddress(d3d9, "Direct3DCreate9");
	if (!fn) {
		log_printf("d3dcap: no Direct3DCreate9 in d3d9.dll");
		return;
	}
#ifdef _WIN64
	log_printf("d3dcap: only the 32-bit build needs this");
	return;
#else
	static const uint8_t prolog[5] = {0x8b, 0xff, 0x55, 0x8b, 0xec}; // mov edi,edi; push ebp; mov ebp,esp
	if (!hook_function(fn, prolog, sizeof(prolog), (void *)create9_hook, (void **)&create9_orig))
		log_printf("d3dcap: Direct3DCreate9 starts with %02x %02x %02x %02x %02x, not the expected prologue", fn[0], fn[1], fn[2], fn[3], fn[4]);
	else
		log_printf("d3dcap: Direct3DCreate9 hooked at %p", (void *)fn);
#endif
}
