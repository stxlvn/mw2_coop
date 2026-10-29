# 32-bit map: the second client context and the frame (package clientctx)

32-bit official build (`FC9E4843...52D7`, image base 0x400000). VA = rva + 0x400000. Ghidra 32-bit project: addresses are VAs. Bytes are from `image32.bin`.
Method: every 64-bit function was decompiled with Ghidra next to its 32-bit candidate (same calls in the same order, same constants); data was matched by the
functions that touch it (a table of "globals touched by function F" in both builds) and by structure strides. "observed" = read in a decompile or disassembly,
"derived" = computed from observed values.

Status: static analysis only (nothing was run). Verified = both decompiles compared; likely = paired by call graph / global footprint but not line by line; unknown = not found.
Open items (all marked in the text): exact lower edge of cgame range 0a; the aim_* dvar pointers; the 10 "likely" function pairs of the 35 client reads; the 0x18 extra bytes between the cls state word and the cl struct.
Two observations about the 64-bit code found on the way: (1) range 0 stops 135 entities into the 2048-entry cg_entities array; (2) `native.c` copies 0x30 bytes of msg_t, which is the 64-bit size only.

## Layout differences that matter for the whole port (observed unless marked)

- **Usercmd ring: 64 entries in 32 bits, 128 in 64 bits.** `usercmd` is 0x40 bytes in both. 32-bit code masks with `& 0x3f` (0x4329e0, 0x4c6f30, 0x579bb0), 64-bit with `& 0x7f`.
  So in 32 bits: ring = 0x1000 bytes, `cmdNumber` at cl + 0x1000, the "number sent" at cl + 0x1004, the playerstate at cl + 0x1018 (64: 0x2000 / 0x2000 / 0x2004 / 0x2018).
  `native.c` lines 3103-3111 (`(n & 127) * 0x40`, `+0x2000`, `+0x2004`) and `CL_BLOCK_PS 0x2018` need the 32-bit values (mask 63, 0x1000, 0x1004, 0x1018).
- **cl block stride: 0xbe9c (32) against 0xce9c (64)** (the 0x1000 difference is the ring). Playerstate size `PS_SIZE` = **0xaca4 in both** (memcpy size in 0x481170, observed).
- **clc (reliable ring) stride 0x3218 in both.** Its layout is the same: a header of 3 words (0xc bytes: one word, then two counters at +4 / +8), 0x2000 bytes of 128 x 0x40 command ring (+0xc), then the server commands (see below). Note the ring here is 128 deep in both builds (only the cl ring is 64 deep in 32 bits).
- **The order of the statics is different**: in 64 bits the coop slot table is before `clc`; in 32 bits `clc` comes first and the slot table follows it (clc[0] = 0x88c330..0x88f548, slot table type field at 0x88f560). The cgame's statics are in a different order too (see the cgame block).
- Pointers are 4 bytes, so every table of pointers (media handles, dvar pointers) is half the size and the offsets inside such tables halve.
- **The 32-bit engine passes some arguments in registers** (see make_cmd below, usercall); everything else listed here is plain cdecl.

## The frame

### Client frame, throttle - 64-bit rva 0xd5b00, called at rva 0x1fc331 (native.c line 4109, `client_frame_hook`)
- 32-bit: function VA 0x4c3660 (rva 0xc3660), `void (void)`, cdecl, no arguments. **Call site to patch: VA 0x496dd7 (rva 0x96dd7)**, inside Com_Frame VA 0x496a90 (64: 0x1fbde0).
- bytes at the call site 0x496dd7: `e8 84 c8 02 00 e8 2f 61 f7 ff 8b 15 dc a0 45 01` (5-byte relative call, target 0x4c3660).
- bytes at the function 0x4c3660: `80 3d e0 1e 93 00 00 75 59 56 8b 35 c0 5b 92 00`.
- the second caller of the function, VA 0x539a1f (`e8 3c 9c f8 ff e8 87 47 f1 ff 2b f0 83 c6 21 3b`), is in the loop of VA 0x5398e0 that runs while a loading screen is up
  (64: rva 0x45770, call at 0x458ce); neither build hooks it.
- evidence: both decompiles are the same function: cls state compared with 6 and `5 < state`, the 0x10 / 0x21 ms throttle (`(state < 4) - 1 & 0x11) + 0x10` = 0x10 or 0x21), a busy flag
  (64: 0x5db410, 32: 0x931ee0), then one call to the one-client-frame function (64: 0xd5890, 32: 0x57adc0).
- confidence: verified

### One client frame - 64-bit rva 0xd5890 (not hooked)
- 32-bit: VA 0x57adc0 (rva 0x17adc0), void(void). Same sequence (update, `cls == 6` branch, the cgame draw `0x4666c0(0,0,0)`, key catcher check `0x4b2140(0, 0x10)`, then the switch part in 0x57ac90).
- confidence: likely

### The cgame's draw-active-frame - 64-bit rva 0xbca00 (RVA_CG_DRAW, hooked in hooks_apply)
- 32-bit: VA 0x4666c0 (rva 0x66c0), called as `(0, 0, 0)` from 0x57adc0 like 64 `FUN_1400bca00(0,0,0)`. First 16 bytes: `83 ec 0c 53 55 33 db 53 e8 33 0e 0a 00 53 e8 5d`. cdecl (three int arguments). 
- confidence: verified for the address and the call (the call site in 0x57adc0 is `push eax; push eax; push eax; call 0x4666c0` with eax = 0 and the callee reads three stack words); the body was not compared line by line

### Client frame wrapper that runs the simulation step - 64-bit rva 0xbc1a0 (calls 0xb8d30), VA 0x44a530 in 32 bits
- 64: `0xbc1a0(a, b, c)` stores a at 0x51dca8, c at 0x51dcb4, b at 0x51dcb8, calls 0xb8d30. 32: VA 0x44a530 `(a, b, c)` stores 0x86a5b4, 0x86a5c4 (b), 0x86a5c0 (c), then calls 0x459d50. Called from 0x57adc0
  with `(DAT_01b2ac7c, -(x != 0) & 2, DAT_00894340)` (64: `(DAT_1419ada80, ..., DAT_140548bb8)`).
- confidence: verified (stores and call match)

### Server step - 64-bit rva 0x2343a0, called at rva 0xb8d50 from 0xb8d30 (native.c line 4132, `server_step_hook`)
- 32-bit: function VA 0x455470 (rva 0x55470), `void (void)`, cdecl. **Call site: VA 0x459d6c (rva 0x59d6c)**, inside VA 0x459d50 (64: 0xb8d30; 32 calls 0x455470 then 0x41a3a0 (= 64 0xb9320 entity cleanup) then 0x56e840 (= 64 0xb8ad0)).
- bytes at the call site: `e8 ff b6 ff ff e8 2a 06 fc ff e8 c5 4a 11 00 c7`. bytes at the function: `c7 05 84 33 b1 01 00 00 00 00 e8 d1 2d 09 00 e8`.
- evidence: 64 0x2343a0 = `flag=0; f1(); f2(); snapshot_take(); f4(); two flags = 0; if (!check && !dvar) final()`. 32 0x455470 = `flag=0; 0x4e8250(); 0x42d110(); 0x4da540(); 0x4f1c00(); two flags=0; if (!0x49cb80() && !dvar) 0x65c710()` (the same shape, 4 calls, tail call).
  The callee order is: 64 0x234260 / 0x2349a0 (server commands) / 0xc99c0 (snapshot take) / 0x233930; 32 0x4e8250 / 0x42d110 (server commands) / 0x4da540 (snapshot take) / 0x4f1c00.
- confidence: verified

## cg_init_all and the per-client init

### cg_init_all - 64-bit rva 0xca0f0, called at rva 0x2336c7 (native.c line 4103, `cg_init_all_hook`)
- 32-bit: function VA 0x428630 (rva 0x28630), `void (void)`, cdecl. **Call site: VA 0x47af60 (rva 0x7af60)**, inside VA 0x47af40 (64: 0x2336a0, 60 bytes: three flags = 0, `0x22d180()`, `[0x1419b48c0] = 2`, `cg_init_all()`, store of `0x171360()`).
- bytes at the call site: `e8 cb d6 fa ff e8 96 97 07 00 a3 10 13 b2 01 c3`. bytes at the function: `a1 7c ac b2 01 53 56 57 50 6a 00 e8 a0 df 07 00`.
- evidence: 64 0xca0f0 = `0xccdc0(0, X); 0xb8940(); loop slots: 0xb8830(lc); 0xb8920(); loop slots: 0xb87b0(lc); flag = 1`. 32 0x428630 = `0x4a65e0(0, DAT_01b2ac7c); 0x4ea1b0(); loop: 0x41dfa0(lc); 0x49cb60(); loop: 0x4956e0(lc); [0x925b8d] = 1`.
- confidence: verified

### The two per-client init parts that `ctx_after_draw` calls - 64-bit `0xb8830(lc)` and `0xb87b0(lc)` (native.c lines 2127-2128)
- 32-bit: `0xb8830(lc)` = **VA 0x41dfa0** (rva 0x1dfa0), `void (int lc)`, cdecl (bytes `d9 ee 56 8b 74 24 08 6a 01 51 d9 1c 24 56 e8 4d`); it calls 0x4a5900(lc, 0.0f, 1) (= 64 0xb8000, snapshot take for one client) and 0x56e6b0(lc) (the rest of 64's 0xb8830 body, compiled as a function of its own in 32 bits: `memset(0x86b180, 0, 0x100)`, cg time stores...).
  `0xb87b0(lc)` = **VA 0x4956e0** (rva 0x956e0), `void (int lc)`, cdecl (bytes `56 8b 74 24 08 56 e8 25 07 fb ff 56 e8 df d5 f9`); same calls: 0x445e10 (= 0xb90d0), 0x432cd0(lc) (= 0xbd610 / 0xb8a20 pair), 0x56e8c0 (the playerstate copy, "spawned" print), ...
- evidence: 64 0xb8830 / 0xb87b0 decompiles against 32 0x56e6b0 / 0x56e8c0 / 0x4956e0 (same stores, the `memset 0x100`, the same strings "spawned: %.1f %.1f %.1f").
- confidence: verified for the structure; `0x41dfa0` as a drop-in for `0xb8830(0)` is likely (it is the exact function cg_init_all loops with)

### Snapshot take (the server-to-client hand-over, both clients) - 64-bit rva 0xc99c0, called at rva 0x2343b8 (native.c line 4104, `snapshot_take_hook`)
- 32-bit: function VA 0x4da540 (rva 0xda540), `void (void)`, cdecl. **Call site: VA 0x455484 (rva 0x55484)**, inside 0x455470 (the third call).
- bytes at the call site: `e8 b7 50 08 00 e8 72 c7 09 00 c7 05 8c 33 b1 01`.
- evidence: 64 0xc99c0 = `0x233690() * 0.001f; 0xb8300(time, 1); loop slots: 0xb8000(lc, time, 1)`. 32 0x4da540 = `0x499b00() * float; 0x4c1c10(time, 1); loop slots: 0x4a5900(lc, time, 1)` (0x4c1c10 = 0xb8300; 0x4a5900 = 0xb8000).
- confidence: verified

## The server-to-client hand-over (hooks in `contexts_install`)

### ps publish - 64-bit `0xd5690(lc, ps)`, call site rva 0x2346ca (native.c line 4105, `ps_publish_hook`)
- 32-bit: function VA 0x481170 (rva 0x81170), `void (uint lc, const void *ps)`, cdecl: `if coop byte: lc = slot[lc].id (u16 at 0x88f568 + lc*0x263c); memcpy(0x926c58 + lc*0xbe9c, ps, 0xaca4)`.
  **Call site: VA 0x65cab8 (rva 0x25cab8)**, inside VA 0x65ca60 (64: the middle of 0x234540). Bytes at the call site: `e8 b3 46 e2 ff 83 c4 0c 81 c7 3c 26 00 00 83 c6` (the call takes two words, `push eax(ps); push esi(lc); call`; the `add esp, 0xc` after it also frees the word pushed for the getter call 0x496ef0 just before).
- offsets: the playerstate of local client n is at **VA 0x926c58 + n * 0xbe9c** (64: 0x535d38 + n * 0xce9c). In context 0 the live address is 0x926c58.
- bytes at the function: `80 3d 7c 89 88 00 00 8b 44 24 04 74 0d 69 c0 3c`.
- confidence: verified

### The ps getter used with it - 64-bit `0x2316c0(lc)`, 32-bit VA 0x496ef0(lc) (called just before the publish: `uVar2 = FUN_00496ef0(i); FUN_00481170(i, uVar2)`)
- confidence: likely

### Server commands to a client - 64-bit `0xd5470(lc, msg)`, call site rva 0x234a02 (native.c line 4106, `server_cmds_hook`)
- 32-bit: function VA 0x453860 (rva 0x53860), `void (int lc, size_t *msg)`, cdecl (bytes `8b 44 24 04 56 57 8b f8 69 ff 18 32 00 00 81 c7`). **Call site: VA 0x42d155 (rva 0x2d155)**, inside VA 0x42d110 (64: 0x2349a0). Bytes at the call site: `e8 06 67 02 00 8b 0d 6c 8c 73 00 8a 15 7c 89 88`.
- msg layout (the server's per-client command block copied into `clc`): `msg[0]` byte count, `msg[1]` sequence, `msg[2]` acknowledge, text from `msg + 3` (same as 64: `param_2[3]`), offsets from `msg[0x403 + (n & 0x7f)]` (same).
- clc destination (32): size at 0x88e33c + lc*0x3218, sequence at 0x88e340, ack at 0x88e344, text at 0x88e348, offsets at 0x88f348 + (n & 0x7f) * 4 (64: 0x54784c / 0x547850 / 0x547854 / 0x547858 / 0x548858).
- the caller loop 0x42d110 (observed): `for lc in 0..count-1: if (coop ? slot[lc].type == 2 : lc < count) { 0x453860(coop ? slot.id : lc, &client_t[lc] + 4); } client_t[lc].ack = client_t[lc].seq; client_t[lc].size = 0;` with client_t at 0x1b23988 + lc*0x1674 (the argument is `client_t + 4` = 0x1b2398c).
- confidence: verified

### Snapshot message - 64-bit `0xd3ee0(msg, time)` (time in EDX), call site rva 0x234925 (native.c line 4108, `snapshot_msg_hook`)
- 32-bit: function VA 0x4d3dc0 (rva 0xd3dc0), `void (void *msg, int time)`, **cdecl with both arguments on the stack**: it stores `time` at 0x925b90 and calls 0x4ff400(msg) (= 64 0xd5610). Bytes: `56 8b 74 24 08 56 e8 55 b2 fb ff 8b 44 24 10 56`.
  **Call site: VA 0x65ccfa (rva 0x25ccfa)**, inside VA 0x65cc90 (64: 0x234540): `push time; push &msg; call; add esp, 0x4038` (the `add` frees the two pushed words together with the local msg_t and the 0x4000-byte buffer: the caller cleans, so the hook is plain cdecl). Bytes at the call site: `e8 c1 70 e7 ff 81 c4 38 40 00 00 c3 cc cc cc cc`.
- msg_t: native.c copies 0x30 bytes (`copy[0x30]`, the 64-bit `msg_t`: `0x2016c0(msg, buf, size)` writes dwords up to +0x2c). **In 32 bits msg_t is 0x28 bytes** (`0x481880(msg, buf, size)` writes dwords 0..9: `[0]=0, [1]=0, [2]=buf, [3]=0, [4]=size, [5..9]=0`): copy and restore **0x28**, not 0x30 (restoring 0x30 would write 8 bytes past the struct, into whatever follows it on the stack). observed.
- the time argument: in 64 bits `0x1419b07ac` is incremented and the old value passed (`LEA EAX,[RDX+1]` stores it, EDX is the old); 32: `iVar1 = DAT_01b26694; DAT_01b26694 += 1; 0x4d3dc0(msg, iVar1)`.
- confidence: verified

### Entity list exclusion of local players - 64-bit rva 0x23459a (`mov reg, [0x3c3eb0]`, native.c `list_players_install`)
- 32-bit: VA 0x65cc10 (rva 0x25cc10), the instruction `mov ebp, [0x00738c6c]`: bytes `8b 2d 6c 8c 73 00` (6 bytes, **the absolute address is at +2**, not rip-relative). It is in 0x65cbf0 (64: the first part of 0x234540), the loop that lists the entities of the snapshot (`if (coop ? slot.type == 2 : i < count) skip i`).
  bytes of the function: `33 d2 39 15 78 33 b1 01 89 15 74 13 b2 01 0f 84`.
- confidence: verified (the use of the register in the same exclusion test `cmp eax, ebp` was read)

### usercmd step - 64-bit rva 0xd02a0(lc), called at rva 0x15ce84 (native.c line 4107, `usercmd_step_hook`)
- 32-bit: function VA 0x4c6f30 (rva 0xc6f30), `void (int lc)`, cdecl (bytes `51 80 3d 7c 89 88 00 00 74 08 83 c4 04 e9 3e d3`). **Call site: VA 0x4d6794 (rva 0xd6794)**, inside VA 0x4d6730 (64: 0x15ce10). Bytes at the call site: `e8 97 07 ff ff 8a 0d 7c 89 88 00 83 c4 04 81 c6`; the argument is pushed (`push eax`) and cleaned by the caller (`add esp, 4`): cdecl.
- body (observed): non-coop path: moves the clc command ring (counters at 0x88c334 / 0x88c338 + lc*0x3218, ring 0x88c33c + (n & 0x7f) * 0x40) through `thunk 0x486920(lc, cmd)` and then sends cl's usercmds `(cmdNumber - sent)` (max 0x20, `MAX_PACKET_USERCMDS`) through `0x4e7750(lc, &cl.ring[(n & 0x3f) * 0x40])`; coop path calls 0x474280 (= 64 0xcd260).
- confidence: verified

### SV_ClientThink analogue - 64-bit rva 0x22d000(lc, cmd) (native.c lines 3111, 3137, 3082)
- 32-bit: VA 0x4e7750 (rva 0xe7750), `void (int lc, const usercmd_t *cmd)`, cdecl. Bytes `8b 44 24 04 56 8b 74 24 0c 57 8b f8 69 ff 74 16`. It copies the 0x40-byte cmd to **client_t[lc] + 0x1630 = VA 0x1b24fb8 + lc * 0x1674**, then calls 0x4aac80(lc) (= 64 0x15bd80).
- confidence: verified

### Make usercmd (CL_CreateNewCommands) - 64-bit rva 0xd03a0(out, lc), call at rva 0xcf8b1 inside 0xcf800 (native.c line 4131, `make_cmd_hook`)
- 32-bit: make_cmd function VA 0x579a80 (rva 0x179a80); **call site: VA 0x579be9 (rva 0x179be9)** inside VA 0x579bb0 (64: 0xcf800). Bytes at the call site: `e8 92 fe ff ff 69 db 9c be 00 00 81 c3 e0 5b 92`; bytes of 0x579a80: `83 ec 10 56 6a 40 8b f0 6a 00 56 e8 30 80 0f 00`.
- **convention: usercall (compiler-made).** At the call site `lea eax, [esp + 0x4c]` is the output buffer (0x40 bytes, zeroed inside by `memset(eax, 0, 0x40)`), **EBX = local client number**, the function returns the buffer in EAX (the caller then `mov esi, eax` and copies 0x40 bytes). 0x579bb0 itself takes the local client in EAX (`mov ebx, eax`). A replacement hook has to be a naked stub: save ebx, call the original with the same register state, read EAX after. `make_cmd_hook(out, a)` in 64 bits has `a` = lc (always 0 at this site).
- usercmd offsets: the same in both: buttons at +4, angles at +8 / +0xc / +0x10, forwardmove +0x1a, rightmove +0x1b, upmove +0x1c, downmove +0x1d (the 32-bit input code writes `*(char*)(cmd + 0x1a/0x1b/0x1c/0x1d)` and `cmd + 4`, 0x578c20).
- the number of ring writes: `0x579bb0` increments cmdNumber at 0x926c40 + lc*0xbe9c and stores into `ring[(cmdNumber & 0x3f) * 0x40]` at 0x925c40 + lc*0xbe9c.
- confidence: verified

## Global variables

### Local player count - 64-bit rva 0x3c3eb0 (RVA_LOCAL_PLAYERS)
- 32-bit: **VA 0x738c6c (rva 0x338c6c)**, int. evidence: the loop bounds `i < DAT_00738c6c` in 0x42d110, 0x4da540, 0x428630, 0x4d6730, 0x4818e0 (same code as the 64 loops with 0x3c3eb0); `0x4818b0` returns it when the coop byte is 0.
- confidence: verified
- the 35 client reads and the G_InitGame store: see further down.

### Co-op byte - 64-bit rva 0x52fb5c (RVA_COOP_BYTE)
- 32-bit: **VA 0x88897c (rva 0x48897c)**, byte. Read by every slot loop (`cmp byte [0x88897c], 0`), 174 references (64: 200).
- confidence: verified

### Co-op slot table - 64-bit rva 0x540bb0 (type int at +p*0x2650), id (u16) at 0x540bb8 (RVA_COOP_SLOTS)
- 32-bit: type int at **VA 0x88f560 + p * 0x263c** (rva 0x48f560), id (u16) at **VA 0x88f568 + p * 0x263c**; two slots (end of the table 0x8941d8; the loops stop at `ptr < 0x8941e0` / `0x8941d8`). Stride **0x263c** (64: 0x2650). Slot 1: type at 0x891b9c, id at 0x891ba4.
- end bound used by the 64 local slot loop `q > base + 0x545857`: 32-bit loop bound is `0x8941e0` (as `ecx >= 0x8941e0` with ecx walking the id field: 0x88f568, 0x891ba4, 0x8941e0).
- confidence: verified

### The coop slot table is a different object from `clc`; 64-bit 0x545844 (clc[0]) comes after the table, in 32 bits `clc[0]` is [0x88c330, 0x88f548), right before the table (clc[1] would overlap the table).

### Server client array - 64-bit rva 0x19ada98 stride 0x1678 (RVA_SV_CLIENTS)
- 32-bit: **VA 0x1b23988 (rva 0x1723988: the first version of this map had 0x1123988) stride 0x1674**; last usercmd at +0x1630 (VA 0x1b24fb8 + lc * 0x1674, observed in 0x4e7750), the reliable command block `{size at +4, seq at +8, ack at +0xc, text from +0x10}` (0x42d110 passes client_t + 4 to the parser and clears `[+4]`, copies `[+8] -> [+0xc]`... checked in 0x42d110, see the next section for sv_add_command).
- confidence: verified for base, stride and +0x1630

### Local slot function - 64-bit rva 0xa5ec0 (native.c `local_slot_hook`, 15-byte check `slot_bytes`)
- 32-bit: **VA 0x4215a0 (rva 0x215a0)**, `uint (uint lc)`, cdecl, result in EAX. Bytes `80 3d 7c 89 88 00 00 75 05 8b 44 24 04 c3 8b 54` (`cmp byte [0x88897c], 0; jne +5; mov eax, [esp+4]; ret; mov edx, [esp+4]; ...`). It returns the slot index whose type is 2 and whose id is `lc`, or 0 (loop over 0x88f568, stride 0x263c, end 0x8941e0).
  The 64-bit check string (`slot_bytes`, 15 bytes) does not apply: use the 32-bit bytes above.
- Note: 0x4fb5a0 is the reverse lookup (`id = slot[i].id`, the first thing 0x481170 and 0x4da540 do) and not this function.
- confidence: verified

## Structure ranges swapped by `ctx_swap` (native.c lines 1672-1682)

In 32-bit VAs. Derived by pairing the code that touches each range in both builds (functions touching the same field, same stride).

| # | 64-bit range (rva) | what | 32-bit range (VA) | size | how delimited |
|---|---|---|---|---|---|
| 1 | 0x51b078, 0x2c38 | cl snapshots: pointer to the current snapshot (0x51b078), next (0x51b080), two snapshots of 0x1610 bytes, snapshot time words | **0x86798c, size 0x2c30** (end 0x86a5bc) | 0x2c30 | `0x4c1c10` (= 0xb8300) stores `DAT_0086798c = DAT_00867990`; the snapshot buffers start at 0x867994 and are 2 x 0x1610 (`&DAT_00867994 + (cur == &DAT_00867994) * 0x1610`), ending 0x86a5b4 = the word `0x44a530` writes (64: 0x51dca8). 64 range = 0x2c38 because it has two 8-byte pointers; 32-bit has two 4-byte pointers: 8 bytes less. The words at 0x86a5c0 / 0x86a5c4 (64: 0x51dcb4 / 0x51dcb8) are outside, as in 64. |
| 2 | 0x52c400, 0x100 | bitmask cleared by the per-client cgame init | **0x86b180, size 0x100** | 0x100 | `memset(&DAT_0086b180, 0, 0x100)` in 0x56e6b0 (64: `memset(0x52c400, 0, 0x100)` in 0xb8830), same code. |
| 3 | 0x52fbc0, 0x21c | input (kbuttons) of local client 0 | **0x8889b0, size 0x21c** (end 0x888bcc) | 0x21c | stride `lc * 0x21c` in 0x578c20 / 0x5792a0; the first element is taken with a LEA at 0x8889b0 (578af0, 578c20, 5792a0) as 64 takes 0x52fbc0 (cf900, ce450); the highest referenced address of the cluster is 0x888bcc = base + 0x21c - 0x4 (64: highest 0x52fdd9). |
| 4 | 0x533cb8, 0x68 | cls state, mouse state, view angle accumulators (up to 0x533d1c) | **0x925bc0, size 0x80** (end 0x925c40) | 0x80 | state word compared with 6 in 0x4c3660 (cls = **0x925bc0**, key catchers 0x925bb8: 64 0x533cb0). The range ends where the cl block starts (64: 0x533d20 = start of cl; 0x533cb8 + 0x68). The accumulators are **pitch 0x925c34, yaw 0x925c38** (64: 0x533d14 / 0x533d18): in 0x578fe0 `_DAT_00925c38 -= yaw delta`, `_DAT_00925c34 += pitch delta`. The 32-bit block between the cls word and the accumulators holds 0x18 more bytes than the 64 one (0x925bc4-0x925bdc, touched by 0x4f3480 and others, a per-client state function): the 64 size 0x68 does not carry over, the end does. |
| 5 | 0x533d20, 0xce40 | cl: usercmd ring, cmdNumber, playerstate (+0x2018), packet history | **0x925c40, size 0xbe3c** (end 0x931a7c; 0xbe40 would also swap one shared dvar pointer word) | 0xbe3c | start: ring base `0x925c40` (0x4329e0: `ring + (n & 0x3f) * 0x40 + 0x925c40`, `cmdNumber` at 0x926c40, **playerstate at 0x926c58 = start + 0x1018**). The cl struct in 64: ring 0x2000, cmdNumber, sent, ps (+0x2018, 0xaca4 bytes, ends 0x5409dc), an array of 0x180 bytes (the packet history, 32 x 12: 0x5409dc..0x540b5c); so 64 cl = 0xce3c and the range's extra 4 bytes (0x540b5c, used by the party code d25e0/22e610/22f990) are the next static. In 32 bits ps ends at 0x926c58 + 0xaca4 = 0x9318fc (words after it: 0x931904 `0x577ec0`, 64: 0x5409e4 `cd260`), history 0x9318fc..0x931a7c, struct = 0xbe3c = 0xce3c - 0x1000. The word at 0x931a7c is a dvar pointer (`mov eax, [0x931a7c]; cmp byte [eax+0x10], 0` in 0x579c40) and 0x931a80 belongs to the snapshot parser (0x4ff400): both outside. Fields (observed): cmdNumber +0x1000, sent +0x1004, ps +0x1018, ps.flags word at 0x926c64 (64: 0x535d44, `& 0x80000`), stride 0xbe9c. |
| 6 | 0x545844, 0x3218 | clc: command ring, server commands | **0x88c330, size 0x3218** (end 0x88f548; the 64 range starts 4 bytes into the struct, 0x545840 is the memset base: use 0x88c330, or 0x88c334 to copy the 64 behaviour exactly) | 0x3218 | 0x4c6f30: two counters at 0x88c334 / 0x88c338, ring `0x88c33c + (n & 0x7f) * 0x40` (0x2000 bytes), server command size at 0x88e33c, sequence 0x88e340, ack 0x88e344, text 0x88e348 (0x1000), offsets 0x88f348 (0x200); the stride 0x3218 is in 0x453860. It is **not** the same place relative to the slot table (see above). |
| 7 | 0x548ce8, 8 | the snapshot message's time and flag | **0x894424, size 8** | 8 | 64 words 0x548ce8 / 0x548cec are written by 0xd5610 (the message parse) and 0xca2c0; in 32 bits 0x894424 / 0x894428 are written by 0x4ff400 (= 0xd5610) and 0x415a70. |
| 0 | 0x404450, 0x4ac00 | cgame block (cgs, cg, entities and neighbours) | **two ranges (see "The cgame block" below): 0a [0x75f280, 0x7a0000) and 0b view model [0x85e8a4, +0x28)** | 0x40d80 + 0x28 | pairing of the cgame functions and the globals each touches |
| 8 | 0x3cf180, 0xe80 | aim assist of local client 0 | **0x73ba90, size 0xe80** | 0xe80 | stride and base from 0x4ae500 (see below) |

Order inside `ranges[]`: the 64-bit table indexes `ctx_store[..][5]` for the cl block, `[4]` for cls (`ctx_store[1][4] + (0x533d14 - 0x533cb8)`), `[0]` for the cgame: keep that order.
In the playerstate-in-cl offsets, 64 `0x2018` -> 32 `0x1018`; `0xaca4` stays. In `ctx_after_draw` the accumulators are `ctx_store[1][4] + (0x533d14 - 0x533cb8)` = 0x5c (pitch) / 0x60 (yaw); in 32 bits pitch is at `0x925c34 - 0x925bc0 = 0x74`, yaw at `0x925c38 - 0x925bc0 = 0x78` of range 4 (derived).

### The real structure extents (the engine's own clears), which also settle the ranges above
`rva 0xd2270` (64) / `VA 0x432800` (32) is the client-state reset (disconnect: `0xb8300(0,0)`, per-client `0xb8000(lc,0,0)`, entity cleanup, `0xb90d0(lc)`, ...). It clears (observed):
- **cl**: 64 `memset(0x533cc0, 0, 0xce9c)`; 32 `memset(0x925be0, 0, 0xbe9c)`. So the cl struct starts at **0x925be0** in 32 bits (0x533cc0 in 64) and the usercmd ring is at +0x60 in both. The 64 range 4 (0x533cb8, 0x68) is therefore "8 bytes before cl + the first 0x60 bytes of cl", and range 5 starts at the ring (0x533d20 = cl + 0x60). In 32 bits the same cut is at 0x925c40 = 0x925be0 + 0x60, the cls state word is 0x20 before cl (0x925bc0) instead of 8 before.
- **clc**: 64 `memset(0x545840, 0, 0x3218)`; 32 `memset(0x88c330, 0, 0x3218)`. The first dword (0x88c330) is not in the 64 range (which starts 4 bytes in, 0x545844); the real struct is `[0x88c330, 0x88f548)`.
- 32-bit 0x432800 also uses 0x925b8c (end bound of an array of shorts at 0x92443a it clears entry by entry; 64: 0x5d8d02, 0xfa8 entries) and sets `0x925b90 = 0` (the snapshot message word, 64: 0x5dac58), as 64 does.

## The cgame block (range 0) in 32 bits

**The cgame's statics are laid out in a different order in 32 bits, so the 64-bit block cannot be carried over as one range with a shifted start.**
How it was derived (observed): the cgame functions were paired (0xa6680 -> 0x433f30, 0xa8b90 -> 0x565300, 0x693e0 -> 0x55a5d0, 0xc4150 -> 0x4b2e00, 0xb8830 -> 0x56e6b0, 0xb8940 -> 0x56e600, 0xb9320 -> 0x41a3a0, 0xca0f0 -> 0x428630, 0xbca00 -> 0x4666c0, 0x9f9a0 -> 0x49a4d0, 0xc3ea0 -> 0x4b5050, ...) and the globals each touches were listed in both builds (a Ghidra script that prints the data addresses a function references). Fields line up with their offsets halved for pointer tables and equal for ints/floats.

Anchors (64 rva -> 32 VA, observed):

| what | 64 | 32 |
|---|---|---|
| media pointer table, first entry (the 64 range start) | 0x404450 | **0x78e518** (stride 8 -> 4: 0x404458 -> 0x78e51c, 0x404af0 -> 0x78e868, 0x404b48 -> 0x78e894) |
| second pointer table of the block (33 / 11 entries) | 0x40c158..0x40c258, 0x40c248..0x40c298 | **0x79239c..0x79241c**, 0x792414..0x79243c (stride 4) |
| cgs-ish part: clientNum copy (`= *(snap_ps + 0x100)`), local client number, `1`, snapshot ptr, ps ptr | 0x40caa0, 0x40caa4, 0x40cab8, 0x40cac0, 0x40cad0, 0x40cad8 | **0x75f2a0**, **0x75f2a4**, 0x75f2b8, 0x75f2bc, 0x75f2c4, 0x75f2c8 |
| cg.time, cg.ps | 0x422434, 0x422440 (origin at 0x42245c) | **0x774c20**, **0x774c2c** (origin 0x774c48; `memcpy(0x774c2c, snap_ps, 0xaca4)` in 0x56e8c0) |
| second playerstate field (+0x100) | 0x422540 | 0x774d2c |
| cg_entities (2048 entries) | base 0x441070 .. 0x515070 (stride 0x1a8) | base **0x792844** (origin x at +0x18 = 0x79285c, as 64), stride **0x198**, end **0x85e844** (the next used cluster 0x85e848 follows it as 0x514fa8 follows 64's end) |
| view model pointers (2 hands: 64 stride 0x20, per client 0x48) | 0x440730..0x440778 | **0x85e8a4..0x85e8d0** (hand stride **0x14**: `(&DAT_0085e8a4)[hand * 5]`, loop `0x85e8a8 + hand * 0x14 < 0x85e8d0`): **after the entity array, not before it** |
| cgame snapshot flag | 0x51b070 | 0x867988 (set 0 in 0x41a3a0) |

Observed facts that decide the ranges:
1. In 64 bits the block `0x404450-0x44f000` is one stretch: media tables A (0x404450-0x406060), second pointer table B (0x40c140-0x40c2a0), cgs/cg parts C (0x40caa0), E (0x422428-0x422980), F-I (0x42d0e8-0x43f480), view model J (0x440730), entity array start K (0x440ec8 on). In 32 bits the order is **C-E-F-G-H-I (0x75f28c .. 0x78e2c0), then A and B (0x78e4a8 .. 0x792440), then K (0x792840 ..), and J (view model) after the end of the 2048-entity array (0x85e8a4)**.
2. The 64-bit range ends at 0x44f000, which is 135 entities into the 2048-entry array ((0x44f000 - 0x441070) / 0x1a8 = 134.98: entities 0..133 whole, 134 cut): **the other ~1913 entities are not swapped in 64 bits** (shared by both contexts). Observation about the 64-bit code (it may be intended: both contexts get the same snapshots; or it may be behind some of the far-NPC artefacts). `ctx_entity_log` reads `0x441070 + i * 0x1a8` for i < 1024.
3. The dvar pointer tables the cgame registers (64: 0x51dca8-0x51e364, 0x521008) are shared and not in the block; in 32 bits they are at 0x754d08-0x75f168 (the registration function 0x47c0f0 writes all of them), below the block.

32-bit ranges to use for range 0 (derived, **likely**: the lower edge was not tested at run time):
- **0a: [0x75f280, 0x7a0000)**: from the first cgame state word (cluster 0x75f28c..0x75f3cc holds 0x75f2a0 and 0x75f2c8) through the media tables A/B and the first 135 entities (0x792844 + 135 * 0x198 = 0x79ff6c, rounded to 0x7a0000); size 0x40d80. This keeps the 64-bit behaviour (entities 0..134 swapped).
- **0b: the view model pointers [0x85e8a4, 0x85e8d0)** (0x2c bytes). The 64 `memset(ctx1 + (0x440730 - start), 0, 0x40)` in `ctx_after_draw` clears 2 hands x 0x20; in 32 bits clear **2 hands x 0x14 = 0x28 bytes at 0x85e8a4**.
- if the whole entity array is to be swapped (worth trying): one range [0x75f280, 0x85e8d4) of 0xff654 bytes covers everything including the view model.
- confidence: likely for the structure; the exact lower edge is unknown (0x75f28c is the first address the paired functions touch; in 64 bits 0x40caa0 is the same kind of "first touched").
- `0x40caa4` (the local client number written for context 1, `ctx1lc`) -> **0x75f2a4**; `0x422434` -> 0x774c20; `0x440730` -> 0x85e8a4; the aspect `0x43c640` is another package's.

## Aim assist (range 8) - 64-bit rva 0x3cf180, 0xe80 (aaGlob)
- 32-bit: **VA 0x73ba90**, entry stride **0xe80** (`param_1 * 0xe80` in 0x4ae500). evidence: 0x4ae500 stores `[0x73ba90 + lc * 0xe80 + 0 .. 0x28]` from the client's data in the same order as 64 0x6a1c0 stores `[0x3cf180 + ...]`; 0x55a5d0 (= 64 0x693e0) touches 0x73ba90 and 0x73bbc4..0x73bbdc, 0x73bbec..0x73bbfc, 64: 0x3cf180, 0x3cf2b4..0x3cf2cc, 0x3cf2dc..0x3cf2ec: the offsets from the base are equal (0x134 ..).
- field offsets inside an entry are the same (ints/floats): 64 `0x3cf1a4` -> 0x73bb34 in 32.
- the aim_* dvar pointers (64: 0x3cf050-0x3cf178) are **not** found yet in 32 bits: they lie before 0x73ba90 (0x73ba88 / 0x73ba8c are touched by 0x48df90); unknown.
- confidence: likely (base and stride from the same code, not run)

## The local player count: the store and the 35 client reads

### G_InitGame store - 64-bit rva 0x1714a6 (`c7 05 <rel32> 01 00 00 00`, immediate at +6; native.c `players_apply`, line 4204)
- 32-bit: **VA 0x4bf9db (rva 0xbf9db)**: bytes `c7 05 6c 8c 73 00 01 00 00 00` (`mov dword [0x738c6c], 1`, length 10, **the immediate is at +6**, the absolute address at +2). The check `at[0] == 0xc7 && at[1] == 5 && at[6] == 1 && at[7..9] == 0` holds unchanged. It is in VA 0x4bf8b0 (64: 0x171370, G_InitGame). The other branch (coop byte set) stores `mov [0x738c6c], eax` at VA 0x4bf9ec (`a3 6c 8c 73 00`, 5 bytes; 64: 0x1714b8). Function bytes `83 ec 10 e8 18 32 fa ff e8 23 63 f7 ff e8 6e 57`.
- confidence: verified (the same write pair, the same function pairing)

### The 35 client-side reads (`client_n_sites`): 64-bit (rva, length, disp offset) -> 32-bit (VA, length, absolute-address offset)
In 32 bits the instruction holds the **absolute address 0x738c6c** (not a rip-relative displacement). `client_n_install` has to compare the 4 bytes at the offset with `base + 0x338c6c` (it is `RVA_LOCAL_PLAYERS` = 0x338c6c) and write the address of the copy variable (an absolute pointer in the process, not a rel32): in a 32-bit process any variable has a 32-bit address, so there is no "near page" problem and the copy variable can be anywhere (but the patch must write the absolute address, and must be undone with the original value).
Encoding seen at the 32-bit sites: `3b <modrm> 6c 8c 73 00` (cmp reg, [abs], 6 bytes, disp at +2), `8b <modrm> 6c 8c 73 00` (mov reg, [abs], 6 bytes, disp +2), `a1 6c 8c 73 00` (mov eax, [abs], 5 bytes, disp +1), `83 3d 6c 8c 73 00 <imm8>` (cmp [abs], imm8, 7 bytes, disp +2).
How the set was chosen: the 64-bit client sites are the 35 reads in rva 0x5000-0xe0000, in 23 functions. In 32 bits the code is not split by address, so each of the 23 64-bit client functions was paired with its 32-bit function (call graph, same strings / same global footprint) and **all reads of the paired function** are listed. The other 59 reads of 32 bits (94 total) belong to the paired server / game-module functions and stay on the real count. Verified = the pairing was checked in decompiles of both builds.

| 64-bit function (rva) -> 32-bit function (VA) | what | 32-bit sites (VA, len, disp offset) | 64-bit sites (rva) | pairing |
|---|---|---|---|---|
| 0x6bae0 -> 0x4818b0 | number of local players (coop ? slots in use : count) | 0x4818b9 (5, 1) | 0x6bae9 | verified (both are `coop ? ... : count`) |
| 0x85f20 -> 0x43a910 | per-client loop | 0x43a926 (6, 2) | 0x85f44 | likely (map matcher + same footprint: 0x738c6c, slots, 0x891ba4) |
| 0x89cf0 -> 0x55b710 | cgame entity / view model code | 0x55b7fa (6, 2) | 0x89e44 | likely (both read the snapshot ptr 0x40cad8 / 0x75f2c8 right after the count: +0x22 / +0x1f) |
| 0x93ac0 -> 0x55ea00 | partner name drawing loop | 0x55ea88 (6, 2) | 0x93d94 | likely (in 32 bits the "waiting for the other player" part is the neighbouring 0x55ef10; the names loop is 0x55ea00) |
| 0x9f9a0 -> 0x416850 | the event handler 0x49a4d0 (CG_EntityEvent analogue) calls this tiny helper | 0x41685e (6, 2) | 0xa08c2 | likely (0x9f9a0 = 0x49a4d0 by the string "Calling event on an unregistered weapon" and size; 0x416850 is called at 0x49b782) |
| 0xa6680 -> 0x433f30 | HUD draw, 6 loops | 0x433f9a, 0x434159, 0x4341eb, 0x4342b4, 0x434327, 0x434378 (6, 2 each) | 0xa6703, 0xa69cf, 0xa6a76, 0xa6b29, 0xa6c44, 0xa6c8c | verified |
| 0xa8b90 -> 0x565300 | HUD draw | 0x56558b, 0x5656be (6, 2) | 0xa8f25, 0xa905d | verified |
| 0xa9240 -> 0x4b74e0 | small per-client function | 0x4b74ed (6, 2) | 0xa9248 | likely |
| 0xa92b0 -> 0x454c40 | | 0x454cb9 (6, 2) | 0xa936b | likely |
| 0xb2130 -> 0x4b5580 | draw's per-client loop (calls 0xb2330 / 0x4b4770 per client; called from the cgame draw: 64 at 0xbcc80, 32 at 0x466845) | 0x4b5594 (6, 2) | 0xb2154 | verified (call from the draw, loop body touches cg.ps + 0x100) |
| 0xb6050 -> 0x497840 | | 0x4978b4, 0x497a44, 0x497ac4 (6, 2) | 0xb60ec, 0xb629a, 0xb630a | likely |
| 0xb71b0 -> 0x4818e0 | `0x4818e0` loops the local clients calling 0x4c7570(lc, text, 7) | 0x48190a (6, 2) | 0xb71df | verified |
| 0xb8d80 -> 0x56e3d0 | | 0x56e414 (6, 2) | 0xb8e34 | likely |
| 0xbca00 -> 0x570bb0 | the draw's own loop (called from the draw at 0x466785; touches the cl snapshot pointer 0x867990 as 64 touches 0x51b080) | 0x570bf9 (6, 2) | 0xbcb49 | likely (the draw 0x4666c0 itself has no direct read) |
| 0xc2820 -> 0x4893c0 | entity origin getter for a client | 0x4893d2 (6, 2) | 0xc2835 | likely |
| 0xc3e50 -> 0x4e7620 | returns `&cgs-ish client struct` (0x75f2a0) for a local client, else 0 | 0x4e7632 (6, 2) | 0xc3e5e | verified |
| 0xc3ea0 -> 0x4b5050 | vector from the cg ps for a local client | 0x4b5062 (6, 2) | 0xc3eb8 | likely |
| 0xc95f0 -> 0x471a10 | | 0x471a64, 0x471ae4, 0x471b47 (6, 2) | 0xc9667, 0xc96f5, 0xc9754 | likely |
| 0xc99c0 -> 0x4da540 | snapshot take | 0x4da584 (6, 2) | 0xc9a04 | verified |
| 0xca0f0 -> 0x428630 | cg_init_all | 0x428666, 0x4286ba (6, 2) | 0xca135, 0xca184 | verified |
| 0xca600 -> 0x430860 | | 0x430868 (6, 2) | 0xca607 | likely |
| 0xd2270 -> 0x432800 | client state reset | 0x43282c, 0x432888 (6, 2) | 0xd22ac, 0xd2304 | verified |
| 0xd5510 -> 0x479040 | | 0x4790ab (6, 2) | 0xd5595 | likely |

Total: **35 sites** (the same count as 64).
All 94 references to 0x738c6c in 32 bits: 35 client reads (table above) and 59 others. The others stay on the real count (server, game module, coop code); of them 4 are writes (0x4bf9db and 0x4bf9ec in G_InitGame, 0x65aaee, 0x65a71f in the coop code). Format `VA/len/disp-offset`:

0x4014e2/6/2, 0x4015e9/6/2, 0x408889/6/2, 0x40a579/6/2, 0x40c714/6/2, 0x418d20/6/2, 0x418d6b/6/2, 0x42aed0/6/2, 0x42af2f/6/2, 0x42d116/6/2, 0x42d15a/6/2, 0x46ce9f/6/2, 0x46cecc/6/2, 0x47840d/6/2, 0x482ea4/6/2, 0x49d008/6/2, 0x49d0e3/6/2, 0x49d3e4/6/2, 0x4a9754/6/2, 0x4a97ca/6/2, 0x4ae390/7/2, 0x4ae6b0/6/2, 0x4bf9db/10/2 (write), 0x4bf9ec/5/1 (write), 0x4d3f7c/6/2, 0x4d6774/6/2, 0x4e7c89/6/2, 0x4e8366/6/2, 0x4e83b7/6/2, 0x501e99/6/2, 0x59a476/6/2, 0x59d86e/6/2, 0x59d939/6/2, 0x5c5ca8/6/2, 0x5c6ba4/6/2, 0x5c74e8/6/2, 0x5d7b19/6/2, 0x5e2870/6/2, 0x659f3f/7/2, 0x659fc8/6/2, 0x65a01d/6/2, 0x65a096/6/2, 0x65a0fa/6/2, 0x65a4d7/6/2, 0x65a71f/6/2 (write), 0x65a898/6/2, 0x65aaee/5/1 (write), 0x65ab2e/6/2, 0x65acfd/5/1, 0x65af35/6/2, 0x65b16f/6/2, 0x65b463/6/2, 0x65b48d/6/2, 0x65b7a9/6/2, 0x65c140/6/2, 0x65c167/6/2, 0x65ca9b/6/2, 0x65cb65/6/2, 0x65cc10/6/2
- The writes of the count by the co-op code (0x65aaee, 0x65a71f, 0x4bf9ec) are in the 64-bit server range too (0x22d9d5, 0x22d67c, 0x1714b8).
- confidence for the whole table: the 35 function pairs are verified or likely as marked; **every site was checked to read 0x738c6c with the encodings above**; a wrong pairing would leave a client loop reading 2, which writes beyond the one-element arrays (a crash on first use), so the 10 "likely" rows deserve a run with logging: enable the patch for one row at a time, or count how many sites `client_n_install` reports (35 of 35).

## Other functions, variables and dvars of the package

### SV_AddServerCommand - 64-bit rva 0x232c30 (native.c line 1617, `sv_add_command`, replaced by `jhook_jump(..., 14, ...)`)
- 32-bit: **VA 0x40a550 (rva 0xa550)**, `void (client_t *cl, const char *text)`, cdecl (stack arguments). Bytes `56 8b 74 24 08 8b ce 81 e9 88 39 b2 01 b8 bd ab` (first instruction pair is 5 bytes: a 5-byte `jmp` overwrite is clean). Callers: 0x46ce50 (two calls, 0x46ce8c / 0x46cebb; 64: 0x233bf0).
- layout identical to 64 (observed): `cl + 4` write offset, `cl + 8` sequence (incremented first), `cl + 0xc` acknowledge, text at `cl + 0x10` (0x1000), offset of command n at `cl + 0x1010 + (n & 0x7f) * 4` with n = the incremented sequence; `%` is replaced by `.`; overflow messages "Server command overflow" / "Reliable command buffer overflow" through Com_Error. `idx = (cl - 0x1b23988) / 0x1674`; in-use test `coop ? slot[idx].type == 2 : idx < count` with slots at `0x88f560 + idx * 0x263c`.
- one difference: before each Com_Error the 32-bit function calls `0x45d4a0(cl)` (a debug dump of the client's commands; no counterpart in `native.c`, safe to leave out).
- confidence: verified

### Com_Error - 64-bit rva 0x1fb5f0 (`void (int code, const char *format, ...)`)
- 32-bit: **VA 0x4584f0 (rva 0x584f0)**, `void (int code, const char *format, ...)`, cdecl, varargs (`__vsnprintf(buf, 0x1000, format, &stack0xc)`). Bytes `e8 9b ba 01 00 84 c0 74 0a e8 72 6e 0b 00 a3 f0`. Code 1 = ERR_DROP as in 64 (the `param_1 == 1 || 0 || 4` test).
- confidence: verified

### Cbuf_AddText (`logcbuf` diagnostics) - 64-bit rva 0x1ee890
- 32-bit: VA 0x409b40 (RVA_CBUF_ADDTEXT 0x9b40 in game.h), `void (int lc, const char *text)`, cdecl. Bytes `6a 21 e8 39 83 08 00 8b 44 24 0c 8a 08 83 c4 04`. The 64-bit byte check (`cbuf_bytes`) does not apply.
- confidence: verified (the console command path of control32.c uses it)

### Renderer worker wait (`ctx_wait_workers`) - 64-bit rva 0x300a0 (`void (int type)`, types 0-18, 19/20 physics)
- 32-bit: **VA 0x52ac60 (rva 0x12ac60)**, `void (int type)`, cdecl. Bytes `51 56 8b 74 24 0c 8b c6 c1 e0 07 8b 88 8c d4 20`. Same code: `if (count[type] > 0) { set 0x73b4fc = type; ...; "wait %s"; wait for the type; 0x73b4fc = -1 }`. The type names at 0x73b508 are the same list in the same order: 0 cell dyn brush, 1 cell dyn model, 2 cell scene ent, ..., 10 add scene ent, ..., 18 skin model, 19 fx physics, 20 physics (read from the image).
- confidence: verified

### The begin-frame and the cgame draw (`hooks_apply`, RVA_BEGIN_FRAME 0xc840, RVA_CG_DRAW 0xbca00)
- begin frame: 32-bit **VA 0x50d800 (rva 0x10d800)**, `void (void)`, cdecl. Bytes `51 53 56 e8 88 67 f6 ff 33 db 84 c0 74 1b 38 1d`. Same code ("wait renderer2", the two frame data sets, flip `& 1`); the data sets are 0x12bb00 bytes apart (64: 0x13d700), base 0x1c93300 (64: 0x458fb00). Called from the cgame draw at 0x466831, from the client frame at 0x57ae48, from 0x45cb50 (0x45cd6d, a loading frame) and 0x518b20.
- cgame draw: **VA 0x4666c0**, `void (int localClient, int, int)`, three stack arguments, caller cleans (cdecl): the call site in the client frame is `push eax; push eax; push eax; call` at 0x57ae1a. Bytes `83 ec 0c 53 55 33 db 53 e8 33 0e 0a 00 53 e8 5d`. Second caller: 0x45cd9c (in 0x45cb50, loading frame).
- confidence: verified for the begin frame (the same body), likely for the draw

### The co-op dvar - 64-bit `0x1905308` (the dvar pointer of `coop`, native.c lines 4183-4195, 4420)
- 32-bit: **VA 0x1a7f0ec (rva 0x137f0ec)**: `MOV [0x1a7f0ec], EAX` right after the registration call with the name "coop" (string at 0x6de9a8) in VA 0x4a6fe0 (64: the store at 0x230eb9 after the registration with the "coop" string 0x351294 in 0x230c00; the two functions are paired by the matcher and by their position as the last registration of the function). The value is at dvar + 0x10 (bool/int), 4-byte pointer.
- confidence: likely

### Dvar pointers used by `client_frame_hook` (native.c lines 2842, 2875, 2882, 2898)
| 64-bit rva | meaning | 32-bit VA | how |
|---|---|---|---|
| 0x51e028 | cg_nopredict dvar pointer | **0x85e8e8** | the store after the registration call of "cg_nopredict" (string 0x6c9df4) at 0x47ccfb in 0x47c0f0 (the cgame's dvar register function = 64 0xa7090); read at 0x4904a0 (= 64 0xb28b0) as in 64 |
| 0x51e1a8 | cg_fov dvar pointer | **0x85e968** | store at 0x47c19e after the registration of "cg_fov" (0x6d8758) |
| 0x51e120 | cg_drawGun dvar pointer | **0x75efec** | store at 0x47c115 after the registration of "cg_drawGun" (0x6bb260) |
| 0x26bdf0 (Dvar_FindVar by name, cdecl in 64) | | **VA 0x633480, USERCALL (name in EDI)**, rva 0x233480 (game.h `RVA_DVAR_FINDVAR`) | existing 32-bit profile; use the wrapper of `game.c` (the 64-bit code calls the function with the name as the first argument) |
| dvar_t value field | +0x10 | +0x10 (the same: `*(int*)(d + 0x10)`, string at +0x10 as a 4-byte pointer) | `game.h` 32-bit struct, and 0x4904a0 reads |
- confidence: verified for the three pointers (each is the store after the call that registers that name; cg_nopredict's reader pairs with 64's)

### cls / client frame globals used in `client_frame_hook`
- key catchers of client 0 (64: 0x533cb0, `& 0x11`): **VA 0x925bb8** (42 references, as 64's 41; the 32-bit key handler 0x4630a0-region tests `& 1` and `& 0x10`, the pause menu bit is 0x10 as in 64).
- cls state (64: 0x533cb8): **VA 0x925bc0** (read by 0x4c3660 and 0x57adc0 against 6).
- the scripts' dvar `mw2coop_split_view` is a script-created dvar (the string is not in the exe): read by name through Dvar_FindVar as in 64.

### Mouse reads (`mouse_read_hook`, `mouse_read2_hook`) - 64-bit call sites rva 0xcfd23 and 0xd0061 of `0xcf4b0(state, &x, &y)`
- 32-bit function **VA 0x578f10 (rva 0x178f10)**, **usercall: ESI = mouse state pointer, two stack arguments (float *x, float *y), the caller cleans (`add esp, 8`)**, no return value used. Bytes `83 ec 0c 8d 4c 24 08 8d 54 24 04 8b c6 e8 7e ff` (it moves ESI to EAX and calls the inner reader 0x578ea0). The mouse state is the first part of the cl struct (64: `0x533cc0` = cl; 32: **0x925be0** = cl), same as the pointer 64 passes as the first argument.
- **call sites: VA 0x579000** (in 0x578fe0 = 64 0xcfcf0; ESI is set by the immediate `mov esi, 0x925be0` at 0x578ffb, i.e. local client 0 is baked into the instruction; bytes at 0x579000 `e8 0b ff ff ff 83 c4 08 f7 05 64 6c 92 00 00 08`) and **VA 0x5799e8** (in 0x579960 = 64 0xcff70, the gamepad-flag path; ESI is whatever the caller left; bytes `e8 23 f5 ff ff 8b 0d d8 41 89 00 d9 41 10 8b 15`). A hook has to be a naked stub that passes ESI through; to make context 1 read no mouse, return with zeroed outputs.
- the view-angle accumulation function `0x578fe0(float frametime)` (64 0xcfcf0, usercall: EDI = usercmd out pointer) is the one that adds the mouse deltas to `0x925c34` (pitch) / `0x925c38` (yaw).
- confidence: verified for the sites and the convention (both call sites read in disassembly), likely for the 0xcff70 pairing (it is the path taken when `ps.flags & 0x80000`, as in 64)

### Entity-based and other small addresses in these lines
- `RVA_COOP_BYTE` 0x52fb5c -> VA 0x88897c; `RVA_LOCAL_PLAYERS` / `_N` -> VA 0x738c6c; `RVA_COOP_SLOTS` -> VA 0x88f560 (stride 0x263c).
- `RVA_SV_CLIENTS_CMD` 0x19ada98 (+0x1630 last cmd, +0x1678 per client) -> VA 0x1b23988 (+0x1630 = 0x1b24fb8, +0x1674 per client).
- `ctx_seed_cgame` copies the cl block except `[0x2018, 0x2018 + 0xaca4)`: in 32 bits `[0x1018, 0x1018 + 0xaca4)` of the range starting at 0x925c40.

## What has no 32-bit counterpart, or differs in kind
- **The cgame block as one contiguous range**: does not exist in 32 bits (C-I, A-B and the entity array / view model are in a different order); two ranges are needed (see above).
- **usercmd ring depth**: 64 entries (mask 63), not 128.
- **make_cmd and 0x579bb0** are usercall (EAX / EBX), not plain calls: the hook needs a naked assembler stub.
- **`0xd5470` guard check** (`_guard_check_icall`) of the 64-bit build does not exist in 32 bits (the 32-bit function is plain).
- **`sv_add_command`**: the 32-bit function calls an extra dump function (0x45d4a0) before Com_Error: not needed.
- **The 32-bit `ps publish` and `snapshot message` call sites sit in two different functions of the 0x65c.. block** (0x65ca60 and 0x65cc90) because the 64-bit 0x234540 is split into 0x65cbf0 / 0x65cba0 / 0x65ca60 / 0x65cc90 in 32 bits; the three hooks are still at call sites that exist.
- **The per-client init 0xb8830** is a pair of functions in 32 bits (0x4a5900 for the snapshot part, 0x56e6b0 for the rest); 0x41dfa0(lc) calls both and is the loop's own callee.
- **msg_t size**: 0x28 in 32 bits (0x30 in 64): the copy in `snapshot_msg_hook` must use 0x28.
- **Not found**: the aim_* dvar pointers (64 0x3cf050-0x3cf178), the exact lower edge of range 0a, the meaning of the 0x18 extra bytes between the cls state word and the cl struct (0x925bc4-0x925bdc).

## Summary of the call sites to patch (for the address table)
| hook | 32-bit call site / function | kind |
|---|---|---|
| client_frame_hook | call at 0x496dd7 -> 0x4c3660 | call patch, cdecl void |
| cg_init_all_hook | call at 0x47af60 -> 0x428630 | call patch, cdecl void |
| snapshot_take_hook | call at 0x455484 -> 0x4da540 | call patch, cdecl void |
| ps_publish_hook | call at 0x65cab8 -> 0x481170 (lc, ps) | call patch, cdecl |
| server_cmds_hook | call at 0x42d155 -> 0x453860 (lc, msg) | call patch, cdecl |
| usercmd_step_hook | call at 0x4d6794 -> 0x4c6f30 (lc) | call patch, cdecl |
| snapshot_msg_hook | call at 0x65ccfa -> 0x4d3dc0 (msg, time) | call patch, cdecl |
| server_step_hook | call at 0x459d6c -> 0x455470 | call patch, cdecl void |
| make_cmd_hook | call at 0x579be9 -> 0x579a80 | call patch, **usercall (EAX out, EBX lc)** |
| local_slot_hook | function 0x4215a0 | function hook (bytes above), cdecl (lc) -> index |
| sv_add_command | function 0x40a550 | 5-byte jump, cdecl (cl, text) |
| list_players_install | `mov ebp, [0x738c6c]` at 0x65cc10 | absolute address at +2 |
| players_apply | store at 0x4bf9db, imm at +6 | byte patch |
| client_n_install | 35 sites listed | absolute address patch |
| begin frame / draw | 0x50d800 / 0x4666c0 | function hooks, cdecl |
| mouse_read_hook / mouse_read2_hook | calls at 0x579000 / 0x5799e8 -> 0x578f10 | call patch, **usercall (ESI = state), two stack args, caller cleans 8** |
