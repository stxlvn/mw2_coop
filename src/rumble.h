#pragma once

// Vibration for the split screen players' pads (rumble.c). Main thread.
void rumble_install(void);
// Each frame of a player with a pad: his motors from the rumbles playing on him or near him (pad: his XInput pad, on: his vibration option)
void rumble_frame(int player, int pad, int on);
// Motors off and every rumble forgotten (a session ends)
void rumble_reset(void);
// QA: log each rumble a script plays ([native] rumblelog=1)
void rumble_set_log(int on);
