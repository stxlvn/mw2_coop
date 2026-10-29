#pragma once

// The game's Direct3D 9 device, caught when the game creates it (src/d3dcap.c). The 32-bit build keeps the device in no global a search finds,
// so d3d9.dll's Direct3DCreate9 is hooked before the game window exists and CreateDevice of the object it returns hands over the device.
void d3dcap_install(void); // from DllMain, before the game creates its device
void *d3dcap_device(void); // the newest device the game created, or NULL
