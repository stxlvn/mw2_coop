#!/bin/sh
# usage: run32.sh "<native lines ; separated>" <timeout> [commands...]  (QA helper for the 32-bit copy)
NAT="$1"; TO="${2:-60}"
D=/d/coop-test/mw2-32
printf '[network]\nport=28960\n\n[native]\n' > $D/mw2coop/settings.ini
echo "$NAT" | tr ';' '\n' >> $D/mw2coop/settings.ini
rm -f $D/mw2coop/mw2coop.log
printf 'set mw2coop_god 1\n!sleep 8000\ndevmap trainer\n!sleep 40000\n!shot %s\n!sleep 1500\n!shot %sb\n' "${3:-c2a}" "${3:-c2a}" > $D/mw2coop/commands.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run_test.ps1 -Dirs "D:\coop-test\mw2-32" -Timeout $TO -Tag run32 2>&1 | tail -1
grep -n "native\|CRASH" $D/mw2coop/mw2coop.log | tail -${4:-25} | cut -c1-230
