#!/bin/sh
OUT="/mnt/us/oasis_report.txt"

echo "=== E-TYPE KINDLE OASIS HARDWARE & BLUETOOTH PROBE ===" > "$OUT"
date >> "$OUT"

echo -e "\n--- [1] KERNEL & CPU ---" >> "$OUT"
uname -a >> "$OUT" 2>&1
cat /proc/cpuinfo >> "$OUT" 2>&1

echo -e "\n--- [2] LOADED MODULES & AVAILABLE .KO MODULES ---" >> "$OUT"
lsmod >> "$OUT" 2>&1
find /lib/modules /usr/lib -name "*.ko" >> "$OUT" 2>&1

echo -e "\n--- [3] INPUT DEVICES (BUTTONS / TOUCH / KEYBOARD) ---" >> "$OUT"
cat /proc/bus/input/devices >> "$OUT" 2>&1
ls -la /dev/input/ >> "$OUT" 2>&1

echo -e "\n--- [4] BLUETOOTH STACK & DAEMONS ---" >> "$OUT"
for cmd in bluetoothctl hcitool hciconfig btmgmt gatttool btgatt-client btmanagerd ace_bt_cli bsa_server lipc-get-prop lipc-probe; do
  which "$cmd" >> "$OUT" 2>&1
done
ps aux | grep -i -E "bt|blue|bsa|hid" >> "$OUT" 2>&1
ls -la /dev/*bt* /dev/*hid* /dev/*uinput* /dev/uhid* >> "$OUT" 2>&1
find /usr/bin /usr/sbin /etc -iname "*bt*" -o -iname "*blue*" -o -iname "*hid*" >> "$OUT" 2>&1

echo -e "\n--- [5] USB OTG / HOST CONTROLLER ---" >> "$OUT"
ls -la /sys/bus/usb/devices/ /sys/class/udc/ /sys/devices/platform/ >> "$OUT" 2>&1

echo -e "\n--- [6] DISPLAY & RUNTIMES (FBINK / EIPS / PYTHON / SHELL) ---" >> "$OUT"
for cmd in fbink eips python3 python lua sqlite3 curl wget nc busybox; do
  which "$cmd" >> "$OUT" 2>&1
done
find /mnt/us -name "fbink" >> "$OUT" 2>&1
ls -la /dev/fb* >> "$OUT" 2>&1

echo -e "\n--- [7] BLUETOOTH & HID DEEP INSPECTION ---" >> "$OUT"
echo ">>> /etc/udev/rules.d/99-bluetooth.rules:" >> "$OUT"
cat /etc/udev/rules.d/99-bluetooth.rules >> "$OUT" 2>&1
echo -e "\n>>> /etc/upstart/btd.conf & btfd.conf:" >> "$OUT"
cat /etc/upstart/btd.conf /etc/upstart/btfd.conf >> "$OUT" 2>&1
echo -e "\n>>> LIPC Bluetooth Properties (lipc-probe):" >> "$OUT"
lipc-probe -a | grep -i -E "bt|blue|hid|input" >> "$OUT" 2>&1
lipc-probe -v com.lab126.btfd >> "$OUT" 2>&1
echo -e "\n>>> /usr/bin/lipcd-scripts/lipc-events/startBT*:" >> "$OUT"
head -n 100 /usr/bin/lipcd-scripts/lipc-events/*bt* /usr/bin/lipcd-scripts/lipc-events/*BT* >> "$OUT" 2>&1
echo -e "\n>>> /usr/bin/hidraw FULL strings:" >> "$OUT"
strings /usr/bin/hidraw >> "$OUT" 2>&1
echo -e "\n>>> /usr/bin/btui FULL strings:" >> "$OUT"
strings /usr/bin/btui >> "$OUT" 2>&1
echo -e "\n>>> Kernel HID / UHID / USB Host support:" >> "$OUT"
ls -la /sys/bus/hid* /sys/class/hid* /sys/module/*hid* /sys/module/*usb* >> "$OUT" 2>&1
if [ -f /proc/config.gz ]; then
  zcat /proc/config.gz | grep -i -E "HID|BT|UINPUT|USB_EHCI|USB_CHIPIDEA|USB_OTG" >> "$OUT" 2>&1
fi
grep -i -E "hid_input|uhid|uinput|hidraw|evdev|usbhid" /proc/kallsyms | head -n 80 >> "$OUT" 2>&1

echo -e "\n=== PROBE COMPLETE ===" >> "$OUT"
sync

if [ -x /mnt/us/libkh/bin/fbink ]; then
  /mnt/us/libkh/bin/fbink -q -pm -y -3 "Probe Complete! Saved oasis_report.txt"
elif [ -x /usr/sbin/eips ]; then
  /usr/sbin/eips 2 2 "Probe Complete! Saved oasis_report.txt"
fi
