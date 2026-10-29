#pragma once

// The split screen lobby's gamepad picture (padicon.c): a copy of the keyboard and mouse material with a gamepad drawn into its own texture.
// Made once, on the first call that finds the keyboard's texture; NULL until then (and in the 32-bit build).
void *padicon_material(void *keyboard_material);
// The same for a keyboard and mouse picture drawn here, on a copy of any material
void *padicon_keyboard_material(void *template_material);
