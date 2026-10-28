// Parts of the 64-bit DLL that the 32-bit build does not have yet; each stub is replaced by the real thing as
// that part is ported (docs/32bit-plan.md), and its source file moves into the x86 line of build.sh.
#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "steamnet.h"
#include "scrdebug.h"
#include "shot.h"

void scrdebug_note_source(const char *ext, const char *text, int len)
{
	(void)ext;
	(void)text;
	(void)len;
}

void scrdebug_trace_method(uintptr_t entry_rva)
{
	log_printf("scrdebug: !trace %llx is not ported to the 32-bit build yet", (unsigned long long)entry_rva);
}

// The 32-bit game's Direct3D device is not located yet: a static search finds no call of its methods in the exe, a throwaway
// device has the Steam overlay's copy of the vtable, and every object of d3d9.dll in the exe's data looks like a device.
void shot_install(void)
{
}

void shot_request(const char *name)
{
	log_printf("shot: %s not saved (the 32-bit build cannot find the game's Direct3D device yet)", name);
}

void shot_burst_start(const char *prefix, int n, int x, int y, int w, int h)
{
	(void)n, (void)x, (void)y, (void)w, (void)h;
	log_printf("shot: burst %s not saved (no Direct3D device in the 32-bit build yet)", prefix);
}

// Steam P2P for Steam joins (steam_relay=1, off by default and untested with two real accounts): not ported to the 32-bit build yet.
void steamnet_install(void)
{
}

int steamnet_send(const char *buf, int len, const struct sockaddr *to)
{
	(void)buf;
	(void)len;
	(void)to;
	return 0;
}

int steamnet_receive(char *buf, int len, struct sockaddr *from, int *fromlen)
{
	(void)buf;
	(void)len;
	(void)from;
	(void)fromlen;
	return 0;
}
