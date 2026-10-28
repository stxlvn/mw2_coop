#pragma once

// The split screen players' gamepad options (padopts.c): player 0 is Alpha (the top half), player 1 Bravo.
typedef struct {
	float sens;   // look sensitivity, 1..10 (3: the tuned speed of [native] padyaw / padpitch)
	int invert;   // pushing the right stick up looks down
	int assist;   // aim assist (slowdown, lock-on, the aim-down-the-sight snap)
	int southpaw; // the sticks swapped: the left one looks, the right one moves
	int tactical; // crouch / prone on the right stick click, melee on B
	int rumble;   // vibration
} padopts_t;

// The options of a player (the defaults until the dvars are registered). Main thread.
void padopts_get(int player, padopts_t *out);
// Menu tick (main thread, in the menus): registers the dvars once, adds the two gamepad pages and their buttons under Options > Controls.
void padopts_tick(void);
