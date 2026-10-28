#pragma once

// Native split screen (src/native.c): one game process, two local players, the picture cut top and bottom. 64-bit build only.
//   settings.ini [native] players=2 experiment=10 ...   QA: the session starts with the game
//   native_split_start() / native_split_stop()          the menu's split screen lobby: the session starts with its Start and ends when the game is back in the menus
void native_install(void);
// 1 when this build has the native split screen
int native_split_available(void);
// 1 while a session runs
int native_split_active(void);
// The Start of a split screen lobby: the game becomes a two local players game; the caller launches the mission. pad: the XInput pad of the second player, -1 none. Returns 1 on success.
int native_split_start(int pad);
int native_split_start2(int dev0, int dev1); // each player's device: an XInput pad 0-3, or -1 the keyboard and mouse
// The session ends (the game is back in the menus): the game's own player count comes back
void native_split_stop(void);
// For the crash log: which client context was live, the session state
const char *native_crash_state(void);
