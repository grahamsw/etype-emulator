#!/bin/sh
# E-Type Kindle Oasis - Display Fullscreen Vintage Typewriter Artwork
FBINK="/mnt/us/libkh/bin/fbink"
IMG="/mnt/us/extensions/oasis_probe/screensaver_typewriter.png"

if [ -x "$FBINK" ] && [ -f "$IMG" ]; then
  # 1. Clear screen to clean canvas
  "$FBINK" -q -c
  # 2. Render 1264x1680 vintage typewriter illustration with hardware/software dithering
  "$FBINK" -q -g file="$IMG",halign=CENTER,valign=CENTER,dither
  # 3. Flashing refresh to lock the e-ink particles in high contrast
  "$FBINK" -q -w
fi
