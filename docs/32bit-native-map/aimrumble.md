# 32-bit map: aim assist, rumble, player state fields and session control (package aimrumble)

Author: research agent "aimrumble". Image base 0x400000 (32-bit exe `FC9E4843...52D7`); `rva` below = VA - 0x400000 (what `native.c` adds to `base`).
Everything marked **verified** was read in the decompile / disassembly of both builds; **likely** = one side read, the other inferred; **unknown** = not found.
Tools used: `tools/ghidra/g.sh` (with a private `TMP` per call: `run.sh` writes `$TMP/mw2coop-ghidra.log`, two parallel runs with the default TMP overwrite each other's output),
`image32.bin` for bytes.

## 0. Summary (what a 32-bit port of these parts needs)

- **Aim assist: the whole design carries over.** `aaGlob` has the same stride (0xe80) and the *same field offsets* in 32 bits (it holds no pointers), the server target
  lists have the same stride (0x210c) and the same entry size (0x2c), the aa target entries are the same 0x34 bytes, and the 32-bit `AimTarget_ProcessEntity`
  is called by the engine the same way. Only the base addresses, the weapon-def offsets (0x148 less), the gentity offsets (pointer-size shifts) and the calling
  conventions of two register functions change. The 38 `aim_*` dvar pointers are **not contiguous** in 32 bits: a table of 38 VAs is needed (section 1.1).
- **Session control:** the player-count store is the *identical instruction* (`c7 05 <abs32> 01 00 00 00`, 10 bytes) at VA 0x4bf9db; the 32-bit operand is an
  absolute address, so redirecting the client's reads of the count is a plain 4-byte write (no `hook_page_near`, no rel32 range limit).

---

## 1. Aim assist

### 1.1 aim_* dvar pointers (registered by 64-bit rva 0x69a60 = 32-bit VA 0x48df90) - native.c line 2491 (`aa_dvar`), padopts.c `unlock_aim_dvars`
- 32-bit: register function VA 0x48df90, rva 0x8df90, convention: cdecl, no arguments (void). It stores the 38 dvar_t pointers into the globals below.
- bytes at VA 0x48df90: d9 05 d4 51 6e 00 68 d8 9d 69 00 6a 44 83 ec 0c
- the dvars are registered by the same strings in the same order, so index n of the 64-bit table (0x3cf050 + 8n) is entry n of the 32-bit one:
  (64-bit rva -> 32-bit VA / rva of the global that holds the `dvar_t *`; the pointer then points to a 32-bit `dvar_t`: type +0xc, current value +0x10, as in `game.h`)

| dvar | 64 rva | 32 VA | 32 rva |
|---|---|---|---|
| aim_aimAssistRangeScale | 0x3cf050 | 0x73ba84 | 0x33ba84 |
| aim_autoAimRangeScale | 0x3cf058 | 0x73ce70 | 0x33ce70 |
| aim_input_graph_enabled | 0x3cf060 | 0x73c934 | 0x33c934 |
| aim_input_graph_debug | 0x3cf068 | 0x73ceb4 | 0x33ceb4 |
| aim_input_graph_index | 0x3cf070 | 0x73c920 | 0x33c920 |
| aim_turnrate_pitch | 0x3cf078 | 0x73ce74 | 0x33ce74 |
| aim_turnrate_pitch_ads | 0x3cf080 | 0x73c930 | 0x33c930 |
| aim_turnrate_yaw | 0x3cf088 | 0x73ba8c | 0x33ba8c |
| aim_turnrate_yaw_ads | 0x3cf090 | 0x73ce88 | 0x33ce88 |
| aim_accel_turnrate_enabled | 0x3cf098 | 0x73ba88 | 0x33ba88 |
| aim_accel_turnrate_debug | 0x3cf0a0 | 0x73ce6c | 0x33ce6c |
| aim_accel_turnrate_lerp | 0x3cf0a8 | 0x73ceac | 0x33ceac |
| aim_slowdown_enabled | 0x3cf0b0 | 0x73c93c | 0x33c93c |
| aim_slowdown_debug | 0x3cf0b8 | 0x73ce9c | 0x33ce9c |
| aim_slowdown_region_width | 0x3cf0c0 | 0x73ce60 | 0x33ce60 |
| aim_slowdown_region_height | 0x3cf0c8 | 0x73ce68 | 0x33ce68 |
| aim_slowdown_pitch_scale | 0x3cf0d0 | 0x73ce94 | 0x33ce94 |
| aim_slowdown_pitch_scale_ads | 0x3cf0d8 | 0x73ceb0 | 0x33ceb0 |
| aim_slowdown_yaw_scale | 0x3cf0e0 | 0x73ce8c | 0x33ce8c |
| aim_slowdown_yaw_scale_ads | 0x3cf0e8 | 0x73ce90 | 0x33ce90 |
| aim_autoaim_enabled | 0x3cf0f0 | 0x73ce7c | 0x33ce7c |
| aim_autoaim_debug | 0x3cf0f8 | 0x73ce98 | 0x33ce98 |
| aim_autoaim_lerp | 0x3cf100 | 0x73c914 | 0x33c914 |
| aim_autoaim_region_width | 0x3cf108 | 0x73cea8 | 0x33cea8 |
| aim_autoaim_region_height | 0x3cf110 | 0x73cea0 | 0x33cea0 |
| aim_automelee_enabled | 0x3cf118 | 0x73c92c | 0x33c92c |
| aim_automelee_debug | 0x3cf120 | 0x73c910 | 0x33c910 |
| aim_automelee_lerp | 0x3cf128 | 0x73c938 | 0x33c938 |
| aim_automelee_region_width | 0x3cf130 | 0x73c928 | 0x33c928 |
| aim_automelee_region_height | 0x3cf138 | 0x73ce78 | 0x33ce78 |
| aim_automelee_range | 0x3cf140 | 0x73c918 | 0x33c918 |
| aim_lockon_enabled | 0x3cf148 | 0x73ce84 | 0x33ce84 |
| aim_lockon_debug | 0x3cf150 | 0x73ba80 | 0x33ba80 |
| aim_lockon_deflection | 0x3cf158 | 0x73ce64 | 0x33ce64 |
| aim_lockon_strength | 0x3cf160 | 0x73c924 | 0x33c924 |
| aim_lockon_region_width | 0x3cf168 | 0x73ce80 | 0x33ce80 |
| aim_lockon_region_height | 0x3cf170 | 0x73c91c | 0x33c91c |
| aim_scale_view_axis | 0x3cf178 | 0x73cea4 | 0x33cea4 |

- evidence: both decompiles of the register function (`@0x69a60`, `@0x48df90`) list the 38 strings in the same order with the same default / flag arguments (flags 0x44 for the first two,
  4 for the rest, in both); the address of each global is the assignment target in the decompile. **Not contiguous in 32 bits** (they spread over 0x73ba80-0x73ceb4, interleaved with
  other statics): `native.c` `aa_dvar(rva)` and `padopts.c` `RVA_AIM_DVARS..RVA_AIM_DVARS_END` (a loop over 38 consecutive qword slots) need the table.
  `native.c` uses these 12: aimAssistRangeScale, autoAimRangeScale, slowdown_enabled, slowdown_yaw_scale, slowdown_yaw_scale_ads, slowdown_pitch_scale, slowdown_pitch_scale_ads,
  autoaim_enabled, autoaim_lerp, lockon_enabled, lockon_deflection, lockon_strength.
- the cheat flag: `padopts.c` clears it on the `dvar_t` (flags +8 in both builds, see game.h 32-bit `dvar_t`); the 32-bit flags argument of the register calls is 4 (cheat) as in 64 bits.
- confidence: verified

### 1.2 aaGlob (the per-local-client aim assist state) - 64-bit rva 0x3cf180, stride 0xe80, native.c lines 2475-2483, 2540-2551, 1681
- 32-bit: VA **0x73ba90**, rva 0x33ba90, stride **0xe80** (same), size per client 0xe80. `aaGlob[n]` = 0x73ba90 + n * 0xe80.
- offsets used by `native.c` (all **identical** in 32 bits, the struct has no pointers):

| field | 64 offset | 32 offset | how known (32-bit) |
|---|---|---|---|
| weapon index (int) | +0x24 | +0x24 | 0x55a5d0 reads `[0x73bab4+n*0xe80]` as the weapon index and indexes the weapon table with it; 0x4ae500 writes it |
| active flag (byte) | +0xb0 | +0xb0 | 0x488a80 tests `byte [aa+0xb0]` first; 0x4d3bf0 (per-client init) sets it to 1 after a memset(aa, 0, 0xe80) |
| regions: 8 floats (half width, half height) of slowdown, auto aim, auto melee, lock-on | +0xb8..+0xd4 | +0xb8..+0xd4 | 0x559840 writes `0x73bb48 + n*0xe80 ..+0x1c` (= +0xb8..+0xd4): pairs from aim_slowdown_region_*, aim_autoaim_region_*, aim_automelee_region_*, aim_lockon_region_* in that order |
| eye position (3 floats) | +0xd8 | +0xd8 | 0x488a80 stores its 2nd argument at +0xd8 |
| view origin (3 floats) | +0xe4 | +0xe4 | 3rd argument at +0xe4 |
| view angles (pitch, yaw, roll) | +0xf0 | +0xf0 | 4th argument at +0xf0 (`view[0]` pitch, `view[1]` yaw) |
| ads lerp (float) | +0x128 | +0x128 | 0x559990 writes `0x73bbb8 + ..` from `[aa+0x20]` or 1.0; 0x55a5d0 reads it as the ads blend of the ranges |
| target list (aa_target_t, 0x34 bytes each) | +0x13c | +0x13c | 0x55a5d0's debug draw walks `0x73bbf4 + n*0xe80 + i*0x34` with entity at element-0x28 (so element start = +0x13c); the sorted insert 0x55aa70 / the update 0x488a80 write the same 0x34-byte elements |
| target count (int) | +0xe3c | +0xe3c | `[0x73c8cc + n*0xe80]` = aa+0xe3c, loop bound in 0x55a5d0, zeroed by 0x488a80 |

- aa_target_t (entry) layout, observed in the 32-bit debug draw 0x55a5d0 (fields of an element e): ent int e+0, mins x/y e+4/e+8, maxs x/y e+0xc/e+0x10, pos e+0x14..0x1c, vel e+0x20..0x28, dist2 e+0x2c, xhair2 e+0x30. Same as `native.c` (0x34 total, `dist2` at +0x2c). 64-bit equivalents were read in 0x6a570's stores (element +0..+0x30).
- the mod swaps `{0x3cf180, 0xe80}` per context (native.c line 1681): 32-bit range `{0x33ba90, 0xe80}` (one client = one context; the whole aaGlob array is 2 * 0xe80 if two clients are kept).
- who fills it: 0x4d3bf0(client, ps) per-client init + 0x4ae500(client, ps) per-frame copy of playerstate fields into aa+0x00..+0x24 (weapon at aa+0x24 is computed there from ps+0x16c / 0x41fbf0(ps)), then 0x488a80 (the screen target update, section 1.4).
- **the ads state**: aa+0x20 = ps+0x2c0 (the zoom / ads lerp source), ads_lerp at aa+0x128 is derived from it by 0x559990 (`0x559990`: eax = usercmd-like arg with +0x2c = client, +0x28 = buttons: if the ps flags `& 0xc00` and `buttons & 0x800`, or 0x41d280(client), ads_lerp = 1.0).
- confidence: verified (offsets compared field by field with the 32-bit code; base address from the `IMUL EBP,EBP,0xe80 / ADD EBP,0x73ba90` in 0x488a80)

### 1.3 weapon def (BG_GetWeaponDef) and its aim ranges - 64-bit rva 0x85850, native.c lines 2474, 2541-2548
- 32-bit: VA 0x427850, rva 0x27850, convention: cdecl, one stack argument (weapon index), returns the `WeaponDef *`: `mov eax,[esp+4]; mov eax,[eax*4+0x1a69d08]; ret`. The table of `WeaponDef *` is at **VA 0x1a69d08** (rva 0x1669d08), 4-byte entries (64-bit: 0x3ee320, 8-byte entries), so `native.c` can index it directly instead of calling.
- bytes at VA 0x427850: 8b 44 24 04 8b 04 85 08 9d a6 01 c3 cc cc cc cc
- offsets in the WeaponDef: aim assist range **+0x2e4** (64-bit +0x42c), same in ads **+0x2e8** (64: +0x430), auto aim range **+0x2e0** (64: +0x428). (shift 0x148)
- evidence: 32-bit 0x55a5d0: `range = ((1-ads)*def[0x2e4] + def[0x2e8]*ads) * aim_aimAssistRangeScale->current; auto = def[0x2e0] * aim_autoAimRangeScale->current; melee = aim_automelee_range`; the 64-bit 0x693e0 has exactly this with 0x42c / 0x430 / 0x428.
  Whether this PC build's weapons carry a non-zero auto aim range is **unknown** (native.c falls back to the aim assist range when it is 0; keep the fallback).
- confidence: verified (both decompiles)

### 1.4 the screen target update (aaGlob fill) - 64-bit rva 0x6a570 (call at 0xbd2c5 inside 0xbca00), native.c lines 4020-4032 (`screen_targets_hook`)
- 32-bit: function VA **0x488a80**, rva 0x88a80, convention: cdecl, 6 stack arguments (int client, float eye[3], float origin[3], float angles[3], float, float aspect), the caller pops 0x18.
- bytes at VA 0x488a80: 81 ec 98 00 00 00 53 55 56 8b b4 24 a8 00 00 00
- the call site to redirect (like `jhook_call(base + 0xbd2c5, ...)`): **VA 0x466adc, rva 0x66adc**, `e8 9f 1f 02 00` (call rel32 -> 0x488a80), inside 0x4666c0 (32-bit counterpart of the cgame draw 0xbca00). The three pointer arguments are static (VA 0x783a64 eye, 0x783a70 origin, 0x783a7c angles), pushed just before the call: `push 0x783a7c / push 0x783a70 / push 0x783a64 / push ebp(client) / call`; the two floats are stored at `[esp]`, `[esp+4]` before the pushes.
- evidence: the 32-bit body stores arg 2 at aa+0xd8, arg 3 at aa+0xe4, arg 4 at aa+0xf0, tests aa[0xb0] first, calls 0x559840-family region helper (0x5599e0) and the target-list getter, builds the sorted list; same as the 64-bit body.
- confidence: verified

### 1.5 the server target list getter (called inside the update) - 64-bit rva 0x6b230, call at rva 0x6a711, native.c lines 4026-4032 (`target_list_hook`)
- 32-bit: function VA **0x4a7e40**, rva 0xa7e40, cdecl, 3 stack arguments (int client, void **list, int *count): `*list = 0x73e4c8 + idx*0x210c; *count = [0x73efc8 + idx*0x210c]` with `idx` = 0x4215a0(client) (the co-op slot lookup).
- bytes at VA 0x4a7e40: 8b 44 24 04 50 e8 56 97 f7 ff 69 c0 0c 21 00 00
- the call site to redirect (64-bit `jhook_call(base + 0x6a711, ...)`): **VA 0x488ba8, rva 0x88ba8**, `e8 93 f2 01 00` (call rel32 -> 0x4a7e40), in 0x488a80.
- `0x4215a0(client)` (64-bit: the loop at the start of 0x6b230 over the co-op slots): VA 0x4215a0, cdecl 1 stack arg, returns `client` when the engine co-op byte [0x88897c] is 0, else the slot index whose type (+0 of the 0x263c-byte slot at 0x88f560) is 2 and whose id matches. Bytes: 80 3d 7c 89 88 00 00 75 05 8b 44 24 04 c3 8b 54.
- caution (also found by the entities package, entities.md section 6): after this call the caller does one `add esp,0x14` that also cleans earlier pushes of the surrounding code; a replacement for the call must be a plain cdecl `(client, list, count)` returning with `ret` (not `ret 12`) and must not touch the stack above its arguments (as `jhook_call` trampolines do).
- confidence: verified

### 1.6 server aim target glob (aimTargetGlob) - 64-bit rva 0x3d0530, native.c lines 4034-4094 (`aim_targets_update`)
- 32-bit: VA **0x73cec0**, rva 0x33cec0, stride **0x210c** per client (same). Per client block: list being built at +0 (0x2c-byte entries, 64 of them = 0xb00 bytes), count at **+0xb00** (VA 0x73d9c0 for client 0; verified: the sorted insert 0x55aa70 uses `[0x73d9c0 + client*0x843*4]` as the count and `0x73cec0 + i*0x2c + client*0x210c` as the entries), the published list at **+0x1608** (VA 0x73e4c8) with its count at **+0x2108** (VA 0x73efc8).
  The entry is 0x2c bytes (ent number +0, squared distance +4 as the sort key, 9 more dwords: screen box / size from 0x55ab10 and 0x55af20).
- the sorted insert 0x55aa70 (64-bit: inlined in 0x6a9a0): VA 0x55aa70, usercall (EAX = the 0x2c-byte entry to insert, stack arg = client), bytes 53 8b 5c 24 08 69 db 0c 21 00 00 55 56 81 c3 c0.
- the publish (copy +0 list to +0x1608, count to +0x2108) is done by the engine in the 32-bit build by code `native.c` replaces (aim_targets_update); where the engine publishes it in 32 bits: **unknown** (not searched; `aim_targets_update` writes both itself so it does not matter).
- confidence: verified (layout and addresses); the publish step: unknown

### 1.7 AimTarget_ProcessEntity and its pieces - 64-bit rva 0x6b5a0 (caller 0x174920 at 0x174afd), 0x6b630, 0x6a9a0, 0x6bae0; native.c lines 4034-4094
The 32-bit **engine does call AimTarget_ProcessEntity**: 0x5bfdb0 (the per-entity think, 64-bit 0x174920) calls it at 0x5bfeb6 for `type == 13 || (eFlags & 0x2000)`. The 64-bit decompile also has the call (0x174afd); the native.c remark that "the PC build never calls it" holds in practice because 0x6bae0 returns the *client* code's local player count (1), see below; the same will hold in 32 bits once `client_n_install` points 0x4818b0 at the copy.
- **AimTarget_ProcessEntity**: 32-bit VA **0x40dc60**, rva 0xdc60, cdecl, 1 stack arg (gentity *); bytes: 57 8b 7c 24 08 80 bf ac 00 00 00 00 74 31 8b c7
  Loops `for (c = 0; c < 0x4818b0(); c++) 0x55b040(ent, c)` after the in-use / enemy test 0x55ac80; else clears `ent->eFlags & 0xffe7ffff`.
- **per-client check** (64-bit 0x6b630 `for_client(client, ent)`): 32-bit VA **0x55b040**, rva 0x15b040, **usercall: EAX = the entity, one stack argument = the client number (pushed), the caller pops it** (`mov eax,edi; push esi; call; add esp,4`). It clobbers EBX/ESI/EDI (saves them) and uses ECX/EDX freely; ends with `ret` (not `ret 4`).
  So `aim_targets_update` cannot call it through a plain C function pointer: it needs a small asm thunk (`mov eax, [esp+8]; push dword [esp+4]; call target; add esp,4; ret`).
  bytes at VA 0x55b040: 8b 4c 24 04 83 ec 2c 53 56 57 8b f9 69 ff 70 02
  Inside: `gentity[client]` = `g_entities_ptr + client*0x270` where `g_entities_ptr` = VA 0x10a3c14 (see 1.8); the helpers it calls (0x55acd0 line-of-sight/range test, 0x55adc0 trace test, 0x55af20 build+insert) are usercalls with ESI/EDI/EAX as implicit arguments; do not call them directly.
- **in-use / enemy test** (64-bit inlined at the start of 0x6b5a0): 32-bit VA 0x55ac80, usercall (EAX = ent), returns AL. Bytes: 80 b8 b0 00 00 00 00 74 2c 80 b8 ac 00 00 00 00. Reads `ent+0xb0`, `ent+0xac`, type `*ent == 13`, health `ent+0x14c` (64: +0x164) > 0, `team = *(*(ent+0x110)+4)` (64: sentient +0x118, team +8) in {1,2,3} via the mask 0xe (`(1<<team)&0xe`); or `(ent+0xc & 0x2000) && ent+0x8c == 0xffffff`.
- **number of clients** (64-bit 0x6bae0): 32-bit VA **0x4818b0**, rva 0x818b0, cdecl, void, returns eax: `if (!coop_byte [0x88897c]) return [0x738c6c]; else return ([0x88f560] != 0) + ([0x891b9c] != 0)` (the two co-op slots' type words; 64-bit: `[0x540bb0] != 0` + `[0x543200] != 0`). Bytes: 80 3d 7c 89 88 00 00 75 06 a1 6c 8c 73 00 c3 33. Reads the local-player count 0x738c6c at 0x4818b9 (one of the sites for client_n_install).
- the entity's "seen by client" bits: `ent+0x138` (64-bit +0x150) bits `0x80000 << client`; cleared mask `0xffe7ffff` in both builds (bits 19 and 20).
- confidence: verified (0x40dc60, 0x55b040, 0x55ac80, 0x4818b0); the helper conventions come from the disassembly of 0x55b040 and 0x40dc60.

### 1.8 gentity / level layout (for aim_targets_update and rumble) - 64-bit G_ENTITIES rva 0xd9e070, stride 0x2a0; level.num_entities rva 0xd96e80
- 32-bit: g_entities array VA **0xea76b8** (rva 0xaa76b8), stride **0x270** (not 0x2a0), 2048 entries (0x4e3dc0 G_Spawn caps at 0x7fe like the 64-bit 0x1a0a20; `nextFree` link at +0x26c, 64-bit +0x298).
  `level.gentities` pointer (what the engine code reads, = the array above, set by G_InitGame at 0x4bfbfd `mov [0x10a3c14], 0xea76b8`): VA **0x10a3c14** (rva 0xca3c14); level.gclients VA 0x10a3c10 (= 0x108dc68, stride 0xafd4, 2 clients); 64-bit: gentities ptr at rva 0xd96e78, gclients 0xd96e70.
  **level.num_entities**: VA **0x10a3c18** (rva 0xca3c18) (64-bit rva 0xd96e80): G_Spawn 0x4e3dc0 does `if (num_entities == 0x7fe) Scr_Error(...)`, `ent = gentities + num_entities++ * 0x270`. firstFreeEnt VA 0x10a3c1c (64: 0xd96e88). level.time VA **0x10a3c44** (64-bit rva 0xd96ec8; `ent->0x104 = time` at spawn).
- gentity field offsets used by `native.c` (64 -> 32) and how known:

| field | 64 | 32 | evidence |
|---|---|---|---|
| type (byte, 13 = actor) | +0 | +0 | `*ent == '\r'` in 0x55ac80 / 0x6b5a0 |
| eFlags | +0xc | +0xc | `(ent+0xc & 0x2000)` in both |
| number (u16) | +0x7e | +0x7e | `ushort ent+0x7e` in 0x55af20 / 0x6a9a0 |
| `ent+0x8c == 0xffffff` | +0x8c | +0x8c | both |
| in use (bytes) | +0xac, +0xb0 | +0xac, +0xb0 | both tests; G_Spawn sets +0xb0 = 1 in 64 |
| origin (r.currentOrigin) | +0xe8 | +0xe8 | 0x55af20 reads `ent+0xe8/+0xec/+0xf0` |
| client (gclient_t *) | +0x108 | +0x108 | 0x55b05a `mov eax,[edi+0x108]`; `[eax+4]` = sessionState (switch cases 2,3,6,7 -> not a target client) |
| actor (the AI struct *) | +0x110 | +0x10c | 0x55af20 `iVar2 = [ent+0x10c]` then +0xd6c..+0xd74 (64: +0x110 then +0xe90..+0xe98) |
| sentient (*) | +0x118 | +0x110 | 0x55ac80 `[ent+0x110]+4` is the team |
| team inside sentient | +8 | +4 | same |
| health | +0x164 | +0x14c | 0x55ac80 `[ent+0x14c] > 0` vs 0x6b5a0 `[ent+0x164] > 0` |
| eFlags-like "seen" word | +0x150 | +0x138 | 0x40dc60 `and [edi+0x138], 0xffe7ffff` vs 64 `+0x150 & 0xffe7ffff` |
| nextFree | +0x298 | +0x26c | G_Spawn |

  rumble.c uses origin (+0xe8, same) and client (+0x108, same) of entities 0 and 1.
- bytes at VA 0xea76b8: not code (data); the check `native.c` can do: `*(uint32_t *)0x10a3c14 == 0xea76b8`.
- confidence: verified for the array / level fields / offsets listed (read in the 32-bit decompiles and disassembly); `+0x110`-actor (0x10c) is likely (one reader).

### 1.9 the mouse move that runs the aim assist's region and auto melee - 64-bit rva 0xcfcf0 (calls 0x6a350 at 0xcff1c); native.c line 2471
- 32-bit: the function that reads the mouse and the two view accumulators is VA **0x578fe0**, rva 0x178fe0, **usercall: the first argument on the stack, EDI = the usercmd being built** (`unaff_EDI+0x1a` forward move, `+0x1b` right move). Bytes: 83 ec 54 83 3d a4 89 88 00 00 0f 86 a7 02 00 00.
  It calls the wrapper 0x4a7ad0 (64-bit 0x6a350: stores the two accumulators' result, computes regions 0x559840 (+ ads lerp 0x559990) and the auto melee 0x55a240) at VA 0x57926d.
- **0x4a7ad0**: cdecl (struct *in, float out[2]); bytes 53 8b 5c 24 0c 56 57 8b 7c 24 10 d9 47 08 d9 1b.
- **0x559840** (the region computation: 64-bit part of 0x6a350): usercall EAX = client; bytes 51 56 8b f0 50 69 f6 80 0e 00 00 e8 c0 e4 ec ff.
- **0x55a240** (auto melee, 64-bit 0x685c0): cdecl (in, out), usercall-free; bytes d9 ee a1 2c c9 73 00 83 ec 0c 55 8b 6c 24 18 56.
- the view angle accumulators (64-bit rva 0x533d14 pitch, 0x533d18 yaw; native.c lines 2669-2675, 2116-2122): **32-bit VA 0x925c34 pitch, 0x925c38 yaw** (rva 0x525c34, 0x525c38), degrees as floats. Evidence: 0x578fe0 does `_DAT_00925c34 += ... * mouse_y`, `_DAT_00925c38 -= ... * mouse_x` and the same pair is passed to 0x4a7ad0 as in the 64-bit code (`DAT_140533d14` += pitch, `DAT_140533d18` -= yaw). The pair is followed in memory by what the 64-bit "0x533cb8..0x533d18" swapped range holds (mouse state at 0x925c28 / 0x925c2c = 64-bit 0x533d08 / 0x533d0c); a 32-bit context-swap range would be about 0x925bb8..0x925c3c: exact extent **unknown** (belongs to the contexts package).
- confidence: verified (accumulators, 0x578fe0, 0x4a7ad0); the swap range extent unknown

### 1.10 key catchers (menu open test) - native.c lines 2637, 2845; 64-bit rva 0x533cb0 (stride 0x10 per local client, set by 0xd1c10)
- 32-bit: VA **0x925bb8** (rva 0x525bb8), `uint[]` at stride 0x10 per client; bit 0x10 = a menu has the keys, bit 1 = console. Setter: VA 0x46d5e0 (`Key_SetCatcher(client, bits)`, 64-bit 0xd1c10), cdecl, bytes 56 8b 74 24 08 c1 e6 04 81 c6 b8 5b 92 00 e8 bd. 0x4630a0 (64-bit 0xcfc30) tests `DAT_00925bb8 & 0x10` / `& 1` the same way.
- confidence: verified

---

## 2. Rumble (src/rumble.c)

Script builtin tables in 32 bits: **12-byte entries** `{const char *name; void (*func)(); int developer;}` (64-bit: 0x18 bytes). Found by searching `image32.bin` for the dwords that point
at the builtin names (Ghidra has no data references into the table in the raw image). The entry for each rumble builtin is the 64-bit entry shifted by the same index
(PlayRumbleOnEntity is 17 entries after `unlink` in both builds: 64 0x342c88 -> 0x342e20, 32 0x6e9f04 -> 0x6e9fd0).

### 2.1 the empty builtin - 64-bit rva 0xa650, rumble.c line 17
- 32-bit: VA **0x40cf10**, rva **0xcf10**: a single `ret` (cdecl, void). It is **shared by every empty stub** (many call sites; PrecacheRumble points at it too), so the mod must compare the entry's `func` with it only for the six known entries, as it does now.
- bytes at VA 0x40cf10: c3 cc cc cc cc cc cc cc
- confidence: verified (all 6 entries below hold this value in the image)

### 2.2 Scr_GetString - 64-bit rva 0x223440 (declared `const char *Scr_GetString(int index)`), rumble.c line 138
- 32-bit: VA **0x4d5f20**, rva 0xd5f20, cdecl, 1 stack arg (index), returns `const char *` (eax), tail-jumps to 0x4e7440 (SL_ConvertToString of the string index read by 0x465ef0 from VM stack slot `index`). 64-bit decompile has the same two-call shape (0x222c40 then 0x21a370).
- bytes: 8b 44 24 04 50 e8 c6 ff f8 ff 83 c4 04 89 44 24
- confidence: likely (shape equal, not run; the 64-bit one takes the index in a register, the 32-bit one on the stack)

### 2.3 Scr_GetVector - 64-bit rva 0x223540 (`void Scr_GetVector(int index, float out[3])`), rumble.c line 160
- 32-bit: VA **0x464850**, rva 0x64850, cdecl, 2 stack args (index, float *out), returns void. It reads the VM stack at `[0x190a880 - index*8]` (type word at +4, 4 = vector, value pointer at +0) bounds-checked against `[0x190a88c]`, copies 3 floats, and raises `"type %s is not a vector"` / `"parameter %d does not exist"` via Scr_Error 0x457ae0 + the error handler 0x612f50 otherwise (the 64-bit code longjmps; same logic).
- bytes: 56 8b 74 24 08 3b 35 8c a8 90 01 73 7a 8d 04 f5
- confidence: verified (structure compared with the 64-bit decompile)

### 2.4 builtin table entries (to repoint) - 64-bit rva 0x342e20.. / 0x3c2138.. , rumble.c lines 277-282
All six entries hold `func = 0x40cf10` in the image. Entry layout: name pointer +0, func +4, developer +8. The name strings are the lowercase script names (linear case-insensitive scan in 0x44d3f0, the method table lookup, and 0x42c650, the function table lookup).

| builtin | kind | 64 entry rva | 32 entry VA | 32 rva | name string VA | bytes (12) |
|---|---|---|---|---|---|---|
| playrumbleonentity | method | 0x342e20 | 0x6e9fd0 | 0x2e9fd0 | 0x6df3c8 | c8 f3 6d 00 10 cf 40 00 00 00 00 00 |
| playrumblelooponentity | method | 0x342e38 | 0x6e9fdc | 0x2e9fdc | 0x69caa0 | a0 ca 69 00 10 cf 40 00 00 00 00 00 |
| stoprumble | method | 0x342e50 | 0x6e9fe8 | 0x2e9fe8 | 0x6b2fc4 | c4 2f 6b 00 10 cf 40 00 00 00 00 00 |
| playrumbleonposition | function | 0x3c2138 | 0x731aec | 0x331aec | 0x6ce784 | 84 e7 6c 00 10 cf 40 00 00 00 00 00 |
| playrumblelooponposition | function | 0x3c2150 | 0x731af8 | 0x331af8 | 0x6a9cc0 | c0 9c 6a 00 10 cf 40 00 00 00 00 00 |
| stopallrumbles | function | 0x3c2168 | 0x731b04 | 0x331b04 | 0x6d6218 | 18 62 6d 00 10 cf 40 00 00 00 00 00 |
| (precacherumble, left alone) | function | 0x3c1ce8 | 0x7318c4 | 0x3318c4 | 0x6c5114 | |

- Tables: methods start at VA 0x6e9e38 (152 entries, first `attach`), functions at VA 0x731450 (173 entries, first `createprintchannel`); `rumble_install` should keep its check `name == expected && func == empty`.
- **calling convention of what the table points to**: methods are cdecl `void f(unsigned entref)` with the entref on the stack, entity number in the low 16 bits and class in the high 16 (checked in `unlink` 0x5cce20: `if (param_1 >> 16 == 0) ent = 0xea76b8 + (param_1 & 0xffff) * 0x270`), so `on_entity_f(uint32_t entref)` works unchanged; functions are cdecl `void f(void)`. Both are called by the VM with the script arguments on the VM stack, read with Scr_GetString / Scr_GetVector by index (0 = the first argument), as in 64 bits.
- the position builtins read `Scr_GetString(0)` then `Scr_GetVector(1, pos)` exactly as in 64 bits (same script signature).
- confidence: verified (entries read from the image; the same index distance from `unlink` as in 64 bits)

### 2.5 entities used by rumble.c (G_ENTITIES, origin, client) - rumble.c lines 20-23, 92-100
- 32-bit: G_ENTITIES VA **0xea76b8** (rva 0xaa76b8), stride **0x270**, origin `+0xe8` (same), client pointer `+0x108` (same). See 1.8.
- XInput part (`XInputSetState`, `xinput1_4.dll`) does not depend on the build.
- confidence: verified

---

## 3. Session control (native.c lines 1600-1960, 4183-4430: players_apply, client_n_install, list_players_install, sv_add_command, coop_dvar_thread, hooks_apply, activate, native_*)

The public API (`native_install`, `native_split_start/start2/stop`, `native_split_active/available`, `native_crash_state`, `activate`, `session_reset`) holds **no address**; the console commands (`mw2coop_native_*`, `mw2coop_press`) use `game_add_command` / `game_cbuf_addtext`, which exist in the 32-bit `game.c` profile. What follows are the addresses `players_apply`, `hooks_apply` and the coop dvar thread need.

### 3.1 the local player count and its store - 64-bit rva 0x3c3eb0 (RVA_LOCAL_PLAYERS), store at rva 0x1714a6 (RVA_LOCAL_PLAYERS_STORE) in G_InitGame (0x171370)
- 32-bit: count **VA 0x738c6c (rva 0x338c6c)**, int. G_InitGame = VA **0x4bf8b0**. The store is the **same instruction**: VA **0x4bf9db, rva 0xbf9db**, `c7 05 6c 8c 73 00 01 00 00 00` = `mov dword [0x738c6c], 1`, right after `cmp byte [0x88897c], 0` (`80 3d 7c 89 88 00 00`) and before the `jz`. `players_apply`'s check (`at[0]==0xc7, at[1]==0x05, at[6]==1, at[7..9]==0`) and the patch of `at[6]` work unchanged. The co-op branch writes the count at 0x4bf9ec (`a3 6c 8c 73 00` = `mov [0x738c6c], eax`, eax = `[0x1a7f0f8]`).
- bytes at VA 0x4bf9d4 (from the cmp): 80 3d 7c 89 88 00 00 c7 05 6c 8c 73 00 01 00 00
- engine co-op byte (64-bit 0x52fb5c, RVA_COOP_BYTE): **VA 0x88897c (rva 0x48897c)**.
- confidence: verified (G_InitGame compared instruction by instruction)

### 3.2 client reads of the count (client_n_sites) - native.c lines 1766-1830
In 32 bits every access is `cmp/mov reg, [abs32 0x738c6c]`: the operand is an **absolute address** inside the instruction (no rip-relative displacement, no range limit). `client_n_install` writes the address of a static `int = 1` of the DLL into the 4 operand bytes; no `hook_page_near`. The operand starts at `+1` for `a1 <abs32>` (mov eax) and at `+2` for `8b/3b <modrm> <abs32>`.
The 64-bit list (35 sites in 23 functions of the cgame / cl / ui code, rva 0x6bae9 .. 0xd5595) cannot be copied: the layouts differ. I mapped each 64-bit client function to its 32-bit one (the matcher's guesses in `map.txt` plus the same loop shape) and listed the instructions of the 32-bit function that read 0x738c6c. **Sites (24, in 13 functions)**: site VA = the start of the reading instruction.

| 32 function | 64 function | sites (VA; operand at +2 unless noted) |
|---|---|---|
| 0x4818b0 num clients | 0x6bae0 | 0x4818b9 (operand at +1) |
| 0x43a910 | 0x85f20 | 0x43a926 |
| 0x433f30 (HUD ammo counters) | 0xa6680 | 0x433f9a, 0x434159, 0x4341eb, 0x4342b4, 0x434327, 0x434378 |
| 0x4b74e0 | 0xa9240 | 0x4b74ed |
| 0x454c40 | 0xa92b0 | 0x454cb9 |
| 0x565300 | 0xa8b90 | 0x56558b, 0x5656be |
| 0x497840 | 0xb6050 | 0x4978b4, 0x497a44, 0x497ac4 |
| 0x4818e0 | 0xb71b0 | 0x48190a |
| 0x56e3d0 | 0xb8d80 | 0x56e414 |
| 0x471a10 | 0xc95f0 | 0x471a64, 0x471ae4, 0x471b47 |
| 0x428630 | 0xca0f0 | 0x428666, 0x4286ba |
| 0x430860 | 0xca600 | 0x430868 |
| 0x479040 | 0xd5510 | 0x4790ab |

Probable client helpers (the same shape as the 64-bit client reads, `i < 2 && (coop ? slot.type == 2 : i < count)`, selecting the cgame's per-client state): 0x4893c0 (site 0x4893d2), 0x4b5050 (0x4b5062), 0x4e7620 (0x4e7632), 0x416850 (0x41685e): **likely**, not matched to a 64-bit function.
Instruction bytes at the sites: `3b 3d|3b 2d|3b 1d|3b 15|3b 05|3b 0d 6c 8c 73 00` (cmp reg,[abs]), `8b 35|8b 15 6c 8c 73 00`, `a1 6c 8c 73 00` (0x4818b9).
**Gap:** the 64-bit client functions 0x89cf0, 0x93ac0 (COOP_WAITINGFORPLAYER), 0x9f9a0, 0xb2130, 0xbca00, 0xc2820, 0xc3e50, 0xc3ea0, 0xc99c0, 0xd2270 have no verified 32-bit counterpart that reads 0x738c6c: either they call 0x4818b0 / 0x4818e0 / 0x416850 / 0x4893c0 instead of reading it, or they are among the other reader functions I could not classify with confidence: 0x59d850, 0x432800, 0x4014a0, 0x5c5c00, 0x55b710, 0x4b5580, 0x4da540, 0x55ea00, 0x570bb0, 0x59a390, 0x5e2710 (each loops over the local clients). Server / game / party readers that must keep 2 (identified by what they do or their strings: SV_SendServerCommand 0x46ce50, SV_AddServerCommand 0x40a550, 0x4e8360, 0x49cff0, 0x42d110 (snapshot loop), 0x418c40 (coopLaunch), 0x5c6530 / 0x5c6d20 (actors), 0x408820 (G_LoadGame), 0x4d6730, 0x4a9730, 0x4d3f40, 0x501e40, 0x4e7c40 (SVC_DirectConnect), 0x482b90 (map load), 0x42aed0, 0x4ae660 (godmode), 0x4bf8b0 (G_InitGame), the 0x659f30-0x65cc10 party / net code).
**Alternative that needs no site list:** the count is a plain int and the contexts package already runs client code in controlled places (`run_both`, the swaps): write `1` to 0x738c6c for the duration of each client-side call and restore the real count afterwards (the server step, G_RunFrame, SV_ and the snapshot builder keep seeing 2). Not tried; the site patch is what the 64-bit build does.
- confidence: verified for the 24 sites (instruction bytes and function role compared), likely for the 4 helpers, unknown for the rest

### 3.3 entity list exclusion of the local players - 64-bit rva 0x23459a (`list_players_install`)
- Same finding as `clientctx.md`, found independently: **VA 0x65cc10 (rva 0x25cc10)**, `8b 2d 6c 8c 73 00` (`mov ebp, [0x738c6c]`, operand at +2), in 0x65cbf0, the first half of the snapshot builder (called from 0x65cc90 at 0x65cc9a; 0x42d110 calls 0x65cc90; 64-bit 0x234540 is called from 0x2349a0). Same logic: `if (i < 2 && (coop ? slot.type == 2 : i < count)) skip`. In 32 bits the patch writes the address of a zero variable into the 4 operand bytes.
- confidence: verified

### 3.4 SV_AddServerCommand (the `sv_add_command` replacement) - 64-bit rva 0x232c30, RVA_SV_ADD_COMMAND
- 32-bit: VA **0x40a550, rva 0xa550**, **cdecl `(client_t *cl, const char *text)`**, plain `ret`; both callers (0x46ce8c and 0x46cebb, in SV_SendServerCommand 0x46ce50) do `add esp, 8`. Bytes: 56 8b 74 24 08 8b ce 81 e9 88 39 b2 01 b8 bd ab. `push esi` + `mov esi,[esp+8]` are exactly 5 bytes, so one clean 5-byte jump (the function is replaced entirely, no trampoline).
- client_t array: base **VA 0x1b23988 (rva 0x1723988)**, stride **0x1674** (64-bit 0x19ada98, 0x1678): `idx = (cl - 0x1b23988) / 0x1674`. Field offsets are identical to the 64-bit ones: +4 write offset, +8 sequence, +0xc acknowledge, text at +0x10 (0x1000 bytes), offset table at **+0x1010** + (n & 0x7f) * 4. The state word `[cl]` is 4 for a connected client (0x46ce50 tests `cmp [esi], 4`).
- engine co-op slot table: `[0x88f560 + idx * 0x263c] == 2` (64-bit: 0x540bb0, stride 0x2650, int type at +0): RVA_COOP_SLOTS(32) = 0x48f560, stride 0x263c.
- Com_Error: VA **0x4584f0, rva 0x584f0**, cdecl `void (int code, const char *fmt, ...)`; bytes e8 9b ba 01 00 84 c0 74 0a e8 72 6e (64-bit 0x1fb5f0). The 32-bit original also calls `0x45d4a0(cl)` (a dump of the client's command buffer) before Com_Error; not needed.
- evidence: decompile of 0x40a550 against 0x232c30 (the same limits 0x80 commands / 0x1000 bytes, the same '%' to '.' replacement, the same strings).
- confidence: verified

### 3.5 the `coop` dvar written by coop_dvar_thread - 64-bit `dvar_t *` at rva 0x1905308; the engine's co-op reset byte at 0x1905302
- 32-bit: the `dvar_t *` for `coop` is at **VA 0x1a7f0ec (rva 0x167f0ec)** (set by 0x4a6fe0: `Dvar_RegisterBool("coop", 0, 0x2080, ...)` then `mov [0x1a7f0ec], eax` at 0x4a71d5; flags 0x2080 as in 64 bits, 0x230ea7). The byte the engine clears with it (64-bit 0x1905302): **VA 0x1a7f0d9 (rva 0x167f0d9)**. The 32-bit `dvar_t` current value is at +0x10 (game.h), so `*(int *)(dvar + 0x10) = 1` is unchanged.
- bytes at VA 0x4a71c3: 68 a8 e9 6d 00 a3 08 f2 a7 01 e8 de f4 fd ff 83 c4 48 a3 ec f0 a7 01 c3
- evidence: the co-op reset function 0x4090c0 touches exactly these three variables in the same order as the 64-bit 0x22dda0 (`DAT_14052fb5c`, `DAT_141905302`, `DAT_141905308`) -> (0x88897c, 0x1a7f0d9, [0x1a7f0ec]).
- confidence: verified

### 3.6 hooks of hooks_apply
- RVA_BEGIN_FRAME 0xc840, the scene 2D call 0x5c5bf, the frame 2D call 0x4545b: see **render.md** (call sites VA 0x5536fc and 0x5537ae). RVA_CG_DRAW 0xbca00: see **clientctx.md** (VA 0x4666c0, cdecl, 3 int args, bytes `83 ec 0c 53 55 33 db 53 e8 33 0e 0a 00 53 e8 5d`).
- Cbuf_AddText (`logcbuf`, 64-bit 0x1ee890): VA **0x409b40, rva 0x9b40**, cdecl `(int localClient, const char *text)`, bytes `6a 21 e8 39 83 08 00 8b 44 24 0c 8a 08 83 c4 04`. **`push 0x21` + a call are 7 bytes: a 5-byte hook must copy both instructions and relocate the call.**
- confidence: verified (the render / cg ones by those files)

---

## 4. scrdebug.c, memdump.c and the QA method tracing

32-bit state today: `src/x86/scrdebug32.c` logs the error message only (a hook on the error handler); `scrdebug_note_source` and `scrdebug_trace_method` are stubs (`src/x86/stubs32.c`). What each missing part needs, with the 32-bit addresses found:

### 4.1 error path (already in scrdebug32.c, re-verified) - 64-bit Scr_Error rva 0x2226a0
- 32-bit Scr_Error: VA 0x457ae0 (cdecl `(const char *msg)`; copies the message into the 1 KB buffer at 0x190ec18 when `[0x18e3f90] == 0`, then calls the handler). Error handler VA **0x612f50** (`void (void)`: tests `[0x18e3f8f]` (flag A) and `[0x1588a34]` (flag B); then `if ([0x190a878] || [0x190a884]) longjmp(0x190a010 + [0x190a00c] * 0x40, -1)`, else `0x424ec0(fmt 0x6c880c, [0x18e3f90])`). Bytes: handler 80 3d 8f 3f 8e 01 00 75 30 80 3d 34 8a 58 01 00; Scr_Error 83 3d 90 3f 8e 01 00 75 21 8b 44 24 04 68 00 04. The mod keeps hooking the handler (the 64-bit code replaces Scr_Error and calls the CRT longjmp itself; not needed in 32 bits).
- mapping to the 64-bit scrdebug.c names: flag A 0x15a038b <-> 0x18e3f8f, flag B 0x1463bce <-> 0x1588a34, depth 0x18f44a0 <-> 0x190a878, jmpbuf index 0x18fca44 <-> 0x190a00c, jmpbufs 0x18fca50 (0x100 bytes each) <-> 0x190a010 (**0x40 bytes each**).
- confidence: verified

### 4.2 the VM state for call stacks (scrdebug.c `log_stack` / `describe_pos`)
| what | 64-bit rva | 32-bit VA | 32 rva | notes |
|---|---|---|---|---|
| live code position (`const char *`) | 0x1900b78 | **0x190a854** | 0x150a854 | the interpreter (VA 0x613be0, 64-bit 0x224910) stores `DAT_0190a854 = pos` at each opcode, as 64-bit `DAT_141900b78 = pos` |
| function_count (call depth) | 0x18f44a0 | **0x190a878** | 0x150a878 | incremented at the call opcode, decremented at return; zeroed by Scr_Init 0x612e20 |
| function_frame_start[] | 0x18f44c0, stride 0x28 | **0x190a890, stride 0x18** | 0x150a890 | the call opcode does `*(ptr) = saved pos; *(ptr+8) = local count; ptr += 0x18; depth++` (pointer variable `[0x190a87c]`, initial value 0x190a890 from 0x612e20); the saved position is the first dword |
| program buffer (`const char *` variable) | 0x15a03e0 | **0x18e3fe0** | 0x14e3fe0 | 0x5018f0 subtracts it |
| program size | 0x1463bc8 | 0x1588a40 | 0x1188a40 | bound in Scr_GetFunctionHandle |
- confidence: verified for position / depth / program buffer / size; likely for the frame array base and stride (the push code was read; the base comes from Scr_Init's initial pointer)

### 4.3 Scr_GetFunctionHandle - 64-bit rva 0x216920 `int (const char *file, const char *name)`
- 32-bit: VA **0x5018f0, rva 0x1018f0**, cdecl, 2 stack args (file, name); returns the function's offset in the program buffer or 0: `((off - [0x18e3fe0]) < [0x1588a40]) ? off - [0x18e3fe0] : 0`. Bytes: 8b 44 24 04 83 ec 08 56 57 50 e8 61 41 f4 ff 8b. It looks the file up in the loaded-scripts table `[0x1568a14]` (64-bit `DAT_141443b8c`), so it works only before the table is freed (4.4).
- confidence: verified (same call sequence and table slot; 64-bit slots 0x1443b88.. <-> 32-bit 0x1568a10..0x1568a24)

### 4.4 Scr_EndLoadScripts - 64-bit rva 0x2166d0 (first call 0x21a3b0)
- 32-bit: VA **0x490180, rva 0x90180**, cdecl void. Bytes: 53 6a 02 e8 c8 2f 02 00 a1 10 8a 56 01 33 db 50 (`push ebx; push 2; call 0x4b3150; mov eax,[0x1568a10]`). It then clears flag B (`[0x1588a34] = 0`) and frees the six slots 0x1568a10..0x1568a24, like the 64-bit function (`DAT_141463bce = 0`, frees 0x1443b88..9c). The 64-bit function has an extra first call (0x21a3b0) that the 32-bit one does not.
- hook options: the first three instructions are 8 bytes and the third is a call (rel32), so a 5-byte jump needs a trampoline that copies `push ebx; push 2`, re-issues `call 0x4b3150` as an absolute call, and continues at **0x490188**; or hook the two callers: **0x40f921** (in 0x40f910) and **0x4171dd** (in 0x4170c0), both `call 0x490180`.
- confidence: verified

### 4.5 va() and the TLS block (the 64-bit `log_error` reads the last two formatted messages)
- not needed in 32 bits: the message is already in `[0x18e3f90]`. For completeness: va = VA 0x48a5d0 (cdecl, varargs), the block = `Sys_GetValue(1)` VA 0x4faf90 (reads fs:[0x2c]), a ring of two 0x400-byte slots at block+0 with the index at block+0x800: the same layout as 64 bits (0x273440, 0x203700).
- confidence: verified

### 4.6 scrdebug_note_source(ext, text, len)
- no address: pure text scanning. Its caller is the `Scr_AddSourceBuffer` hook of `scripts.c` (VA 0x47f8e0, docs/32bit-plan.md), which already exists in the 32-bit build; it only needs the real scrdebug.c to be linked.
- confidence: verified (by reading)

### 4.7 scrdebug_trace_method (QA `!trace <rva>`)
- the method table entry layout differs: 12-byte `{name +0, func +4, developer +8}` (64-bit: `func` at +8 of 0x18 bytes). `trace_wrap` takes `uint32_t entref` (cdecl, on the stack), same meaning (low 16 bits entity, high 16 class). Example entry: `unlink` = **VA 0x6e9f04** (rva 0x2e9f04), its function VA 0x5cce20 (64-bit entry rva 0x342c88). The sanity checks `name < base` work as they are.
- confidence: verified

### 4.8 memdump.c (QA dump commands)
Compiled in 32 bits today: `!asset`, `!font` (32-bit layouts already in the source), `!rva`, `!mem`. `DB_FindXAssetHeader` VA 0x4f6f10 and `DB_IsXAssetDefault` VA 0x4c32b0 are already in `game.h`. Still 64-bit only; what 32 bits would need:
- `!bones` / `!surfs`: the 32-bit XModel / XSurface layouts are the ones `transplant.c` already uses on 32 bits (its `M_*`, `L_*`, `S_*` macros, `#else` branch near line 67); reuse them instead of the 64-bit offsets written in memdump.c.
- `!weapons`: WeaponDef pointer table **VA 0x1a69d08**, 4-byte entries (64-bit 0x3ee320, 8-byte entries). The WeaponCompleteDef table (64-bit 0x3edce0): **unknown** (not found; the dumped image has these tables empty). The ints at WeaponDef +0x40..+0x64 are 64-bit offsets: 32-bit **unknown** (the aim fields are at +0x2e0/+0x2e4/+0x2e8, 1.3).
- `!menu` / `!menus`: the menuDef_t / itemDef_t layouts and the `uiContext` (64-bit 0x1a3ab00: menus +0x38, count +0x1438) in 32 bits: **unknown** (not searched).
- expression function names (64-bit table 0x3c4b38, 8-byte entries, 169 names): **VA 0x7332ec** (rva 0x3332ec), 4-byte entries (`int`, `string`, `float`, `sin`, `cos`, `min`, `max`, `milliseconds`, `dvarint`, `dvarbool`, ...). Found by searching the image for the pointer run to those strings. Bytes at VA 0x7332ec: 24 dd 6a 00 c8 9f 6d 00 (pointers to "int" and "string"). The operator numbering (the first function is 27) belongs to the menu parser and was not re-checked in 32 bits.
- confidence: verified (names table, weapon def table, the existing 32-bit font code); unknown for the rest

---

## 5. steamnet.c (Steam P2P relay for Steam joins)

The 32-bit exe imports `steam_api.dll` (Valve's 32-bit one) with these names, read from the PE import table of the image: `SteamUser, SteamFriends, SteamMatchmaking, SteamNetworking, SteamRemoteStorage, SteamAPI_RegisterCallResult, SteamAPI_UnregisterCallResult, SteamAPI_UnregisterCallback, SteamAPI_RegisterCallback, SteamAPI_RunCallbacks, SteamUtils, SteamAPI_Init, SteamAPI_Shutdown, SteamUserStats`. IAT slots (VA; they hold the resolved function pointers at run time): SteamUser 0x691520, **SteamFriends 0x691524, SteamNetworking 0x69152c, SteamAPI_RegisterCallback 0x691540**. So `GetModuleHandleA("steam_api.dll")` + `GetProcAddress` of `SteamNetworking`, `SteamFriends`, `SteamAPI_RegisterCallback` works as in 64 bits (flat cdecl exports returning the interface pointers), or the IAT slots can be called directly.

### 5.1 what the 64-bit code takes from the exe
Only the Steam join function (`steam_join_bytes` at rva 0x22dda0) and what it calls: 0xd7cb0 PartyClient_Join, 0xd7dd0 Party_Reset, 0x26d800 Dvar_SetBool, bytes 0x52fb5c and 0x1905302, dvar 0x1905308. The rest goes through steam_api64.dll.

### 5.2 the join hook - 64-bit rva 0x22dda0 `steam_join(localClient, hostInfo, ip_out, port_out)`
- 32-bit: VA **0x4090c0, rva 0x90c0**, **cdecl, 4 stack args** `(int localClient, hostInfo *, int *ip, short *port)`. It ends in `jmp 0x42fc40` (PartyClient_Join) with the same stack arguments (a tail call): the same shape as 64 bits. Its only caller is 0x6380e0 (at 0x638106), which the Steam lobby join 0x638210 (64-bit 0x28f1b0) calls. The `hostInfo` layout (+8 internal IPv4, +0xc external, +0x10/+0x12 ports) is the one `party.c` already uses in the 32-bit build.
- bytes at VA 0x4090c0 (14, a clean cut: cmp [abs],0 / jz / push imm): 80 3d 7c 89 88 00 00 74 3b 68 10 a7 6b 00. A 5-byte jump over the first instruction (7 bytes) is enough because `steam_join` repeats the whole original body.
- the original body: `if ([0x88897c]) { [0x88897c] = 0; [0x1a7f0d9] = 0; Dvar_SetBool([0x1a7f0ec], 0); Party_Reset(); } return PartyClient_Join(...)` with Dvar_SetBool = VA **0x44d0f0** (cdecl `(dvar_t *, int)`, bytes 8b 44 24 08 8b 4c 24 04 6a 00 50 51 e8 ff cc 07; 64-bit 0x26d800), Party_Reset = VA **0x42f0f0** (void; game.h `RVA_PARTY_RESET` 0x2f0f0; 64-bit 0xd7dd0), PartyClient_Join = VA **0x42fc40** (cdecl, 4 args, bytes 8b 44 24 04 56 57 50 e8 04 51 08 00 8b f0 e8 9d; game.h `RVA_PARTY_JOIN`).
- confidence: verified (the body compared with the 64-bit decompile)

### 5.3 the Steam P2P interface in the 32-bit build: differences that break the 64-bit design
Observed in the exe's own users of `SteamNetworking()` (0x4ef290 send, 0x4daa10 receive loop, 0x4b4d90 session request callback):
- **no channel argument.** The exe calls `SendP2PPacket(steamID (2 dwords), data, size, EP2PSend = 2)` (5 stack dwords, `k_EP2PSendReliable`), `IsP2PPacketAvailable(&size)` (1 arg), `ReadP2PPacket(buf, cap, &msgSize, ...)`, `AcceptP2PSessionWithUser(steamID)` (2 dwords). It is the early interface (`ISteamNetworking001`: vtable 0 SendP2PPacket, 1 IsP2PPacketAvailable, 2 ReadP2PPacket, 3 AcceptP2PSessionWithUser, the same first four slots the 64-bit code uses) **without the `nChannel` parameter**: `CHANNEL 7` cannot be used, the relay's packets and the game's own address-swap messages share one channel.
- the game drains the queue itself: 0x4daa10 (`while (IsP2PPacketAvailable) { ReadP2PPacket; handle }`, no arguments, bytes 83 ec 10 53 57 8b 3d 2c 15 69 00 33 db 89 5c 24) reads every pending packet and feeds it to its message parser 0x66b330, so relay packets would be eaten by the game. A relay needs a payload tag and a hook on 0x4daa10 that pulls its own packets first, or another transport. (Who calls 0x4daa10 per frame: not traced.)
- **the calling convention is `thiscall`**: `this` in ECX, arguments on the stack, the callee cleans them (the exe: `mov ecx, eax; call [edx+0xc]`, `ret 4`). A `uint64` (CSteamID) is passed as **two stack dwords** (low, high). In GCC use `__attribute__((thiscall))` (or fastcall with a dummy edx argument) for every vtable call, not a plain function pointer.
- index 7 (`AllowP2PPacketRelay`): the exe never calls it; whether the 32-bit interface has it at index 7: **unknown**.
- `GetFriendRelationship(steamID)` (`steam_friend`, ISteamFriends vtable index 5): the exe uses other indices of `SteamFriends()` (0 = persona name in 0x4d5c30; 19 = the overlay call with "LobbyInvite" in 0x4e5a40). Index 5 is the same in old and new interfaces: **likely**, not observed in the exe.
- the exe already registers a callback for **id 0x4b2 = 1202 (P2PSessionRequest_t)**: handler VA **0x4b4d90** (thiscall `(this, P2PSessionRequest_t *)`, `ret 4`; bytes 56 ff 15 2c 15 69 00 8b 4c 24 08 8b 71 04 8b 09) which calls `AcceptP2PSessionWithUser(remote)` for **every** request. So the 32-bit game already accepts P2P sessions from anyone (no friends-only check); the mod's own callback could not reject (both run). Registering a second callback for the same id: untested.
- callback object layout for `SteamAPI_RegisterCallback(obj, id)`, read from the exe's own objects (0x4b4ec0 builds them): `+0 vtable, +4 uint8 flags, +8 int id` (64-bit steamnet.c: flags +8, id +0xc); a CCallback also has `+0xc m_pObj, +0x10 m_Func`. **The vtable order is `{ Run(void *param, bool ioFailure, uint64 call) [thiscall, ret 0x10], Run(void *param) [thiscall, ret 4], GetCallbackSizeBytes() [thiscall] }`**, observed in the vtable of callback type 0x4b2 at VA 0x6d6a90: slot 0 = 0x472970 (`ret 0x10`), slot 1 = 0x435bb0 (jumps through m_Func), slot 2 = 0x4fc250 (`mov eax, 8; ret`, the size of P2PSessionRequest_t). The 64-bit steamnet.c declares `{Run(param), Run(param, io, call), size}`; MSVC lays overloaded virtuals out in reverse order, so **the 64-bit callback may have its first two slots swapped as well**: worth checking in the 64-bit exe's vtable of a `CCallback<P2PSessionRequest_t>`.
- confidence: verified (argument counts, no channel, thiscall, vtable order, callback layout read in the code); unknown / likely where marked

### 5.4 steamnet_send / steamnet_receive in the rest of the mod
The call sites in `net.c` / `proxy.c` (winsock `sendto` / `recvfrom`) are the same in both builds; nothing in the exe is needed for them.

---

## 6. What has no 32-bit counterpart or was not found (summary)

- `aa_dvar` / `padopts.c` aim dvar loop: the contiguous range does not exist in 32 bits (use the 38-entry table in 1.1).
- weapon def aim fields at +0x42c / +0x430 / +0x428: different offsets in 32 bits (+0x2e4 / +0x2e8 / +0x2e0).
- `memdump.c` `!menu`, `!menus`, `!weapons` (WeaponCompleteDef table, the int fields): unknown; `!bones` / `!surfs`: reuse transplant.c (4.8).
- steamnet: no channel parameter (the relay's channel 7 does not exist), the game drains the P2P queue itself (0x4daa10), the callback vtable order, all interface calls are thiscall with two-dword Steam ids.
- scrdebug: the function frame array base and stride are likely, not verified by running; Scr_EndLoadScripts has no equivalent of the first call 0x21a3b0.
- `client_n_sites`: 24 verified sites of about 35; the rest unknown (3.2).
- Not searched (outside this package): the playerstate fields that feed aaGlob: 0x4ae500 copies ps+0x28, +0x2c, +0x30, +0xac, +0x14, +0xc, +0x2bc, +0x1f8, +0x2c0 into aa+0x00..+0x20 and derives the weapon at aa+0x24 (from ps+0x16c / 0x41fbf0(ps)); the cg.ps view angle fields belong to clientctx.md / entities.md.
