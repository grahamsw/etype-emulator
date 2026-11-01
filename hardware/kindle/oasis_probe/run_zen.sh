#!/bin/sh
# E-Type Kindle Oasis - Fullscreen Zen Mode (Pauses Amazon GUI)
FBINK="/mnt/us/libkh/bin/fbink"
LOG="/mnt/us/zen_log.txt"
BIN="/mnt/us/extensions/oasis_probe/etype_unified"

echo "=== E-TYPE ZEN MODE START ===" > "$LOG"
date >> "$LOG"

if [ -x "$FBINK" ]; then
  "$FBINK" -q -c
  "$FBINK" -q -pm -m -y 2 "E-Type Zen Mode (Fullscreen)"
  "$FBINK" -q -pm -m -y 3 -- "----------------------------"
  "$FBINK" -q -pm -y 5 "Pausing Amazon Kindle GUI..."
  "$FBINK" -q -pm -y 7 "To exit back to Kindle: Press Esc or Ctrl+Q"
  "$FBINK" -q -pm -y 8 "(Or hold power button 15s to cold restart)"
fi
sleep 2

# 1. Pause Amazon GUI framework (kills home screen, popular reads, store, top bar)
echo "[ZEN] Pausing Amazon lab126_gui framework..." >> "$LOG"
stop lab126_gui 2>/dev/null || /etc/init.d/framework stop 2>/dev/null || true
sleep 1

# 2. Clean up any stale sockets
rm -f /var/local/zbluetooth/*.sock /var/local/zbluetooth/ba_* 2>/dev/null || true

# 3. Ensure Broadcom Bluetooth daemon is running
if ! pidof bsa_server >/dev/null 2>&1; then
  echo "[ZEN] Starting bsa_server..." >> "$LOG"
  nice -n -10 bsa_server -d /dev/ttymxc2 -u /var/local/zbluetooth/ -p /opt/brcm_4343w/bluetooth/firmware/BCM4343A1_001.002.009.0152.0517.hcd -lpm > /var/log/bsa_server_launch.log 2>&1 &
  sleep 2
fi

# 4. Clear the e-ink screen completely for edge-to-edge drafting
if [ -x "$FBINK" ]; then
  "$FBINK" -q -c
fi

# 5. Launch Fullscreen Typewriter Engine
chmod +x "$BIN"
"$BIN" >> "$LOG" 2>&1
EXIT_CODE=$?
echo "[ZEN] Typewriter exited with code: $EXIT_CODE" >> "$LOG"

# 6. Restore Amazon Kindle GUI on exit
if [ -x "$FBINK" ]; then
  "$FBINK" -q -c
  "$FBINK" -q -pm -m -y 10 "Restoring Amazon Kindle GUI..."
fi

echo "[ZEN] Restarting lab126_gui framework..." >> "$LOG"
start lab126_gui 2>/dev/null || /etc/init.d/framework start 2>/dev/null || true
sync

echo "=== E-TYPE ZEN MODE FINISHED ===" >> "$LOG"
date >> "$LOG"
