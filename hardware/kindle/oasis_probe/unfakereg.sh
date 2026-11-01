#!/bin/sh
# Remove local fake registration (restore stock unregistered state)
mntroot rw

chattr -i /var/local/java/prefs/reginfo 2>/dev/null || true
rm -f /var/local/java/prefs/reginfo

mntroot ro
touch /mnt/us/DISABLE_CORE_DUMP
sync

if [ -x /mnt/us/libkh/bin/fbink ]; then
  /mnt/us/libkh/bin/fbink -q -pm -y -3 "FakeReg Removed! Restart Kindle from Settings."
fi
