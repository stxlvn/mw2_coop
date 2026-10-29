// Parts of the 64-bit DLL that the 32-bit build does not have yet; each stub is replaced by the real thing as
// that part is ported (docs/32bit-plan.md), and its source file moves into the x86 line of build.sh.
#include <stddef.h>
#include <stdint.h>

#include "log.h"
#include "steamnet.h"
#include "scrdebug.h"

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
