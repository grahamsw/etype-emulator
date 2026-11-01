#!/bin/sh
# E-Type Kindle Oasis - Live Unified Auto-Discovery & Typing Engine
FBINK="/mnt/us/libkh/bin/fbink"
LOG="/mnt/us/bt_hh_log.txt"
BIN="/mnt/us/extensions/oasis_probe/etype_unified"
PIDFILE="/tmp/etype_bt.pid"

# Prevent duplicate runs
if [ -f "$PIDFILE" ]; then
  OLD_PID=$(cat "$PIDFILE" 2>/dev/null)
  if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
    echo "Already running (PID $OLD_PID) - ignoring duplicate trigger." >> "$LOG"
    exit 0
  fi
fi
echo $$ > "$PIDFILE"
trap 'rm -f "$PIDFILE"' EXIT INT TERM

echo "=== E-TYPE LIVE TYPEWRITER ENGINE START ===" > "$LOG"
date >> "$LOG"

if [ -x "$FBINK" ]; then
  "$FBINK" -q -c
  "$FBINK" -q -pm -m -y 2 "E-Type Live Typewriter"
  "$FBINK" -q -pm -m -y 3 -- "----------------------"
  "$FBINK" -q -pm -y 5 "Starting Broadcom Bluetooth engine..."
fi

# 1. Clean kill any lingering instances and daemons to ensure fresh sockets
killall -9 etype_unified etype_bt_hh etype_discovery bsa_server 2>/dev/null || true
rm -f /var/local/zbluetooth/*.sock /var/local/zbluetooth/ba_* 2>/dev/null || true
sleep 1

# 2. Ensure btd is running
if ! pidof btd >/dev/null 2>&1; then
  start btd 2>/dev/null || nice -n -10 btd -f &
  sleep 1
fi

# 3. Launch bsa_server if not already started by btd
if ! pidof bsa_server >/dev/null 2>&1; then
  nice -n -10 bsa_server -d /dev/ttymxc2 -u /var/local/zbluetooth/ -p /opt/brcm_4343w/bluetooth/firmware/BCM4343A1_001.002.009.0152.0517.hcd -lpm > /var/log/bsa_server_launch.log 2>&1 &
  sleep 2
fi

# 4. Run our compiled Live Typewriter Engine
chmod +x "$BIN"
"$BIN" >> "$LOG" 2>&1
echo "Exit code: $?" >> "$LOG"

sync

if [ -x "$FBINK" ]; then
  "$FBINK" -q -pm -y -2 "Session ended. Tap to restart."
fi
