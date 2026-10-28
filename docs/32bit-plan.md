# 32-bit support: plan and state

The mod was made for the 64-bit build of Modern Warfare 2 (2009) that Steam moved the game to in September 2026.
The build before that (32-bit, `iw4sp.exe` linked 2010-02-10) is what most existing mods and many players still use.
Same design (a `winmm.dll` proxy that patches script text in memory and hooks the engine), other addresses and other
structure sizes. The 64-bit build must not break: `sh build.sh` builds it as before, `sh build.sh x86` the 32-bit one.

## Getting a 32-bit game to work on (no download from outside Steam)

Steam keeps the old depots: `download_depot 10180 <depot> <manifest>` in the Steam console puts them in
`steamapps\content\app_10180\depot_<id>` without touching the installed game (a community guide lists the manifests
for depots 10182, 10181 and the language depot, 10189 for Russian). Merge the three folders into a new folder
(never over the installed game), then copy that folder to a test copy.

## What was learnt on the first run (2026-10-25)

- The exe imports `WINMM.dll` (the same proxy slot as the 64-bit game), `WSOCK32.dll`, `steam_api.dll` (Valve's own,
  115,960 B), `mss32.dll`, `binkw32.dll`, `d3d9.dll`. The real `winmm.dll` is `SysWOW64\winmm.dll` (192 exports,
  `tools/gen_winmm_proxy.py` writes the 32-bit stubs into `src/x86/`).
- **The exe is rewritten by Steam's wrapper at its first start**, per account (3,497,984 -> 3,503,672 bytes, new hash),
  and its code is encrypted on disk until the wrapper unpacks it. So the file's hash is not the build's identity, and
  a static analysis of the file is useless. The build is known by the SHA-256 of the executable sections **in memory**
  once the game window exists (`src/x86/main32.c`): `FC9E4843...52D7`, 2,685,679 bytes, image base 0x400000, the same on
  every launch tried.
- A test copy needs `steam_appid.txt` (`10180`) next to the exe, besides what `tools/run_test.ps1` sets (environment,
  the Steam registry pid): without it the wrapper asks Steam to relaunch the game and Steam starts the real install
  (the runner's guard closes it).
- The addresses in iw4x-sp's `symbols.hpp` do **not** fit this build (they point into the middle of functions): it is
  a different version of the exe. They are a guide to what the functions do, not a table.
- To read the code: put a file `mw2coop\dump_image` into the game folder before the start; the proxy writes
  `mw2coop\image32.bin` (the image from memory, 34,930,688 bytes) and deletes the flag. Load it in Ghidra as a raw
  binary (language `x86:LE:32:default`, base address 0x400000).

## Addresses found (official build, code SHA-256 `FC9E4843...52D7`, image base 0x400000)

Found by Ghidra on the memory dump (`tools/ghidra/run.sh`, raw binary `image32.bin`), each read from the code, not
taken from another project. All are cdecl unless said otherwise. Bytes are from the dump.

| Function | VA | First bytes | How it was found |
|---|---|---|---|
| `Scr_AddSourceBuffer(filename, extFilename, codePos, archive)` | 0x47F8E0 | `8b 44 24 10 8b 4c 24 04 53 8b 5c 24 10 56 8b 74 24 10 50 51 e8` | the script loader (0x410920, `"%s.gsc"`) calls it with 4 pushes and cleans 0x10; it only forwards to 0x6100d0 with ESI/EBX as register arguments, so this thin cdecl wrapper is the hook point (2 callers: 0x410920, 0x607660) |
| the usercall body behind it | 0x6100d0 | `51 a1 b8 2b 95 01 8b 48 10 80 39 00` | not hooked |
| rawfile read (the zone path) | 0x610020 | `53 56 8b 74 24 10 56 6a 24 e8` | `DB_FindXAssetHeader(0x24, name)`, `DB_IsXAssetDefault(0x24, name)`, length, `Hunk_AllocateTempMemoryHigh`, copy |
| `DB_FindXAssetHeader(type, name)` | 0x4F6F10 | `51 53 8b 1d 6c 11 69 00` | called from 0x610020 |
| `DB_IsXAssetDefault(type, name)` | 0x4C32B0 | `53 8b 5c 24 0c 56 57 8b 7c 24 10` | same |
| `DB_GetRawFileLen(RawFile *)` | 0x4E3080 | `8b 44 24 04 8b 40 08 83 c0 01 c3` | returns `[rawfile+8] + 1` |
| `DB_GetRawBuffer(RawFile *, buf, len)` | 0x4E55F0 | `83 ec 3c 53 55 56 57 68 00 00 02 00` | same |
| `Hunk_AllocateTempMemoryHigh(size)` | 0x40EED0 | `a1 8c 75 95 01 8b 54 24 04` | same |

In 32 bits the hook is a 5-byte relative jump at 0x47F8E0 (the first two instructions are 8 bytes, so a trampoline
copies them cleanly); `size_t` and pointers are 4 bytes, so `scripts.c` needs the build profile of step 3 before it can
be shared.

## Done in the 32-bit DLL (2026-10-25, test copy only)

- `scripts.c` builds for both bit widths (`#ifdef _WIN64`): the 32-bit profile holds the VAs above as RVAs and the
  wrapper's expected bytes; `hook.c` writes a 5-byte relative jump in 32 bits (trampolines are built in place).
- `src/x86/control32.c`: `mw2coop\commands.txt` (plain lines to `Cbuf_AddText`, found at 0x409b40, cdecl, locks
  critical section 0x21; `!sleep`).
- Checked in the official game (`devmap trainer` through the command file, patch file `patches/maps/trainer.gsc.patch`
  with one appended function): the log says `scripts: maps/trainer.gsc <- zone + 1/1 patches`, the map loaded. The
  command line `+devmap` is ignored by this build (main menu only).
- The mod's patches from the 64-bit scripts all apply to the 32-bit zone scripts (trainer 26/26, `_utility` 7/7, `_coop` 6/6,
  `_introscreen` 19/19 ...) and the FNV hashes of the patched `_utility`, `_coop` and `_mw2coop` are the same as in the
  64-bit build: the scripts are identical, which both builds need for one lockstep session.
- More addresses (official build): `Dvar_FindVar` 0x633480 (usercall, name in EDI), 32-bit `dvar_t` (type +0x0c, value +0x10,
  next +0x4c), `Cmd_AddCommand(name, fn, node, 0)` 0x428090, `listen` 0x57a860 (host + `gameSession` lobby object at
  [0x934450]), `coopLaunch` 0x57aa20 (sets 0x93444b; the frame function 0x57c800 launches only with two lobby members:
  bytes +0x68 and +0xa0 of the lobby), map launch `void (int)` 0x47bd80, `Party_Reset` 0x42f0f0.
- `control32.c` also has `!watch`, `!dvars`, `!peek`, `!pokeptr`, `!key` and the console command `mw2coop_launch_map`
  (launches `coop_mapName` without waiting for a second member). One copy then starts the map as a co-op host (map
  `trainer` runs, briefing video plays); the engine's `coop` dvar stays 0 there, so the mod's scripts stay inactive: a real
  session needs a second copy joining (party/net port).
- Not yet: the mod's real patches on 32-bit scripts (they were generated from the 64-bit scripts; `check_patches.py`
  against the 32-bit zone text will say which still apply), natives the patched scripts call, party/menus.

## Two 32-bit copies in one co-op session (2026-10-25)

`D:\coop-test\mw2-32` (host) and `mw2-32-b` (client) run a real session: the host `listen`s and `coopLaunch`es, the client runs
`mw2coop_join <own LAN ip>` (control32.c: the game's own `PartyClient_Join` at 0x42fc40 with a host-info block, then
`connect`). `!xuid_add 1` on the client (both copies share one Steam account; the host rejects its own XUID).
`MW2COOP_BITS=32 sh deploy.sh` and `MW2COOP_BITS=32 sh tools/qa_pair.sh <scenario> <start> <seconds> <map>` do what they do in
64 bits. `xrequirelivesignin` is not needed (and made the join flaky). `zones.c` is ported (level zone loader 0x4d0450, hooked
with the same table): the Spec Ops zone brings the `coop_body_*` models, so player 2 has a body and a name in the 32-bit
S.S.D.D. (owner saw both). Not ported yet: model transplant (No Russian, ice picks), the pool extension (Just Like Old
Times), `mw2coop_bind` (QA button presses), the menus/lobby UI, Steam invites, the relay, progress/saves, mirror.

Scenario `bay` on trainer: PASS 1-5 on both copies, three runs in a row.

## Porting state of the 64-bit modules (one source tree for both builds)

Ported (built into `build/x86/winmm.dll`, `src/x86/stubs32.c` holds stubs for the rest): `game` (profile in `game.h`, the
usercall `Dvar_FindVar` in `game.c`), `hook`, `scripts`, `zones` (no pool extension), `control` (the QA command file, dvar
channel, script requests, `mw2coop_bind` as `+button <key>` console commands, `setdvar` wrapper), `party` (join, launch
map), `settings`, `upnp`, `crash`, `fov`. `shot` is stubbed: the 32-bit exe has no call of a D3D9 device method that a byte search finds (no `call [reg+0xA8]`), a throwaway NULLREF device gets the Steam overlay's copy of the vtable, and every d3d9 object in the exe's data passes a "119 methods" test (vtables lie next to each other), so the game's device is not located yet (idea: hook `Direct3DCreate9` before the game creates its device, or read the pointer from a known frame function). Not yet: `net`, `steamnet`, `firewall`, `access`, `progress`, `mirror`, `menus`, `localize`,
`scrdebug`, `memdump`, `transplant`, `shot`. A 32-bit `cmd_function_t` node is 0x18 bytes (`flag` last);
a 20-byte node let `Cmd_AddCommand` overwrite a neighbouring static (it zeroed the settings port).
The `net_port` default is patched in DllMain in 32 bits too (checked 2026-10-28: settings.ini port 28990 gives "net_port default 28990").

**To port (owner, 2026-10-28): everything done for the 64-bit native split screen since then**, because the 32-bit build's addresses, structure
layouts and resources differ. Each item names its 64-bit rvas in `src/native.c`, `src/menus.c`, `src/instance.c`, `src/padicon.c`:
the native split screen itself (mode 10: contexts, compositor, 2D layers, hooks of `native.c`; the 32-bit build has the two-process split only);
the renderer worker wait before a context swap (rva 0x300a0, command types 0-18); the title lines over both halves (HUD element draw 0xa3b80, the
`_introscreen` patch's sort marker, placements 0x5e0b80); subtitles / notices per half (game message window draw 0xcb4f0, windows 0x52f348);
partner names (0x93ac0 loop, 0xd3110 name); the compass mask in `layer_fix`; pads for both players (action table 0x3be770, 0xd1810 / 0xce450,
key event 0xd0f80 at 0x1fbc1d, key catchers 0x533cb0, view accumulators 0x533d14); the lobby: device choice by press and cancel, coopPlayerInfo
(0x2442b0, the Alpha / Bravo cards), the cards' pictures (item background +0xa8, material `pc_keyboard_mouse` and its texture table, `padicon.c`),
player 1's pad in the lobby (mw2coop_menukey), the split lobby's title and hidden online rows; frame bursts (`shot.c`, no D3D device found in 32
bits yet). Check each resource name too (materials, menus, localized strings): the 32-bit zones may differ.

## Steps

1. Done: the 32-bit proxy loads into the real game, logs, recognises the build, dumps the image.
2. Find the addresses by function, in this order, each checked by the bytes at the address (as the 64-bit hooks do):
   `Scr_AddSourceBuffer` (script patches: with it the campaign scripts of the mod already run, in 32-bit);
   `DB_FindXAssetHeader` and `DB_IsXAssetDefault`; the level zone loader and `DB_LoadXAssets`; the command and dvar
   functions (`Cmd_AddCommand`, `Cbuf_AddText`, `Dvar_FindVar`); `Scr_Error` and `Scr_EndLoadScripts` (script error log);
   then the party and menu structures (the lobby), the heaviest part (`menus.c`, `party.c`, `access.c`, `progress.c`).
3. A per-build profile in the DLL: the addresses and the sizes of the structures come from a table chosen by the
   build, instead of constants in `game.h`; the 64-bit build is one profile.
4. `hook.c` for x86: a 5-byte relative jump and trampolines (the 64-bit helper writes a 14-byte absolute jump).
5. The Steam parts (invite, "friends only", the relay) work with the official copy as in 64-bit (the game's own
   `steam_api.dll`); with a repack that carries a Steam emulator they switch themselves off (join by address).
6. QA on two 32-bit copies, the same scenarios as the 64-bit ones.

## Not to use

A copy of the game from an unknown source (a repack with a Steam emulator was looked at statically only: its
`steam_api.dll` is 1.29 MB, its exe is not Valve's). Not run, not reused.

## State after the font and hang work

Ported and checked in the 32-bit build: script hook, QA channel, lobby/menus, access, net, firewall, transplant (No Russian crew),
pool extension (fixed the airport client hang), fonts, script error logging (scrdebug32).

Deliberately 64-bit only (no player-facing effect):
- `steamnet` (Steam P2P relay): experimental, off by default, never tried with two real accounts even on 64 bits. A stub on 32 bits.
- `shot` (screenshots from the game's D3D device): a QA aid; the 32-bit device was not found, stubbed.
- `scrdebug` call-stack mapping: a debugging aid; 32-bit logs the error message only.

Open: an intermittent hang of both copies in DB_FindXAssetHeader (see `crash.c` dumpstacks; `tools/qa_pair.sh` now watches
for it from 20 s after the start and keeps stacks in `hang-reports/`).

## Cross play 64 <-> 32 (checked 2026-10-25)

`MW2COOP_BITS_HOST=64 MW2COOP_BITS_CLIENT=32 sh tools/qa_pair.sh bay "" 35 trainer` (and the other way round): the join, the
level load, both players in the level, `bay` PASS 5/5 in both directions, one run each, trainer only. The patched `_utility`,
`_coop` and `_mw2coop` hashes are equal in both builds. Not checked: other maps, set pieces, long sessions.
