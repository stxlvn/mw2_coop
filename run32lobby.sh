#!/bin/sh
# QA: the split screen lobby of the 32-bit copy (menu path): sh run32lobby.sh <timeout s> <name>
D=/d/coop-test/mw2-32
printf '[network]\nport=28960\n\n[native]\n' > $D/mw2coop/settings.ini
rm -f $D/mw2coop/mw2coop.log
printf 'set mw2coop_god 1\n!sleep 8000\nmw2coop_split_menu_open\n!sleep 2500\nmw2coop_host_go\n!sleep 4000\n!shot %s-a\n!sleep 500\nmw2coop_split_pad 0 -1\n!sleep 1500\n!shot %s-b\n!sleep 500\nmw2coop_start\n!sleep 14000\n!key ESC\n!sleep 4000\n!key ESC\n!sleep %s\nset mw2coop_qa_frame %s-c1\n!sleep 1500\nset mw2coop_qa_frame %s-c2\n!sleep 1500\n' "$2" "$2" "${WAIT:-90000}" "$2" "$2" > $D/mw2coop/commands.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/run_test.ps1 -Dirs "D:\coop-test\mw2-32" -Timeout $1 -Tag lobby 2>&1 | tail -1
grep -n "menus:\|native:\|CRASH\|padicon" $D/mw2coop/mw2coop.log | grep -v "HANG\|client frame\|split draws\|composed\|present hits\|step \|half was" | tail -${3:-30} | cut -c1-200
