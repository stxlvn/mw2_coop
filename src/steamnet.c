// Game traffic through Steam (settings.ini [network] steam_relay=1; off by default, untested
// with two real accounts yet).
//
// A Steam invite makes the client join the host's Steam lobby and then "connect <host ip>
// <port>" (rva 0x28f1b0 -> 0x22dda0 -> PartyClient_Join 0xd7cb0): plain UDP, so a host behind
// the provider's NAT (CGNAT) or a strict firewall cannot be reached. The game itself only uses
// Steam P2P (ISteamNetworking, channel 0, rva 0x2413a0) to swap addresses for a NAT punch.
//
// With steam_relay on, each Steam peer gets a virtual address 240.10.0.<n> (class E, never on a
// real network). Packets the game sends there go through Steam P2P (channel 7), which crosses
// NATs and falls back to Valve's relays; packets from Steam come back to the game as if from
// that address. The client side: 0x22dda0 is replaced so the host info from the Steam lobby
// points at the host's virtual address. The host side: P2P sessions are accepted from the
// host's Steam friends only (P2PSessionRequest_t, callback 1202).
//
// ISteamNetworking005 vtable: 0 SendP2PPacket(id, data, size, EP2PSend, channel),
// 1 IsP2PPacketAvailable(*size, channel), 2 ReadP2PPacket(buf, cap, *size, *id, channel),
// 3 AcceptP2PSessionWithUser(id), 7 AllowP2PPacketRelay(bool).
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "hook.h"
#include "log.h"
#include "settings.h"
#include "steamnet.h"

// The Steam interfaces are C++ objects: in the 32-bit build their methods take `this` in ECX (thiscall) and a CSteamID as two stack dwords; the 64-bit build has one convention.
#ifdef _WIN64
#define THISCALL
#define STEAM_DLL "steam_api64.dll"
#else
#define THISCALL __attribute__((thiscall))
#define STEAM_DLL "steam_api.dll"
#endif

#define CHANNEL 7
#define MAX_PEERS 16
#define VIRTUAL_A 240
#define VIRTUAL_B 10
#define P2P_SEND_UNRELIABLE 0
#define CALLBACK_P2P_SESSION_REQUEST 1202

static int enabled;
static uint8_t *base;
static uint64_t peers[MAX_PEERS]; // peers[n - 1] has the address 240.10.0.<n>
static CRITICAL_SECTION peers_lock;

static void *networking(void)
{
	static void *(*get)(void);
	if (!get) {
		HMODULE h = GetModuleHandleA(STEAM_DLL);
		get = h ? (void *(*)(void))GetProcAddress(h, "SteamNetworking") : NULL;
	}
	return get ? get() : NULL;
}

#define VT(obj, i) ((*(void ***)(obj))[i])

static int steam_friend(uint64_t id)
{
	static void *(*get)(void);
	if (!get) {
		HMODULE h = GetModuleHandleA(STEAM_DLL);
		get = h ? (void *(*)(void))GetProcAddress(h, "SteamFriends") : NULL;
	}
	void *friends = get ? get() : NULL;
	return friends && ((int (THISCALL *)(void *, uint64_t))VT(friends, 5))(friends, id) == 3;
}

static int peer_index(uint64_t id, int add)
{
	int found = -1;
	EnterCriticalSection(&peers_lock);
	for (int i = 0; i < MAX_PEERS && found < 0; i++)
		if (peers[i] == id)
			found = i;
	for (int i = 0; add && found < 0 && i < MAX_PEERS; i++)
		if (!peers[i]) {
			peers[i] = id;
			found = i;
			log_printf("steamnet: peer %016llx is 240.10.0.%d", (unsigned long long)id, i + 1);
		}
	LeaveCriticalSection(&peers_lock);
	return found;
}

static int is_virtual(const uint8_t *ip)
{
	return ip[0] == VIRTUAL_A && ip[1] == VIRTUAL_B && ip[2] == 0 && ip[3] >= 1 && ip[3] <= MAX_PEERS;
}

int steamnet_send(const char *buf, int len, const struct sockaddr *to)
{
	if (!enabled || !to || to->sa_family != AF_INET)
		return 0;
	const uint8_t *ip = (const uint8_t *)&((const struct sockaddr_in *)to)->sin_addr;
	if (!is_virtual(ip))
		return 0;
	uint64_t id = peers[ip[3] - 1];
	void *net = networking();
	if (id && net)
		((char (THISCALL *)(void *, uint64_t, const void *, uint32_t, int, int))VT(net, 0))(net, id, buf, (uint32_t)len,
										   P2P_SEND_UNRELIABLE, CHANNEL);
	return 1; // handled (a virtual address never goes to the real network)
}

int steamnet_receive(char *buf, int len, struct sockaddr *from, int *fromlen)
{
	if (!enabled)
		return 0;
	void *net = networking();
	uint32_t size = 0;
	if (!net || !((char (THISCALL *)(void *, uint32_t *, int))VT(net, 1))(net, &size, CHANNEL) || !size)
		return 0;
	static char packet[2048];
	uint64_t id = 0;
	uint32_t got = 0;
	if (!((char (THISCALL *)(void *, void *, uint32_t, uint32_t *, uint64_t *, int))VT(net, 2))(net, packet, sizeof(packet), &got,
											      &id, CHANNEL))
		return 0;
	int n = peer_index(id, 1);
	if (n < 0 || (int)got > len)
		return 0;
	memcpy(buf, packet, got);
	if (from && fromlen && *fromlen >= (int)sizeof(struct sockaddr_in)) {
		struct sockaddr_in *in = (struct sockaddr_in *)from;
		memset(in, 0, sizeof(*in));
		in->sin_family = AF_INET;
		in->sin_port = htons(settings_port());
		uint8_t *ip = (uint8_t *)&in->sin_addr;
		ip[0] = VIRTUAL_A, ip[1] = VIRTUAL_B, ip[2] = 0, ip[3] = (uint8_t)(n + 1);
		*fromlen = sizeof(*in);
	}
	return (int)got;
}

// ---------------------------------------------------------------- P2P session requests

// A C++ CCallbackBase for SteamAPI_RegisterCallback: vtable {Run(param), Run(param, io failure,
// call), GetCallbackSizeBytes()}, then uint8 flags at +8, int callback id at +12.
typedef struct {
	void **vtable;
	uint8_t flags;
	uint8_t pad[3];
	int id;
} CallbackBase;

static void THISCALL on_session_request(void *self, void *param)
{
	(void)self;
	uint64_t id = *(uint64_t *)param; // P2PSessionRequest_t {CSteamID remote}
	void *net = networking();
	int ok = steam_friend(id);
	if (ok && net)
		((char (THISCALL *)(void *, uint64_t))VT(net, 3))(net, id);
	log_printf("steamnet: P2P session from %016llx %s", (unsigned long long)id, ok ? "accepted (Steam friend)" : "ignored");
}

static void THISCALL on_session_request_io(void *self, void *param, char io_failure, uint64_t call)
{
	(void)io_failure, (void)call;
	on_session_request(self, param);
}

static int THISCALL session_request_size(void *self)
{
	(void)self;
	return 8;
}

static void *callback_vtable[] = {(void *)on_session_request, (void *)on_session_request_io, (void *)session_request_size};
static CallbackBase session_request = {callback_vtable, 0, {0}, CALLBACK_P2P_SESSION_REQUEST};

// ---------------------------------------------------------------- the client's Steam join

// 0x22dda0(localClient, hostInfo, *ip, *port), called by the Steam lobby join (0x28f1b0) only.
// hostInfo (64 bytes): +0 the host's XUID = SteamID, +8 internal IPv4, +0xc external IPv4,
// +0x10/+0x12 ports (see party.c). 64 bits: the original body is repeated after the rewrite.
// 32 bits: VA 0x4090c0, cdecl (localClient, hostInfo, *ip, *port) too: it ends the co-op shutdown of an earlier session and jumps to
// PartyClient_Join (VA 0x42fc40) with the same stack arguments, so the replacement rewrites the host info and calls the original.
static void steam_host_info(uint8_t *host_info)
{
	uint64_t host;
	memcpy(&host, host_info, 8);
	int n = peer_index(host, 1);
	if (n >= 0) {
		uint8_t ip[4] = {VIRTUAL_A, VIRTUAL_B, 0, (uint8_t)(n + 1)};
		uint16_t port = settings_port();
		memcpy(host_info + 8, ip, 4);
		memcpy(host_info + 0xc, ip, 4);
		memcpy(host_info + 0x10, &port, 2);
		memcpy(host_info + 0x12, &port, 2);
		log_printf("steamnet: joining host %016llx through Steam (240.10.0.%d)", (unsigned long long)host, n + 1);
	}
}

#ifdef _WIN64
#define RVA_STEAM_JOIN 0x22dda0
#define RVA_PARTY_CLIENT_JOIN 0x0d7cb0
#define RVA_PARTY_STOP 0x0d7dd0

static char steam_join(int local_client, uint8_t *host_info, uint8_t *ip_out, uint16_t *port_out)
{
	steam_host_info(host_info);
	// the original 0x22dda0
	if (base[0x52fb5c]) {
		base[0x52fb5c] = 0;
		base[0x1905302] = 0;
		((void (*)(void *, char))(base + 0x26d800))(*(void **)(base + 0x1905308), 0);
		((void (*)(void))(base + RVA_PARTY_STOP))();
	}
	return ((char (*)(int, uint8_t *, uint8_t *, uint16_t *))(base + RVA_PARTY_CLIENT_JOIN))(local_client, host_info,
												 ip_out, port_out);
}

static const uint8_t steam_join_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24};
#else
#define RVA_STEAM_JOIN 0x90c0

static char (*steam_join_original)(int, uint8_t *, uint8_t *, uint16_t *);

static char steam_join(int local_client, uint8_t *host_info, uint8_t *ip_out, uint16_t *port_out)
{
	steam_host_info(host_info);
	return steam_join_original(local_client, host_info, ip_out, port_out);
}

// cmp byte ptr [0x88897c], 0 (the co-op running flag), the first instruction: 7 bytes, no relative operand
static const uint8_t steam_join_bytes[] = {0x80, 0x3d, 0x7c, 0x89, 0x88, 0x00, 0x00};
#endif

void steamnet_install(void)
{
	enabled = settings_steam_relay();
	if (!enabled)
		return;
	InitializeCriticalSection(&peers_lock);
	base = (uint8_t *)GetModuleHandleA(NULL);
	uint8_t *at = base + RVA_STEAM_JOIN;
	if (memcmp(at, steam_join_bytes, sizeof(steam_join_bytes)) != 0) {
		log_printf("steamnet: unexpected code at rva 0x%x, Steam relay off", RVA_STEAM_JOIN);
		enabled = 0;
		return;
	}
#ifdef _WIN64
	hook_jump(at, 14, steam_join, 0x90);
#else
	if (!hook_function(at, steam_join_bytes, sizeof(steam_join_bytes), steam_join, (void **)&steam_join_original)) {
		log_printf("steamnet: could not hook the Steam join at rva 0x%x, Steam relay off", RVA_STEAM_JOIN);
		enabled = 0;
		return;
	}
#endif

	HMODULE h = GetModuleHandleA(STEAM_DLL);
	void (*reg)(void *, int) = h ? (void (*)(void *, int))GetProcAddress(h, "SteamAPI_RegisterCallback") : NULL;
	if (reg)
		reg(&session_request, CALLBACK_P2P_SESSION_REQUEST);
	void *net = networking();
	if (net)
		((char (THISCALL *)(void *, char))VT(net, 7))(net, 1); // AllowP2PPacketRelay
	log_printf("steamnet: on (Steam joins go through Steam P2P, channel %d)", CHANNEL);
}
