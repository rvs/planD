#!/bin/sh

AUTH_DOM=${AUTH_DOM:-local}
AUTH_USER=${AUTH_USER:-bootes}
AUTH_METHOD=none

DEBUG=${DEBUG:+-D}
ROOT=${ROOT:-/rootfs}

bail() {
   echo "$@"
   exit 1
}

if [ -n "$AUTH_SECRET" ]; then
  AUTH_METHOD=p9any
  cat > /etc/u9fs.key <<__EOT__
$AUTH_SECRET
$AUTH_USER
$AUTH_DOM
__EOT__
fi

socat -d -d TCP-LISTEN:564,reuseport,fork EXEC:"u9fs $DEBUG -a $AUTH_METHOD $ROOT" &

touch /tmp/u9fs.log
tail -f /tmp/u9fs.log
