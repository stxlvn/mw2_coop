#pragma once

// QA: save this machine's next game frame as <game dir>\mw2coop\shots\<name>.bmp (src/shot.c).
void shot_install(void);
// Saves the frame about to be shown as <game dir>\mw2coop\shots\<name>.bmp; only on the render thread at EndScene (src/native.c).
void shot_save_now(void *dev, const char *name);
void shot_request(const char *name);
// A burst: a rectangle of the next n frames' back buffer, kept in video memory (no stall per frame), saved as <prefix>-NN.bmp once the last is in; shot_burst_tick on the render
// thread after each frame is composed (src/native.c).
void shot_burst_start(const char *prefix, int n, int x, int y, int w, int h);
void shot_burst_tick(void *dev);
