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

# SDL's database only knows this GUID as the RG28XX pad, which has no L2/R2 or
# sticks. Button numbers are the ones Knulli's es_input.cfg gives the H700
# Anbernics (RG35XX-H, RG40XX, CubeXX); positional, so Cross is the bottom key.
if [ -z "$SDL_GAMECONTROLLERCONFIG" ]; then
	export SDL_GAMECONTROLLERCONFIG="19000000010000000100000000010000,Anbernic H700 Controller,a:b4,b:b3,x:b5,y:b6,back:b9,start:b10,guide:b11,leftshoulder:b7,rightshoulder:b8,lefttrigger:b13,righttrigger:b14,leftstick:b12,rightstick:b15,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a3,platform:Linux,"
fi

# No DRM node means the fbdev build of the Mali blob; don't bother with GBM.
if [ -z "$ARMSX2_EGL_PLATFORM" ] && ! ls /dev/dri/card* > /dev/null 2>&1; then
	export ARMSX2_EGL_PLATFORM=fbdev
fi

# Four in-order A53s need every cycle they have; put the governor back after.
GOV=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
OLD_GOV="$(cat "$GOV" 2>/dev/null)"
echo performance > "$GOV" 2>/dev/null

cd "$APP" || exit 1
./armsx2-sdl "$@" > "$APP/armsx2.log" 2>&1
RET=$?

[ -n "$OLD_GOV" ] && echo "$OLD_GOV" > "$GOV" 2>/dev/null
exit $RET
