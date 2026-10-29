#!/bin/sh
# Native split screen QA (docs/native-splitscreen-plan.md): ONE copy, two players in one process, in 1920x1080 on the display named in local/test-display.txt.
# Usage: tools/qa_native.sh [seconds in the level after the map command, default 200] [extra [native] lines, ";"-separated, e.g. "p1pad=0;entlog=1"]
#   MW2COOP_MAP     the map (default trainer)
#   MW2COOP_PRE     extra console commands before the map, ";"-separated
#   MW2COOP_POST    extra console commands after the level time, ";"-separated (the run is then LEVEL_S + their time long: give the script a bigger first argument)
#   MW2COOP_LOBBY=1 the whole menu path: the split screen button, Continue, the players' devices (MW2COOP_PAD player 2's pad, MW2COOP_PAD0 player 1's, -1 the keyboard), Start
#                   (the mission is the lobby's, not MW2COOP_MAP); frames of the lobby before and after: mw2coop\shots\lobby-a.bmp, lobby-b.bmp
#   MW2COOP_PLAIN=1 no native split screen at all: the game as it is (to see what the original draws)
#   MW2COOP_MENU=1  the session is started the way the menu does it (console command mw2coop_native_launch), not by settings.ini; MW2COOP_PAD the second player's pad
# Writes the copy's settings.ini and commands.txt, runs it, prints the native/split/crash lines and puts the composed frame in local/scratch/native-last.png.
LEVEL_S=${1:-200}
EXTRA=${2:-}
MAP=${MW2COOP_MAP:-trainer}
RES=${MW2COOP_RES:-1920x1080}
RAW_AT=${MW2COOP_RAW_AT:-115}
ROOT=${MW2COOP_TEST_ROOT:-/d/coop-test}
A=$ROOT/mw2-64
HERE=$(cd "$(dirname "$0")" && pwd -W)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/close_test_copies.ps1" -Root "$(cygpath -w "$ROOT")" | tr -d '\r'
{
	echo "[network]"
	echo "port=28960"
	echo
	echo "[native]"
	if [ -z "${MW2COOP_MENU:-}" ] && [ -z "${MW2COOP_LOBBY:-}" ] && [ -z "${MW2COOP_PLAIN:-}" ]; then
		echo "players=2"
		echo "coopdvar=1"
		echo "experiment=${MW2COOP_MODE:-10}"
		echo "fov=${MW2COOP_FOV:-49}"
		echo "p1cmd=1"
		echo "listplayers=1"
	fi
	echo "seqat=3000" # twelve composed frames from composed frame 3000 on (the newest becomes local/scratch/native-last.png); off in the game itself
	[ -n "$EXTRA" ] && echo "$EXTRA" | tr ';' '\n'
} > "$A/mw2coop/settings.ini"
{
	echo "set mw2coop_god 1"
	echo "set r_fullscreen 0"
	echo "set r_customMode \"$RES\""
	[ -n "${MW2COOP_PRE:-}" ] && echo "$MW2COOP_PRE" | tr ';' '\n'
	echo "!sleep 6000"
	if [ -n "${MW2COOP_LOBBY:-}" ]; then # the lobby's own way: the split screen button, the host popup's Continue, the second player's button, Start
		echo "mw2coop_split_menu_open"
		echo "!sleep 2000"
		echo "mw2coop_host_go"
		echo "!sleep 4000"
		echo "!shot lobby-a"
		echo "!sleep 500"
		echo "mw2coop_split_pad ${MW2COOP_PAD:-0} ${MW2COOP_PAD0:--1}"
		echo "!sleep 1500"
		echo "!shot lobby-b"
		echo "!sleep 500"
		echo "mw2coop_start"
	elif [ -n "${MW2COOP_PLAIN:-}" ]; then echo "devmap $MAP"
	elif [ -n "${MW2COOP_MENU:-}" ]; then echo "mw2coop_native_launch $MAP ${MW2COOP_PAD:--1} ${MW2COOP_START:-}"; else echo "devmap $MAP"; fi
	if [ -n "${MW2COOP_RAW:-}" ]; then # two raw frames (the game's own, before the compositor) once the picture is split
		echo "!sleep $((RAW_AT * 1000))"
		echo "!shot raw1"
		echo "!sleep 1700"
		echo "!shot raw2"
		echo "!sleep $(((LEVEL_S - RAW_AT) * 1000))"
	elif [ -n "${MW2COOP_MID:-}" ]; then # console commands MW2COOP_MIDAT seconds into the level (";"-separated)
		echo "!sleep $((${MW2COOP_MIDAT:-100} * 1000))"
		echo "$MW2COOP_MID" | tr ';' '\n'
		echo "!sleep $(((LEVEL_S - ${MW2COOP_MIDAT:-100}) * 1000))"
	else
		echo "!sleep $((LEVEL_S * 1000))"
	fi
	[ -n "${MW2COOP_POST:-}" ] && echo "$MW2COOP_POST" | tr ';' '\n'
} > "$A/mw2coop/commands.txt"
# the window is made 1920x1080 at start: the config of the copy holds it (no vid_restart: it loses the mod's fonts)
CFG=$A/players/config.cfg
sed -i 's/^seta r_mode .*/seta r_mode "'"$RES"'"/; s/^seta r_customMode .*/seta r_customMode "'"$RES"'"/; s/^seta r_fullscreen .*/seta r_fullscreen "0"/; s/^seta vid_xpos .*/seta vid_xpos "'"${MW2COOP_XPOS:-0}"'"/; s/^seta vid_ypos .*/seta vid_ypos "0"/' "$CFG"
rm -f "$A/mw2coop/mw2coop.log" "$A/mw2coop"/shots/seq-*.bmp
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$HERE/run_test.ps1" -Tag native -Timeout ${MW2COOP_TIMEOUT:-$((LEVEL_S + 50))} -TestRoot "$(cygpath -w "$ROOT")" -Dirs "$(cygpath -w "$A")" | tail -2
grep -a -E "CRASH: exc|split view|device reset|compositor on" "$A/mw2coop/mw2coop.log" | sed 's/^\([0-9:.]*\) \[[ 0-9]*\] /\1 /' | cut -c1-120
LAST=$(ls "$A"/mw2coop/shots/seq-*.bmp 2>/dev/null | tail -1)
if [ -n "$LAST" ]; then
	OUT=$(cygpath -w "$(cd "$HERE/../.." && pwd)/scratch/native-last.png")
	mkdir -p "$(dirname "$(cygpath -u "$OUT")")"
	powershell.exe -NoProfile -Command "Add-Type -AssemblyName System.Drawing; \$i=[System.Drawing.Image]::FromFile('$(cygpath -w "$LAST")'); \$i.Save('$OUT',[System.Drawing.Imaging.ImageFormat]::Png); \$i.Dispose()"
	echo "frame: $OUT"
fi
