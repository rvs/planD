# we need our base as a reference for users and groups
FROM plan9d/base:latest as base

# first we need a few tools from plan9port
FROM plan9d/port:latest as tools
ENV CC9="gcc -static"
WORKDIR /plan9port/src/cmd
RUN mk o.9p o.9pserve
RUN cd 9660srv; mk

# now lets download the torrent file
FROM ubuntu:latest as torrent
ARG P9FRONT_URL
COPY --from=tools /plan9port/src/cmd/o.9p /bin/9p
COPY --from=tools /plan9port/src/cmd/o.9pserve /bin/9pserve
COPY --from=tools /plan9port/src/cmd/9660srv/o.9660srv /bin/9660srv
COPY --from=base /etc/passwd /etc/group/ /etc/shadow /etc/

# install packages
RUN apt update
RUN apt install -y transmission-daemon

# download 9front torrent
RUN transmission-daemon ; sleep 5; transmission-remote localhost -a "$P9FRONT_URL" ;\
    while true; do sleep 10; transmission-remote localhost -l | tee /tmp/log ; grep -q "1.*Done" /tmp/log && exit; done

# unpack the ISO
RUN gzip -d ~/Downloads/*iso.gz

# now lets turn ISO into a tar
WORKDIR /plan9
ENV PLAN9 /
ENV NAMESPACE /tmp
RUN 9660srv ; 9p -a /tmp/9660 -A ~/Downloads/*iso tar / | tar xf -

# now filter out some of the things we don't need
RUN rm -rf dist/plan9front/.* || :

FROM plan9d/u9fs:latest
ENV ROOT /plan9
COPY --from=torrent /plan9 /plan9
