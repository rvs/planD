FROM alpine:latest as build

WORKDIR /
COPY *.patch /
RUN apk add --no-cache git gcc musl-dev make groff
RUN git clone --depth 1 https://github.com/mjl-/authsrv9.git
RUN cat /*.patch | patch -p0
RUN make -C authsrv9

FROM alpine:latest
RUN apk add --no-cache socat sysklogd
COPY --from=build /authsrv9/authsrv9 /authsrv9/passtokey /bin/
COPY entry.sh /bin
COPY qwerty#123 /tmp

WORKDIR /
# create the user we will be running as + system ones
RUN for u in _authsrv9 upas glenda adam eve bootes sys adm none; do addgroup $u ; adduser -G $u -S $u; done || : 
# create all the state directories
RUN for d in dev bin auth/users; do mkdir -p /var/authsrv9/$d; done
# initialize default values
RUN echo -n bootes >/var/authsrv9/auth/authid
RUN echo -n local >/var/authsrv9/auth/authdom
RUN echo adm >/var/authsrv9/auth/badusers
# populate our fake keyfs(4) database
#   status ok means that the user is still enabled
#   expire is either never or unix timestamp
#   key is derived a 7-byte DES key from the password typed in 
#          (must be >=8 and <=28 bytes) and default one is
#          qwerty#123
RUN for u in bootes glenda sys; do                        \
      mkdir -p /var/authsrv9/auth/users/$u               ;\
      echo -n ok >/var/authsrv9/auth/users/$u/status     ;\
      echo -n never >/var/authsrv9/auth/users/$u/expire  ;\
      cp /tmp/qwerty#123 /var/authsrv9/auth/users/$u/key ;\
done
# finally, fix permissions
RUN chgrp -R _authsrv9 /var/authsrv9
RUN chmod -R u=rwX,g=rX,o= /var/authsrv9
RUN chmod g+w /var/authsrv9/auth/users/*/key
# and install the binary "just so"
RUN install -m750 -o root -g _authsrv9 /bin/authsrv9 /var/authsrv9/bin/authsrv9

EXPOSE 567
ENTRYPOINT ["/bin/entry.sh"]
