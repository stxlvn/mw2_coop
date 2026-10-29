#!/bin/sh
# Build build/winmm.dll (x64) with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
# Usage: ./build.sh [release]      ./build.sh x86 [release] builds the 32-bit game's build/x86/winmm.dll
#   LLVM_MINGW  llvm-mingw folder (default: ../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64)
#   release     optimized and stripped: no debug info and no local paths inside the DLL
set -e
cd "$(dirname "$0")"
LLVM_MINGW=${LLVM_MINGW:-../tools/llvm-mingw/llvm-mingw-20261006-ucrt-x86_64}
CC="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang"
SRC="src/main.c src/log.c src/proxy.c src/hooks.c src/hook.c src/game.c src/control.c src/crash.c src/scripts.c src/localize.c src/memdump.c src/menus.c src/mirror.c src/transplant.c src/access.c src/settings.c src/upnp.c src/steamnet.c src/firewall.c src/zones.c src/net.c src/fonts.c src/splitscreen.c src/instance.c src/native.c
	src/party.c src/progress.c src/scrdebug.c src/scrdebug_stack.c src/shot.c src/fov.c src/padicon.c src/padopts.c src/padglyphs.c src/rumble.c src/winmm_stubs.S src/winmm.def"
if [ "$1" = x86 ]; then
	STRIP=; { [ "$2" = release ] || [ "$3" = release ]; } && STRIP=-s   # release: stripped, no local paths in the DLL (`x86 iw4x release` too)
	# the 32-bit build of the game (see docs/32bit-plan.md): only the first slice is ported so far
	CC32="$LLVM_MINGW/bin/i686-w64-mingw32-clang"
	# `sh build.sh x86 iw4x`: the profile of the iw4x-sp client (1.0.159 iw4sp.exe), into build/x86-iw4x/
	OUT=build/x86; DEF=
	if [ "$2" = iw4x ]; then OUT=build/x86-iw4x; DEF=-DMW2COOP_IW4X; fi
	mkdir -p $OUT
	"$CC32" -O2 $STRIP -Wall -Wextra $DEF -shared -static -static-libgcc -Isrc/x86 -Isrc -o $OUT/winmm.dll 		src/x86/main32.c src/x86/stubs32.c src/log.c src/proxy.c src/hooks.c src/hook.c src/game.c src/scripts.c src/zones.c 		src/control.c src/party.c src/settings.c src/upnp.c src/crash.c src/fov.c src/mirror.c src/localize.c src/progress.c src/fonts.c src/splitscreen.c src/instance.c src/native.c src/addr159.c src/menus.c src/padicon.c src/padopts.c src/padglyphs.c src/rumble.c src/access.c src/net.c src/steamnet.c src/firewall.c src/memdump.c src/transplant.c src/shot.c src/d3dcap.c src/x86/scrdebug32.c src/scrdebug_stack.c src/x86/winmm_stubs.S src/x86/winmm.def -lbcrypt -lws2_32 -liphlpapi -lgdi32
	echo "built $OUT/winmm.dll"
	exit 0
fi
mkdir -p build
if [ "$1" = release ]; then
	"$CC" -O2 -Wall -Wextra -shared -static -static-libgcc -Isrc -s -o build/winmm.dll $SRC -lws2_32 -liphlpapi -lgdi32
else
	"$CC" -O2 -g -Wall -Wextra -shared -static -static-libgcc -Isrc -o build/winmm.dll $SRC -lws2_32 -liphlpapi -lgdi32 \
		-Wl,--pdb=build/winmm.pdb
fi
echo "built build/winmm.dll${1:+ ($1)}"
