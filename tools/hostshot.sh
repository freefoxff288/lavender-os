#!/bin/sh
# Снимок окна QEMU средствами niri (когда screendump не работает с GL). hostshot.sh out.png
id=$(niri msg -j windows | python3 -c 'import json,sys; print(next(w["id"] for w in json.load(sys.stdin) if w["app_id"]=="qemu"))')
before=$(/bin/ls -t ~/Pictures/Screenshots/*.png 2>/dev/null | head -1)
niri msg action screenshot-window --id "$id" --write-to-disk true >/dev/null
for i in 1 2 3 4 5 6 7 8 9 10; do f=$(/bin/ls -t ~/Pictures/Screenshots/*.png | head -1); [ "$f" != "$before" ] && break; sleep 0.3; done
mv "$f" "$1"
