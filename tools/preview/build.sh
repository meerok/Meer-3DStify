#!/bin/sh
# builds the PC preview renderer: sh tools/preview/build.sh <stb include dir>
set -e
D=$(cd "$(dirname "$0")" && pwd)
S="$D/../../source"
INC=${1:-$D}
cc -O2 -g -I"$S" -I"$D" -I"$INC" -o "$D/preview" \
  "$D/preview.c" "$D/gfx_host.c" "$D/sys_host.c" "$D/player_host.c" \
  "$S/app.c" "$S/art.c" "$S/library.c" "$S/icons.c" "$S/gfx_common.c" "$S/stb_impl.c" -lm -lpthread
echo built "$D/preview"
