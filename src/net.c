// Packet logging for QA: iw4sp.exe imports sendto (#20) and recvfrom (#17) from WSOCK32.dll by
// ordinal; their IAT slots are pointed at wrappers that log the game's UDP traffic.
// They also carry mw2coop's own packets: the lobby's ready state (menus.c), and the start point,
// sent by the host just before each coopLaunch it sends (see progress.c).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "access.h"
#include "game.h"
#include "log.h"
#include "net.h"
#include "menus.h"
#include "progress.h"
#include "steamnet.h"

typedef int(WINAPI *sendto_t)(SOCKET, const char *, int, int, const struct sockaddr *, int);
typedef int(WINAPI *recvfrom_t)(SOCKET, char *, int, int, struct sockaddr *, int *);

static sendto_t real_sendto;
static recvfrom_t real_recvfrom;
static volatile LONG budget = 150; // max logged packets per run

// The co-op partner: the host learns it from the client's coopConnect, the client from its own.
// mw2coop's lobby packets (net_send_mod) go there on the game's socket.
static SOCKET peer_socket = INVALID_SOCKET;
static struct sockaddr_in peer;
static CRITICAL_SECTION peer_lock;
// the game's UDP socket (the one its out-of-band packets use), for broadcasts
static volatile SOCKET game_socket = INVALID_SOCKET;

static void log_packet(const char *dir, const struct sockaddr *sa, const char *buf, int len)
{
	// skip DemonWare traffic (not 0xffffffff out-of-band) and the lobby bandwidth test ("bw")
	if (len < 6 || *(const unsigned int *)buf != 0xffffffffu || (buf[4] == 'b' && buf[5] == 'w'))
		return;
	if (len >= 17 && memcmp(buf + 4, "mw2coop_ready", 13) == 0) // menus.c's, once a second
		return;
	if (InterlockedDecrement(&budget) < 0)
		return;
	char addr[64] = "?";
	if (sa && sa->sa_family == AF_INET) {
		const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
		const unsigned char *ip = (const unsigned char *)&in->sin_addr;
		snprintf(addr, sizeof(addr), "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], ntohs(in->sin_port));
	}
	char text[200];
	int n = 0;
	for (int i = 0; i < len && n < (int)sizeof(text) - 5; i++) {
		unsigned char c = (unsigned char)buf[i];
		if (c >= 0x20 && c < 0x7f && c != '\\')
			text[n++] = (char)c;
		else
			n += snprintf(text + n, sizeof(text) - n, "\\x%02x", c);
	}
	text[n] = 0;
	log_printf("net: %s %s len %d: %s", dir, addr, len, text);
}

static int contains(const char *buf, int len, const char *word)
{
	int wl = (int)strlen(word);
	for (int i = 0; i + wl <= len; i++)
		if (memcmp(buf + i, word, wl) == 0)
			return 1;
	return 0;
}

// every send of the game and of mw2coop: a Steam peer's virtual address goes through Steam
static int send_to(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
	if (steamnet_send(buf, len, to))
		return len;
	return real_sendto(s, buf, len, flags, to, tolen);
}

static void remember_peer(SOCKET s, const struct sockaddr *sa)
{
	if (!sa || sa->sa_family != AF_INET)
		return;
	EnterCriticalSection(&peer_lock);
	if (peer_socket != s || memcmp(&peer, sa, sizeof(peer)) != 0) {
		peer_socket = s;
		memcpy(&peer, sa, sizeof(peer));
		const unsigned char *ip = (const unsigned char *)&peer.sin_addr;
		log_printf("net: co-op partner %u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], ntohs(peer.sin_port));
	}
	LeaveCriticalSection(&peer_lock);
}

void net_log_budget(int packets)
{
	InterlockedExchange(&budget, packets);
	log_printf("net: logging the next %d packets", packets);
}

int net_send_mod_to(const uint8_t ip[4], uint16_t port, const char *text)
{
	SOCKET s = game_socket;
	char pkt[128];
	int n = snprintf(pkt, sizeof(pkt), "\xff\xff\xff\xff%s", text);
	if (s == INVALID_SOCKET || !real_sendto || n <= 0 || n >= (int)sizeof(pkt))
		return 0;
	struct sockaddr_in to = {0};
	to.sin_family = AF_INET;
	to.sin_port = htons(port);
	memcpy(&to.sin_addr, ip, 4);
	return send_to(s, pkt, n, 0, (struct sockaddr *)&to, sizeof(to)) == n;
}

int net_send_mod(const char *text)
{
	char pkt[128];
	int n = snprintf(pkt, sizeof(pkt), "\xff\xff\xff\xff%s", text);
	if (n <= 0 || n >= (int)sizeof(pkt) || !real_sendto)
		return 0;
	EnterCriticalSection(&peer_lock);
	int ok = peer_socket != INVALID_SOCKET &&
		 send_to(peer_socket, pkt, n, 0, (const struct sockaddr *)&peer, sizeof(peer)) == n;
	LeaveCriticalSection(&peer_lock);
	return ok;
}

// Every IPv4 network the PC is on (LAN, Wi-Fi, Radmin VPN, Hamachi...) gets the packet at its
// broadcast address, plus 255.255.255.255; port in host order.
int net_broadcast_mod(const char *text, uint16_t port)
{
	SOCKET s = game_socket;
	if (s == INVALID_SOCKET || !real_sendto)
		return 0;
	char pkt[128];
	int n = snprintf(pkt, sizeof(pkt), "\xff\xff\xff\xff%s", text);
	if (n <= 0 || n >= (int)sizeof(pkt))
		return 0;
	BOOL on = TRUE;
	setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof(on));
	struct sockaddr_in to = {0};
	to.sin_family = AF_INET;
	to.sin_port = htons(port);
	to.sin_addr.s_addr = INADDR_BROADCAST;
	int sent = real_sendto(s, pkt, n, 0, (struct sockaddr *)&to, sizeof(to)) == n;

	ULONG size = 16384;
	IP_ADAPTER_ADDRESSES *list = HeapAlloc(GetProcessHeap(), 0, size);
	if (list && GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
					 NULL, list, &size) == NO_ERROR) {
		for (IP_ADAPTER_ADDRESSES *a = list; a; a = a->Next) {
			if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
				continue;
			for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
				if (u->Address.lpSockaddr->sa_family != AF_INET || u->OnLinkPrefixLength >= 32)
					continue;
				ULONG ip = ntohl(((struct sockaddr_in *)u->Address.lpSockaddr)->sin_addr.s_addr);
				ULONG mask = u->OnLinkPrefixLength ? 0xffffffffu << (32 - u->OnLinkPrefixLength) : 0;
				to.sin_addr.s_addr = htonl(ip | ~mask);
				sent += real_sendto(s, pkt, n, 0, (struct sockaddr *)&to, sizeof(to)) == n;
			}
		}
	}
	if (list)
		HeapFree(GetProcessHeap(), 0, list);
	return sent;
}

// mw2coop's start packet (the host's session dvars and difficulty) and ready packet (the partner's id and
// readiness) mean something only from the other player of this session. Anybody who knew the port could send
// them, so they are acted on only when they come from the address the game talks to (remember_peer; by IP: the
// port a NAT gives may differ). The lobby's own packets (access, list of games) are answered as before.
static volatile LONG ignored_packets;

static int is_session_packet(const char *buf, int len)
{
	static const char start[] = "\xff\xff\xff\xffmw2coop_start";
	static const char ready[] = "\xff\xff\xff\xffmw2coop_ready";
	static const char brief[] = "\xff\xff\xff\xffmw2coop_briefdone";
	return (len >= (int)sizeof(start) - 1 && memcmp(buf, start, sizeof(start) - 1) == 0) ||
	       (len >= (int)sizeof(ready) - 1 && memcmp(buf, ready, sizeof(ready) - 1) == 0) ||
	       (len >= (int)sizeof(brief) - 1 && memcmp(buf, brief, sizeof(brief) - 1) == 0);
}

static int from_partner(const struct sockaddr *from)
{
	if (!from || from->sa_family != AF_INET)
		return 0;
	EnterCriticalSection(&peer_lock);
	int same = peer_socket != INVALID_SOCKET &&
		   ((const struct sockaddr_in *)from)->sin_addr.s_addr == peer.sin_addr.s_addr;
	LeaveCriticalSection(&peer_lock);
	return same;
}

static int WINAPI my_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
	if (len > 4 && *(const unsigned int *)buf == 0xffffffffu)
		game_socket = s;
	if (len > 4 && *(const unsigned int *)buf == 0xffffffffu && !game_peek(RVA_PARTY_IS_HOST, 1) &&
	    contains(buf, len, "coopConnect"))
		remember_peer(s, to);
	// The party resends coopLaunch until the client acks it; the start packet goes along each time.
	if (len > 4 && *(const unsigned int *)buf == 0xffffffffu && game_peek(RVA_PARTY_IS_HOST, 1) &&
	    contains(buf, len, "coopLaunch")) {
		char pkt[256];
		int n = progress_launch_packet(pkt, sizeof(pkt));
		log_packet("send", to, pkt, n);
		send_to(s, pkt, n, flags, to, tolen);
	}
	log_packet("send", to, buf, len);
	return send_to(s, buf, len, flags, to, tolen);
}

static int WINAPI my_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
	int r = steamnet_receive(buf, len, from, fromlen);
	if (r <= 0)
		r = real_recvfrom(s, buf, len, flags, from, fromlen);
	if (r > 0) {
		log_packet("recv", from, buf, r);
		if (r > 4 && *(const unsigned int *)buf == 0xffffffffu && game_peek(RVA_PARTY_IS_HOST, 1) &&
		    contains(buf, r, "coopConnect"))
			remember_peer(s, from);
		char reply[128];
		int reply_len = 0, lobby = 0;
		if (r > 4 && *(const unsigned int *)buf == 0xffffffffu && from && from->sa_family == AF_INET) {
			const struct sockaddr_in *in = (const struct sockaddr_in *)from;
			game_socket = s;
			lobby = access_on_packet(buf, r, (const uint8_t *)&in->sin_addr, ntohs(in->sin_port), reply,
						 sizeof(reply), &reply_len) ||
				menus_on_lobby_packet(buf, r, (const uint8_t *)&in->sin_addr, ntohs(in->sin_port), reply,
						      sizeof(reply), &reply_len);
			if (reply_len > 0 && reply_len < (int)sizeof(reply))
				send_to(s, reply, reply_len, 0, from, sizeof(struct sockaddr_in));
		}
		int ours = lobby;
		if (!ours && is_session_packet(buf, r)) {
			// consumed either way: the game has nothing to do with it
			if (from_partner(from)) {
				if (!progress_on_packet(buf, r))
					menus_on_packet(buf, r);
			} else if (InterlockedIncrement(&ignored_packets) <= 5)
				log_printf("net: a start or ready packet from an address that is not the partner's was ignored");
			ours = 1;
		}
		if (ours) {
			WSASetLastError(WSAEWOULDBLOCK); // ours: the game sees "no packet this time"
			return SOCKET_ERROR;
		}
	}
	return r;
}

void net_install(void)
{
	InitializeCriticalSection(&peer_lock);
	BYTE *base = (BYTE *)GetModuleHandleA(NULL);
	IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
	IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
		if (lstrcmpiA((const char *)(base + d->Name), "WSOCK32.dll") != 0)
			continue;
		IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(base + d->OriginalFirstThunk);
		IMAGE_THUNK_DATA64 *iat = (IMAGE_THUNK_DATA64 *)(base + d->FirstThunk);
		for (; names->u1.Ordinal; names++, iat++) {
			if (!IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
				continue;
			WORD ord = (WORD)IMAGE_ORDINAL64(names->u1.Ordinal);
			void *wrapper = NULL;
			if (ord == 20) {
				real_sendto = (sendto_t)iat->u1.Function;
				wrapper = (void *)my_sendto;
			} else if (ord == 17) {
				real_recvfrom = (recvfrom_t)iat->u1.Function;
				wrapper = (void *)my_recvfrom;
			}
			if (!wrapper)
				continue;
			DWORD old;
			VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
			iat->u1.Function = (ULONGLONG)wrapper;
			VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
			log_printf("net: hooked WSOCK32 #%u", ord);
		}
	}
}
