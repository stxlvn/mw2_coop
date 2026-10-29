# Package: render and compositor (native.c lines 1-1610, 2730-2830, 2973-2990)

Conventions: VA = virtual address in the 32-bit exe (image base 0x400000), rva = VA - 0x400000 (`native.c` takes `base + rva`). Ghidra's 32-bit project has
image base 0, so the addresses Ghidra prints are VAs. All "bytes at VA" are from `image32.bin`. "verified" = both decompiles compared; "likely" = one
side read closely and the other matches in role/neighbours; "unknown" = not found.

How it was mapped: the 64-bit and 32-bit decompiles were compared function by function (same callees in the same order, same constants, same strings:
"wait renderer2", "wait %s", "exec cmds", "end frame", "Post Effects", "Emissive", "SunPost", "subwindow to draw: left, right, top, bottom"). The matcher
(`map.txt`) had only 3 of the functions below right (`bca00`, `300a0`, `8150` was mapped to the right function); its `93ac0 -> 55ef10` and `d5890 -> 57ac90`
are wrong or incomplete (see the end).

## Summary: what is different from the 64-bit code (read this first)

1. **Two hooked renderer functions take the scene pointer in ESI**, not on the stack: the scene's 2D pass (64: rva 0x63680) and the 2D command list
   (64: rva 0x5c010). A C function cannot receive ESI; both hooks need a small naked stub (`push esi; call c_hook; add esp,4; ret`, and to call the original:
   `mov esi,scene; call original`). The other hooked functions are plain cdecl with stack arguments. The scene function that calls the 2D pass
   (64: 0x5c240) takes its scene on the stack (cdecl).
2. **The three 2D placements are not at a uniform stride**: 0x937500 (the view placement, 0x6c bytes then the mode dword at 0x93756c), 0x937578 and
   0x9375e8 (0x70 apart). The order is also reversed against 64-bit (see below). Use an explicit list of three addresses, not `base + n * 0x70`.
3. The scene struct is smaller (stride 0xe40, 64: 0x13a0) and the renderer's frame data set is 0x12bb00 bytes (64: 0x13d700). The field `half_note` /
   `half_find` rely on (the cgame time at scene+0x180) **is at the same offset 0x180**.
4. Video globals, the cgame aspect, the D3D device pointer, cg.time, serverTime, the entity slot array all have their own 32-bit addresses (table at the end).
   The entity slot array has stride 0x198 (64: 0x1a8).
5. Not addresses but build breaks in this part of native.c: `watchdog_thread` reads `ctx.Rsp`, `ctx.Rip` and `uint64_t` stack words (32-bit: `Esp`, `Eip`,
   4-byte words); `scan_place` uses `IMAGE_NT_HEADERS64`; the compositor itself (D3D9 vtable slots 3, 16, 17, 34, 37, 41, 42, 43, 47, 75, 81-84, 121, 132)
   is identical in 32-bit, the typedefs are already `STDMETHODCALLTYPE`.
6. The 64-bit single function that builds the view (rva 0x7e40) is split into pieces in 32-bit (below), so there is no one place to hook it; native.c does
   not hook it, it only sets the dvar's value, which works as before.

## Hook targets and call sites

### RVA_CG_DRAW - 64-bit rva 0xbca00, native.c line 28: the cgame's DrawActiveFrame (local client, int, int)
- 32-bit: VA 0x4666c0, rva 0x0666c0, convention **cdecl**, arguments (int local_client, int, int), void; size 1269. Callers: 0x57ae1a (inside 0x57adc0, the
  client frame; pushes 0,0,0) and 0x45cd9c (inside 0x45cb50; pushes 0, esi, edi). Epilogue `add esp,0xc; ret` (bytes at 0x466bb5: `5d 88 1d d8 e1 78 00 5b 83 c4 0c c3`).
- bytes at VA: 83 ec 0c 53 55 33 db 53 e8 33 0e 0a 00 53 e8 5d  (the 64-bit check bytes `48 8b c4 55 56 41 57 48 81 ec c0 00 00 00` have no
  counterpart; compare only the first 8: `83 ec 0c 53 55 33 db 53`; bytes 9-13 are a relative call).
- evidence: 64 calls in order 7c50(0), 11d710(0), dad10(), stores the local client to a global, d4a10, bff00, **8150(&refdef,0)**, a9c40, 11dca0 ... **c840()** ...
  be710 ... **8150(&refdef,0)** again, 11f0c0, ... 433360-twin. 32 calls 507500(0), 4f2f30(0), 4bb380(), stores param_1 to 0x75e688, 40c2e0, 413c90, **507a00(&0x77fad0,0)**,
  492b00, 4b7c20, ..., **50d800()** ..., 571120, ..., **507a00(&0x77fad0,0)** again, ..., 433360(param_1). Same constants (0x7ff "no entity", flag tests `>>10 & 1`, `& 8`).
  Local client global: 64 0x51deb8 (DAT_14051deb8), 32 0x75e688. Refdef: 64 0x14042d2f8, 32 0x77fad0; the cgame stores cg.time to refdef+0x58 in both
  (`_DAT_0077fb28 = DAT_00774c20`).
- confidence: verified

### RVA_BEGIN_FRAME - 64-bit rva 0xc840, native.c line 29: swaps the renderer's two frame data sets
- 32-bit: VA 0x50d800, rva 0x10d800, convention cdecl, void(void); size 619; epilogue `5e 5b 59 c3`. Called once per frame from the cgame draw (0x4666c0).
- bytes at VA: 51 53 56 e8 88 67 f6 ff 33 db 84 c0 74 1b 38 1d
- difference: starts with `FUN_005258d0(-1,"wait_renderer2")` then `FUN_0052afc0()` (the engine's help-the-workers loop; 64: `FUN_1400301e0(FUN_1402039f0)`
  = the same loop with its predicate as argument), then, if DAT_01c931c8 == 0, 0x4a07f0.
- frame data (offsets 64 -> 32): index byte toggled `(x-1)&1`: 0x4458fa64 -> VA 0x1c93264; set base 0x458fb00 -> VA 0x1c93300 (rva 0x1893300); set size 0x13d700 -> **0x12bb00**;
  current set pointer DAT_14410fd00 -> DAT_01c93238; views: count at set+0x41a34 -> **+0x419c4**, array pointer +0x41a38 -> **+0x419c8**, current view index +0x41a30 -> +0x419c0,
  flags +0x41a60 (byte) -> +0x41a38, view-info count +0x419e8 -> **+0x41980**, view infos (0x150 each) at +0x1100 in both, command-ring pointer DAT_14458fab0 -> DAT_01c93268.
- evidence: both reset the same sequence of fields to 0 after the toggle (set+0x1640, 0x1680, 0x16a0 ... +0x16e0, +0x1710 in 64; +0x1640, 0x1660, 0x1680, 0x16a0, 0x16c0 in 32; pointer-size shifts).
- confidence: verified (function, call order); offsets listed are likely-to-verified (read in both decompiles).

### r_subwindow dvar - 64-bit rva 0x4116158 (RVA_SUBWINDOW_DVAR), native.c line 30
- 32-bit: the pointer variable is at VA **0x1f19f98**, rva **0x1b19f98**. The value is at `dvar + 0x10` (4 floats left, right, top, bottom; offsets +0x10, +0x14, +0x18, +0x1c): the
  same as in 64-bit.
- evidence: Dvar_RegisterVec4 (0x49f330) is called at 0x51caa5 with the name string at VA 0x721d5c "r_subwindow" and description 0x721d30 "subwindow to draw: left, right, top, bottom";
  the result is stored by `mov [0x1f19f98],eax` at 0x51cac1 (the store at 0x51ca7c to 0x1f19fe0 belongs to the dvar registered just before). The only reader of 0x1f19f98 is 0x505551 inside 0x505530
  (the view setup, below). 64: registered in 0x21660 (call at 0x21b69, stored at 0x144116158 at 0x21b8a), read in 0x7e40 at 0x7e95.
- note: image32.bin holds 0x01970cb0 there (the static placeholder before registration); read the pointer after the game registered its dvars, as native.c already does.
- confidence: verified

### How the renderer's view setup reads it - 64-bit rva 0x7e40 (called from 0x8150 and 0x8210)
- In 32-bit the function is split. **0x505530** is its first half, in a register convention (ESI = output view struct + 0x100, EDI = input view parameters, no stack args), 253 bytes. It writes
  out[+0x100..0x108] = in[2..4]; matrix piece via 0x4b5940; out[+0x130] = dvar.left (dvar+0x10), out[+0x134] = dvar.top (dvar+0x18),
  out[+0x138] = max(left + epsilon (DAT 0x6a2138), dvar.right (dvar+0x14)), out[+0x13c] = max(top + epsilon, dvar.bottom (dvar+0x1c)), out[+0x140] = in[0], out[+0x144] = in[1],
  out[+0x148] = fov-ish, out[+0x14c] = -(dvar 0x1f19f94 value). These are **the same offsets as the 64-bit 0x150-byte view info** (+0x130..+0x14c).
- Callers of 0x505530: 0x507a00 (static "view" struct 0x1c86900) and 0x507b10 (the scene queue, per-view info slot).
- confidence: verified (arithmetic and offsets compared line by line)

### 64-bit rva 0x8150 (view setup wrapper, called twice per frame, reads the refdef)
- 32-bit: VA 0x507a00, rva 0x107a00, cdecl, (refdef *, int); size 224; plain `ret` (0x507ae0: `c3`).
- bytes at VA: a1 80 a0 f1 01 80 78 0d 00 53 8b 5c 24 08 56 57
- inside: if dvar(0x1f1a080)[0xd]: memset(0x1c86900, 0, 0x150), 0x505530, 0x402c00, 0x441540, 0x504820, 0x5048e0; then 0x507940(param_2) (64: 0x8090); then stores refdef+0x4c/+0x50/+0x54 to
  0x1c5181c/0x1c51820/0x1c51824 and refdef+0x58 (the time) to **0x1c51814**, `(float)time * 0.001` to 0x1c51818 (64: 0x1440d29c0..0x1440d29d0).
- confidence: verified

### The scene queue (64: 0xca4b0 -> 0x8210, "submit the scene")
- 32-bit: wrapper VA 0x449660 (cdecl, (refdef *)), calls **0x507b10** (rva 0x107b10, cdecl, (refdef *, undefined4); 754 bytes): allocates 0x3f38 bytes from the arena (0x4ce360; 64: 0x4708), writes the
  scene's command-list pointer into `scene_array[view_index*0xe40 + 0x2f4]` (64: +0x308), memsets the 0x150-byte view info at set+0x1100+idx*0x150, increments the count at set+0x41980, runs 0x505530 on it,
  then 0x5064b0 (64: 5160, the scene fill; 3757 bytes) which calls **0x505f80** (64: 4a70, copies view info + the time block into the scene).
- bytes at VA 0x507b10: 55 8b ec 83 e4 f8 83 ec 1c 53 56 57 68 38 3f 00;  0x449660 is not hooked by native.c.
- confidence: verified for roles; likely for the sub-function names

## The scene struct (the one the renderer's 2D hooks receive)
| field | 64-bit | 32-bit | how known |
|---|---|---|---|
| view info copy (0x150 bytes: matrices, viewport rect +0x130..) | scene+0 | scene+0 | 64 4a70 copies 0x150 bytes of param_3; 32 505f80 copies 0x54 dwords |
| time block (dword 0 = cg time ms, dword 1 = float seconds, origin x y z, ...) | scene+0x180, 0x30 bytes | **scene+0x180**, 0x28 bytes | 64: `param_1[0x30]=_DAT_1440d29c0..` in 4a70; 32: 505f80 `puVar4=&DAT_01c51814; puVar5=param_1+0x60; 10 dwords` |
| source of the time block | 0x1440d29c0 (rva 0x40d29c0), written by 0x8150 from refdef+0x58 | **0x1c51814** (rva 0x1851814), written by 0x507a00 from refdef+0x58 | both decompiles |
| view type (2 = main 3D view) | scene+0x1380 | scene+0xe20 | `cmp [esi+0xe20],2` at 0x553798; 64 `if (*(lVar4+0x1380)==2)` |
| frame-data back pointer | scene+0x1378 | scene+0xe18 | `mov [esi+0xe18],edi` at 0x55379f |
| command list pointer (tested by the 2D command list function) | scene+0x308 | scene+0x2f4 | first instruction of 0x553050 `cmp [esi+0x2f4],0`; 64 5c010 `*(param_1+0x308)` |
| scene stride in the frame set's array | 0x13a0 | **0xe40** | `add ebp,0xe40` at 0x5537b6 |
| viewport ints (x y w h as ints) | scene+0x150..0x15c | scene+0x150..0x15c (558af0 reads `[esi+0x150]`, `[esi+0x154]`) | likely |
- `scene2d_hook` reads `*(int *)((uint8_t *)vp + 0x180)` = refdef.time = the cgame's cg.time of that frame: unchanged in 32-bit. `half_note()` must pass the 32-bit cg.time at **VA 0x774c20** (below).
- confidence: verified

## 2D pass of the scene - 64-bit rva 0x63680, called at 0x5c5bf inside 0x5c240 (native.c lines 1225-1280)
- The caller (64: 0x5c240, scene draw, 978 bytes, reached through 0x5c620 for draw-path selector > 2) is in 32-bit **VA 0x5533f0** (rva 0x1533f0), cdecl, scene on the stack
  (`sub esp,0x18; push ebx; push ebp; mov ebp,[esp+0x24]`), 849 bytes, bytes `83 ec 18 53 55 8b 6c 24 24 83 bd a8 01 00 00 01`. Its caller is **0x5538e0** (64: 0x5c620) and the selector is
  DAT_0221e7a8 (0: clear only, 1: 0x552ec0, 2: 0x553280, else 0x5533f0). Scene+0x1a8 (64: +0x1b0) tests ==1; +0x1ac / +0x1ad (64: +0x1b4 / +0x1b5) are the same flags.
- The passes at the end of 5c240 (64) and 5533f0 (32), in order: 62c70 / 558640, 62890 (conditional) / 5584a0, 62e80 / 5587e0, 63090 / 558960, **63680 / 558af0**, 5c170 / 553300, 4e030 / 543570,
  5b7f0 / 552ae0. The log markers show it: "Emissive" is logged right before the call at 0x5536fc, "SunPost" right after.
- **32-bit function: VA 0x558af0, rva 0x158af0**, size 193, **convention: scene pointer in ESI, no stack arguments**, returns void (`ret`, `mov esp,ebp; pop ebp` epilogue). It begins
  `push ebp; mov ebp,esp; and esp,-16; mov eax,0x16b0; call 0x672b00 (alloca probe)` and uses `[esi+0x8d0]`, `[esi+0xe20]`, `[esi+0x150]`, `[esi+0x154]` at once.
- bytes at VA 0x558af0: 55 8b ec 83 e4 f0 b8 b0 16 00 00 e8 00 a0 11 00
- **hook site: the call at VA 0x5536fc, rva 0x1536fc** (64: 0x5c5bf); bytes: `e8 ef 53 00 00 68 3c 9c 72 00 6a ff e8 c3 21 fd` (e8 = relative call; only caller). ESI = scene (`mov esi,ebp` at 0x55360b, no later write to ESI before the call).
  The hook stub must pass ESI to `scene2d_hook(void *vp)` and must load ESI before calling the original.
- confidence: verified (function, site, ESI origin checked in the disassembly)

## Final 2D (command list) - 64-bit rva 0x5c010, called at 0x4545b inside 0x453c0 (native.c lines 1282-1303)
- 64: the render thread's frame function 0x45690 ("exec cmds" / "end frame") calls 0x453c0; its default branch loops over the views and for views of type 2 calls `0x484c0(scene)` then `0x5c010(scene)`: the call at 0x4545b.
- 32: the frame function is **0x5397e0** (cdecl void, 248 bytes, bytes `51 53 56 8b 35 74 4c 53 02 c7 05 24 e8 21 02 01`; strings "exec cmds", "end frame"), calls **0x539680** (64: 0x453c0, 346 bytes,
  bytes `55 8b ec 83 e4 f8 a1 b4 e5 21 02 f6 80 38 1a 04`), which for the default path calls **0x553750** ("Post Effects" loop, 387 bytes). Its loop (VA 0x553790):
  `mov esi,[edi+0x419c8]; add esi,ebp; cmp [esi+0xe20],2; mov [esi+0xe18],edi; jnz skip; mov eax,esi; call 0x53efa0 (64: 0x484c0, scene in EAX); call 0x553050`.
- **32-bit function: VA 0x553050, rva 0x153050**, size 240, **convention: scene pointer in ESI, no stack arguments**; first instruction `cmp dword ptr [esi+0x2f4],0`. It ends with a tail jump (`5f e9 c2 64 fe ff`
  at 0x553138), so a replacement must call the original as an ordinary function.
- bytes at VA 0x553050: 83 be f4 02 00 00 00 57 0f 84 e0 00 00 00 6a 00
- **hook site: the call at VA 0x5537ae, rva 0x1537ae** (64: 0x4545b); bytes `e8 9d f8 ff ff 83 c3 01 81 c5 40 0e 00 00 3b 9f`. A second call of the same function exists at 0x55316e (in 0x553140, the
  selector 1/2 paths): `e8 dd fe ff ff 83 c3 01 81 c5 40 0e 00 00 3b 9f`; native.c does not hook the equivalents in 64-bit (0x45537, 0x45577), keep that.
- confidence: verified

## Worker wait before a context swap - 64-bit rva 0x300a0 (native.c line 1714)
- 32-bit: VA **0x52ac60**, rva 0x12ac60, size 110, convention **cdecl, void (int type)** (arg at [esp+4] on entry; `push ecx; push esi; mov esi,[esp+0xc]`; epilogue `5e 59 c3`).
- bytes at VA: 51 56 8b 74 24 0c 8b c6 c1 e0 07 8b 88 8c d4 20
- body: `if (*(int*)(0x220d48c + type*0x80) > 0)` (commands in flight for the type; 64: 0x1448b998c, stride 0x80 too) { 0x73b4fc = type (64: DAT_1403c8dc8); lock-min of 0x21fa5fc with type through the import slot 0x691174
  (64: DAT_1448a68e4); if DAT_02204804 != 0: call 0x4f06a0 (64: 0x203b60); log "wait %s" with the name from the table at **0x73b508** (64: 0x370340); then the helper loop 0x52afc0 with its predicate in ESI = 0x52ac40
  (the predicate at 0x52acd0: same count test on the type in DAT_0073b500); 0x73b4fc = -1 }.
- command types: the name table at VA 0x73b508 has the same entries in the same order: 0 cell dyn brush, 1 cell dyn model, 2 cell scene ent, 3 dpvs ent, 4 bound ent, 5 spot shadow ent,
  6 fx non dependent, 7 glass, 8 fx remaining, 9 cell static, 10 add scene ent, 11 cell glass, 12 glass light, 13 glass verts, 14 mark verts, 15 dyn mark verts, 16 fx verts, 17 smodelcache, 18 skin model,
  **19 fx physics, 20 physics**: `for t = 0..18` is the same set in 32-bit.
- worker queue pieces: in-flight counters dword at 0x220d48c + type*0x80; helper loop 0x52afc0 ("wait cmd"; works through 0x52ae70); the scene queue's "wait_renderer2" marker is in 0x50d800.
- confidence: verified (bodies and tables compared)

## Direct3D device - 64-bit rva 0x410fd20 (RVA_D3D_DEVICE), native.c line 131, used once, at line 1309 (comp_hook: `*(IDirect3DDevice9 **)(base + RVA_D3D_DEVICE)`)
- 32-bit: pointer at VA **0x1c8c088**, rva **0x188c088**. Evidence: 0x539680 ends with `(*DAT_01c8c088 + 0xa8)(device)` = vtable slot 42 = EndScene and 0x539370 calls `+0xa4` = slot 41 = BeginScene;
  64: 0x453c0 calls `(*DAT_14410fd20 + 0x150)` = slot 42. Superseded by `d3dcap_device()` as agreed; the only user of the rva is `comp_hook()`.
- confidence: verified

## The 2D placements - 64-bit rva 0x5e0b80, stride 0x70 (PLACE_RVA, PLACE_SIZE, native.c lines 2726-2774, `scan_place` 1370-1407)
- layout inside an entry (floats p[0..27]): same in 32-bit for p[0..25]: p[0],p[1] scaleVirtualToReal, p[2],p[3] scaleVirtualToFull, p[4],p[5] scaleRealToVirtual, p[6],p[7] real viewport position, p[8],p[9] real viewport size
  (1920, 1080 in the memory image), p[10..13] virtual viewable min/max, p[14..17] real viewable min/max, p[18..21] virtual adjustable, p[22..25] real adjustable; p[26] = 240.0. Observed in image32.bin: p[0]=2.25 p[1]=2.25 p[2]=3.0 p[3]=2.25 p[4]=p[5]=0.4444 p[8]=1920 p[9]=1080
  p[10]=64 p[11]=36 p[12]=789.33 p[13]=444 p[14]=144 p[15]=81 p[16]=1776 p[17]=999 (the two with a margin) / zeros and 853.33 480 (the one without), exactly the values native.c expects.
- **32-bit addresses** (no common stride): view placement **VA 0x937500** (rva 0x537500; size 0x6c, followed at 0x93756c by the placement-mode dword, value 2 in the image), **VA 0x937578** (rva 0x537578),
  **VA 0x9375e8** (rva 0x5375e8); the last two are 0x70 apart. Entries 0x937500 and 0x937578 carry the safe-area margin, 0x9375e8 does not.
- mapping to 64-bit (by the getters): 64 0x5e0c60 (the view array, `daf10`/`daf50` use `+ lc*0x6c`, mode dword at 0x5e0ccc) = 32 0x937500 (`0x448a30` / `0x427d10`: `lc*0x6c + 0x937500`, mode at 0x93756c);
  64 0x5e0bf0 = 32 0x937578 (the default returned by 0x448a30 when the mode is not 1); 64 0x5e0b80 = 32 0x9375e8 (initialised by 0x43beb0 together with 0x937578; 64 db2b0). **The order in memory is reversed**;
  native.c treats the three alike, so any order works, but not `base + 0x70 * n`.
  Note: the placement array (the view one) has ONE element in both builds (the mode dword follows it).
- bytes (data) at 0x937500: 00 00 10 40 00 00 10 40 00 00 40 40 00 00 10 40 (floats 2.25 2.25 3.0 2.25 in the image dump).
- evidence: 0x4f4ce0 (initialises 0x937500 with the screen size 0x894374 x 0x894378 through 0x4a7320), 0x43beb0 (initialises 0x9375e8 through 0x4f1160 and 0x937578 through 0x4a7320), 0x448a30 as above;
  64: db2b0 initialises 0x5e0b80 and 0x5e0bf0; daf10 as above. Video size globals 0x894374/0x894378 (64: 0x548bfc / 0x548c00: `db060` reads them).
- confidence: verified for the three addresses and the layout; the A/B/C pairing is likely

## Video globals (native.c video_push / video_pop, mode 9 only; cgame aspect and pixel aspect also used in modes 7, 8 and 10)
| global | 64-bit rva | 32-bit VA (rva) | evidence |
|---|---|---|---|
| render width | 0x4113720 | 0x1c8ed9c (0x108ed9c) | 64 8210 `iVar2 = DAT_144113720` and 32 0x507b10 `iVar1 = DAT_01c8ed9c`, used as `x*w/divisor`; 32 writer 0x50a4f0 `DAT_01c8ed9c = in[0x14]` (64: 10550 `DAT_144113720 = param_1[3]`) |
| render height ("h1") | **0x4113724** | **0x1c8eda0** (0x108eda0) | same code, y: `param_1[1]*h/divisor` |
| window width divisor | 0x4113728 | 0x1c8eda4 | same |
| window height ("h2") | **0x411372c** | **0x1c8eda8** (0x108eda8) | same; both write the 4 values together (64: 10550 lines `DAT_144113720..2c`; 32: 0x50a4f0) |
| renderer viewport height ("h3") | **0x4358e84** (struct 0x4358e78: x, y, w, h) | **0x2163b28** (struct 0x2163b1c: x, y, w, h) | 64 0x453c0 `_DAT_144358e78 = 0; ...e80 = DAT_144113728; DAT_144358e84 = DAT_14411372c`; 32 0x539680 `DAT_02163b1c = 0; b20 = 0; b24 = DAT_01c8eda4; b28 = DAT_01c8eda8` |
| aspect ("a[0]") | **0x4113738** | **0x1c8edb4** (0x108edb4) | 64 10550 sets it from a switch on dvar `r_aspectRatio` (constants 4/3, 16/10, 16/9) then `pixel = height*aspect/width`; 32 0x50a4f0 same switch; image holds 1.7778 |
| pixel aspect ("a[1]", [native] pixelaspect) | **0x411373c** | **0x1c8edb8** (0x108edb8) | `_DAT_01c8edb8 = aspect*h/w` in 0x50a4f0 (64: `_DAT_14411373c = DAT_144113724*DAT_144113738/DAT_144113720`); the two floats are adjacent in both, so the 8-byte `memcpy` of mode 7 carries over |
| next float (64 0x4113740) | | 0x1c8edbc | not used by native.c |
- video setup function (64: rva 0x10550): 32-bit VA **0x50a4f0**, register convention (EAX = pointer to the video mode struct, no stack arguments); not hooked by native.c.
- confidence: verified

## The cgame's aspect - 64-bit rva 0x43c640 (native.c lines 1500, 1525, 1537-ish: `*(float *)(base + 0x43c640)`; also video_push `ca`)
- 32-bit: VA **0x75f018**, rva **0x35f018** (float). Evidence: the field of view function chain. 64: `0xbe710` ends with `bc1e0(lc)` which computes the fov and calls `266cf0(fov, DAT_14043c640, &refdef+0x10, &refdef+0x14)`
  (tan(fov/2) scaled, `*param_3 = tan*aspect; *param_4 = tan`); 32: 0x571120 (twin of be710) ends with `FUN_00440d20()` (twin of bc1e0, returns the fov in ST0) then **0x56fad0** which calls `0x466330(fov, _DAT_0075f018, &DAT_0077fae0, &DAT_0077fae4)` with
  the same body (`*p3 = tan*k*aspect; *p4 = tan*k`); the outputs are refdef+0x10 / +0x14 in both (32: 0x77fad0 + 0x10).
  The aspect is read at 0x56fad0, 0x565813, 0x4a2f9d (twin of 64 0xa96a0: compares aspect with 4/3 to pick a dvar) and referenced as data at 0x43416e (64: a6680 at 0xa69e4).
- the aspect was 0.0 in the memory image (set by the cgame later); no instruction writes it directly in either build (the cgame init writes it through a pointer), so the "restore on exit" logic works the same.
- confidence: likely (verified call chain and arithmetic; the writer was not located in either build)
- Also: the 64 function 0xbc1e0 (CG_CalcFov) is in 32-bit **VA 0x440d20** (cdecl-looking, but it takes the local client in a register: `float10 (undefined4)`; returns the fov in ST0), bytes `83 ec 0c 53 56 57 68 2c 4c 77 00 e8 c0 ee fd ff`. Not hooked by native.c.

## cg / cl globals read by the diagnostics in cg_draw_hook (lines 1428-1497)
| what | 64-bit rva | 32-bit VA (rva) | evidence / confidence |
|---|---|---|---|
| cg.time | 0x422434 | **0x774c20** (0x374c20) | 0x425820 begins `mov ecx,[0x774c20]; mov eax,[0x86a5b4]; mov [0x774c20],eax` = cg.time = serverTime (64 b8c50); also stored to refdef+0x58 by the cgame draw. **verified** |
| cg.frametime | 0x422430 | 0x774c1c | the same function (0x425820) writes 0x774c1c and 0x774c18 once each as 64 b8c50 writes 0x422430 / 0x42242c; **likely** |
| cg.frameInterpolation | 0x42242c | 0x774c18 | likely (as above) |
| cg.ps base (the playerState inside cg) | 0x422440 | **0x774c2c** | `&DAT_00774c2c` is the argument of 0x41fbf0 / 0x491840 / 0x4acbe0 in 0x440d20, as `&DAT_140422440` is for 85820 / 857c0 / 86690 in 64 bc1e0; cg.time sits at ps-0xc in both (0x422434 = 0x422440-0xc, 0x774c20 = 0x774c2c-0xc). **verified** for the base |
| cg.ps fields: origin +0x1c (0x42245c), pitch +0x108, yaw +0x10c, roll +0x110 (viewlog) | | unknown | the playerState has a different size in 32-bit (view angles etc. are pointer-free, but only the base is verified). **unknown** (diagnostic only) |
| cl.serverTime | 0x51dca8 | **0x86a5b4** (0x46a5b4) | written by 0x4b74e0 (64: 0xbc1a0 `DAT_14051dca8 =`), read by the cgame draw (`< DAT_0086a5b4`) and copied to cg.time by 0x425820. **verified** |
| snapshot lerp fraction | 0x51dcb0 | unknown (guess 0x86a5bc) | not checked; diagnostic only |
| entity slot array | 0x441070 + n*0x1a8 | **0x792848 + n*0x198** | 64 cg_draw `FUN_14009f840(lc, &DAT_140441070 + ent*0x1a8)`, 32 `FUN_00438f80(lc, &DAT_00792848 + ent*0x198)`. **verified** (stride too). Float index 63..65 (origin, +0xfc) of an entity: unknown in 32-bit |
| view angle accumulators 0x533d14 / 0x533d18, cl playerstate copy 0x535d38 | | not in this package (client contexts); unknown here |
| coop byte | 0x52fb5c | **0x88897c** | 0x48acb0 reads `DAT_0088897c` where 64 0x22eb70 reads 0x14052fb5c; **verified** |
| cls state of lc (dword at stride 0x10) | 0x533cb8 | **0x925bc0** | 0x48acb0 `cmp [lc*16 + 0x925bc0],6` vs 64 `cmp [0x533cb8 + lc*0x10],6`; **verified** |

## wait_alpha_hook - 64-bit function rva 0x22eb70, called at 0x94039 from 0x93ac0 (native.c lines 2971-2982)
- 32-bit function: **VA 0x48acb0**, rva 0x8acb0, **cdecl, float (int lc)**, result in ST0 (x87), `push ecx; mov eax,[esp+8]; shl eax,4; cmp [eax+0x925bc0],6; ...` and ends `pop ecx; ret`. (The decompiler shows __thiscall because of the `push ecx` scratch slot; the argument is on the stack.)
- bytes at VA: 51 8b 44 24 08 c1 e0 04 83 b8 c0 5b 92 00 06 7c
- **call site: VA 0x55ef17**, rva 0x15ef17 (64: 0x94039); bytes `e8 94 bd f2 ff d9 5c 24 08 d9 ee 83 c4 04 d8 5c`. The caller is the small function **0x55ef10** (219 bytes, register arg EAX = lc, `sub esp,0x14; push esi; mov esi,eax; push esi; call 0x48acb0`) that draws the "COOP_WAITINGFORPLAYER" text
  (string at VA 0x6a9a68) and is *not* part of a large HUD function as in 64-bit (the matcher's `93ac0 -> 55ef10` is therefore only the text part; 64's partner-name loop at 0x93ac0 is elsewhere, other package).
- the body is the same: `if (state[lc] > 5 && coop byte)`, then a check of the slot table (0x4f6e50 here, 64: inline loop over 0x140540bb0 stride 0x994 dwords), then `(now - dvar) ...*0.001 ... + const` scaled: alpha. Note the 32-bit one uses 0x65a010 (time) and dvar 0x1a7f204.
- confidence: verified for function and site

## Text draw and inline pictures (padglyphs.c, requested in the package brief; not in native.c)
- 64: text command handler 0x44b20 calls the text draw 0x433b0 at 0x44c12 (text in rcx = command struct + 0x5c, x +4, y +8, font +0x10 ...). 
- 32-bit text draw: **VA 0x537ec0**, rva 0x137ec0, **cdecl, 21 stack arguments** (`ADD ESP,0x54` after the call; prologue `push ebp; mov ebp,esp; and esp,-8; sub esp,0x8c`), void. Argument order as 64: (char *text, float x, float y, Font *font, float xscale, float yscale, float sin, float cos, float rotation-ish, int maxChars, uint flags, int cursorPos, ..., Material *glow material at param_20, param_21).
- bytes at VA: 55 8b ec 83 e4 f8 81 ec 8c 00 00 00 8b 45 28 53
- **call site: VA 0x53919d, rva 0x13919d** (64: 0x44c12), bytes (from 0x53919c): `52 e8 1e ed ff ff 8b 06 0f b7 48 02 83 c4 54 03`: `push edx` (the text pointer = command struct + 0x50, 64: +0x5c) is the last push, so **the text is the first stack argument, at [esp] at the call**. The function that contains the call (the 32-bit twin of 0x44b20, ends `add esp,0x54; ... add esp,0x10; pop esi; ret` at 0x5391b3) starts before 0x5390a0 and is not known to Ghidra (it has no function there); its command struct offsets: x +4, y +8, font +0x10(? loaded into the push list), text +0x50.
  Only caller of 0x537ec0.
- the stub in padglyphs.c (rcx patched in registers) becomes: patch the dword at [esp+4] after the return address (i.e. the first stack argument) in a normal cdecl wrapper.
- **inline picture format**: the text draw tests `fVar7 == '^'` (94) and `*ptr == 1 or 2`, then skips **7 bytes** (`local_70 + 7`; 64-bit: 11): `'^'`, type byte (1 or 2), width byte, height byte, then **4 bytes** (the Material *, 8 bytes in 64-bit). The picture size rule is the same as 64-bit (0x5377d0: `((n - 16) * fontHeight + 16) / 32`: `iVar3 = (param_1[1] - 0x10) * font[4] + 0x10; (iVar3 + (iVar3>>31 & 31)) >> 5` for width and height; it reads ECX = pointer to the header byte `'1'/'2'` and EAX = font).
  The picture draw is 0x5377d0 (ECX = header, EAX = font, 7 stack args; called at 0x538388 `e8 43 f4 ff ff`) which calls 0x533b90 (the quad draw). **The position of the material pointer inside the 7 bytes is not verified (4 bytes at offset +4 after '^' by the size, i.e. after type, width, height: likely).**
- Key binding text function (64: 0xd0e40) and its two callers (0x2513ce, 0x1913fa), and DB_FindXAssetHeader (64: 0xef5d0, map guess `4f6f10`): not examined here (other packages / not asked in detail).
- confidence: verified for function, call site and argument convention; likely for the picture header layout.

## Other items in my line ranges
- RVA_LOCAL_PLAYERS_STORE (0x1714a6), `client_frame_hook` (64: 0xd5b00, call at 0x1fc331), mouse reads (0xcf4b0 at 0xcfd23, 0xd0061) and the context ranges are other packages. For reference: the 64 client frame `0xd5b00` is 32-bit **0x4c3660** (map.txt, verified: it calls 0x57adc0), and the 64 one-frame function `0xd5890` is **0x57adc0** (it contains the call to the cgame draw at 0x57ae1a), not the matcher's `57ac90`.
- `layer_fix`, `t_scissor`, `layer_to_scene`, the compositor, the Present hooks, `dedupe` shader: no game addresses at all (D3D9 vtable slots and HLSL), nothing to map. One 32-bit thing: `native.c:1103 rt_full` etc. use the backbuffer size from the device, fine.
- `trace_draw` logs `off < 0x4000000` offsets from `base`: harmless in 32-bit (only a logging filter, the bound can stay).
- Things in the ranges that have **no 32-bit counterpart**: none found. The console-style renderer path (draw-path selector values 1 and 2 with the 0x5c620 branches that use `FUN_14005bee0` and `FUN_14005ba20` etc.) exists in 32-bit too (0x552ec0, 0x553280 and 0x553140); native.c never relied on them. The 64-bit-only tail of the cgame draw (a block using `DAT_140521008` "mover/vehicle" origin compensation, lines 175-196 of the 64 decompile, guarded by a dvar) is not present in the 32-bit cgame draw; nothing in native.c depends on it.

## Open (unknown)
- cg.ps view angles / origin offsets in the 32-bit playerState (diagnostic code only).
- the snapshot lerp fraction (0x51dcb0 in 64).
- who writes the cgame aspect (0x75f018) and whether `aspect` is also copied into the cgame block that `video_push` pokes (`ctx_store[1][0] + (0x43c640 - ranges[0].rva)`): needs the context ranges of the other package (the aspect lives in 32-bit rva 0x35f018, check that it lies inside the cgame block range there).
- the exact position of the Material pointer in the inline picture (see above).
