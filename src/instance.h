#pragma once

// The second game process of a split screen session, made from the same install (instance.c).
//   mw2coop_split <pad>   console command: starts it (pad: the XInput pad index that drives it, -1: none)
// Returns 1 when the process was started.
int instance_start(int pad);
// Closes the second process (when this one quits).
void instance_stop(void);
// 1 while the second process of a split screen session runs.
int instance_running(void);
// The lobby of a split screen session: the popup that asks the second player to press a button on his device, then his A is his Ready.
void instance_assign_begin(void);
void instance_assign_end(void);
int instance_pad(void);          // the two-process split screen: player 2's pad, -1 until a pad was pressed
// The native split screen: each player's device, chosen by a press in the lobby (the first device pressed is player 1's)
#define DEVICE_NONE (-2)
#define DEVICE_KEYBOARD (-1)
int instance_device(int player); // DEVICE_NONE until he pressed, DEVICE_KEYBOARD, or an XInput pad 0-3
int instance_menu_pad(void);     // player 1's pad in the lobby (he works the menus), -1 when he is not on a pad
int instance_second_ready(void); // 1 when he pressed Ready
void instance_install(void);
