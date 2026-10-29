# Package ENTITIES, DOBJ AND ANIMATION: 32-bit counterparts

Build: 32-bit official, image base 0x400000 (Ghidra project base 0: VAs below are real VAs, rva = VA - 0x400000). All "bytes at VA" are the first 16
bytes of `image32.bin`. Agent: ent. Method: decompiled both builds side by side (g.sh, own TMP so runs do not share a log), compared calls, constants and
offsets, then read the bytes. "observed" = read in the 32-bit decompile or disassembly, "concluded" = my reading.

Read this first (shared facts):

- **centity_t (cg_entities)**: 64-bit base rva 0x441070 stride 0x1a8; **32-bit base VA 0x792848 (rva 0x392848), stride 0x198, 2048 entries** (observed: ent_clean
  loop ends at 0x85e8f8 = 0x7928f8 + 0x800*0x198; own_write indexes `n*0x198`).
- **nextState (the 0xac-byte entityState copy) starts at 64-bit +0xe0, 32-bit +0xd4** (observed: transition copies 0x2b dwords = 0xac bytes from the server's
  entity-info table into `0x79291c + n*0x198` = base+0xd4; 64 copies the same 0xac bytes to base+0xe0). Fields inside the entityState carry over unchanged, so
  **every 64-bit centity offset at or after 0xe0 is 64-bit minus 0xc** (verified at six independent points, see the table). Offsets before 0xe0 are NOT a uniform shift.
- **entityState / "entity info" (0xac bytes, pointer free)**: same layout in both builds (eType byte +0, entity number ushort +0x7e, byte +0x58). The server-side
  table of them: 64-bit fn 0x234520(n) = 0x1419c6410 + n*0xac; **32-bit fn 0x40dd30(n) = 0x1b2aca0 + n*0xac** (cdecl, bytes below).
- Local-client slot rule (64 fn 0xa5ec0) = **32-bit fn 0x4215a0**, and `own_write` calls it (it is not inlined in 32 bits, see the own_write entry: this changes the hook).
- Tool note for whoever reruns Ghidra: `tools/ghidra/run.sh` writes its log to `$TMP/mw2coop-ghidra.log`, a path shared by every agent that does not set TMP; two
  agents running at once get each other's output (it happened to me once: a 32-bit request printed 64-bit code). Set `TMP=<own dir>`.

---

## 1. DObj layout and the client / server DObj tables

### DObj struct - 64-bit stride 0xb0 at rva 0x1327640 - native.c lines 2360-2368, 3548, 3610...
- 32-bit: **stride 0x9c, pool VA 0x145d018 (rva 0x105d018)**; a handle is a short, handle 0 = none, DObj = 0x145d018 + (short)handle * 0x9c (observed in 0x4f3c70,
  0x495740, 0x454790 and the copy function 0x447d00, which copies exactly 0x27 dwords = 0x9c bytes).
- offsets, field -> 64-bit -> 32-bit (how known):
  - tree pointer -> +0x00 -> +0x00 (observed: `*param_4 = param_3` in create 0x48c470 / 64 0x2956d0; 0x43f4e0 = `return *param_1`, like 64 0x2968e0)
  - anim tree index (ushort inside the tree struct: `*(short*)(tree+8)`) -> tree+8 -> **tree+4** (observed: 0x4275f0 `*(short*)(iVar1+4)` where 64 0x29a120 has `+8`)
  - ushort cleared at create -> +0x08 -> +0x04 (observed, create code)
  - ushort set from the create call's 5th argument -> +0x0a -> +0x06 (observed)
  - numModels (byte) -> **+0x0d -> +0x09** (observed: copy fn `*(byte*)(param_1+9) * 5` bytes of model array; 64 `+0xd` `* 9`)
  - numBones (byte) -> +0x0e -> +0x0a (concluded from the same create code: byte +0xb cleared where 64 clears +0xf; not proven)
  - partBits sets (4 x 6 dwords) -> +0x18..+0x77 -> +0x14..+0x73 (concluded: create does memset(+0x14, 0, 0x68); 64 zeroes +0x18..+0x87)
  - hidePartBits (6 dwords, the block the anim transfer queue copies) -> +0x8c..+0xa3 -> **+0x80..+0x97** (observed: 0x4275f0 reads `param_1[0x20..0x25]`; 64 0x29a120 reads +0x8c..+0xa3)
  - **models pointer (array of model pointers, 1 parent-index byte each)** -> **+0xa8 -> +0x98** (observed: copy fn `param_2[0x26] = alloc(numModels*5)`, `memcpy(.., param_1[0x26], n*5)`; 64 `param_2[0x15]` qword)
  - models[i] is a 4-byte pointer in 32 bits (array size numModels*5 = 4 + 1 per model; 64: *9 = 8 + 1)
  - the XModel name: `*(const char**)models[i]` (XModel +0) is the same code path in the engine's weapon-name checks (0x561c80 calls a name compare with `0x43f4e0(dobj)` etc.), but I did
    not read a 32-bit XModel struct: **likely** the name is at XModel+0 (first field in IW4); confirm at run time with IsBadReadPtr + printable test, as native.c already does.
  - timeStamp / mat / radius -> +0x78/+0x80/+0x88 -> unknown (native.c does not use them; only the QA logs read +0xb0/+0xb8/+0xbc, drop those).
- confidence: tree, numModels, models, hidePartBits, stride: verified. Others likely/unknown as listed.

### Client DObj table fn (current) - 64-bit rva 0x1fece0, native.c lines 2376, 2426, 2455, 3557, 3593...
- 32-bit: VA 0x4f3c70, rva 0xf3c70, **cdecl, one stack arg `int entnum`, returns DObj* or 0**; table = shorts at VA 0x14ac128 (`movzx eax, word [eax*2+0x14ac128]`).
- bytes at VA: 8b 44 24 04 0f b7 04 45 28 c1 4a 01 66 85 c0 74
- 64-bit table (shorts) 0x141380f50; 32-bit 0x14ac128 (rva 0x10ac128). The engine sometimes calls it with a second argument 0 (`4f3c70(n, 0)`): ignored.
- evidence: both decompiles are 4 lines with the same shape (table lookup, pool + handle*stride); map.txt guess 4f3c70 confirmed.
- confidence: verified

### Pending table fn - 64-bit rva 0x1fed10, table 0x14137ff40
- 32-bit: table VA 0x14ab020 (shorts). Accessor is **likely 0x4a0d10** (called as `4a0d10(n, 0)` in the transition where 64 calls 1fed10; I did not decompile it). Writer = 0x46d1f0 (below).
- confidence: table address verified (written by 0x46d1f0, promoted by 0x4fd8a0); accessor likely.

### Server DObj table fn - 64-bit rva 0x1fee30, table 0x141381f60
- 32-bit: VA 0x495740, rva 0x95740, cdecl `(int entnum)` -> DObj* or 0; table shorts at **VA 0x145b818** (rva 0x105b818): `movsx eax, word [eax*2+0x145b818]`.
- bytes at VA: 8b 44 24 04 0f bf 04 45 18 b8 45 01 85 c0 74 0c
- confidence: verified (map guess confirmed by decompile and bytes).

### Make pending client DObj - 64-bit rva 0x1fec60 (called from 0xb86f0)
- 32-bit: VA 0x46d1f0, cdecl `(int entnum)`; allocates a handle with 0x601ff0, stores it in the pending table (0x14ab020), copies the server DObj onto it with 0x447d00 (= 64 0x2955d0).
- bytes at VA: 56 e8 fa 4d 19 00 8b 4c 24 08 66 89 04 4d 20 b0
- confidence: verified (same sequence: alloc, store short, copy server->new, error string when pool empty).

### Promote pending -> current - 64-bit rva 0x1fec30 (ent_clean calls it per flagged entity)
- 32-bit: VA 0x4fd8a0, cdecl `(int entnum)`; moves short from 0x14ab020 to 0x14ac128, clears pending.
- bytes at VA: 8b 54 24 04 0f b7 0c 55 20 b0 4a 01 66 85 c9 8d
- confidence: verified

### Clear bit "client DObj present" - 64-bit rva 0x1fefa0 (native.c line 2395), byte array 0x14137fe40
- 32-bit: VA **0x410f50**, cdecl `(int n)`: `byte[0x14ac028 + (n>>3)] &= ~(1 << (n&7))`. Same array is tested by 0x44db60 (= is the bit set).
- bytes at VA: 8b 4c 24 04 8b c1 83 e1 07 b2 01 d2 e2 c1 f8 03
- array VA 0x14ac028 (rva 0x10ac028), 0x100 bytes (2048 bits) before the pending table, like 64-bit 0x14137fe40 before 0x14137ff40.
- confidence: verified (the transition 0x4c1c10 calls it right where 64 0xb8300 calls 0x1fefa0).

### Free client DObj by handle - 64-bit rva 0x1feee0 (`1feee0(n,0)` in ent_clean), used by viewmodel setup (handle 0x800+..)
- 32-bit: VA 0x4e9c70, cdecl `(handle, 0)`. (concluded from ent_clean: same position in the flow as 64 `1feee0`.)
- confidence: likely

---

## 2. The snapshot transition: dobj_make, ent_clean, flags

### dobj_make 0xb86f0 - 64-bit rva 0xb86f0, native.c line 2450 (hook) and 2393 (called by hand)
- 32-bit: **VA 0x56e780, rva 0x16e780** (map guess confirmed). **Register calling convention: EDI = info (the entityState*, `40dd30(n)`), EBX = dobj_info (the server DObj's first
  field = tree pointer, `*(void**)serverDObj` = what 0x43f4e0 returns), no stack arguments, returns EAX (1 made / 0 not), `ret` plain.** It pushes only ESI. Observed
  at the call site 0x4c1d25..0x4c1d35: `call 43f4e0; mov ebx,eax; push ebp; call 40dd30; add esp,4; mov edi,eax; call 56e780` (then `test eax,eax`, no `add esp`).
  The decompile of the function itself shows `unaff_EBX`/`unaff_EDI`. A C hook needs a naked asm stub (read edi/ebx, call the C hook with them as arguments, put them back
  in edi/ebx for the original, preserve ESI); a by-hand call from body_dobj_sync needs `mov edi,info; mov ebx,sinfo; call` inside inline asm with edi/ebx/esi saved.
- bytes at VA: 85 db 56 74 48 0f b6 07 83 e8 0d 74 05 83 e8 02
- the hook's reads: info byte +0 (type) and ushort **+0x7e (entity number): same offsets in 32 bits** (observed `[EDI]`, `[EDI+0x58]`, `[EDI+0x7e]`).
- other facts observed: for types 13/15 (`*info == 0xd or 0xf`) with `0x44ad20(info[0x58])` true it takes a pooled DObj (actor pool, below), else a fresh one via `0x43f4e0` + `0x4e8ba0`
  (= 64 0x2968e0 + 0x1fe600); then `0x4d7810` (= 64 0x29bab0), `0x46d1f0(entnum)` (pending make), `0x4fb220` (= 64 0x296c50), and sets a 3-float table at **VA 0x758660 + entnum*0xc**
  to a float constant (64: 0x140515070, `0x48000000`).
- callers (all four to hook-test): 0x4a5900 at 0x4a5a28 and 0x4a5ae7 (= 64 0xb8000, the snapshot transition), 0x4c1c10 at 0x4c1d35 and 0x4c1e73 (= 64 0xb8300, the one that runs ent_clean).
- confidence: verified (both decompiles compared; convention proven at the call site)

### ent_clean 0xb9320 - 64-bit rva 0xb9320, native.c line 3968
- 32-bit: **VA 0x41a3a0, rva 0x1a3a0** (map guess confirmed). cdecl, no arguments.
- bytes at VA: 81 ec 78 02 00 00 53 55 56 33 f6 57 89 35 88 79
- flag array (64-bit 0x52c300, 64 dwords, bit n = dword n>>5, mask 0x80000000>>(n&31)): **VA 0x86b280 (rva 0x46b280)**, 64 dwords (observed loop `local_270 = 0x40`, `0x86b280`).
  The transition sets bits there after a successful dobj_make (observed `0x4c1d3a..`: `0x410f50(n)` then `(&0x86b280)[n>>5] |= bit`), exactly as 64 native.c's by-hand code does.
- also at the top: it clears `DAT 0x867988` (64: 0x14051b070; rva 0x467988) and calls the anim transfer processor 0x45a8f0 (= 64 0x29a210). The pointer to the current snapshot-entity list is 0x86798c / next 0x867990.
- per-entity inner loop (observed): `4f3c70(n)`, `43f4e0(dobj)`, `4e9c70(n,0)`, free via `4df9b0` (actor pool) or `4e6730` (= 64 0x12c730 / 0x1fe610), `4fd8a0(n)` promote.
- confidence: verified (both decompiles compared line by line)

### Snapshot transition functions - 64-bit 0xb8000 and 0xb8300 (callers of dobj_make / xfer)
- 32-bit: 0xb8000 = **0x4a5900** (rva 0xa5900, 565 bytes, no ent_clean call); 0xb8300 = **0x4c1c10** (rva 0xc1c10, 768 bytes, calls ent_clean 0x41a3a0 first).
- bytes: 4a5900: 83 ec 08 83 3d 14 4c 77 00 00 74 0d 8b 44 24 0c; 4c1c10: 81 ec 18 01 00 00 53 55 33 db 39 1d 88 79 86 00
- evidence: 64 b8300 calls 0xb9320, 0x1fefa0, 0x29a120, 0xb86f0; 32 4c1c10 calls 0x41a3a0, 0x410f50, 0x4275f0, 0x56e780. 4a5900 calls the same minus ent_clean.
- confidence: verified

### Anim transfer queue - 64-bit 0x29a120 (xfer_hook, native.c line 2410), processor 0x29a210, records 0x141c0f240, count 0x141c0f234
- 32-bit: queue function **VA 0x4275f0, rva 0x275f0**, cdecl `(DObj *src /*server DObj*/, DObj *dest /*client DObj*/, float time /*passed as 32-bit*/)`, `ret` plain.
- bytes at VA: a1 6c 08 a5 01 3d 00 04 00 00 72 28 68 3c fc 6a
- queue: records at **VA 0x1a47868**, **0x24 bytes (9 dwords)** each, count at **VA 0x1a5086c** (limit 0x400 as in 64), `DAT 0x1a50868` = processed index (set 0 by the processor). Record layout (observed):
  +0 dest DObj*, +4..+0x18 six dwords copied from src DObj +0x80..+0x94 (64: +0x8c..+0xa3), +0x1c time, +0x20 flag (`tree.animIndex != 0`).
- processor: **VA 0x45a8f0** (rva 0x5a8f0), cdecl no args, loops `0x63f060()` (the per-record apply) for `DAT 0x1a5086c` records. Bytes: 56 33 f6 39 35 6c 08 a5 01 89 35 68 08 a5 01 76. Called from ent_clean start (ctx0 only in native.c).
- the xfer hook's `dest != client_dobj(0)` test: use 0x4f3c70(0). Its "time" argument is the 3rd stack dword; 64 passes it as a float in the third integer register: in 32 bits read it from the stack.
- callers: 0x4a5aca (in 0x4a5900), 0x4c1e56 (in 0x4c1c10) = 64 0xb825d / 0xb85e3.
- confidence: verified for 0x4275f0 (decompile shape identical, record fields match); processor likely (shape and caller match, loop body 0x63f060 not compared)

### Actor client DObj pool - 64-bit rva 0x12c6d0 (112 slots), flags 0xf50ec0, DObjs 0xf50b40, index 0xf50f30
- 32-bit: pool alloc fn **VA 0x4cf000, rva 0xcf000**, cdecl no args, returns the pooled pointer or 0. Slot flags (bytes) **VA 0x10c60ec**, pointers (dword array) **VA 0x10c5f2c**, round-robin index **VA 0x10c615c**; 0x70 = 112 slots (observed). On exhaustion it calls
  `0x466380(0x12, "G_AllocAnimClientTree: failed alloc")`; the 64-bit one returns 0 silently (the 32-bit also returns 0 after the error message: expect an error popup/log line when the pool is full).
- bytes at VA: a1 5c 61 0c 01 33 c9 eb 07 8d a4 24 00 00 00 00
- native.c line 3596 reads `base[0xf50ec0 + k]`: 32: `base[0xcc60ec + k]` (VA - 0x400000).
- free: 0x4df9b0 (= 64 0x12c730).
- confidence: verified

---

## 3. Entity draw path

### add_entity (CG_AddCEntity) - 64-bit rva 0x9c960, native.c line 3829 (add_entity_hook)
- 32-bit: **VA 0x46df50, rva 0x6df50** (map guess confirmed), **cdecl, one stack arg `int n`** (the entity number), `ret` plain.
- bytes at VA: 83 ec 18 53 55 8b 6c 24 24 56 57 8b fd 69 ff 98
- evidence: both read pose origin / angles into locals, call a per-entity pose update (64 0x9ce30 / 32 0x4e2130), compare against the 0xffffff + eType 5 case, then the same
  "moved > threshold" block with the 3-float table (64 0x140515070 / 32 0x758660), then `0x4f3c70(n)` DObj lookups. It ends by calling the add-by-type function below.
- confidence: verified

### add-by-type switch - 64-bit rva 0x9ea20 (size 1102), called at the end of add_entity; native.c line 3741 uses its address range for `p0general`
- 32-bit: split. add_entity calls **0x4470b0** (73 bytes: tests `ushort[+0xd8]` = nextState+4 and calls `0x561e20`, then `(flags+0xe0 & 0x30) != 0x30` -> **0x42a920(centity)**, the type switch on byte `+0xd4`, 116 bytes)
  and then `0x4d3400` if flag bit 0x40000000.
- the 64 cases 0, 2 and 5 call the scene add (0x2bb0) directly inside 0x9ea20 with `eFlags & 0x3f0`; **the 32 counterparts are 0x561d00 (case 0, 90 bytes), 0x561d60 (case 2, 183 bytes), 0x5621d0 (case 5, 196 bytes)**.
  The p0general return-address test therefore becomes: return address in [0x561d00, 0x561d5a) or [0x561d60, 0x561e17) or [0x5621d0, 0x562294). (Case 3, 8 and 13/15 call it from other functions, as in 64 bits.)
  Type 13/15 (players/actors) go to 0x413dc0 = 64 0x894b0.
- bytes: 42a920: 56 8b 74 24 08 0f b6 86 d4 00 00 00 83 f8 0f 77; 4470b0: 56 8b 74 24 08 66 83 be d8 00 00 00 00 74 05 e8; 561d00: 0f b7 86 52 01 00 00 83 ec 0c 57 6a 00 50 e8 5d;
  561d60: 0f b7 86 58 01 00 00 99 b9 c8 00 00 00 f7 f9 83; 5621d0: 0f b7 86 52 01 00 00 83 ec 0c 81 be 60 01 00 00 (all three take the centity in ESI).
- confidence: verified for the dispatch (same cases in the same order); the three case functions identified by their callee sets (case 0: 4f3c70, 503c00; case 2: weapon def 427850 + 4f3c70 + 503c00; case 5: viewhands name check + 503c00), likely that no other direct caller matters.

### pass2 (second per-entity pass) - 64-bit rva 0xb0680, native.c line 3499 (pass2_hook)
- 32-bit: **VA 0x4202d0, rva 0x202d0** (87 bytes), **cdecl, one stack arg `lc`** (`push ebp; mov ebp,[esp+8]` after the push = first argument), plain `ret`.
- bytes at VA: 55 8b 6c 24 08 56 57 33 ff be 48 28 79 00 8b ff
- evidence (observed): loop over entities 0 and 1 only (`pointer < 0x792b78 = 0x792848 + 2*0x198`), `if (0x4215a0(lc) != i && (centity[+0x180] & 1))` then `0x569ba0(lc)` (centity in ESI) and
  `if (DAT 0x85e8cc) 0x438f80(lc, centity)`. 64: same loop to 0x1404413e0, `0xa5ec0`, valid at +0x18c, then the body (0x569ba0 = the inline draw: tests the pointer at centity +0x38 (64: +0x40),
  `4f3c70(number at +0x152)`, `438110` (= 299ee0), `503c00(dobj, centity, number, (eFlags+0xe0 & 0x2f0)|0x104, &origin, 0)`, `40ee20` matrix, `411330` (= c0b80), `4d3400` (= 92c00)).
- confidence: verified

### scene "add DObj" - 64-bit rva 0x2bb0, native.c line 3537 (dobj_add_hook), 8 declared args
- 32-bit: **VA 0x503c00, rva 0x103c00**, **cdecl, 6 stack args: (DObj *dobj, centity *pose, ushort/uint entnum, uint flags, float *origin, float extra)**, `ret` plain.
  64 passes (rcx, rdx, r8, r9) + stack: origin and the float (0) on the stack. For 32 the hook needs exactly the six stack arguments (declare 6; the 8 of the 64 hook are wrong here).
  The callers push `(dobj, centity, number, flags, &origin, 0.0f)`; observed in 0x413dc0 and 0x569ba0 (the number is `movzx ... [esi+0x152]`, only the low 16 bits are the number).
- bytes at VA: d9 ee 83 ec 14 d8 5c 24 2c 53 8b 5c 24 28 57 df
- internal scene list (observed, not needed by the hooks): 0x400 entries x 0x48 bytes at VA 0x1c6c680.
- confidence: verified (map guess confirmed; argument list read from two call sites and the prototype)

### own_write - 64-bit rva 0xb28b0, native.c line 3753 (own_write_hook)
- 32-bit: **VA 0x4904a0, rva 0x904a0** (guess confirmed), **cdecl, one stack arg `lc`** (`sub esp,0x10; push ebp; mov ebp,[esp+0x18]`), plain `ret`.
- bytes at VA: 83 ec 10 55 8b 6c 24 18 56 57 55 e8 f0 10 f9 ff
- **IMPORTANT DIFFERENCE**: in 64 bits the lc -> slot rule is inlined at 0xb28d2 (so hooking 0xa5ec0 does not affect it and native.c moves the write to slot 1 by hand). In 32 bits the function
  does `call 0x4215a0` (observed `iVar4 = FUN_004215a0()` with lc pushed). With 0x4215a0 hooked (ctx1, lc 0 -> 1), own_write_original already writes into slot 1: the hand-made
  "copy s0+0x18 to s1+0x18, restore s0" logic must NOT run in 32 bits (it would overwrite the slot-1 pose with the stale slot-0 one). Keep only the `p0_put(s0)` part.
- what it writes (observed, per slot = `0x792848 + slot*0x198`): origin (3 floats) at **+0x14** (64: +0x18) = lerp(prev ps +0x1c.., next ps +0x1c..); angles at **+0x20** (64: +0x24): pitch 0, **yaw at +0x24** (64: +0x28)
  = lerp of centity float at +0x9c and +0x118 (64: +0xa8 and +0x124), roll 0 at +0x28. The "ps" pointers: DAT 0x75f2c4 (prev) / 0x75f2c8 (next) / 0x75f2c0 (64: 0x40cad0 / 0x40cad8 / 0x40cac0) and the lerp fraction `_DAT 0x774c18`
  (64: 0x42242c); ps float origin at ps+0x1c..0x24 and the eye-height offset `ps+0x118` are the same in both builds (observed). The 64 playerState block in the live cl block 0x535d38 is the 32-bit
  block at ... see section 6 (not my package).
- confidence: verified

### local-client slot (the "mine" rule) - 64-bit rva 0xa5ec0, native.c line 3343
- 32-bit: **VA 0x4215a0, rva 0x215a0**, 66 bytes, cdecl `(uint lc)` -> slot. Flag byte **VA 0x88897c (rva 0x48897c)** (64: 0x52fb5c). Table: first `ushort` at **VA 0x88f568** (64: 0x540bb8), entry stride **0x263c** (64: 0x2650),
  int type at q-8 (== 2 local), ushort local client number at q; end bound: returns 0 when q >= 0x8941e0 (64: q > 0x545857); 2 entries. Table base = q - 0x18 = VA 0x88f550; entry type VA 0x88f560 (the
  "second entry present" test in the num-local-clients fn reads 0x88f560 and 0x891b9c).
- bytes at VA: 80 3d 7c 89 88 00 00 75 05 8b 44 24 04 c3 8b 54
- callers (observed): 0x4202d0 (pass2), 0x440d20, 0x4666c0, 0x4904a0 (own_write), 0x4a7e40, 0x4ba460 (all benefit from the hook, as in 64 bits for the non-inlined ones).
- confidence: verified

### number of local players fn - 64-bit rva 0x6bae0
- 32-bit: **VA 0x4818b0**, cdecl no args. With the coop flag 0 it returns the client-side local player count **VA 0x738c6c (rva 0x338c6c)** (64: 0x3c3eb0; native.c `client_n_sites` patches reads of it; 32 sites are not mapped here).
  With the flag set: `(dword[0x88f560] != 0) + (dword[0x891b9c] != 0)`.
- bytes at VA: 80 3d 7c 89 88 00 00 75 06 a1 6c 8c 73 00 c3 33
- confidence: verified

### foot IK - 64-bit rva 0x89780, call site 0x896d3 (in 0x894b0), native.c line 2437 (ik_hook)
- 32-bit: function **VA 0x55c640, rva 0x15c640** (463 bytes), **cdecl `(centity *ent, DObj *dobj)`**, plain `ret`. Single call site **VA 0x413e89 (rva 0x13e89)** inside 0x413dc0 (= 64 0x894b0); bytes at the site: e8 b2 87 14 00 83 c4 08 (call, then `add esp,8`: two stack args).
- bytes at VA 0x55c640: 83 ec 50 53 8b 5c 24 5c 55 56 8b 74 24 60 33 c0
- state per entity: table of **0x48-byte entries at VA 0x75e6a8 (rva 0x35e6a8)** (64: 0x440770), allocated by index byte at **ent+0x49** (64: +0x51) with `0xff` = none. Per-context separate state: the block is in the swapped range as in 64 (swap ranges are another package's).
- evidence: 0x413dc0 matches 0x894b0 (same call order: DObj lookup, the lean/velocity fn, origin block with the +0x1c smoothing, ragdoll call, **IK**, `438110` (= 299ee0), `503c00`, `4fb530` (= a5b10), `48ed80` (= a57f0), `4d3400` (= 92c00)); 0x55c640 does the same bone lookups (`0x41f630` = 296020) and
  the 0x48-byte state slot logic with `0xff`/2/10.
- the map.txt guess for 0x89780 was none: this one is new.
- confidence: verified (both decompiles compared)

### the player / actor entity add (64 0x894b0) - for orientation
- 32-bit 0x413dc0 (rva 0x13dc0), cdecl `(centity *)`. Its direct callers include the type switch 0x42a920 (cases 13, 15). Bytes: see the file (not hooked by native.c).

---

## 4. centity_t offsets the hooks read (64-bit -> 32-bit)

Base 0x441070 / stride 0x1a8 -> base VA 0x792848 / stride 0x198. "float idx" in native.c = byte offset / 4.

| field | 64 offset (float idx) | 32 offset (float idx) | how known |
|---|---|---|---|
| pose header (lighting, eType, localClientNum, ragdoll handles; p0keep bit 0 keeps it) | 0x00..0x18 | **0x00..0x14** | origin starts at 0x14 (413dc0, 569ba0, own_write); the 64 header has 4 more bytes. likely |
| pose origin (3 floats; pidx 6) | 0x18 (6..8) | **0x14 (5..7)** | observed: 0x413dc0 `param_1+0x14/0x18/0x1c`, own_write `0x79285c = base+0x14`; verified |
| pose angles (pitch/yaw/roll) | 0x24 (9..11) | **0x20 (8..10)** | observed: own_write zeroes +0x20 and +0x28, writes yaw at **+0x24**; verified |
| pose yaw (float idx 10) | 0x28 (10) | **0x24 (9)** | same; verified |
| rest of pose (p0keep bit 1: +0x30..+0x60) | 0x30..0x60 | **0x2c..0x58** | likely: currentState starts at 0x58 in 32 (0x64 in 64). Inside it the 64 pointer at +0x40 is at **+0x38** (observed: pass2 `[esi+0x38] != 0`), shifts are not uniform: treat the whole range as "pose, 32-bit own data" |
| "currentState" eFlags dest (copy target in transition) | 0x70 | **0x64** | observed in ent_clean copy (`0x7928ac`): 0x1c dwords from nextState+0xc to it; verified |
| current yaw (float idx 42) | 0xa8 (42) | **0x9c (39)** | observed own_write `0x7928e4`; verified |
| current origin (pidx 32) | 0x80 (32..34) | **0x74 (29..31)** | concluded: yaw-0x28 in both builds (64 0xa8-0x28); likely |
| next yaw (float idx 73) | 0x124 (73) | **0x118 (70)** | observed own_write `0x792960`; verified |
| next origin (pidx 63) | 0xfc (63..65) | **0xf0 (60..62)** | concluded: yaw-0x28; likely |
| nextState begins | 0xe0 | **0xd4** | observed (ent_clean copy dest `0x79291c`); verified |
| eType (byte, `e[0xe0]`) | 0xe0 | **0xd4** | observed: 0x42a920 switch `[param_1+0xd4]`, 0x413dc0 `==0xd`; verified |
| nextState+4 ushort | 0xe4 | 0xd8 | observed: 0x4470b0 `[+0xd8]`; verified |
| eFlags (u32; hide bits 0x30, `&0x3f0`) | 0xec | **0xe0** | observed: 0x4470b0 `& 0x30`, 0x413dc0 `& 0x2f0`; verified |
| weapon index (`+0x15c`) | 0x15c | **0x150** | concluded (nextState+0x7c); likely |
| entity number ushort (`+0x15e`) | 0x15e | **0x152** | observed in every 32 draw fn (`[esi+0x152]`); verified |
| ushort in cleanup (client no / tag) | 0x164 | 0x158 | observed ent_clean `0x7929a0`; verified |
| int compared with 0xffffff | 0x16c | **0x160** | observed `0x7929a8`; verified |
| "animation tree pointer" `+0x170..0x178` (p0keep bit 2) | 0x170 | **0x164..0x16c** | concluded: it is inside the 0xac entityState (nextState+0x90, two dwords), NOT a pointer, same offset relative to nextState; likely |
| "valid" dword (bit 1 = entity present) | 0x18c | **0x180** | observed: 0x4202d0 `[+0x180] & 1`, ent_clean `0x7929c8`; verified |
| tail after valid (copy keeps `+0x178..` region) | 0x178..0x1a8 | 0x16c..0x198 | consequence of the above; likely |
| struct size | 0x1a8 | **0x198** | verified |

Consequences for the code that copies whole slots (`p0_ent[0x1a8]`, `saved[0x1a8]`, `t[0x1a8]`, `uint8_t *s1 = s0 + 0x1a8`): use stride 0x198. `slot + 0xec` -> `+0xe0`, `+0x18c` -> `+0x180`. The yawkeep writes to floats 10/42/73 -> **float idx 9/39/70**, pidx 6/32/63 -> **5/29/60**.
Everything in `native.c` lines 3846-3912 that indexes floats of `base+0x441070` must use the new indices.

Entity-info (entityState) fields read through `info` by dobj_make: type +0, entity number ushort +0x7e, byte +0x58: unchanged in 32 bits.

---

## 5. Viewmodel hooks (contexts_install sites 0xc7499, 0xc41b1, 0xc58ed, 0xc594a, 0xc5dfb, 0xc6033)

### viewmodel_blend (call at 64 0xc7499 -> fn 0xc6ee0), native.c line 4002
- 32-bit: fn **VA 0x572150, rva 0x172150** (219 bytes). **Not a plain cdecl: the DObj pointer is passed in ESI**; stack args `[esp+4] = float a`, `[esp+8] = int which (0x23 test)`; the caller pops 8 (`add esp,8` after the call), the function returns with plain `ret`.
- call site: **VA 0x5723ec (rva 0x1723ec)**, bytes e8 5f fd ff ff 83 c4 08, in 0x572310 (= 64 0xc73b0). ESI there = `[eax*4 + 0x85e8a4]` with eax = hand*5 (observed at 0x572320): the per-hand DObj slot.
- bytes at fn: 83 7c 24 08 23 d9 ee 6a 00 75 40 6a 01 6a 00 83
- hook rule (64: `if (!tree || !*(void**)tree) return;`): in 32 bits test **ESI** (`ESI == 0 || *(void**)ESI == 0` -> return without calling): a naked stub that checks `esi`/`[esi]` then `jmp original` or `ret`.
  In the function ESI is the first argument of the 8-argument animation-set call (`4632c0(ESI, 0x23, ...)` = 64 0x29e3b0(param_2, 0x23, ...)) and `0x43f4e0(ESI)` = `*dobj` = 64 0x2968e0(param_2): this is how I know it is the DObj.
- confidence: verified (decompiles compared, ESI source seen)

### viewmodel_setup (64 fn 0xc6480), native.c line 4013
- 32-bit: fn **VA 0x572ba0, rva 0x172ba0**, **cdecl `(int lc, ps*, int weapon, int param_4, char param_5, char param_6)`** (6 stack args; callers `add esp,0x18`), plain `ret`. Handle used: `local_3c + 0x800 + lc*2` (**same formula as 64**: 0x800 + 2*lc + hand); 4e9c70 frees it, 0x454790 makes the DObj with that handle (client table 0x14ac128 must hold >= 0x804 entries).
- bytes at fn: 83 ec 3c 55 8b 6c 24 4c 85 ed 0f 84 2c 04 00 00
- call sites (all `e8 rel32`, then `add esp,0x18`): **0x4b2e52** (in 0x4b2e00 = 64 0xc5fd0, 64 site 0xc6033), **0x4acea0** (in 0x4acd80 = 64 0xc5b70, site 0xc5dfb), **0x40b72a** (in 0x40b6f0 = 64 0xc4150, site 0xc41b1), **0x4af6c1 and 0x4af719** (in 0x4af680 = 64 0xc5880, sites 0xc58ed and 0xc594a).
  As rva: 0xb2e52, 0xacea0, 0xb72a, 0xaf6c1, 0xaf719. bytes at sites: 4b2e52 e8 49 fd 0b 00 83 c4 18; 4acea0 e8 fb 5c 0c 00 83 c4 18; 40b72a e8 71 74 16 00 83 c4 18; 4af6c1 e8 da 34 0c 00 83 c4 18; 4af719 e8 82 34 0c 00 83 c4 18.
- per-hand state block that native.c clears when a context starts (64: 0x440730 + 0x20 * hand, entries: DObj* at +0, tree* at +8, int -1 at +0x18): **32-bit VA 0x85e8a4 (rva 0x45e8a4) + 0x14 * hand: DObj* at +0, tree* at +4 (freed by 4e6730), +8 a model-ptr, +0x10 = 0xffffffff**. (observed in 0x572ba0: `(&0x85e8a4)[h*5]`, `0x85e8a8`, `0x85e8ac`, `0x85e8b0 = -1`.) Stride 0x14 in 32 vs 0x20 in 64 (per hand, not per client).
- confidence: verified (matching handle formula, same structure of callers). The hook's replacement of arg 0 by 1: `[esp+4]`.

### viewmodel advance - 64-bit rva 0x29a3d0 (`advance tree by dt`; native.c comments only)
- 32-bit: **VA 0x4023f0**, cdecl `(DObj*, float dt, int)` (called `4023f0(handle_slot_dobj, _DAT 0x6e8a14, 0)` in 0x572ba0 where 64 has `29a3d0(plVar20[-1], uVar2, 0)`). Bytes: 83 ec 20 56 57 8b 7c 24 2c 8b 37 85 f6 c7 05 60. Confidence: likely (map guess + call context).

---

## 6. Aim targets (the entity parts of aim_targets_update, screen_targets_hook, target_list_hook)

### screen target update - 64-bit rva 0x6a570 (call at 0xbd2c5 in 0xbca00), native.c line 4024
- 32-bit: fn **VA 0x488a80, rva 0x88a80**, **cdecl `(int lc, float*eye, float*origin, float*angles, uint a, float b)`** (six stack args; `sub esp,0x98; ... mov esi,[esp+0xa8]`), plain `ret`. Single call site **VA 0x466adc (rva 0x66adc)** inside 0x4666c0 (= 64 0xbca00, the cgame draw), bytes e8 9f 1f 02 00 d9 05 a4.
- bytes at fn: 81 ec 98 00 00 00 53 55 56 8b b4 24 a8 00 00 00
- aaGlob: 64 base 0x3cf180 stride 0xe80 -> **32 base VA 0x73ba90 (rva 0x33ba90) stride 0xe80** (observed: `iVar5 = lc*0xe80`, `&0x73ba90 + iVar5` passed to the list insert 0x559e70; "active" byte at +0xb0 = 0x73bb40; sorted-list count at +0xe3c (0x73c8cc - 0x73ba90); 64: 0x3cffbc - 0x3cf180 = 0xe3c, same).
- it reads the entity data through centity: valid `[+0x180] & 1`, the pose angles `&0x792868 + n*0x198` (= +0x20) and origin at +0x14 (observed); entry size 0x2c from the target list (same as 64).
- confidence: verified

### target list getter - 64-bit rva 0x6b230 (call at 0x6a711), native.c line 4029
- 32-bit: **VA 0x4a7e40, rva 0xa7e40**, cdecl `(int lc, void **list, int *count)`; internally `call 0x4215a0`. Single caller **0x488a80 at VA 0x488ba8 (rva 0x88ba8)**, bytes of the site e8 93 f2 01 00 83 c4 14 (five args pushed... the call is `4a7e40(lc, &list, &count)` after `push ecx; push esi`; `add esp,0x14` cleans more than this call).
  Note: after this call the caller does one `add esp,0x14`, i.e. the compiler batched the cleanup of earlier pushes too; a stub that replaces the call must be plain cdecl `(lc, list, count)` that returns with `ret` (not `ret 12`) and must not touch the stack above its three arguments.
- bytes at fn: 8b 44 24 04 50 e8 56 97 f7 ff 69 c0 0c 21 00 00
- lists: **aimTargetGlob** (64: 0x3d0530, stride 0x210c, list being built at +0, count +0xb00, published list +0x1608, count +0x2108) -> **32 base VA 0x73cec0 (rva 0x33cec0), stride 0x210c** (observed: published list `0x73e4c8 + slot*0x210c` = base+0x1608, count `0x73efc8` = base+0x2108; the 0xb00 and 0x2c entry size are the same as 64). Confidence verified for +0x1608/+0x2108; +0 and +0xb00 concluded.
- confidence: verified

### AimTarget_ProcessEntity - 64-bit rva 0x6b5a0 (the PC build never calls it; native.c reimplements the loop)
- 32-bit: **VA 0x40dc60 (rva 0xdc60)**, cdecl `(gentity *)`. bytes: 57 8b 7c 24 08 80 bf ac 00 00 00 00 74 31 8b c7. Loop calls the per-client fn **0x55b040** (below) for `0 .. 0x4818b0()-1`.
- confidence: verified. It tests `0x55ac80` (see below) where 64 tests inline.

### per-client aim target check - 64-bit rva 0x6b630 `(client, gentity)` (native.c `for_client`)
- 32-bit: fn **VA 0x55b040 (rva 0x15b040)**, **register convention: gentity in EAX, client as the stack arg [esp+4]**, caller pops 4 (observed in 0x40dc60: `push esi; mov eax,edi; call 55b040; add esp,4`), plain `ret`. 270 bytes.
- bytes at fn: 8b 4c 24 04 83 ec 2c 53 56 57 8b f9 69 ff 70 02
- target-list insert (64 0x6a9a0 `(client, gentity, buf)`) -> **0x55af20** (EAX = gentity, `[esp+4]` client, `[esp+8]` buffer).
- to port the by-hand call: `mov eax, gentity; push client; call 0x55b040; add esp,4` (inline asm).
- confidence: verified

### g_entities (gentity) used by aim_targets_update - 64: array 0xd9e070 stride 0x2a0, level.num_entities 0xd96e80
- 32-bit: **array VA 0xea76b8 (rva 0xaa76b8), stride 0x270** (the level pointer **VA 0x10a3c14** is set to 0xea76b8 in G_InitGame 0x4bf8b0 at 0x4bfbfd; every aim function uses `n*0x270 + [0x10a3c14]`).
  level.num_entities: **VA 0x10a3c18 (rva 0xca3c18)** (concluded: written in the same G_InitGame, +4 after the pointer like +8 in 64; likely).
- 64 -> 32 field offsets (observed in 0x55ac80 / 0x55acd0 / 0x55af20 / 0x55b040 / 0x40dc60):
  - type byte (13 = actor) +0 -> +0 (`*in_EAX == '\r'`)
  - in-use bytes +0xac / +0xb0 -> **+0xac / +0xb0** (same)
  - eFlags +0xc (0x2000 test) and `+0x8c == 0xffffff` -> same
  - origin (currentOrigin) +0xe8/+0xec/+0xf0 -> same
  - entity number ushort +0x7e -> same (entityState part)
  - health (int) +0x164 -> **+0x14c**
  - sentient pointer +0x118 -> **+0x110**; the team int inside it: sentient+8 -> **sentient+4**; team values as in 64 (mask 0xe = teams 1..3; the axis test `team == 1` is unchanged)
  - the "seen by client" bits (`0x80000 << client`), 64 +0x150 -> **+0x138** (mask 0xffe7ffff clears both clients' bits in both builds)
  - client pointer +0x108 -> +0x108 (observed `[edi+0x108]`; the ps team at client+4 used in the checks)
- confidence: verified for the fields above; array base and num_entities likely (static initializer value read from code, not from memory).

---

## 7. Lag fix, p0 copy, own entity code: what to change

- All the `0x441070`, `0x1a8`, float-index and `+0xec`/`+0x18c`/`+0x15e`/`+0x15c`/`+0x164` accesses: see section 4.
- Player state reads in lag_fix_yaw / p0_put (`ctx_store[k][5] + CL_BLOCK_PS + 0x10c` = ps view yaw) belong to the client-block package (not mine): in 32 bits `own_write` shows ps origin at `ps+0x1c..0x24` in the same form as 64.
- `CFG_ONCE` strings and QA logs that read `0x1fece0` -> `0x4f3c70`, `0xf50ec0` -> `0xcc60ec`, `0xd9e070 + ent*0x2a0 + 0x150` -> `0xaa76b8 + ent*0x270 + 0x138` (the last is a QA log only).

---

## 8. No 32-bit counterpart found / not done

- **Tree node table `base + 0x1bc0a30` stride 0x40 (treelog QA, native.c 3634)**: not looked for in 32 bits (tree idx lives at tree+4; QA only). unknown.
- **0x29a0c0 (server tree advance, native.c comment), 0x2311d0 (G_RunFrame call), 0x9d8b0 (viewmodel dt), 0x9f9a0**: not mapped (comments only, no hook). unknown. 0x9f9a0's map guess 0x49a4d0 not checked.
- **0xcb4f0 msgwin, 0xa3b80 hudelem, 0x93ac0 name loop, 0x93daa / 0x93e2f name stub/call, 0xd3110 name getter**: not in my package (HUD); map.txt guesses 0x623f50 / 0x563f90 / 0x55ef10 are unverified by me.
- **hud_c6070 / c14e0 / 94d70 / x0..x6 call sites at 0xbd034..0xbd206**: diagnostics only (skiphud, default 0); not mapped here (0xc5b70's 32 twin is 0x4acd80, verified through the viewmodel_setup call site).
- **Pose partition for p0keep** (bits 1 and 2) is only "likely": see section 4; a run-time check is cheap: log which dwords of slot 0..1 differ before/after a copy (the existing entdump QA does this).
- The XModel name offset (+0) in 32 bits: not proven (section 1).
- The pending-table accessor 0x4a0d10 and the processor body 0x63f060: not decompiled.

## 9. Observations vs guesses (summary of the evidence quality)

- Observed in both builds and compared: dobj tables and pool, ent_clean, dobj_make, transition callers, xfer queue, add_entity, pass2, own_write, IK, viewmodel setup/blend, aim list/process entity, local slot rule, num local players.
- Concluded (offsets by constant shift or from one read): pose partition, origin triples of current/next, weapon index, 0x170 region, level.num_entities, g_entities array base.
