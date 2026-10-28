// The mod's own localized strings (&"MW2COOP_..." in scripts). The game draws Cyrillic only from
// its localized strings, which this build keeps in Windows-1251 (COOP_WAITINGFORPLAYER reads
// ce e6 e8 e4 ...): a plain script string with Cyrillic shows as dots. So
// DB_FindXAssetHeader(ASSET_TYPE_LOCALIZE_ENTRY, "MW2COOP_...") is answered here, in Russian
// when the game is Russian (MENU_YES reads "Да"), else in English.
//
// DB_FindXAssetHeader (rva 0x0ef5d0) starts with three 5-byte moves that use no RIP-relative
// address (mov [rsp+8],rbx / mov [rsp+10h],rbp / mov [rsp+18h],rsi); those 15 bytes are run from
// a trampoline that then jumps back to +15.
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "fonts.h"
#include "localize.h"
#include "scripts.h"
#include "transplant.h"
#include "hook.h"
#include "log.h"

#define ASSET_TYPE_LOCALIZE_ENTRY 27

typedef struct {
	const char *value;
	const char *name;
} LocalizeEntry;

typedef void *(*db_find_header_t)(int type, const char *name);

#define ASSET_TYPE_FONT 24
static db_find_header_t original;
static int russian = -1;	// -1 unknown yet

typedef struct {
	const char *name;
	const char *ru;	// Windows-1251
	const char *en;
} ModString;

static const ModString strings[] = {
	// Вы оглушены. Ожидайте второго игрока
	{"MW2COOP_STUNNED", "\xc2\xfb \xee\xe3\xeb\xf3\xf8\xe5\xed\xfb. \xce\xe6\xe8\xe4\xe0\xe9\xf2\xe5 \xe2\xf2\xee\xf0\xee\xe3\xee \xe8\xe3\xf0\xee\xea\xe0",
	 "You are stunned. Wait for the other player"},
	// Удерживайте [{+activate}], чтобы сесть за пулемёт (Team Player: the passenger takes the turret)
	{"MW2COOP_SWAP_SEATS", "\xd3\xe4\xe5\xf0\xe6\xe8\xe2\xe0\xe9\xf2\xe5 [{+activate}], \xf7\xf2\xee\xe1\xfb \xf1\xe5\xf1\xf2\xfc \xe7\xe0 \xef\xf3\xeb\xe5\xec\xb8\xf2",
	 "Hold [{+activate}] to take the turret"},
	// Огонь по своим: вкл / выкл (the lobby row: can the players hurt each other)
	{"MW2COOP_FF_ON", "\xce\xe3\xee\xed\xfc \xef\xee \xf1\xe2\xee\xe8\xec: \xe2\xea\xeb",
	 "Friendly fire: on"},
	{"MW2COOP_FF_OFF", "\xce\xe3\xee\xed\xfc \xef\xee \xf1\xe2\xee\xe8\xec: \xe2\xfb\xea\xeb",
	 "Friendly fire: off"},
	// Готов
	{"MW2COOP_READY_SET", "\xc3\xee\xf2\xee\xe2",
	 "Ready"},
	// Не готов
	{"MW2COOP_READY_UNSET", "\xcd\xe5 \xe3\xee\xf2\xee\xe2",
	 "Not ready"},
	// Старт
	{"MW2COOP_START_GO", "\xd1\xf2\xe0\xf0\xf2",
	 "Start"},
	// Старт: нужен второй игрок
	{"MW2COOP_START_NEED_PARTNER", "\xd1\xf2\xe0\xf0\xf2: \xed\xf3\xe6\xe5\xed \xe2\xf2\xee\xf0\xee\xe9 \xe8\xe3\xf0\xee\xea",
	 "Start: a second player is needed"},
	// Старт: вы не готовы
	{"MW2COOP_START_WAIT_YOU", "\xd1\xf2\xe0\xf0\xf2: \xe2\xfb \xed\xe5 \xe3\xee\xf2\xee\xe2\xfb",
	 "Start: you are not ready"},
	// Старт: напарник не готов
	{"MW2COOP_START_WAIT_PARTNER", "\xd1\xf2\xe0\xf0\xf2: \xed\xe0\xef\xe0\xf0\xed\xe8\xea \xed\xe5 \xe3\xee\xf2\xee\xe2",
	 "Start: your partner is not ready"},
	// Старт: ждём готовности
	{"MW2COOP_START_WAIT_BOTH", "\xd1\xf2\xe0\xf0\xf2: \xe6\xe4\xb8\xec \xe3\xee\xf2\xee\xe2\xed\xee\xf1\xf2\xe8",
	 "Start: waiting for both players"},
	// Ждём старта от хоста
	{"MW2COOP_START_HOST", "\xc6\xe4\xb8\xec \xf1\xf2\xe0\xf0\xf2\xe0 \xee\xf2 \xf5\xee\xf1\xf2\xe0",
	 "Waiting for the host to start"},
	// ПРИСОЕДИНИТЬСЯ К ИГРЕ
	{"MW2COOP_JOIN_IDLE", "\xcf\xd0\xc8\xd1\xce\xc5\xc4\xc8\xcd\xc8\xd2\xdc\xd1\xdf \xca \xc8\xc3\xd0\xc5",
	 "JOIN A GAME"},
	// ПОИСК ИГР В СЕТИ...
	{"MW2COOP_JOIN_SEARCHING", "\xcf\xce\xc8\xd1\xca \xc8\xc3\xd0 \xc2 \xd1\xc5\xd2\xc8...",
	 "SEARCHING THE NETWORK..."},
	// ИГР В СЕТИ НЕ НАЙДЕНО
	{"MW2COOP_JOIN_NONE", "\xc8\xc3\xd0 \xc2 \xd1\xc5\xd2\xc8 \xcd\xc5 \xcd\xc0\xc9\xc4\xc5\xcd\xce",
	 "NO GAMES FOUND"},
	// ПОДКЛЮЧЕНИЕ...
	{"MW2COOP_JOIN_CONNECTING", "\xcf\xce\xc4\xca\xcb\xde\xd7\xc5\xcd\xc8\xc5...",
	 "CONNECTING..."},
	// Игры в сети
	{"MW2COOP_LOBBIES", "\xc8\xe3\xf0\xfb \xe2 \xf1\xe5\xf2\xe8",
	 "Games on the network"},
	// Доступ: свободный
	{"MW2COOP_ACCESS_OPEN", "\xc4\xee\xf1\xf2\xf3\xef: \xf1\xe2\xee\xe1\xee\xe4\xed\xfb\xe9",
	 "Access: open"},
	// Доступ: по паролю
	{"MW2COOP_ACCESS_PASSWORD", "\xc4\xee\xf1\xf2\xf3\xef: \xef\xee \xef\xe0\xf0\xee\xeb\xfe",
	 "Access: password"},
	// Доступ: только друзья Steam
	{"MW2COOP_ACCESS_STEAM", "\xc4\xee\xf1\xf2\xf3\xef: \xf2\xee\xeb\xfc\xea\xee \xe4\xf0\xf3\xe7\xfc\xff Steam",
	 "Access: Steam friends only"},
	// Пароль: не задан / задан (the lobby row under Friendly fire, in password mode)
	{"MW2COOP_PW_UNSET", "\xcf\xe0\xf0\xee\xeb\xfc: \xed\xe5 \xe7\xe0\xe4\xe0\xed",
	 "Password: not set"},
	{"MW2COOP_PW_SET", "\xcf\xe0\xf0\xee\xeb\xfc: \xe7\xe0\xe4\xe0\xed",
	 "Password: set"},
	// Пароль сессии
	{"MW2COOP_EDIT_PASSWORD", "\xcf\xe0\xf0\xee\xeb\xfc \xf1\xe5\xf1\xf1\xe8\xe8",
	 "Session password"},
	// Адрес хоста
	{"MW2COOP_EDIT_ADDRESS", "\xc0\xe4\xf0\xe5\xf1 \xf5\xee\xf1\xf2\xe0",
	 "Host address"},
	// Пароль:
	{"MW2COOP_LABEL_PASSWORD", "\xcf\xe0\xf0\xee\xeb\xfc:",
	 "Password:"},
	// Адрес:
	{"MW2COOP_LABEL_ADDRESS", "\xc0\xe4\xf0\xe5\xf1:",
	 "Address:"},
	// Ввести адрес вручную...
	{"MW2COOP_BY_ADDRESS", "\xc2\xe2\xe5\xf1\xf2\xe8 \xe0\xe4\xf0\xe5\xf1 \xe2\xf0\xf3\xf7\xed\xf3\xfe...",
	 "Enter an address..."},
	// (пароль)
	{"MW2COOP_LOCKED", "(\xef\xe0\xf0\xee\xeb\xfc)",
	 "(password)"},
	// Неверный пароль
	{"MW2COOP_BAD_PASSWORD", "\xcd\xe5\xe2\xe5\xf0\xed\xfb\xe9 \xef\xe0\xf0\xee\xeb\xfc",
	 "Wrong password"},
	// Хост пускает только друзей Steam
	{"MW2COOP_FRIENDS_ONLY", "\xd5\xee\xf1\xf2 \xef\xf3\xf1\xea\xe0\xe5\xf2 \xf2\xee\xeb\xfc\xea\xee \xe4\xf0\xf3\xe7\xe5\xe9 Steam",
	 "The host only lets Steam friends in"},
	// РАЗДЕЛЕНИЕ ЭКРАНА (the console game's own wording of this button, MENU_SPLIT_SCREEN_CAPS; suggested in issue 2)
	{"MW2COOP_SPLITSCREEN", "\xd0\xc0\xc7\xc4\xc5\xcb\xc5\xcd\xc8\xc5 \xdd\xca\xd0\xc0\xcd\xc0",
	 "SPLIT SCREEN"},
	// Второй игрок: нажмите любую кнопку на своём устройстве (the lobby's hint line)
	{"MW2COOP_SPLIT_HINT", "\xc2\xf2\xee\xf0\xee\xe9\x20\xe8\xe3\xf0\xee\xea\x3a\x20\xed\xe0\xe6\xec\xe8\xf2\xe5\x20\xeb\xfe\xe1\xf3\xfe\x20\xea\xed\xee\xef\xea\xf3\x20\xed\xe0\x20\xf1\xe2\xee\xb8\xec\x20\xf3\xf1\xf2\xf0\xee\xe9\xf1\xf2\xe2\xe5",
	 "Second player: press any button on your device"},
	// Альфа / Браво: the split screen lobby's player cards (call signs, owner 2026-10-28)
	{"MW2COOP_CALLSIGN_A", "\xc0\xeb\xfc\xf4\xe0", "Alpha"},
	{"MW2COOP_CALLSIGN_B", "\xc1\xf0\xe0\xe2\xee", "Bravo"},
	// (Back: отменить) / (Backspace: отменить): how a player takes his device choice back
	{"MW2COOP_SPLIT_CANCEL_PAD", " (Back: \xee\xf2\xec\xe5\xed\xe8\xf2\xfc)", " (Back: cancel)"},
	{"MW2COOP_SPLIT_CANCEL_KB", " (Backspace: \xee\xf2\xec\xe5\xed\xe8\xf2\xfc)", " (Backspace: cancel)"},
	// Игрок 1: / Игрок 2: (the split screen lobby's hint line: each player presses a button on his device)
	{"MW2COOP_SPLIT_P1", "\xc8\xe3\xf0\xee\xea 1: ", "Player 1: "},
	{"MW2COOP_SPLIT_P2", "\xc8\xe3\xf0\xee\xea 2: ", "Player 2: "},
	// нажмите кнопку на своём устройстве / (клавиатура или геймпад), on two lines
	{"MW2COOP_SPLIT_PRESS", "\xed\xe0\xe6\xec\xe8\xf2\xe5 \xea\xed\xee\xef\xea\xf3 \xed\xe0 \xf1\xe2\xee\xb8\xec \xf3\xf1\xf2\xf0\xee\xe9\xf1\xf2\xe2\xe5\n(\xea\xeb\xe0\xe2\xe8\xe0\xf2\xf3\xf0\xe0 \xe8\xeb\xe8 \xe3\xe5\xe9\xec\xef\xe0\xe4)",
	 "press a button on your device\n(keyboard or gamepad)"},
	// клавиатура и мышь
	{"MW2COOP_SPLIT_KEYBOARD", "\xea\xeb\xe0\xe2\xe8\xe0\xf2\xf3\xf0\xe0 \xe8 \xec\xfb\xf8\xfc", "keyboard and mouse"},
	// геймпад %d
	{"MW2COOP_SPLIT_PAD", "\xe3\xe5\xe9\xec\xef\xe0\xe4 %d", "gamepad %d"},
	// Старт: игроки выбирают устройства
	{"MW2COOP_START_NEED_DEVICES", "\xd1\xf2\xe0\xf0\xf2: \xe8\xe3\xf0\xee\xea\xe8 \xe2\xfb\xe1\xe8\xf0\xe0\xfe\xf2 \xf3\xf1\xf2\xf0\xee\xe9\xf1\xf2\xe2\xe0",
	 "Start: the players choose their devices"},
	// КАМПАНИЯ
	{"MW2COOP_CAMPAIGN", "\xca\xc0\xcc\xcf\xc0\xcd\xc8\xdf",
	 "CAMPAIGN"},
	// Брандмауэр Windows
	{"MW2COOP_FIREWALL_TITLE", "\xc1\xf0\xe0\xed\xe4\xec\xe0\xf3\xfd\xf0 Windows",
	 "Windows Firewall"},
	// Второй игрок не сможет войти. / Разрешить доступ?
	{"MW2COOP_FIREWALL_TEXT", "\xc2\xf2\xee\xf0\xee\xe9 \xe8\xe3\xf0\xee\xea \xed\xe5 \xf1\xec\xee\xe6\xe5\xf2 \xe2\xee\xe9\xf2\xe8.\n\xd0\xe0\xe7\xf0\xe5\xf8\xe8\xf2\xfc \xe4\xee\xf1\xf2\xf3\xef?",
	 "The other player cannot get in.\nAllow access?"},
	// Другой напарник
	{"MW2COOP_PARTNER_TITLE", "\xc4\xf0\xf3\xe3\xee\xe9 \xed\xe0\xef\xe0\xf0\xed\xe8\xea",
	 "Different partner"},
	// Кампания «%s» сохранена с: %s. / Продолжить с %s?
	{"MW2COOP_PARTNER_FMT", "\xca\xe0\xec\xef\xe0\xed\xe8\xff \xab%s\xbb \xf1\xee\xf5\xf0\xe0\xed\xe5\xed\xe0 \xf1: %s.\n\xcf\xf0\xee\xe4\xee\xeb\xe6\xe8\xf2\xfc \xf1 %s?",
	 "The campaign \"%s\" was saved with: %s.\nContinue with %s?"},
	// Участники прохождения
	{"MW2COOP_MEMBER_TITLE", "\xd3\xf7\xe0\xf1\xf2\xed\xe8\xea\xe8 \xef\xf0\xee\xf5\xee\xe6\xe4\xe5\xed\xe8\xff",
	 "Playthrough members"},
	// Добавить %s в участники / этого прохождения?
	{"MW2COOP_MEMBER_FMT", "\xc4\xee\xe1\xe0\xe2\xe8\xf2\xfc %s \xe2 \xf3\xf7\xe0\xf1\xf2\xed\xe8\xea\xe8\n\xfd\xf2\xee\xe3\xee \xef\xf0\xee\xf5\xee\xe6\xe4\xe5\xed\xe8\xff?",
	 "Add %s to the members\nof this playthrough?"},
	// Новая игра
	{"MW2COOP_NEWGAME_TITLE", "\xcd\xee\xe2\xe0\xff \xe8\xe3\xf0\xe0",
	 "New game"},
	// Начать новую игру с %s / с самого начала?
	{"MW2COOP_NEWGAME_FMT", "\xcd\xe0\xf7\xe0\xf2\xfc \xed\xee\xe2\xf3\xfe \xe8\xe3\xf0\xf3 \xf1 %s\n\xf1 \xf1\xe0\xec\xee\xe3\xee \xed\xe0\xf7\xe0\xeb\xe0?",
	 "Start a new game with %s\nfrom the very beginning?"},
	// ВЫБРАТЬ СОХРАНЕНИЕ
	{"MW2COOP_PICK_SAVE", "\xc2\xdb\xc1\xd0\xc0\xd2\xdc \xd1\xce\xd5\xd0\xc0\xcd\xc5\xcd\xc8\xc5",
	 "PICK A SAVE"},
	// Сохранения
	{"MW2COOP_SAVES", "\xd1\xee\xf5\xf0\xe0\xed\xe5\xed\xe8\xff",
	 "Saves"},
	// без напарника
	{"MW2COOP_SAVE_NO_PARTNER", "\xe1\xe5\xe7 \xed\xe0\xef\xe0\xf0\xed\xe8\xea\xe0",
	 "no partner"},
	// ПРОДОЛЖИТЬ: 
	{"MW2COOP_CONTINUE_PREFIX", "\xcf\xd0\xce\xc4\xce\xcb\xc6\xc8\xd2\xdc: ",
	 "CONTINUE: "},
	// НОВАЯ КАМПАНИЯ
	{"MW2COOP_NEW_CAMPAIGN", "\xcd\xce\xc2\xc0\xdf \xca\xc0\xcc\xcf\xc0\xcd\xc8\xdf",
	 "NEW CAMPAIGN"},
};
#define STRING_COUNT (int)(sizeof(strings) / sizeof(strings[0]))

// Names the menus show whose text the mod switches at run time: the lobby's ready and start
// buttons and the campaign menu's join button (localize_select: one of strings[]), the rows of
// the list of games on the network (localize_set_text: any text, Windows-1251). The UI looks the
// entry up each frame, so a switch shows at once.
typedef struct {
	const char *name;
	const char *first; // the string shown until a switch, NULL for text
	volatile LONG shown; // index into strings[], -1 for text, -2 until <first> is looked up
	char text[128];
	LocalizeEntry entry;
} Alias;

static Alias aliases[] = {
	{"MW2COOP_READY", "MW2COOP_READY_SET", -2, "", {NULL, NULL}},
	{"MW2COOP_START", "MW2COOP_START_GO", -2, "", {NULL, NULL}},
	{"MW2COOP_JOIN", "MW2COOP_JOIN_IDLE", -2, "", {NULL, NULL}},
	{"MW2COOP_ACCESS", "MW2COOP_ACCESS_OPEN", -2, "", {NULL, NULL}},
	{"MW2COOP_FF", "MW2COOP_FF_ON", -2, "", {NULL, NULL}},
	{"MW2COOP_PW", "MW2COOP_PW_UNSET", -2, "", {NULL, NULL}},
	{"MW2COOP_EDIT_TITLE", "MW2COOP_EDIT_PASSWORD", -2, "", {NULL, NULL}},
	{"MW2COOP_EDIT_LABEL", "MW2COOP_LABEL_PASSWORD", -2, "", {NULL, NULL}},
	{"MW2COOP_CONTINUE", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_LOBBY_1", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_SAVE_1", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_PARTNER_Q", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_MEMBER_Q", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_NEWGAME_Q", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_SAVE_2", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_SAVE_3", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_SAVE_4", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_LOBBY_2", NULL, -1, "", {NULL, NULL}},
	{"MW2COOP_LOBBY_3", NULL, -1, "", {NULL, NULL}},
};
#define ALIAS_COUNT (int)(sizeof(aliases) / sizeof(aliases[0]))

static LocalizeEntry entries[sizeof(strings) / sizeof(strings[0])];

int localize_is_russian(void);
static int game_is_russian(void)
{
	if (russian < 0) {
		LocalizeEntry *yes = original(ASSET_TYPE_LOCALIZE_ENTRY, "MENU_YES");
		russian = yes && yes->value && (unsigned char)yes->value[0] >= 0xc0;
		log_printf("localize: game language %s", russian ? "Russian" : "not Russian, mod strings in English");
	}
	return russian;
}

int localize_is_russian(void)
{
	return game_is_russian();
}

static const char *text_of(int i)
{
	return game_is_russian() ? strings[i].ru : strings[i].en;
}

static int string_index(const char *name)
{
	for (int i = 0; i < STRING_COUNT; i++)
		if (strcmp(name, strings[i].name) == 0)
			return i;
	return -1;
}

static void *find_header(int type, const char *name)
{
	if (type == ASSET_TYPE_LOCALIZE_ENTRY && name && strncmp(name, "MW2COOP_", 8) == 0) {
		for (int i = 0; i < ALIAS_COUNT; i++) {
			if (strcmp(name, aliases[i].name) != 0)
				continue;
			if (aliases[i].shown == -2)
				InterlockedCompareExchange(&aliases[i].shown, string_index(aliases[i].first), -2);
			aliases[i].entry.name = aliases[i].name;
			aliases[i].entry.value = aliases[i].shown < 0 ? aliases[i].text : text_of(aliases[i].shown);
			return &aliases[i].entry;
		}
		for (int i = 0; i < STRING_COUNT; i++) {
			if (strcmp(name, strings[i].name) != 0)
				continue;
			entries[i].name = strings[i].name;
			entries[i].value = text_of(i);
			return &entries[i];
		}
	}
#ifdef MW2COOP_IW4X
	if (type == 0x24) { // a script of ours: the iw4x client reads it as a rawfile (scripts.c)
		void *raw = scripts_raw_override(name);
		if (raw)
			return raw;
	}
#endif
	if (type == 4 && name && name[0] == 'm') {
		void *clone = transplant_clone(name, original);
		if (clone)
			return clone;
	}
	void *found = original(type, name);
	if (type == ASSET_TYPE_FONT)
		fonts_seen(found, name, original);
	return found;
}

void localize_select(const char *alias, const char *name)
{
	for (int a = 0; a < ALIAS_COUNT; a++) {
		if (strcmp(alias, aliases[a].name) != 0)
			continue;
		int i = string_index(name);
		if (i >= 0)
			InterlockedExchange(&aliases[a].shown, i);
	}
}

void localize_set_text(const char *alias, const char *text)
{
	for (int a = 0; a < ALIAS_COUNT; a++) {
		if (strcmp(alias, aliases[a].name) != 0)
			continue;
		InterlockedExchange(&aliases[a].shown, -1);
		lstrcpynA(aliases[a].text, text, sizeof(aliases[a].text));
	}
}

// Another text for one of the game's own strings (NULL puts the game's text back). Main thread only.
static LocalizeEntry *override_entry;
static const char *override_orig;
static char override_text[256];

void localize_override(const char *name, const char *text)
{
	LocalizeEntry *e = find_header(ASSET_TYPE_LOCALIZE_ENTRY, name);
	if (!e || !e->value || !e->name || strcmp(e->name, name) != 0)
		return;
	if (text) {
		if (e != override_entry) {
			override_entry = e;
			override_orig = e->value;
		}
		lstrcpynA(override_text, text, sizeof(override_text));
		e->value = override_text;
	} else if (override_entry == e) {
		e->value = override_orig;
		override_entry = NULL;
	}
}

const char *localize_text(const char *name)
{
	LocalizeEntry *e = find_header(ASSET_TYPE_LOCALIZE_ENTRY, name);
	return e && e->value && e->name && strcmp(e->name, name) == 0 ? e->value : NULL;
}

#ifdef _WIN64
static const uint8_t expected[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
#else
// 32-bit DB_FindXAssetHeader (0x4f6f10): push ecx; push ebx; mov ebx, [0x69116c] (an absolute address, copied as it is)
#ifdef MW2COOP_IW4X
// the 1.0.159 exe (iw4x-sp): the same, with the absolute address 0x69415c
static const uint8_t expected[] = {0x51, 0x53, 0x8b, 0x1d, 0x5c, 0x41, 0x69, 0x00};
#else
static const uint8_t expected[] = {0x51, 0x53, 0x8b, 0x1d, 0x6c, 0x11, 0x69, 0x00};
#endif
#endif

void localize_install(void)
{
	uint8_t *target = (uint8_t *)GetModuleHandleA(NULL) + RVA_DB_FIND_HEADER;
	if (memcmp(target, expected, sizeof(expected)) != 0) {
		log_printf("localize: unexpected bytes at DB_FindXAssetHeader, not hooking");
		return;
	}
	if (!hook_function(target, expected, sizeof(expected), find_header, (void **)&original))
		return;
	log_printf("localize: DB_FindXAssetHeader hooked for MW2COOP_ strings");
}
