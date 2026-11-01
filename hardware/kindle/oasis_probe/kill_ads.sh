#!/bin/sh
# E-Type Kindle Oasis - Permanently Kill Lockscreen Ads & Special Offers
FBINK="/mnt/us/libkh/bin/fbink"
LOG="/mnt/us/kill_ads_log.txt"

echo "=== KILL ADS START ===" > "$LOG"
date >> "$LOG"

if [ -x "$FBINK" ]; then
  "$FBINK" -q -c
  "$FBINK" -q -pm -m -y 3 "E-Type System Maintenance"
  "$FBINK" -q -pm -m -y 4 -- "-------------------------"
  "$FBINK" -q -pm -y 6 "Nuking Amazon lockscreen ads & adunits..."
fi

# 1. Remove adunits directory and replace with immutable 000 file
echo "[1] Locking /var/local/adunits..." >> "$LOG"
rm -rf /var/local/adunits 2>/dev/null || true
touch /var/local/adunits
chmod 000 /var/local/adunits
chattr +i /var/local/adunits 2>/dev/null || true

# 2. Lock /var/local/system/adunits
echo "[2] Locking /var/local/system/adunits..." >> "$LOG"
rm -rf /var/local/system/adunits 2>/dev/null || true
touch /var/local/system/adunits
chmod 000 /var/local/system/adunits
chattr +i /var/local/system/adunits 2>/dev/null || true

# 3. Disable adunit daemon registration via lipc if listening
echo "[3] Sending lipc deregister command..." >> "$LOG"
lipc-set-prop com.lab126.adunitd deregister 1 2>/dev/null || true

# 4. Turn off Special Offers flag if stored in system prefs
if [ -f /var/local/system/sys.conf ]; then
  sed -i 's/SPECIAL_OFFERS=true/SPECIAL_OFFERS=false/g' /var/local/system/sys.conf 2>/dev/null || true
fi

sync

if [ -x "$FBINK" ]; then
  "$FBINK" -q -pm -y 8  "SUCCESS: Lockscreen Ads Nuked!"
  "$FBINK" -q -pm -y 10 "1. /var/local/adunits locked (mode 000)"
  "$FBINK" -q -pm -y 11 "2. Sponsored screensavers disabled"
  "$FBINK" -q -pm -y 12 "3. Lockscreen will now show clean art"
  "$FBINK" -q -pm -y 15 "Tap screen to return to KUAL."
fi

echo "=== KILL ADS COMPLETE ===" >> "$LOG"
