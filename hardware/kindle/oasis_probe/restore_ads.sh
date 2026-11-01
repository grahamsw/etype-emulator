#!/bin/sh
# E-Type Kindle Oasis - Restore Lockscreen Ads Directory
FBINK="/mnt/us/libkh/bin/fbink"

chattr -i /var/local/adunits 2>/dev/null || true
chattr -i /var/local/system/adunits 2>/dev/null || true
rm -f /var/local/adunits /var/local/system/adunits 2>/dev/null || true
mkdir -p /var/local/adunits /var/local/system/adunits
chmod 777 /var/local/adunits /var/local/system/adunits

sync

if [ -x "$FBINK" ]; then
  "$FBINK" -q -c
  "$FBINK" -q -pm -m -y 5 "Ad directories restored to defaults."
fi
