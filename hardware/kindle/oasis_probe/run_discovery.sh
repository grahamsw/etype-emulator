#!/bin/sh
# E-Type Kindle Oasis - Live Unified Auto-Discovery & Typing Engine
FBINK="/mnt/us/libkh/bin/fbink"
LOG="/mnt/us/discovery_log.txt"
BIN="/mnt/us/extensions/oasis_probe/etype_unified"

echo "=== E-TYPE LIVE TYPEWRITER ENGINE START ===" > "$LOG"
date >> "$LOG"

if [ -x "$FBINK" ]; then
  "$FBINK" -q -c
  "$FBINK" -q -pm -m -y 2 "E-Type Live Typewriter"
  "$FBINK" -q -pm -m -y 3 -- "----------------------"
  "$FBINK" -q -pm -y 5 "Starting Broadcom Bluetooth engine..."
fi

# 1. Clean kill any lingering instances and daemons to ensure fresh sockets
killall -9 etype_unified etype_discovery etype_bt_hh bsa_server 2>/dev/null || true
rm -f /var/local/zbluetooth/*.sock /var/local/zbluetooth/ba_* 2>/dev/null || true
sleep 1

# 2. Ensure btd is active
if ! pidof btd >/dev/null 2>&1; then
  start btd 2>/dev/null || nice -n -10 btd -f &
  sleep 1
fi

# 3. Launch bsa_server if not already running
if ! pidof bsa_server >/dev/null 2>&1; then
  nice -n -10 bsa_server -d /dev/ttymxc2 -u /var/local/zbluetooth/ -p /opt/brcm_4343w/bluetooth/firmware/BCM4343A1_001.002.009.0152.0517.hcd -lpm > /var/log/bsa_server_launch.log 2>&1 &
  sleep 2
fi

# 4. Run the Live Unified Typewriter Engine
chmod +x "$BIN"
"$BIN" >> "$LOG" 2>&1
echo "Typewriter Exit Code: $?" >> "$LOG"

echo "" >> "$LOG"
echo "=== BSA SERVER LAUNCH LOG ===" >> "$LOG"
cat /var/log/bsa_server_launch.log >> "$LOG" 2>/dev/null || true

sync

if [ -x "$FBINK" ]; then
  "$FBINK" -q -pm -y -2 "Session ended. Tap to restart."
fi
