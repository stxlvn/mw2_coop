// Vibration for the split screen players' pads. This PC build has no rumble: its script builtins (PlayRumbleOnEntity, PlayRumbleLoopOnEntity, StopRumble,
// PlayRumbleOnPosition, PlayRumbleLoopOnPosition, StopAllRumbles, PrecacheRumble) all point at one empty function (rva 0xa650), and the console's rumble assets
// are not in the PC zones. The levels still call them (damage, grenades, artillery, vehicles, the ice picks...), so the builtins are pointed here: a rumble on
// player 1 or 2 (entities 0 / 1) shakes that player's pad; one on another entity or at a position shakes the players near it, less with the distance (a loop on
// an entity follows it). Each rumble name the levels use has an approximation of the console's below (the two motors' strength and the length).
// Builtins are {const char *name; void (*func)(); int developer} (methods take the entity reference: entity number in the low 16 bits, class in the high 16).
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "log.h"
#include "rumble.h"

#ifdef _WIN64
#define RVA_EMPTY_BUILTIN 0xa650
#define RVA_SCR_GET_STRING 0x223440 // const char *Scr_GetString(int index)
#define RVA_SCR_GET_VECTOR 0x223540 // void Scr_GetVector(int index, float out[3])
#define RVA_G_ENTITIES 0xd9e070
#define GENTITY_SIZE 0x2a0
#define GENTITY_ORIGIN 0xe8   // r.currentOrigin
#define GENTITY_CLIENT 0x108
#define RB_ON_ENTITY 0x342e20 // the builtin table entries (24 bytes each in 64 bits)
#define RB_LOOP_ON_ENTITY 0x342e38
#define RB_STOP 0x342e50
#define RB_ON_POSITION 0x3c2138
#define RB_LOOP_ON_POSITION 0x3c2150
#define RB_STOP_ALL 0x3c2168
#else
// the 32-bit official build (docs/32bit-native-map/aimrumble.md): entries are 12 bytes {name, func, developer}
#define RVA_EMPTY_BUILTIN 0xcf10
#define RVA_SCR_GET_STRING 0xd5f20
#define RVA_SCR_GET_VECTOR 0x64850
#define RVA_G_ENTITIES 0xaa76b8
#define GENTITY_SIZE 0x270
#define GENTITY_ORIGIN 0xe8
#define GENTITY_CLIENT 0x108
#define RB_ON_ENTITY 0x2e9fd0
#define RB_LOOP_ON_ENTITY 0x2e9fdc
#define RB_STOP 0x2e9fe8
#define RB_ON_POSITION 0x331aec
#define RB_LOOP_ON_POSITION 0x331af8
#define RB_STOP_ALL 0x331b04
#endif

static const struct {
	const char *name;
	float low, high; // the heavy (left) and the light (right) motor, 0..1
	int ms;
	float range; // how far from the entity or the position players feel it (0: only the entity itself, when it is a player)
} profiles[] = {
    {"damage_heavy", 0.9f, 0.6f, 350, 0},
    {"damage_light", 0.45f, 0.35f, 200, 0},
    {"grenade_rumble", 1.0f, 0.8f, 500, 600},
    {"artillery_rumble", 1.0f, 0.9f, 800, 1500},
    {"arcadia_artillery_rumble", 1.0f, 0.9f, 800, 1500},
    {"tank_rumble", 0.35f, 0.15f, 500, 700},
    {"steady_rumble", 0.25f, 0.25f, 500, 1000},
    {"minigun_rumble", 0.35f, 0.6f, 300, 0},
    {"stinger_lock_rumble", 0.0f, 0.35f, 300, 0},
    {"light_1s", 0.3f, 0.3f, 1000, 1000},
    {"light_2s", 0.3f, 0.3f, 2000, 1000},
    {"light_3s", 0.3f, 0.3f, 3000, 1000},
    {"heavy_1s", 0.8f, 0.6f, 1000, 1000},
    {"heavy_2s", 0.8f, 0.6f, 2000, 1000},
    {"heavy_3s", 0.8f, 0.6f, 3000, 1000},
    {"icepick_climb", 0.2f, 0.5f, 150, 0},
    {"icepick_slide", 0.3f, 0.5f, 400, 0},
    {"icepick_release", 0.5f, 0.4f, 200, 0},
    {"icepick_hang", 0.2f, 0.3f, 150, 0},
    {"falling_land", 0.6f, 0.4f, 250, 0},
    {"crash_heli_rumble", 1.0f, 0.8f, 1200, 2000},
    {"collapsing_building", 0.8f, 0.6f, 2000, 3000},
    {"mig_rumble", 0.7f, 0.5f, 1500, 3000},
    {"c130_flyby", 0.6f, 0.4f, 2000, 4000},
    {"shot_collapse", 0.8f, 0.6f, 600, 0},
    {"bodytoss_impact", 0.7f, 0.5f, 300, 0},
    {"dsm_rummage", 0.2f, 0.2f, 300, 0},
    {"smg_fire", 0.15f, 0.35f, 80, 0},
    {"pistol_fire", 0.2f, 0.4f, 70, 0},
    {"pistol_fire_auto", 0.2f, 0.4f, 70, 0},
    {"shepherd_pistol", 0.6f, 0.6f, 200, 0},
    {NULL, 0.5f, 0.4f, 300, 800}, // any other name
};
#define PROFILES (int)(sizeof(profiles) / sizeof(profiles[0]))

#define MAX_RUMBLES 16
typedef struct {
	int profile;
	DWORD start;
	int loop;
	int ent;        // the entity it plays on, or -1: at a position
	float pos[3];   // the position (ent -1)
	char name[40];  // for StopRumble
} rumble_t;
static rumble_t rumbles[MAX_RUMBLES];
static int rumble_count;

static uint8_t *base;
static DWORD(WINAPI *xinput_set)(DWORD, void *);
static volatile LONG last_set[2];   // GetTickCount of each player's last motor write (the watchdog stops motors no frame has written lately)
static volatile LONG motors_on[4];  // per pad: the last strengths written, packed (low << 16 | high)
static int logging;

static int profile_of(const char *name)
{
	for (int i = 0; i < PROFILES - 1; i++)
		if (name && !_stricmp(name, profiles[i].name))
			return i;
	return PROFILES - 1;
}

static const float *ent_origin(int ent)
{
	return (const float *)(base + RVA_G_ENTITIES + (size_t)ent * GENTITY_SIZE + GENTITY_ORIGIN);
}

static int is_player(int ent)
{
	return ent >= 0 && ent < 2 && *(void **)(base + RVA_G_ENTITIES + (size_t)ent * GENTITY_SIZE + GENTITY_CLIENT) != NULL;
}

static void add(const char *name, int loop, int ent, const float *pos)
{
	if (!name)
		return;
	rumble_t *r = NULL;
	for (int i = 0; i < rumble_count && !r; i++) // the same rumble on the same thing starts again
		if (rumbles[i].ent == ent && !_stricmp(rumbles[i].name, name) && (ent >= 0 || !memcmp(rumbles[i].pos, pos, sizeof(rumbles[i].pos))))
			r = &rumbles[i];
	if (!r) {
		if (rumble_count == MAX_RUMBLES) // the oldest goes
			memmove(rumbles, rumbles + 1, --rumble_count * sizeof(rumbles[0]));
		r = &rumbles[rumble_count++];
	}
	r->profile = profile_of(name);
	r->start = GetTickCount();
	r->loop = loop;
	r->ent = ent;
	if (pos)
		memcpy(r->pos, pos, sizeof(r->pos));
	lstrcpynA(r->name, name, sizeof(r->name));
	if (logging)
		log_printf("rumble: %s%s on %s %d (%s)", loop ? "loop " : "", name, ent >= 0 ? "entity" : "position", ent >= 0 ? ent : (int)pos[0],
			   r->profile == PROFILES - 1 ? "unknown name, the default" : "known");
}

static void stop(const char *name, int ent)
{
	for (int i = 0; i < rumble_count;)
		if ((ent < -1 || rumbles[i].ent == ent) && (!name || !_stricmp(rumbles[i].name, name)))
			memmove(rumbles + i, rumbles + i + 1, (--rumble_count - i) * sizeof(rumbles[0]));
		else
			i++;
}

static const char *arg_string(int index)
{
	return ((const char *(*)(int))(base + RVA_SCR_GET_STRING))(index);
}

static void on_entity_f(uint32_t entref)
{
	if (entref >> 16 == 0)
		add(arg_string(0), 0, (int)(entref & 0xffff), NULL);
}
static void loop_on_entity_f(uint32_t entref)
{
	if (entref >> 16 == 0)
		add(arg_string(0), 1, (int)(entref & 0xffff), NULL);
}
static void stop_f(uint32_t entref)
{
	if (entref >> 16 == 0)
		stop(arg_string(0), (int)(entref & 0xffff));
}
static void on_position_f(void)
{
	float pos[3];
	const char *name = arg_string(0);
	((void (*)(int, float *))(base + RVA_SCR_GET_VECTOR))(1, pos);
	add(name, 0, -1, pos);
}
static void loop_on_position_f(void)
{
	float pos[3];
	const char *name = arg_string(0);
	((void (*)(int, float *))(base + RVA_SCR_GET_VECTOR))(1, pos);
	add(name, 1, -1, pos);
}
static void stop_all_f(void)
{
	stop(NULL, -2);
}

static void set_motors(int pad, float low, float high)
{
	if (!xinput_set || pad < 0 || pad > 3)
		return;
	WORD v[2] = {(WORD)(low * 65535.0f), (WORD)(high * 65535.0f)};
	LONG packed = (LONG)((v[0] >> 1) << 16 | (v[1] >> 1)); // (halved: a LONG keeps it positive)
	LONG was = InterlockedExchange(&motors_on[pad], packed);
	if (was == packed)
		return;
	DWORD r = xinput_set((DWORD)pad, v);
	if (logging && (was != 0) != (packed != 0))
		log_printf("rumble: pad %d motors %s (%.2f %.2f, XInput %lu)", pad, packed ? "on" : "off", low, high, (unsigned long)r);
}

static int pad_of[2] = {-1, -1};
void rumble_frame(int player, int pad, int on)
{
	if (player < 0 || player > 1)
		return;
	if (pad_of[player] >= 0 && pad_of[player] != pad)
		set_motors(pad_of[player], 0, 0);
	pad_of[player] = pad;
	InterlockedExchange(&last_set[player], (LONG)GetTickCount());
	float low = 0, high = 0;
	DWORD now = GetTickCount();
	for (int i = 0; i < rumble_count;) {
		rumble_t *r = &rumbles[i];
		int ms = profiles[r->profile].ms;
		DWORD age = now - r->start;
		if (!r->loop && age >= (DWORD)ms) { // over
			memmove(rumbles + i, rumbles + i + 1, (--rumble_count - i) * sizeof(rumbles[0]));
			continue;
		}
		i++;
		float scale;
		if (r->ent == player && is_player(player))
			scale = 1.0f;
		else {
			float range = profiles[r->profile].range;
			if (range <= 0 || !is_player(player))
				continue;
			const float *at = r->ent >= 0 ? ent_origin(r->ent) : r->pos, *me = ent_origin(player);
			float d = sqrtf((at[0] - me[0]) * (at[0] - me[0]) + (at[1] - me[1]) * (at[1] - me[1]) + (at[2] - me[2]) * (at[2] - me[2]));
			if (d >= range)
				continue;
			scale = 1.0f - d / range;
		}
		if (!r->loop && age > (DWORD)ms * 7 / 10) // the last 30% fade out
			scale *= (float)(ms - age) / (ms * 0.3f);
		if (profiles[r->profile].low * scale > low)
			low = profiles[r->profile].low * scale;
		if (profiles[r->profile].high * scale > high)
			high = profiles[r->profile].high * scale;
	}
	if (!on)
		low = high = 0;
	set_motors(pad, low, high);
}

void rumble_set_log(int on)
{
	logging = on;
}

void rumble_reset(void)
{
	rumble_count = 0;
	for (int pad = 0; pad < 4; pad++)
		if (motors_on[pad])
			set_motors(pad, 0, 0);
}

// motors no frame has written for 200 ms stop (a pause menu, a level load, a hang, the game closing)
static DWORD WINAPI watchdog(void *arg)
{
	(void)arg;
	for (;;) {
		Sleep(100);
		for (int pl = 0; pl < 2; pl++) {
			int pad = pad_of[pl];
			if (pad >= 0 && motors_on[pad] && GetTickCount() - (DWORD)last_set[pl] > 200)
				set_motors(pad, 0, 0);
		}
	}
	return 0;
}

void rumble_install(void)
{
	base = (uint8_t *)GetModuleHandleA(NULL);
	HMODULE m = LoadLibraryA("xinput1_4.dll");
	if (!m)
		m = LoadLibraryA("xinput1_3.dll");
	if (!m)
		m = LoadLibraryA("xinput9_1_0.dll");
	if (m)
		xinput_set = (DWORD(WINAPI *)(DWORD, void *))GetProcAddress(m, "XInputSetState");
	static const struct {
		uint32_t rva;
		const char *name;
		void *func;
	} builtins[] = {
	    {RB_ON_ENTITY, "playrumbleonentity", (void *)on_entity_f},
	    {RB_LOOP_ON_ENTITY, "playrumblelooponentity", (void *)loop_on_entity_f},
	    {RB_STOP, "stoprumble", (void *)stop_f},
	    {RB_ON_POSITION, "playrumbleonposition", (void *)on_position_f},
	    {RB_LOOP_ON_POSITION, "playrumblelooponposition", (void *)loop_on_position_f},
	    {RB_STOP_ALL, "stopallrumbles", (void *)stop_all_f},
	};
	int done = 0;
	for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
		uint8_t *entry = base + builtins[i].rva;
		const char *name = *(const char **)entry;
		void **func = (void **)(entry + sizeof(char *));
		if (!name || strcmp(name, builtins[i].name) || *func != (void *)(base + RVA_EMPTY_BUILTIN)) {
			log_printf("rumble: %s is not the empty builtin at rva 0x%x, left alone", builtins[i].name, builtins[i].rva);
			continue;
		}
		DWORD old;
		VirtualProtect(func, sizeof(*func), PAGE_READWRITE, &old);
		*func = builtins[i].func;
		VirtualProtect(func, sizeof(*func), old, &old);
		done++;
	}
	CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
	log_printf("rumble: %d rumble builtins on the pads (XInput %s)", done, xinput_set ? "found" : "missing");
}
