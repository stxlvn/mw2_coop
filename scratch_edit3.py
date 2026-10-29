import re
p = 'src/native.c'
s = open(p, newline='').read()


def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    assert n >= 1, 'missing: ' + old[:90]
    if cnt == 0:
        s = s.replace(old, new)
    else:
        assert n == cnt, ('count %d != %d: ' % (n, cnt)) + old[:90]
        s = s.replace(old, new)


rep('	jhook_jump(base + RVA_SV_ADD_COMMAND, 14, sv_add_command, 0xcc);',
    '''#ifdef _WIN64
	jhook_jump(base + RVA_SV_ADD_COMMAND, 14, sv_add_command, 0xcc);
#else
	jhook_jump(base + RVA_SV_ADD_COMMAND, 5, sv_add_command, 0xcc);
#endif''')

rep('''	static const uint32_t probes[] = {0x422434, 0x40caa0, 0x40cad0, 0x40cad8, 0x40cab8, 0x422440};''',
    '''#ifdef _WIN64
	static const uint32_t probes[] = {0x422434, 0x40caa0, 0x40cad0, 0x40cad8, 0x40cab8, 0x422440};
#else
	static const uint32_t probes[] = {0x374c20, 0x35f2a0, 0x35f2c8, 0x35f2bc, 0x35f2b8, 0x374c2c}; // cg.time, the cgame's client numbers and pointers, cg.ps
#endif''')

rep('static void body_dobj_sync(void)\n{', 'static void body_dobj_sync(void)\n{\n#ifndef _WIN64\n	return; // 32 bits: the entity part of the port comes later (docs/32bit-native-map/entities.md)\n#endif')
rep('static void place_push(void)\n{', 'static void place_push(void)\n{\n#ifndef _WIN64\n	return; // 32 bits: the 2D placement of the title lines comes with mode 10\n#endif')
rep('static void place_pop(void)\n{', 'static void place_pop(void)\n{\n#ifndef _WIN64\n	return;\n#endif')

# hooks_apply32: the contexts
rep('''		log_printf("native: unexpected code at the cgame draw or its begin-frame call, the 32-bit split screen is off");
		return 0;
	}
	hooks_ok = 1;
	return 1;''', '''		log_printf("native: unexpected code at the cgame draw or its begin-frame call, the 32-bit split screen is off");
		return 0;
	}
	if (cfg("experiment", 0) >= 5 && cfg("ctx", 1) && !hooks_apply32_contexts())
		return 0;
	if (cfg("experiment", 0) >= 5)
		CreateThread(NULL, 0, coop_dvar_thread, NULL, 0, NULL);
	hooks_ok = 1;
	return 1;''')
# forward declarations needed by hooks_apply32 (defined later in file)
rep('#ifndef _WIN64\n// The 32-bit port, step 1', '''#ifndef _WIN64
static int hooks_apply32_contexts(void);
static DWORD WINAPI coop_dvar_thread(LPVOID unused);
#endif
#ifndef _WIN64
// The 32-bit port, step 1''')

# activate(): the 32-bit port runs up to mode 5 so far
rep('	mode = cfg("experiment", 10);\n	if (!hooks_apply() || !players_apply(players)) {',
    '''	mode = cfg("experiment", 10);
#ifndef _WIN64
	if (mode > 5)
		mode = 5; // 32 bits: the compositor's 2D layers (mode 10) are not ported yet
#endif
	if (!hooks_apply() || !players_apply(players)) {''')

# native_install
old = s[s.index('#ifndef _WIN64\n	// the 32-bit port is switched on piece by piece'):s.index('	for (size_t i = 0; i < sizeof(native_commands)')]
new = '''#ifndef _WIN64
	// the 32-bit port is switched on piece by piece: experiment=2 (alternate frames, compositor), then players=2 with experiment=5 (two client contexts)
	if (cfg("players", 0) < 2) {
		if (cfg("experiment", 0) == 2) {
			mode = 2;
			hooks_apply32();
			log_printf("native: 32-bit experiment 2 %s", hooks_ok ? "on" : "failed");
		} else {
			log_printf("native: the 32-bit port is off ([native] experiment=2 or players=2 experiment=5)");
		}
		return;
	}
#endif
'''
s = s.replace(old, new)
rep('	padglyphs_install(text_pad_player);', '#ifdef _WIN64\n	padglyphs_install(text_pad_player);\n#endif')
rep('	if (cfg("treelog", 0) && cfg("players", 0) < 2', '#ifdef _WIN64\n	if (cfg("treelog", 0) && cfg("players", 0) < 2')
rep('		log_printf("native: dobj add bytes differ (treelog in the plain game)");\n', '		log_printf("native: dobj add bytes differ (treelog in the plain game)");\n#endif\n')
open(p, 'w', newline='').write(s)
print('ok')
