#!/bin/sh
# E-Type Kindle Oasis - Copy Bluetooth Shared Libraries (~800KB) for Local Analysis
FBINK="/mnt/us/libkh/bin/fbink"
OUTDIR="/mnt/us/bt_libs"

if [ -x "$FBINK" ]; then
  "$FBINK" -q -pm -y -5 "Copying BT libraries (2 seconds)..."
fi

mkdir -p "$OUTDIR"
cp -f /usr/lib/libbsa.so.1.0 "$OUTDIR/" 2>/dev/null || true
cp -f /usr/lib/libbthal.so.1.0 "$OUTDIR/" 2>/dev/null || true
cp -f /usr/lib/libbtcli.so.1.0 "$OUTDIR/" 2>/dev/null || true
cp -f /usr/lib/libbtconnector.so "$OUTDIR/" 2>/dev/null || true
cp -f /usr/lib/libbtfdlib.so.1.0 "$OUTDIR/" 2>/dev/null || true
cp -f /usr/lib/libaipc.so.1.0 "$OUTDIR/" 2>/dev/null || true
cp -f /usr/bin/btd "$OUTDIR/" 2>/dev/null || true
cp -f /usr/bin/btui "$OUTDIR/" 2>/dev/null || true
cp -f /usr/bin/btconnectionhelper "$OUTDIR/" 2>/dev/null || true
cp -f /var/log/bsa_server_log "$OUTDIR/" 2>/dev/null || true

sync

if [ -x "$FBINK" ]; then
  "$FBINK" -q -pm -y -3 "Done! Plug Kindle into Mac."
fi
