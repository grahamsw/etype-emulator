#!/bin/sh
# Fake-register Kindle Oasis locally to silence "Cloud not available" popups
mntroot rw

mkdir -p /var/local/java/prefs
chattr -i /var/local/java/prefs/reginfo 2>/dev/null || true

cat > /var/local/java/prefs/reginfo << 'EOF'
#Mon Jul 29 22:44:35 GMT+02:00 2019
deviceName=E-Type Oasis
deviceEmailAddress=etype@pseudo.url
userName=E-Type
userId=amzn1.account.XXXXXXXX1A2B3C4YYYYYYYYYYYYY
givenName=E-Type
EOF

chmod 644 /var/local/java/prefs/reginfo
chattr +i /var/local/java/prefs/reginfo 2>/dev/null || true

# Prevent deregistration wipe
mkdir -p /var/local/system
touch /var/local/system/DONT_DELETE_CONTENT_ON_DEREGISTRATION

# Block Amazon registration check endpoints in /etc/hosts if not already blocked
if ! grep -q "firs-ta-g7g.amazon.com" /etc/hosts 2>/dev/null; then
  cat >> /etc/hosts << 'EOF'
127.0.0.1 firs-ta-g7g.amazon.com
127.0.0.1 todo-ta-g7g.amazon.com
127.0.0.1 det-ta-g7g.amazon.com
EOF
fi

mntroot ro
touch /mnt/us/DISABLE_CORE_DUMP
sync

if [ -x /mnt/us/libkh/bin/fbink ]; then
  /mnt/us/libkh/bin/fbink -q -pm -y -3 "FakeReg Applied! Restart Kindle from Settings."
fi
