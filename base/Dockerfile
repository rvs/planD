FROM alpine:latest as build

WORKDIR /plan9
# create the user we will be running as + system ones
RUN for u in _authsrv9 upas glenda adam eve bootes sys adm none; do addgroup $u ; adduser -G $u -S $u; done || :
