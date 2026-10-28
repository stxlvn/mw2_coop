# Native split screen (one process, two local players): plan

Decision of the owner (2026-10-26): the two-process split screen (`src/splitscreen.c`, `src/instance.c`) is dropped as the way forward; a split
screen inside the game itself is to be built. The two-process code stays in the repo as a fallback until the native one works; it is not polished
any more.

## What the Xbox 360 game does (reference: https://github.com/PaulCombal/mw2-recompiled, docs/gameplay.md and docs/rendering.md)

- One process, one `cg`/`cl` state per local client, up to 4 local players; two players: the screen is cut top and bottom.
- The campaign's scripts (`_load.gsc`) write `cg_fovScale` 0.75 in split screen and 1 otherwise; `cg_fov` is the angle across a 4:3 picture (65), the view is as tall
  as the 4:3 one and as wide as the picture's shape says.
- The 360 code is not available to us: the reference project builds from the player's own disc and carries no engine source.

## What the PC game (`iw4sp.exe`, 64-bit first) has left of it (found so far)

- The script builtin `IsSplitScreen()` is a stub that returns false (rva 0x194720). The local-client machinery is not there.
- UI/HUD leftovers by name: `con_gameMsgWindow%dSplitscreenScale`, `waypointSplitscreenScale`, `adsOverlay{Width,Height}Splitscreen`,
  `coopNameFontSizeSplitscreen`, `splitscreenplayercount`, `splitscreencontrols`, `allsplitscreenprofiles...` (scripts and menus use them).
- **The renderer can draw a sub-window of the picture:** the dvar `r_subwindow` (a vec4 "subwindow to draw: left, right, top, bottom", registered at rva
  0x21b8a) is read by `FUN_140007e40` (rva 0x7e40), which fills the view parameters (viewport rect at +0x130..+0x13c) and corrects the projection matrix
  (scale and offset) for that rectangle. This is the part a split view needs most. Called from rva 0x8150 and rva 0x8210.
- The video setup (`FUN_140010550`, rva 0x10550) stores the picture's aspect (floats at rva 0x4113738 / 0x411373c). A split view has to give each view the
  aspect of its own rectangle (this is the "everything looks stretched" bug of the old two-process version, solved there by writing the real aspect).

## The design to try: two local clients as two contexts in one process

The server already holds both players (the co-op lobby makes two players in one level). What is missing is a second *client*: its own connection, its own
`cg`/`cgs`/`cl`/`clc` state, its own view and HUD. The idea is to run the client code twice per frame with the client-side globals swapped:

1. **Connection.** The process hosts the level (as now) and client context 0 connects as it does today. Context 1 connects to the same server through a second
   UDP socket of ours (`src/net.c` already hooks `sendto`/`recvfrom`), so the server sees a second address and a second client with its own netchan. The
   second client has its own player id (the `!xuid_add` trick of the QA tools is the existing way to a second XUID).
2. **Context switch.** A table of the client-side globals (memory ranges: `cg`, `cgs`, `cl`, `clc`, the snapshot buffers, the view/refdef, the HUD
   state, the per-client dvars if any) is saved and restored when the frame goes from context 0 to context 1 and back. Everything shared (the server, the
   renderer's device, assets, sound) stays single.
3. **Two views in one frame.** In one `SCR_UpdateScreen`-like pass: context 0 draws its 3D view and HUD into the top rectangle, context 1 into the bottom one
   (`r_subwindow` for the projection, the scissor and the 2D placement for the HUD), one `Present`. Post-processing that uses full-screen targets has to run
   per view (or the targets be sized per view).
4. **Input.** One keyboard/mouse (the focused window) and XInput pads (`src/splitscreen.c` has the pad reading and the "press any button" assignment); each
   context gets its own `usercmd` stream.
5. **Game logic.** The scripts of the mod (`_mw2coop*.gsc`, `mirror.c`, `net.c`, `party.c`) assume two machines (host and client: "this machine's player", the partner's
   packets, the mirror of effects). With both clients in one process several of these must see "both players are mine".
6. **Sound** has one listener: player 1's, or a mix; **saves, subtitles, cinematics, loading screens** have to be settled per case.

## Phases, each ends with something that can be checked, and a go/no-go

| Phase | Work | Check |
|---|---|---|
| 0. Map the client side | Ghidra: where `cg`, `cgs`, `cl`, `clc`, `cls`, the snapshot ring, the refdef and the HUD state live; the frame's call order (`CL_Frame`, `SCR_UpdateScreen`, the cgame frame, the renderer's begin/scene/end); how many places read each global; which of them are per client in the engine's own code (arrays indexed by a local client number) and which are plain statics | a table of addresses, sizes, owners; a call graph of one frame |
| 1. A second client over a second socket | no rendering: a second connection of the same process, logged (snapshot arrival, `cg.time`, own player's origin) | both clients in the server's list with different addresses; each receives snapshots |
| 2. Context switch | save/restore of the table; the second client's frame functions run in its own context | the two contexts log different, stable positions with no cross talk for a minute; the first one plays as before |
| 3. Two views | `r_subwindow` per context, scissor, HUD placement, one present | a frame screenshot of both players' first-person views, top and bottom, right proportions (the aspect fix as in the old version) |
| 4. Input and menus | two devices, "press a button" lobby step, no Ready button, the mode in the menu | a QA bot drives both players through a level start |
| 5. Scripts, sound, saves | review of the mod's machine-bound parts | the QA scenarios of the mod pass with both players local |
| 6. Release | docs, changelog, 32-bit build | per the playbook's QA levels |

Go/no-go: after phase 1 (is a second client in the process possible at all), after phase 2 (is a clean context switch possible), after phase 3 (does the
renderer take two views in one frame). If one fails the plan is redone or dropped; the two-process version is the fallback.

## Unknowns that decide the cost

- How many client-side globals there are and whether they are cleanly separable (statics scattered through the code are the main risk).
- Whether the renderer keeps per-frame state that breaks when it is run twice per frame (the shadow maps, the full-screen post-processing targets, the
  per-frame arenas).
- Whether the engine's client code keeps pointers into its own globals (a swap by copying contents is only safe if nothing points into the swapped ranges).
- The 32-bit build needs its own addresses (the address finding is done with Ghidra on a memory dump).

## Rules

The owner's installed game and saves are never touched; test copies live in `D:\coop-test`; nothing is pushed or published without the owner's word.

## Phase 0 results so far (64-bit official build, Ghidra; 2026-10-26)

How the engine keeps the client's state:

- **The code still passes a local client number everywhere** (`CG_CalcFov`, rva 0xbc1e0, takes it; the frame calls the cgame with a literal 0), **but the arrays
  behind it were folded to one element**: the client globals are read at constant addresses (the cgame's `cg` is at rva 0x422440, e.g. `cg+0x14` flags, `cg+0xac`).
  So nothing can be had by "growing an array": the swap of whole memory ranges in the plan above is the way.
- **There is a 2-player table:** `CG_CalcFov` picks `cg_playerFovScale0` or `cg_playerFovScale1` (rva 0x51e1c0 / 0x51e1c8) by looking the local client up in
  a table at rva 0x540bb8 (stride 0x2650 bytes, a type field of 2 and a client id): the Special Ops two-player mode keeps a slot per player.
- **The frame, top down:** the main loop (rva 0x2913a0) -> rva 0x1fbd40 -> rva 0x1fbde0 (the frame's housekeeping, writes config.cfg on demand) -> rva 0xd5b00
  (the client frame, rate limited: `cls` state at rva 0x533cb8, 0 to 7, 6 is active) -> rva 0xd5890 (one client frame: update, the state switch, **rva 0xbca00 =
  the cgame's draw-active-frame for local client 0**, then the 2D and the screen update) -> rva 0xbe710 (the view values) -> rva 0xbc1e0 (the field of view).
  The renderer's view setup with `r_subwindow` (rva 0x7e40) is reached from rva 0x8150 and 0x8210.
- **How much one client frame touches:** the cgame draw and everything it calls (about 4000 functions) read or write about 5800 distinct writable globals, in
  roughly 190 clusters over the whole image. Most of it is the engine at large (renderer, dvars, sound), not per-client state. The client-state candidates are
  the clusters at rva 0x404450-0x44f000 (the cgame's `cgs` at rva 0x40c1a8 and `cg` at rva 0x422440 and their neighbours, about 260 KB), 0x51dca8-0x51e364 (the
  cgame's dvar pointers: shared, not state), 0x533cb0 (`cls` state), 0x540bb8 (the player slots), 0x547e84-0x548d07 and others to be sorted out.
- Tools made for this: `tools/ghidra/CallChain.java` (callers level by level), `tools/ghidra/GlobalsOf.java` (the writable globals a function and its callees touch,
  in address clusters); both run through `GHIDRA_SCRIPT=<name> sh tools/ghidra/run.sh <rva> ...`.

What is still unknown and decides phases 1 and 2: the exact extent of the client state (which of the clusters are per client and which are shared), whether pointers
into those ranges are kept elsewhere (the swap by copying contents is only safe if not), and how the connection state (`cls`, `clc`, the netchan) is laid out for a
second connection.

## Experiment 1 (2026-10-26): can the renderer take two scenes in one frame? No, not as the engine is used now

`src/native.c` (settings.ini `[native] experiment=1`, 64-bit): hooks the cgame's draw (rva 0xbca00) and runs it twice per frame, the second time with the bottom half
as `r_subwindow`; the begin-frame (rva 0xc840) is skipped for the second run. Results:

- Calling only the scene submit (rva 0xca4b0, which `rva 0x941b0` reaches at the end of the cgame draw) twice, and calling the whole cgame draw twice with and without the
  begin-frame skipped: all four variants run for about 30 s (the black intro) and then crash on the worker thread in the renderer's back end, always in
  the draw loop at rva 0x63f50 / 0x64e80 (`IDirect3DDevice9::DrawIndexedPrimitive` fed from a surface stream whose entries are garbage; `rdi` = 1 or 2, the view index).
  So the stream the front end makes for a second view in the same frame is not valid for the back end, or is made by something this build does not run for more than
  one view.
- What the renderer is: the front end is double buffered (`rva 0xc840` toggles two frame data sets of 0x13d700 bytes at rva 0x458fb00, resets the command buffers and the
  view count at +0x419e8); a scene is queued by `rva 0x8210` (view info of 0x150 bytes per view, arena allocation of 0x4708 bytes) and drawn on worker threads.

Conclusion: **two scenes per frame needs the renderer's multi-view data understood far better than one session of reading gives, and it is the riskiest part.**

## The design that avoids it: alternate frames, one scene per frame

Draw the player 1's frame on even frames and the player 2's on odd frames (the Oblivion split screen mod does the same, the "alternate" mode), so the renderer only ever
sees one scene per frame, exactly as it does now:

1. each frame, the context of player `frame & 1` is swapped in (the client-side globals of the plan above) and the normal client frame runs: network receive, usercmd, snapshot, cgame, scene;
2. `r_subwindow` is set to the top half for context 0 and the bottom half for context 1 (the renderer already corrects the projection for it); the 2D (HUD, menus) goes through the viewport/scissor of that half;
3. a Direct3D 9 `Present` hook (the 64-bit `shot.c` finds the device) copies the half just drawn into a texture of its own and, before presenting, draws both textures into the back buffer: top and bottom, always complete;
4. each view runs at half the frame rate (60 fps game: 30 per view); the server's own rate (sv_fps 20) and the client's packet rate (cl_maxpackets 30) are not above that.

What stays unknown (the plan above, phases 1 and 2): the second connection and the extent of the client state to swap. Phase 3 becomes "the Present compositor and the half-frame 2D", which is
much smaller than a multi-view renderer.

## Experiments 2 and 3, and more phase 0 facts (2026-10-26, later)

- **Experiment 2 works (alternate frames + compositor).** `[native] experiment=2`: even frames draw the top half, odd frames the bottom half (`r_subwindow`), and a hook on
  `IDirect3DSwapChain9::Present` (the game presents through the swap chain, once per frame, one view queued per frame; the device's own Present and PresentEx are not used)
  copies the half just drawn into a texture and fills the other half from the other texture. The owner watched it: the two halves are right and there is no flicker.
  What it does not do yet: the two halves are the same player; the 2D (HUD, text) is drawn over the whole picture, so each half shows only its part of it.
- **Experiment 3 fails at once:** `experiment=3` runs the cgame draw for LOCAL CLIENT 1 on the odd frames: the process dies within 0.2 s of the first composed frame (no CRASH line:
  not caught by the exception logger). Local client 1 is not usable by changing the argument alone.
- **The 2-slot table at rva 0x540ba0 (stride 0x2650) is the coop session's connection table, used by both roles:** the server side handler of the out-of-band "coopConnect" message
  (rva 0x22d460) sets up `Netchan_Setup(1 = server socket, slot netchan, address, ...)` for slot `iVar2` taken from the message, marks it with `FUN_1401fe660(slot, 2)` and answers "coopAcceptAck";
  the client's connect handler (rva 0xd23c0) sets up the same table's slot 0 with socket 0. The server-side frame loop (rva 0x22f400) walks both slots and sends to every slot in use.
  So the engine's coop code already thinks in two connection slots; what is needed for a local second player is a slot whose netchan is a local one (the loopback).
- **Some per-client arrays are real 4-entry arrays** (for example the array with stride 0xe80 at rva 0x3cf180, 16 KB = 4 entries, indexed by the local client number in `rva 0x6a570`), while the main
  cgame state (`cg` at rva 0x422440) is addressed at a constant. A mix: some modules keep the console's MAX_LOCAL_CLIENTS = 4 arrays, the main client state is a single static.
- The workflow of six research agents (`docs/native-splitscreen-research/`) stopped on the usage limit before it wrote anything (1.47 million tokens spent, no report): the questions it was given
  (second connection, client state extent, 2D per half, input, server side and mod assumptions, time slicing) are still open and listed above.

## Runtime look at the coop slot table (host, trainer, two copies, 2026-10-26)

`!rva 540ba0 192` and `!rva 5431f0 192` on the host (the two slots), taken 45 s after `coopLaunch` with the partner in:

- **Slot 0 (rva 0x540ba0): the host's own client over the LOOPBACK.** At +0x10 the address: type 2 (NA_LOOPBACK), ip 127.0.0.1; the netchan carries two 0x1000 byte fragment buffers (pointers at +0x30 and +0x48, sizes 0x1000).
- **Slot 1 (rva 0x5431f0): the remote partner by IP.** Address type 4 (NA_IP), the PC's LAN address (192.168.100.4 here: the QA copies join the PC's own address) and its port; its own buffers; a field with value 2 at +0x20.
- `rva 0x52fb5c` (the coop session flag) = 1, `cls` state (rva 0x533cb8) = 6 (active).

So the engine already has what a second LOCAL client needs on the SERVER side: the server talks to a slot over a netchan whose address can be the loopback. What is missing for a second local client is the CLIENT side of that second loopback connection:
the client code (cgame, snapshot parse, usercmd send) is written for one connection (the client connect handler uses slot 0 at a constant) and its state is single statics.
The loopback queue itself (the engine's `NET_` loopback, one client/server pair) would need a second pair or a tag to tell the two clients' packets apart: not examined yet.

## The loopback (phase 1 lead, 2026-10-26)

Found by reading the engine's network layer (64-bit):

- **`NET_SendPacket(sock, length, data, to)` is rva 0x202db0.** For a `to` of type 2 (NA_LOOPBACK) nothing touches a socket: with `sock < 1` (a client sending) the packet goes into **one client-to-server ring** of 64 entries of 0x2008 bytes
  (rva 0x1391690, write counter rva 0x1411894, tagged with the sender's `sock` at entry+0x2004); with `sock >= 1` (the server sending) it goes into **a ring per destination client, selected by the 16-bit field of the
  address (its port)**, 12 entries of 0x2008 bytes each, queue stride 0x18068 (rings from rva 0x14118a0, counters rva 0x1429900/0x1429904 + index * 0x18068).
- **`NET_GetLoopPacket(sock, &from, &msg)` is rva 0x202a60:** `sock == 1` is the server's read (the client-to-server ring; the `from` it gives has the sender's tag as its port, so the server tells loopback clients apart by that port);
  any other `sock` is a client's read of ITS ring `sock * 0x18068`. In this build the client's socket is 0 and the server's is 1, so a second local client could not read its ring with `sock = 1`: the engine was built with one client, but
  its loopback is already shaped for several clients addressed by port. Replacing these two functions (hooks) with a loopback of our own that keys the clients by an explicit context number is small.
- **How the host connects itself over it (the `listen` handler, rva 0xd25e0):** builds the address {type 2, ip 127.0.0.1, port 0}, clears both table slots, marks slot 0 as type 2, calls `Netchan_Setup(0, slot-0 netchan, address, qport 0, buffers 0x1000 x 2)` (rva 0x203190),
  sets the coop flag rva 0x52fb5c, adds the local party member (rva 0xd7220, local client 0, local flag 1) and sets the client state (rva 0x24ee50, 0xb). The partner's connection is made by the server's "coopConnect" handler (rva 0x22d460) on slot 1.
  A second local client would be: slot 1 marked type 2, its netchan set up for the address {type 2, port 1}, its party member added with local client 1, and its client side state brought up in its own context.
- The `listen` handler also runs the console command `nosplitscreen` first; strings `splitscreencontrols`, `nosplitscreen`, `splitscreen` appear in the connect code. Those commands are the console's local split-screen switches: where they are handled is still to be found
  (the PC has the names, the code may be a stub like `IsSplitScreen()`).

So the engine's server side and its loopback are already built for several local clients; the client side is one static copy. The next steps are the hooks on the two loopback functions (queues keyed by context number), a second `Netchan_Setup` and member add for context 1,
and a look at what the client's connect state machine (`cls` state, rva 0x24ee50 and the challenge/connect sequence) needs in its own context.

## Client-state extent (2026-10-26)

`tools/ghidra/MemsetScan.java` (LEA of a global followed by `MOV R8D,size` and a CALL, i.e. "memset this global") finds **no** such clears of the big client
statics in rva 0x3f0000-0x560000 (1211 LEAs into the range, none of that shape): the engine clears them another way (inline stores / `rep stos`, or never). So the
struct sizes cannot be read from clear calls. Working assumption instead: the whole cluster rva 0x404450-0x44f000 (about 300 KB, `cgs` + `cg` + neighbours) is
swapped as one block per frame (a 300 KB copy costs microseconds), plus the small clusters listed above; anything shared in that block must be identical in both
contexts, which the first experiment with a second context will show (a difference after a swap round trip = shared state that must be excluded).

## Correction (2026-10-26, same day): the partner IS a second real player, the earlier "puppet" finding was wrong

The section that stood here claimed the SP server has one real client and the partner is a script puppet. That is wrong. The mod's own notes (`games/modern-warfare-2/NOTES.md`):
the engine's Spec Ops co-op is **lockstep**: both PCs run the whole simulation with **two real player entities** (`level.players.size == 2`, `level.player2` defined on both, usercmds
of both slots relayed per frame, `coop_maxFramesClientCanBeBehind`, `coop_fastForwardThreshold`). The stock scripts take `is_coop()` as `IsSplitScreen() || coop == 1`: split screen and
co-op are the same code path in the level scripts. So the server side of a second local player exists; it was always the CLIENT side (one static `cg`/`cl`/`clc`) that is single.
That is also why experiment 3 died: the cgame was run for local client 1 against client statics that only exist once.

Reference for the original design: PaulCombal/mw2-recompiled (Xbox 360 recompilation, native 1-4 player split screen). Its `runtime/title.h` shows the 360 build keeps the client state
as per-local-client arrays (client connection state stride 3216, `clientActive_t` array stride 27904, `clientConnection_t` array stride 0x51568, the cgame state array stride 0xFDE00) and a
`Session_LocalClientNum`; the campaign's co-op frame (host-relayed, per slot: "select client, that client's usercmds, that client's reliable command") is the same frame the PC binary
has. The PC exe folded the arrays to single statics and stubbed `IsSplitScreen()`. The plan above (second client context, swap of the client statics, alternate frames) stands.

## Packet flow, mapped by three Ghidra readers (2026-10-26; details and rvas in `docs/native-splitscreen-research/coop-*.md`)

- **Server and client share one process and hand data over by direct calls, not over a netchan.** `Netchan_Transmit` (0x203200) has only two callers: the client's usercmd packet (0xcd340, always on the
  constant slot-0 netchan 0x540ba0) and the host's coop frame send (0x22f400, skips local slots). There is no snapshot over the wire: `0x234540` copies the game client's playerstate into
  `cl[lc].ps` for every LOCAL slot (type 2), plus shared entity lists. A second local client can get its own snapshot; no second socket is needed.
- **The slot table is symmetric: type 2 = local player, the slot's port = the local client number, type 4 = remote.** The loops in the coop code and in the game module already iterate both slots that way
  (0x15ce10, 0x22d040, 0x22e330, 0x22f400, 0x2302d0, ...). The engine is written for two local players; what is missing is only storage: `cl` (stride 0xce9c, from 0x535d38), the reliable ring
  (0x545844, stride 0x3218), `cls` state (0x533cb8 + 0x10*n), input (0x52fbc0, stride 0x21c) have ONE element (cl[1] would overlap the slot table at 0x540ba0).
- **Host side, one player = one function: `0x22d000(n, &usercmd)`** (SV_ClientThink analogue: copies the cmd into `client_t[n]+0x1630`, runs `0x15bd80(n)`). In co-op the cmds go through a frame
  (0x22f830 begin, 0x22f0d0 append a client's packet, 0x22efd0 end + send, 0x2302d0 execute); a one-machine session can skip the frame and call 0x22d000(1, cmd) directly.
- **The engine has a second configuration with no network at all (coop byte 0, N local players):** only the store in G_InitGame (`DAT_1403c3eb0 = 1`, imm32 at rva 0x1714ac) pins N to 1. That is the
  console's split screen, left in the PC binary. Design Y = patch that to 2; Design X = keep the coop byte, make slot 1 local (type 2, port 1) and hook `0x22ec30` ("partner 10 frames behind").
- Missing/hard parts: memory for the client-side arrays of context 1 (swap, as planned), `0x1400cd340` is hard-wired to slot 0's netchan, creating the second party member for Design X
  (0xd7220 arguments partly unknown), what the mod's C code and scripts assume for `coop == 1`.

Next experiment (E4, server side only): Design Y on a test copy: set the immediate to 2 and watch whether the SP server creates two client_t / two player entities and survives (the client arrays of
index 1 overlap unused memory in non-coop mode, so a crash is a finding too).

## Experiment 4 (2026-10-26): the server with two local players (`[native] players=2`, `src/native.c`)

Patch of the immediate at rva 0x1714ac (1 -> 2), plus a replacement of the server's reliable-command adder (rva 0x232c30) that drops the commands for client 1, then `devmap trainer` on a test copy:

- The patch takes (`DAT_1403c3eb0` = 2) and **the server makes both clients**: `client_t[0]` and `client_t[1]` (rva 0x19ada98 / 0x19af110) both reach state 4; the level loads and `sv_running` is 1.
- Without the drop the level dies at once with `Com_Error` "Reliable command buffer overflow" (rva 0x232cf8): nothing acknowledges the server's commands to client 1 (its client side does not exist). With the drop it stays up.
- **The simulation does not advance:** server time stays at 300 ms, `cg.time` at 300 and `cl_paused` stays 1 (forcing `cl_paused 0` changes nothing: time still 300). cls state of client 0 is 6. The engine frame driver
  (rva 0x233190) only steps the simulation on its normal branch (client connection state test 0xd5820, the party/coop predicates 0x22c340, 0x22ed70, 0x22e470, 0x22e7a0); which of them holds for N=2 is not yet known.
- So Design Y alone is not enough: the client side of local client 1 (`cl`, `clc`, `cls`, the snapshot take 0xc99c0 -> 0xb8000(lc), the usercmd send 0xd02a0(lc), the snapshot publish 0xd5690 / 0xd5470) writes into memory that
  belongs to other statics for lc = 1 (stride 0xce9c and 0x3218 arrays of one element). The wrapper-with-context-swap design stands: every engine function that takes a local client number and touches client state
  runs for client 1 as local client 0 with the client statics swapped to context 1's copy. `tools/rvabytes.py` prints a function's bytes from the exe on disk (for the hook prologue checks).

## Experiment 5 (2026-10-26): a level with TWO players in ONE process, no network (`[native] players=2`, `coopdvar=1`, `src/native.c`)

**Correction to experiment 4:** the "simulation stalls at 300 ms" was a wrong probe: the addresses read (0xd96ec8, 0x19c3868) are not the game clock, and the plain baseline (one player, same commands) shows
the same values. The level does run (the trainer's intro cinematic plays with subtitles in baseline and in every run below). The real problems of experiment 4 were the client loops over local client 1 writing into
other statics (the playerstate copy of client 1 into `cl[1]` = rva 0x542bd4.. overwrote the renderer flags at 0x548ba4/0x548bac with zeros: black screen; and a null cgame dvar pointer read by the render thread).

What works now (64-bit, test copy, `devmap trainer`, nothing else):
1. `players=2`: the immediate in G_InitGame is 2 and the server's reliable commands to client 1 are dropped (no client behind it yet): both `client_t` are active.
2. **35 reads of the player count in the client code (rva 0x5000-0xe0000) are pointed at a copy that stays 1** (`client_n_sites` in native.c, found with `tools/ghidra/RipRefs.java`: instruction address, length,
   offset of the rip-relative displacement, patched at run time). The server and the game module (rva 0x120000-0x240000) keep reading the real 2. So the client side only ever sees local client 0 and never touches the arrays of one element.
3. `ps_publish` for client 1 (the server's copy of its playerstate, call site 0x2346ca) goes straight into context 1's copy of the cl block, not into the live memory behind `cl[1]`.
4. `coopdvar=1`: the scripts decide co-op from the dvar `coop` (script-side writes are refused), so a thread keeps it at 1 while the engine's co-op byte (rva 0x52fb5c) stays 0 (the engine takes its local-players branch).
   Result: the mod's recorder reports **`rec: 300 level trainer start '' players 2`** (level.players has two players) and the mod's script shows "waiting for the other player" (its partner handshake never completes: no partner exists yet).
   No crash; the intro plays.

Open: the mod's partner handshake (scripts wait for the other player; net.c/party.c assume a remote partner), player 1's usercmds (`0xd02a0(1)` is skipped: player 1 stands still), context 1 for the cgame
(`ctx_swap`, `run_both`, wrappers are in place but only context 0 is exercised: context 1's init is deferred to `ctx_after_draw`), the second view, HUD, input, sound listener.
`[native] players=1` installs only the hooks, for comparison runs. Test setup that works: `players=2` + `coopdvar=1` in the copy's `mw2coop\settings.ini`, commands `devmap trainer`, `!shot`, `!rva`, `!watch`.

## Experiment 6 (2026-10-26, night): two independent views of two players in one process (`[native] experiment=5`)

Settings: `players=2`, `coopdvar=1`, `experiment=5`. What it does on top of experiment 5:
- **Mode 5: the engine's whole client frame (rva 0xd5b00, called at 0x1fc331 once per engine frame) runs for context 0 on even frames and for context 1 on odd frames**, with the client statics
  swapped (`ctx_swap`, ranges in native.c: cgame block 0x404450-0x44f000, cl snapshots 0x51b078-0x51dcb0, 0x52c400, input 0x52fbc0, cls state, cl block 0x533d20-0x540b60 (playerstate at +0x2018),
  clc ring 0x545844, snapshot message time 0x548ce8). Everything the client does in a frame (time, snapshot use, usercmds, cgame draw, 2D) so happens in the right context; the renderer still gets one scene
  per frame. `r_subwindow`: top half on even, bottom on odd frames; the swap-chain Present hook composes both halves (mode 2's compositor).
- Context 1 is started a few frames after the first cgame draw (the cgame registers dvars and media while it runs the first frames and every thread's 2D reads them): its block is a copy of context 0's,
  except the playerstate the server published for client 1; then only the two per-client init parts run (`0xb8830(0)`, `0xb87b0(0)`), not the whole cgame init (that resets shared effects and media: black screen).
- The server's per-frame hand-over is done for both contexts: playerstate of client 1 into context 1's copy (call site 0x2346ca), the snapshot message parsed twice, the message read again from its start
  (call site 0x234925: 48-byte msg_t copy), the snapshot take 0xc99c0 run for both.
- **Result (composed frame at frame 2600 of the run): the top half shows player 0's view (the camo net he looks up at), the bottom half a DIFFERENT view (looking down at the floor, an ammo crate, a soldier's legs): player 1's own
  first-person camera, `cg.clientNum` = 1 in context 1, `cg.time` advancing in both.** No crash.
- Not done: player 1's input (he stands still), aspect/FOV per half (both halves are stretched), HUD and 2D per half, the other player's body is missing from the entity list
  (the server leaves local players' entities out and feeds them through the playerstate; the cgame of each context has to build the other one), sound listener, the mod's own script/net layer for local partners,
  and the briefing cinematic shows the text "waiting for the other player" (source not found: the engine's COOP_WAITINGFORPLAYER draw at rva 0x93ac0 is gated by the engine's co-op byte, which is 0 here and the draw
  is never reached; probably a menu of the cinematic skip, which needs both players).

## Experiment 7 (2026-10-26, evening): the bottom camera, proportions, the waiting text

- **Why the bottom camera jerked:** the simulation step and the hand-over of its results (rva 0x2343a0) run INSIDE the client frame (called at 0xb8d50). During context 1's frame player 0's playerstate was written into
  the live (context 1's) cl block, and player 1's into the stored copy, so the bottom view alternated between the two players' eyes (about 76 units apart). Fix: the step runs with context 0 live (`server_step_hook`),
  `ps_publish_hook` writes client n's playerstate into the live block if context n is live, else into its stored copy. A series of 12 consecutive composed frames (`seq-<n>.bmp`) is stable now.
- Also: the mouse state and the view-angle accumulators (0x533cb8-0x533d20) belong to the contexts (a jerk of the mouse made player 0 spin: the accumulators were shared); context 1 reads no mouse (0xcf4b0 at 0xcfd23 and
  0xd0061), its usercmds lose buttons and movement (0xd03a0 at 0xcf8b1) and go to the server through 0x22d000(1, cmd) from its own ring.
- **Proportions (mode 6, `experiment=6`):** the old modes drew the top or bottom half of the normal view into each half (so each player saw only the upper or lower half of his view, which looked like a "camera inside
  the floor"). Now both views are drawn as the middle band of the full picture (`r_subwindow` = 0.25-0.75) and each band is stretched into its half: right proportions, half the vertical field, no squeeze.
- Player 1's view: the angle accumulators of context 1's block (`p1yawdeg`, default 60) turn him; the entity list keeps both bodies with `listplayers=1` (works with the corrected setup, no DirectX error).
- The co-op "waiting for the other player" text of the briefing is blanked while both players are local (`localize_override` of COOP_WAITINGFORPLAYER and SPECIAL_OPS_WAITING_OTHER_PLAYER; the engine draw at 0x93ac0
  was never reached, the text comes from the localized string).
- Open: HUD/2D per half (the 2D is drawn for the whole frame, the band crop cuts what is at the screen edges), sound (one listener), the mod's partner logic (net/mirror/party for a local partner), a second device
  for player 1 and the menu/lobby flow, the 32-bit build.

## Experiment 8 (2026-10-26, evening): what the owner asked for, one by one

- **Mode 7** (`experiment=7`): each view is built for the shape of a half (the video aspect floats at rva 0x4113738 / 0x411373c are set to the half's aspect: wide field, no cropping), drawn over the full frame, and the compositor squeezes it into its half
  (`StretchRect`, linear): the field is as wide as in the reference screenshots, proportions are right, the 2D (subtitles, HUD) is drawn inside each half.
- **Split only after the intro:** the mod's script sets the dvar `mw2coop_split_view` to 1 when the level's `introscreen_complete` flag is set (`_mw2coop.gsc: split_view_gate`); until then one picture
  (briefing, title card). The C side reads it every client frame and restores the video aspect when the split is off.
- **The other player in the bottom view:** the cgame hides "my" entity by the entity NUMBER equal to the local client (0). Context 1 is local client 0 but player 1, so it hid player 0 and drew itself (the big dark
  head over the camera). Fix: in context 1's draw the data of entities 0 and 1 trade places (`swap_entities`, swapped back after the frame); player 1's own body is then the "mine" entity and player 0's body is drawn.
  (`ctx1lc`, `skipent`, `swapbits`, `entdraw`, `entlog`, `nogun`, `nopredict` are diagnostics.)
- **DirectX error:** the half textures of the compositor are in the default pool; a device reset (lost device, `vid_restart`, a change of the desktop, a remote session) fails while they are alive. The `Reset` / `ResetEx` slots
  now release them first (`reset_hook`). Not yet confirmed by a long run.
- Open: sound (one listener), the mod's partner logic (net/mirror/party), a second device and the menu/lobby for the mode, the 32-bit build, an HUD laid out for the half instead of a squeezed one.

## Experiment 9 (2026-10-26/27, night): mode 10, the console look, and the lobby

- **Where the 2D is drawn.** A render thread trace (`[native] d3dtrace=1`: render targets, viewports, clears, stretches, draw runs with their callers, written to `<game dir>\mw2coop\d3dtrace.txt`) showed one frame
  is: shadow maps, the HDR scene target, then ONE pass into the back buffer (opaque, transparent, and at its end the texts and signs), a copy of the back buffer, the blur chain at 480x270, a composite, a few last 2D
  draws. The pass is `FUN_140062...` called from the scene function at rva 0x5c240; its last call (rva 0x5c5bf, function 0x63680, draw list at +0x868 of the scene struct) draws the 2D (subtitles, objective,
  names, signs). The glow and blur that follow work on the back buffer.
- **Mode 10** = mode 8's 3D (the cgame's aspect at rva 0x43c640 is the half's, the view is built over the whole frame and squeezed into the half, right proportions, cg_fov 49 = the console's 0.75 scale) plus:
  - the scene's 2D call (rva 0x5c5bf) is hooked: the render thread sets the render target to a per-half A8R8G8B8 layer (cleared to transparent) for that call and puts the back buffer back; with the engine's
    blending left alone the layer would hold the alpha squared, so while the layer is drawn `SEPARATEALPHABLENDENABLE` is set (alpha blend factors one / inverse source alpha for the alpha channel only);
  - the compositor squeezes the back buffer (the view after the glow) into the half texture and lays the layer's top half over it with the fixed pipeline (a state block saves and restores everything);
  - the 2D is laid out for a half: the three screen placement structs at rva 0x5e0b80 (stride 0x70: scaleVirtualToReal, scaleVirtualToFull, scaleRealToVirtual, viewport position and size, viewable and
    adjustable rectangles; found by scanning the image for the pair 3.0 / 2.25) are given a 1920 x height/2 viewport, so the virtual canvas of 480 units is 540 pixels tall and text is not squeezed.
  Result: normal text (small, as in the console: half the height means half the size), the view-model fully visible in each half, HUD per half.
- **Crash fixed:** both contexts process every snapshot and both ran the cgame's cleanup of removed entities (rva 0xb9320), which frees the engine's shared per-entity objects; the second free crashed in the pool
  (Contingency, a few seconds after the intro). Only context 0 does it now (`entclean=0` turns it off).
- **Session lifecycle** (`native_split_start/stop`, console `mw2coop_native_start [pad]`, `mw2coop_native_stop`, `mw2coop_native_launch <map> [pad]`): every code patch of the file goes through a journal;
  the patches that make the game a two local players game (player count in G_InitGame, the server command filter, the client's reads of the count, the entity list) are put back when the session ends, the hooks stay
  and pass everything through while no session runs. The menu's Split screen button now opens the lobby as before (the second player presses a button on his pad); Start no longer launches a second process:
  the lobby's party is let go, the session starts and `map <mission>` runs (`menus.c: native_launch`). The session ends when the menus are back (`native_session_tick`). Player angles of a session from the menu are
  the spawn's (the QA runs turn the players to face each other with `p1yawdeg`/`p0turn`).
- Tools: `tools/sheet.py` (contact sheet of BMP frames, pure python), `tools/crop.py`, `tools/qa_native.sh` with `MW2COOP_MENU=1` (the console launch), `MW2COOP_LOBBY=1` (the lobby's whole path), `MW2COOP_POST`.
- Open: global texts (objective, subtitles) are drawn in both halves (they belong to client 0 and context 1 draws as client 0); sound listener; a second keyboard; HUD size dvars of the console's split screen;
  the 32-bit build; long runs; a restart after both players are down.

### Experiment 9, later the same night

- **A second 2D source.** The frame's 2D command list (rva 0x5c010, called at 0x4545b after the glow and blur) holds texts the scene's 2D pass does not: the intro's title lines, fades, menus. It is drawn into the
  same per-half layer (not cleared again), so these texts get the half's scale too.
- **Global texts once.** The objective, the subtitles and the intro lines are drawn by both contexts (they belong to the game's client 0, and context 1 draws as client 0). When the bottom half's layer is laid
  over its half, a pixel shader (HLSL compiled at run time by the system's `d3dcompiler_47.dll`, target ps_2_0) compares it with the top half's layer inside three bands (the objective's strip, the subtitles' band
  above the crosshair, the bottom-left/bottom-centre band of the intro lines) and drops the bottom pixels that are the same; for the first 14 s of a split the bands are cut out whatever the layers hold (the intro lines
  are random glyphs that settle one by one, no comparison matches them). Per-player parts (ammo, compass, signs over heads) are outside the bands or differ.
- **One picture while a menu or the console has the keys:** the key catchers of client 0 (rva 0x533cb0, bits 0x11) switch the split off (pause menu, objectives, end of game, console), so those screens are not drawn twice.
- **The view model is per context.** The first-person weapon's DObjs are made under the handles 0x800 + 2 * local client + hand; both contexts ran as local client 0 and shared them, so a weapon change in one
  freed what the other pointed to (crash on the first weapon change by either player: null tree behind rva 0x440730). The five calls of the setup function (rva 0xc6480, at 0xc41b1, 0xc58ed, 0xc594a, 0xc5dfb,
  0xc6033) are made for local client 1 in context 1 (handles 0x802/0x803), its copy of the cgame's view model pointers is cleared when it starts, and the blend (0xc6ee0, at 0xc7499) waits while its tree is null.
- **Checked:** the lobby path and the console path start a session; Pause/Escape and resume; a second session in the same process after `mw2coop_native_stop`; trainer -> roadkill; the restart after both players
  are down; 5+ minutes without a hang; a pad walking and turning player 2 (fakepad.txt, `p1pad`). Not solved: player 2's weapon switch by pad (the game has no bindable button for it that we could find; the
  client's usercmd overrides a script's SwitchToWeapon for him), sound, a second keyboard.

## Status when paused (2026-10-27, morning)

Mode 10 is the split screen (see experiment 9). It runs from the menu: Split screen button -> lobby (the second player presses a button on his XInput pad) -> Start. Tested with bots and
screenshots on one PC (`MW2COOP_LOBBY=1 sh tools/qa_native.sh 150`), not played by two people yet; owner's play tests of 2026-10-27 gave the list below.

### Done since experiment 9 (owner's feedback fixed)
- Black box behind green text (speaker names): additive/other blend states no longer add coverage to the 2D layer (`layer_fix`).
- Subtitles once, in the bottom half; objective once, in the top half (pixel shader that compares the two layers inside bands, `dedupe_make`, shader targets ps_2_b/2_a/2_0).
- The scope: the engine clips the lens with a scissor rectangle worked out from the half-height 2D placement; the rows of that rectangle are doubled for full-size 3D targets (`t_scissor`), the lens is round
  in its half again, in both halves. The scope marks which half is scoped (`scope_tick`), the cut-outs of the shader then fill with black instead of making holes in the scope's overlay.
- Which half a frame belongs to is read from the frame (the scene struct holds the cgame time at +0x180, `half_note` / `half_find`), not from a queue (a queue stayed off by one for a whole session after one early frame).
- One picture while the pause menu, the briefing, the console have the keys (key catchers at rva 0x533cb0); the split starts when the mission title's black screen is gone (`intro_black` in the
  `_introscreen` patch), with a 45 s timeout for intros that never set the flag.
- Crash fixes: the removed-entity cleanup freed the same objects from both contexts; the first-person weapon's DObjs shared handles (now context 1 uses local client 1's).
- Sound listener: only context 0 sets it (hook at rva 0xbd2c5).

### Open (owner's list, 2026-10-27)
1. DONE 2026-10-28: subtitles once, in the bottom half, no ghost. The game message windows (rva 0x52f348, outside the swapped ranges, so one set that both contexts draw) are drawn by
   rva 0xcb4f0 (local client, window, x, y as ints in r8d/r9d, ...); window 2 is the channel "subtitle" (default filters at rva 0x3bf1f0: "gamenotify obituary", "boldgame", "subtitle", "").
   `msgwin_hook` draws window 2 in context 1 only and window 0 (the notices at the top left, e.g. a new objective) in context 0 only (`subonce=0` off). Checked on trainer frames.
2. The dark backing was the compass's mask: it writes a rectangle into the alpha alone (blending off, colour write 8), multiplies it by its round edge (ZERO/SRCALPHA, colour write 8),
   then draws its map by destination alpha (DESTALPHA/INVDESTALPHA). `layer_fix` forced "keep the alpha" on the multiply, so the map covered the whole rectangle: a dark square
   around the compass, seen on snow. Alpha-only draws now keep the game's own blending (fixed 2026-10-28, checked on trainer frames while walking).
   The compass and the ammo counter over the scope's black overlay are not a bug (owner, 2026-10-28): they stay as they are.
3a. DONE 2026-10-28: names over the partner in both halves. rva 0x93ac0 draws them for clients 0 and 1 and skips "self" (CMP EBP,EAX at 0x93db3); context 1 skipped player 1.
   A stub at 0x93daa (`name_self_install`) skips player 2 in context 1; client 1 is never tested as local there (the local player count it reads is 1), so `name_get_hook` (the call at
   0x93e2f) gives it an empty name in context 1. Player 2's name: [native] p2name, else the profile's name + " (2)" (rva 0xd3110 gives the profile's name for every client).
3. The "Follow" waypoint hint of the objective must show in both halves (only the top one has it; the bottom context does not draw it).
4. The helicopters of the mission before the Gulag move jerkily back and forth: not investigated; suspects: moving state outside the swapped ranges stepped by both contexts, or the entity cleanup.
5. The second player joins too late: he must appear right after the loading and the black title screen (the context 1 starts ~5 frames after the first cgame draw, but the split switches on after the intro, and in
   Contingency the first minute shows one picture: check what is slow).
6. Weapon switch for the second player by pad: no bindable "weapnext" found; a script `SwitchToWeapon` is overridden by his usercmd.
7. Sound (one listener), a second keyboard, the 32-bit build, console HUD sizes.
8. The Steam overlay (`gameoverlayrenderer64.dll`) sometimes overflows its stack when the game window is closed at the end of a run (not seen in play).

### The Nexus gamepad mod (MW2 Native Gamepad 1.2.1, nexusmods codmw2/mods/83), read for ideas, nothing run
An ASI plugin (loaded through an ASI loader renamed winmm.dll: it clashes with this mod's own winmm.dll proxy): XInput polling at 250 Hz, then SendInput / mouse_event and hooks of
GetAsyncKeyState / GetKeyState / GetKeyboardState, a radial dead zone with a look curve, ADS toggled by pulsing the right mouse button on LT press and release (MW2 PC has toggle ADS),
"View tap = Use" in the campaign, weapon cycling on the key "1". Useful for the second player's pad: the same mapping idea, but through the engine's key handler (`pad_binding`) per context.
Ghidra project of its binary (import only): `local/ghidra-proj-gp` (not in the repo).

### Tools of this stage
`tools/qa_native.sh` options: `MW2COOP_LOBBY=1` (whole menu path), `MW2COOP_MENU=1` (console launch), `MW2COOP_PLAIN=1` (no split), `MW2COOP_MID`/`MW2COOP_MIDAT` (commands during the level), `MW2COOP_POST`,
`MW2COOP_TIMEOUT`; settings `[native]`: `qa_ads=<ms>`, `qa_ads1=<ms>` (scope for player 0 / 1), `layerdebug=1|2|3`, `d3dtrace=1` (`tracepops`, `tracehalf`), `skiphud`, `skiprot`, `skipctx`, `scenescan`,
`entcleanlog`, `timelog`, `dedupe=0`; console `mw2coop_press <binding> <1|0>`, `mw2coop_qa_move <fwd> <fire>`, `mw2coop_native_start|stop|launch`; `tools/sheet.py`, `tools/crop.py`, `tools/leascan.py`.
A sleeping display stops Present (the picture freezes): keep the display awake while tests run when the owner is away.
