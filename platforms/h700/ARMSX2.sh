#!/bin/sh
# ARMSX2 launcher for Allwinner H700 handhelds (RG35XX H/Plus/SP, RG28XX,
# RG40XX) on Knulli, muOS and the stock firmware. Put this script next to the
# unpacked armsx2-linux-arm64-sdl-a53 folder, renamed to "armsx2":
#
#   ARMSX2.sh
#   armsx2/armsx2-sdl
#   armsx2/resources/
#   armsx2/bios/          <- your PS2 BIOS dump goes here
#
# Run with no arguments for the game list, or pass a disc image:
#   ./ARMSX2.sh /path/to/game.chd

DIR="$(cd "$(dirname "$0")" && pwd)"
APP="$DIR/armsx2"

# Keep settings, BIOS, memory cards and saves inside the armsx2 folder.
[ -f "$APP/portable.txt" ] || touch "$APP/portable.txt"

# Four in-order A53s need every cycle they have; put the governor back after.
GOV=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
OLD_GOV="$(cat "$GOV" 2>/dev/null)"
echo performance > "$GOV" 2>/dev/null

cd "$APP" || exit 1
./armsx2-sdl "$@" > "$APP/armsx2.log" 2>&1
RET=$?

[ -n "$OLD_GOV" ] && echo "$OLD_GOV" > "$GOV" 2>/dev/null
exit $RET
