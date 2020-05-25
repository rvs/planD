#!/bin/sh

syslogd -p /var/authsrv9/dev/log
socat -d -d TCP-LISTEN:567,reuseport,fork EXEC:"chroot /var/authsrv9 /bin/authsrv9 -d" &
sleep 5
tail -f /var/log/*
