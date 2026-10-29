#!/bin/sh
# QA helper for the 32-bit copy: sh run32.sh "<[native] lines, ';'-separated>" <timeout s> <shot name> <log lines to print>
#   WAIT=<ms>   how long after the briefing keys the shots are taken (default 60000)
#   PRE=<cmd>   one console command before the map (e.g. "set mw2coop_qa armed")
# The composed picture of a split session is saved by the mod itself (set mw2coop_qa_frame <name>): <name>c1 and <name>c2; !shot gives only the raw frame.
NAT="$1"; TO="${2:-60}"; SHOT="${3:-c2a}"
D=/d/coop-test/mw2-32
printf '[network]\nport=28960\n\n[native]\n' > $D/mw2coop/settings.ini
echo "$NAT" | tr ';' '\n' >> $D/mw2coop/settings.ini
grep -q "^p1cmd=" $D/mw2coop/settings.ini || echo "p1cmd=1" >> $D/mw2coop/settings.ini
rm -f $D/mw2coop/mw2coop.log
printf 'set mw2coop_god 1\n%s\n!sleep 8000\ndevmap %s\n!sleep 14000\n!key ESC\n!sleep 4000\n!key ESC\n!sleep %s\n!shot %s\n!sleep 1500\nset mw2coop_qa_frame %sc1\n!sleep 1500\nset mw2coop_qa_frame %sc2\n!sleep 1500\n' "${PRE:-}" "${MAP32:-trainer}" "${WAIT:-60000}" "$SHOT" "$SHOT" "$SHOT" > $D/mw2coop/commands.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run_test.ps1 -Dirs "D:\coop-test\mw2-32" -Timeout $TO -Tag run32 2>&1 | tail -1
grep -n "native\|CRASH" $D/mw2coop/mw2coop.log | tail -${4:-25} | cut -c1-230
