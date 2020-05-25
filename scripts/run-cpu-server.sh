#!/bin/sh
# we need to publish the following ports
#    17010 for cpu
#    17007 for import
#    17019 for rcpu
PROXY_PORTS=hostfwd=tcp::17010-:17010,hostfwd=tcp::17007-:17007,hostfwd=tcp::17019-:17019

ARCH=${ARCH:-amd64}

case "$ARCH" in
   amd64) qemu-system-x86_64 -netdev user,id=eth0,net=192.168.1.0/24,dhcpstart=192.168.1.10,"$PROXY_PORTS" -device e1000,netdev=eth0 \
	                     -kernel $1 -append console=0 -serial mon:stdio -display none
	 ;;
   arm64) qemu-system-aarch64 -M raspi3 -kernel $1 -append console=0 -serial mon:stdio
         ;;
       *) echo "Don't know how to run Plan9 on $ARCH"	 
	  exit 1
	 ;;
esac
