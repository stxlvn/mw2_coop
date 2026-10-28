#pragma once

// Pictures of a pad's buttons in the game's texts (padglyphs.c): a hint's "[{+usereload}]" shows the button of a player on a pad.
// pad_player: the player whose context is live when a text names a key, if he plays on a pad, else -1 (native.c); tactical: his layout
void padglyphs_install(int (*pad_player)(int *tactical));
